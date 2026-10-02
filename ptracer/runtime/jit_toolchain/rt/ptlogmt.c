/*
 * ptlogmt.c -- the buffer/gt/count sinks' pthread shim: a new thread must never
 * keep logging through its CREATOR's %gs region.
 *
 * WHY THIS EXISTS
 * ------------------------------------------------------------
 * `rt/ptlogrt.c' sets up a thread's %gs region lazily, on the SIGSEGV its first
 * `%gs'-relative access takes when the GS base is 0.  But Linux INHERITS the GS
 * base across clone(2):
 *
 *      arch/x86/kernel/process_64.c, copy_thread():
 *          savesegment(gs, p->thread.gsindex);
 *          p->thread.gsbase = p->thread.gsindex ? 0 : me->thread.gsbase;
 *
 * so a new thread starts with the CREATING thread's base and, until something
 * changes it, stores through the creator's cursor: values are lost to the race
 * and mis-attributed.  This shim interposes
 * pthread_create(3) and, in the START-ROUTINE WRAPPER of the new thread, drops
 * an inherited base that is not the thread's own, so the thread's next %gs
 * access faults into the runtime's lazy allocator and it gets its own TCB.
 *
 * WHAT IT CANNOT DO, AND WHO DOES IT
 * -----------------------------------------------------------------------------
 * The wrapper runs AFTER glibc's start_thread, and in a whole-program build the
 * ~5 000 instrumented instructions of start_thread (plus the `call' in clone3's
 * thread_start) already logged through the inherited base.  Nothing in this shim
 * can be earlier: glibc calls clone3 internally (no PLT to interpose), and the
 * clone sits inside a region where glibc has EVERY signal blocked -- creator
 * side until the clone returns, child side until start_thread restores the
 * mask -- so a SIGSEGV taken there is fatal by kernel rule
 * (force_sig_info_to_task: blocked -> SIG_DFL, the mechanism above).  That
 * rules out every fault-based scheme (a NULL base for the child, or a NULL base
 * for the creator with emulated accesses).  The only agent that can act on the child BEFORE
 * its first instruction without a fault is a ptracer: `pt_capture2 --sideband'
 * (PTRACE_O_TRACECLONE) pops a spare, fully initialised TCB from the runtime's
 * pool through the creator's %gs base and installs it as the child's GS base at
 * the child's first stop (ptlog_abi.h).  Under such a capture the child arrives
 * here already owning its TCB (its tid inside), and this wrapper keeps it.
 * Without a tracer (a plain run, the count sink) the child owns a region from
 * its start routine onwards.
 *
 * It is a separate object from ptlogrt.so on purpose: the primary runtime is
 * *injected* by E9Patch (no dynamic symbols, no interposition), so this shim
 * has to be preloaded next to it.  It touches nothing but %gs, so preloading it
 * next to an uninstrumented binary is harmless.
 *
 *      gcc -shared -fPIC -O2 -o ptlogmt.so ptlogmt.c -ldl
 *      LD_PRELOAD=.../ptlogmt.so PTLOG_DIR=... ./prog.e9
 *
 * LIMITS.  Threads created with a raw clone(2)/clone3(2) (Go, some runtimes)
 * bypass pthread_create and are not covered by the wrapper; under pt_capture2
 * they DO get a spare TCB like any other thread (the tracer sees every clone).
 * A target that uses %gs itself still conflicts with the buffer sink.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "ptlog_abi.h"

/* Experimental, default off. A single self-describing PTWRITE of RSP before
 * the application start routine anchors a thread whose native libc startup
 * was not instrumented. No CV-buffer slot is added. Requires PTWRITE hardware
 * and PT capture with PTW enabled; raw clone callers bypass this wrapper.
 * Build a SEPARATE shim with -DPTLOG_THREAD_RSP=1 for matched validation. */
#ifndef PTLOG_THREAD_RSP
#define PTLOG_THREAD_RSP 0
#endif

#ifndef ARCH_SET_GS
#define ARCH_SET_GS 0x1001
#define ARCH_GET_GS 0x1004
#endif

struct ptlogmt_arg
{
    void *(*fn)(void *);
    void *arg;
};

/* Is `gs' one of the runtime's TCBs?  A page-aligned mmap carrying the magic. */
static int ptlogmt_is_tcb(unsigned long gs)
{
    return gs != 0 && (gs & 4095) == 0 &&
           *(volatile unsigned long *)(gs + PTLOG_TCB_MAGIC_OFF) == PTLOG_MAGIC;
}

/* ptlogrt.so's exported ptlog_thread_headroom, looked up once (NULL when
 * the runtime is not preloaded: then nothing to do).  Threshold PTLOG_EXIT_HEADROOM
 * bytes (default 0 = half of the current buffer, decided by the runtime: see below). */
static int (*ptlogmt_headroom_fn)(unsigned long) = 0;
static int ptlogmt_headroom_looked = 0;
static unsigned long ptlogmt_headroom_bytes = 0;
static int ptlogmt_headroom_log = 0;   /* PTMT_HEADROOM_LOG=1: one stderr line per exiting thread (diagnostic) */
static void ptlogmt_headroom(void *unused)
{
    (void)unused;
    unsigned long gs = 0;
    syscall(SYS_arch_prctl, ARCH_GET_GS, &gs);
    if (ptlogmt_headroom_fn == 0 || !ptlogmt_is_tcb(gs))
        return;
    /* bufbytes at +80 (struct ptlog_tcb, ptlogrt.c); rotate when more than half full,
     * or below the explicit PTLOG_EXIT_HEADROOM. */
    unsigned long want = ptlogmt_headroom_bytes ? ptlogmt_headroom_bytes
                                                : *(volatile unsigned long *)(gs + 80) / 2;
    int r = ptlogmt_headroom_fn(want);
    if (ptlogmt_headroom_log)
    {
        char b[96];
        int n = snprintf(b, sizeof b, "[ptlogmt] exit headroom tid=%ld used=%lu/%lu rotated=%d\n",
                         (long)syscall(SYS_gettid), *(volatile unsigned long *)gs - (*(volatile unsigned long *)(gs + 24) +
                         *(volatile long *)(gs + 88) * *(volatile unsigned long *)(gs + 72)),
                         *(volatile unsigned long *)(gs + 80), r);
        if (n > 0) { ssize_t w = write(2, b, (size_t)n); (void)w; }
    }
}

static void *ptlogmt_start(void *v)
{
    struct ptlogmt_arg *a = (struct ptlogmt_arg *)v;
    void *(*fn)(void *) = a->fn;
    void *arg = a->arg;
    free(a);
    /* Keep a base that is already this thread's own (the ptracer's spare TCB,
     * see ptlog_abi.h) or none at all; drop anything else -- the creator's inherited
     * base -- so this thread's next %gs access faults into the lazy allocator
     * (signals are unblocked again by now: start_thread restored the mask). */
    unsigned long gs = 0;
    syscall(SYS_arch_prctl, ARCH_GET_GS, &gs);
    if (gs != 0)
    {
        long tid = syscall(SYS_gettid);
        if (!ptlogmt_is_tcb(gs) || *(volatile long *)(gs + PTLOG_TCB_TID_OFF) != tid)
            syscall(SYS_arch_prctl, ARCH_SET_GS, 0UL);
    }
#if PTLOG_THREAD_RSP
    __asm__ __volatile__ ("ptwrite %%rsp" ::: "memory");
#endif
    /* Give the thread a fresh buffer
     * before glibc's signal-blocked exit path runs, on a normal return AND on
     * pthread_exit/cancellation (the cleanup handler runs during the unwind,
     * before start_thread blocks signals). */
    void *r;
    pthread_cleanup_push(ptlogmt_headroom, 0);
    r = fn(arg);
    pthread_cleanup_pop(1);
    /* NON-TEMPORAL VALUE STORES.  This thread is
     * about to die, and its LAST, partly filled buffer is published and drained
     * by ANOTHER thread at teardown -- so the write-combining buffers holding
     * the tail of it have to be retired here, on this CPU, while this thread can
     * still do it.  The kernel's own exit path would almost certainly do it (a
     * context switch writes CR3, which is serialising), but "almost certainly"
     * is not a memory model: one `sfence' per thread lifetime removes the
     * argument.  It is unconditional because the `--nt-store' flag lives in the
     * rewritten image and this shim cannot see it; on an ordinary build it
     * retires nothing and costs a handful of cycles once per thread. */
    __asm__ __volatile__ ("sfence" ::: "memory");
    return r;
}

/* fork WITHOUT exec (e.g. multiprocessing pool workers).  fork(2) copies the calling thread's GS base
 * into the child, whose TCB copy still carries the PARENT's tid/pid: the child would keep logging into its copy
 * of the parent's buffer until the runtime's rotate path notices (the INHERITED-%gs warning) and abandons it.
 * Drop the inherited base in the child right after fork, exactly as ptlogmt_start() does for a new thread, so the
 * child's next %gs access faults into the runtime's lazy allocator and it gets its own TCB + cv file.
 * (Values logged by the instrumented libc between the fork syscall and this handler still go to the copy.) */
#define PTLOGMT_CTL_WRITER_OFF 16   /* struct ptlog_ctl: ntcb, stop, WRITER (layout pinned by ptlog_abi.h) */
static void ptlogmt_atfork_child(void)
{
    unsigned long gs = 0;
    syscall(SYS_arch_prctl, ARCH_GET_GS, &gs);
    /* The drain (writer) thread is NOT copied by fork(2), but its `writer = 1' flag is: the child's teardown
     * would then poll for a writer that does not exist.  Clear it. */
    if (ptlogmt_is_tcb(gs))
    {
        unsigned long ctl = *(volatile unsigned long *)(gs + PTLOG_TCB_CTL_OFF);
        if (ctl != 0)
            *(volatile long *)(ctl + PTLOGMT_CTL_WRITER_OFF) = 0;
    }
    if (gs != 0)
    {
        long tid = syscall(SYS_gettid);
        if (!ptlogmt_is_tcb(gs) || *(volatile long *)(gs + PTLOG_TCB_TID_OFF) != tid)
            syscall(SYS_arch_prctl, ARCH_SET_GS, 0UL);
    }
}

__attribute__((constructor)) static void ptlogmt_init(void)
{
    pthread_atfork(0, 0, ptlogmt_atfork_child);
}


int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*fn)(void *), void *arg)
{
    static int (*real)(pthread_t *, const pthread_attr_t *,
                       void *(*)(void *), void *);
    if (real == 0)
        real = (int (*)(pthread_t *, const pthread_attr_t *,
                        void *(*)(void *), void *))
               dlsym(RTLD_NEXT, "pthread_create");
    if (real == 0)
        return 1;                               /* EPERM-ish; cannot happen */
    struct ptlogmt_arg *a =
        (struct ptlogmt_arg *)malloc(sizeof(struct ptlogmt_arg));
    if (a == 0)
        return real(thread, attr, fn, arg);      /* out of memory: no worse */
    if (!ptlogmt_headroom_looked)
    {
        ptlogmt_headroom_fn = (int (*)(unsigned long))dlsym(RTLD_DEFAULT, "ptlog_thread_headroom");
        const char *h = getenv("PTLOG_EXIT_HEADROOM");
        if (h != 0)
            ptlogmt_headroom_bytes = strtoul(h, 0, 0);
        ptlogmt_headroom_log = getenv("PTMT_HEADROOM_LOG") != 0;
        if (h != 0 && ptlogmt_headroom_bytes == 0)
            ptlogmt_headroom_fn = 0;             /* PTLOG_EXIT_HEADROOM=0: off */
        __atomic_store_n(&ptlogmt_headroom_looked, 1, __ATOMIC_RELEASE);
    }
    a->fn = fn;
    a->arg = arg;
    /* Creator side: glibc's pthread_create blocks EVERY signal in the CREATOR
     * around the clone (create_thread -> clone3), and that stretch of instrumented libc
     * logs values too; make sure it cannot reach a guard page either. */
    ptlogmt_headroom(0);
    int r = real(thread, attr, ptlogmt_start, a);
    if (r != 0)
        free(a);
    return r;
}
