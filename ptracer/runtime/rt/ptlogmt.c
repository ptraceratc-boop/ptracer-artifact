/*
 * ptlogmt.c -- the buffer/gt sinks' pthread shim: a new thread must never keep
 * logging through its CREATOR's %gs region.
 *
 * WHY THIS EXISTS
 * ---------------
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
 * and mis-attributed.  This shim interposes pthread_create(3) and, in the
 * START-ROUTINE WRAPPER of the new thread, drops an inherited base that is not
 * the thread's own, so the thread's next %gs access faults into the runtime's
 * lazy allocator and it gets its own TCB.
 *
 * WHAT IT CANNOT DO, AND WHO DOES IT
 * ----------------------------------
 * The wrapper runs AFTER glibc's start_thread, and in a whole-program build the
 * instrumented instructions of start_thread (plus the `call' in clone3's
 * thread_start) already logged through the inherited base.  Nothing in this shim
 * can be earlier: glibc calls clone3 internally (no PLT to interpose), and the
 * clone sits inside a region where glibc has EVERY signal blocked -- creator
 * side until the clone returns, child side until start_thread restores the
 * mask -- so a SIGSEGV taken there is fatal by kernel rule
 * (force_sig_info_to_task: blocked -> SIG_DFL).  That rules out every
 * fault-based scheme.  The only party that can act on the child BEFORE its
 * first instruction without a fault is a ptracer: `pt_capture2 --sideband'
 * (PTRACE_O_TRACECLONE) pops a spare, fully initialised TCB from the runtime's
 * pool through the creator's %gs base and installs it as the child's GS base at
 * the child's first stop (ptlog_abi.h).  Under such a capture the child arrives
 * here already owning its TCB (its tid inside), and this wrapper keeps it.
 * Without a tracer the wrapper's behaviour applies: the child owns a region
 * from its start routine onwards.
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
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "ptlog_abi.h"

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

static void *ptlogmt_start(void *v)
{
    struct ptlogmt_arg *a = (struct ptlogmt_arg *)v;
    void *(*fn)(void *) = a->fn;
    void *arg = a->arg;
    free(a);
    /* Keep a base that is already this thread's own (the ptracer's spare TCB)
     * or none at all; drop anything else -- the creator's inherited base --
     * so this thread's next %gs access faults into the lazy allocator
     * (signals are unblocked again by now: start_thread restored the mask). */
    unsigned long gs = 0;
    syscall(SYS_arch_prctl, ARCH_GET_GS, &gs);
    if (gs != 0)
    {
        long tid = syscall(SYS_gettid);
        if (!ptlogmt_is_tcb(gs) || *(volatile long *)(gs + PTLOG_TCB_TID_OFF) != tid)
            syscall(SYS_arch_prctl, ARCH_SET_GS, 0UL);
    }
    return fn(arg);
}

/* fork WITHOUT exec (multiprocessing Pool workers).  fork(2) copies the calling thread's GS base
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
    /* the drain (writer) thread is NOT copied by fork(2), but its `writer = 1' flag is: the child's teardown
     * would then poll for it 100 000 x 10 us on every exit.  Clear it. */
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
    a->fn = fn;
    a->arg = arg;
    int r = real(thread, attr, ptlogmt_start, a);
    if (r != 0)
        free(a);
    return r;
}
