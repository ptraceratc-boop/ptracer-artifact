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
#include <string.h>
#include <sys/auxv.h>

/* ---- synchronization events (ptsync.S) -------------------------------------------------------
 * Only in the SYNC build, ptlogmt_sync.so = this file with -DPTLOG_SYNC=1 + ptsync.S:
 *      gcc -shared -fPIC -O2 -DPTLOG_SYNC=1 -o ptlogmt_sync.so ptlogmt.c ptsync.S -ldl
 * which preloads INSTEAD of ptlogmt.so.  Events are on by default there; PTLOG_SYNC_EVENTS=0 turns the
 * wrappers into tail jumps (A/B only).  Needs FSGSBASE (rdgsbase), else it stays off.  The plain
 * ptlogmt.so build is unchanged. */
#ifndef PTLOG_SYNC
#define PTLOG_SYNC 0
#endif
#if PTLOG_SYNC
__attribute__((visibility("hidden"))) unsigned char ptsync_on = 0;
static int ptsync_fsgs = 0;
struct ptsync_frame { unsigned long ret, a0, a1, a2; };
struct ptsync_tls_t { unsigned long n, flags; struct ptsync_frame f[16]; };
__attribute__((visibility("hidden"), tls_model("initial-exec")))
__thread struct ptsync_tls_t ptsync_tls;
#define PTSYNC_OP_START 2
#define PTSYNC_OP_EXIT  3
/* The real functions ptsync.S jumps to (hidden .data words named ptsync_real_<fn>). */
#define PTSYNC_FNS(X) X(pthread_join) X(pthread_tryjoin_np) X(pthread_timedjoin_np) X(fork) X(vfork) \
    X(posix_spawn) X(posix_spawnp) X(execve) X(execv) X(execvp) X(execvpe) X(fexecve) X(wait) X(waitpid) \
    X(wait4) X(waitid) X(pthread_mutex_lock) X(pthread_mutex_trylock) X(pthread_mutex_timedlock) \
    X(pthread_mutex_clocklock) X(pthread_mutex_unlock) X(pthread_cond_wait) X(pthread_cond_timedwait) \
    X(pthread_cond_clockwait) X(pthread_cond_signal) X(pthread_cond_broadcast) X(pthread_rwlock_rdlock) \
    X(pthread_rwlock_wrlock) X(pthread_rwlock_tryrdlock) X(pthread_rwlock_trywrlock) \
    X(pthread_rwlock_timedrdlock) X(pthread_rwlock_timedwrlock) X(pthread_rwlock_unlock) X(sem_wait) \
    X(sem_trywait) X(sem_timedwait) X(sem_clockwait) X(sem_post)
#define PTSYNC_DECL(f) extern void *ptsync_real_##f __attribute__((visibility("hidden")));
PTSYNC_FNS(PTSYNC_DECL)
/* Called by the constructor, and by ptsync.S if a wrapper runs before it (an earlier library's
 * initializer).  Lives in .ptsync_text: ptrecon skips it like the wrappers. */
__attribute__((visibility("hidden"), section(".ptsync_text"), used)) void ptsync_resolve_all(void)
{
#define PTSYNC_RES(f) if (ptsync_real_##f == 0) ptsync_real_##f = dlsym(RTLD_NEXT, #f);
    PTSYNC_FNS(PTSYNC_RES)
}
/* Enter ptsync.S's ptsync_emit_c by a jump in BOTH modes: this file's own instructions (which
 * ptrecon replays as program code) must not depend on the switch.  op | force<<8. */
static inline __attribute__((always_inline)) void ptsync_c(void *ip, unsigned long opf)
{
    register unsigned long r11 __asm__("r11") = opf;
    unsigned long rax = (unsigned long)ip;
    __asm__ __volatile__("lea 1f(%%rip), %%r10\n\tjmp ptsync_emit_c\n1:"
                         : "+a"(rax), "+r"(r11) : : "r10", "cc", "memory");
}
#endif

#ifndef ARCH_SET_GS
#define ARCH_SET_GS 0x1001
#define ARCH_GET_GS 0x1004
#endif

struct ptlogmt_arg
{
    void *(*fn)(void *);
    void *arg;
#if PTLOG_SYNC
    unsigned long force;        /* the creator had a TCB: the runtime is present (ptsync START) */
#endif
};

/* Is `gs' one of the runtime's TCBs?  A page-aligned mmap carrying the magic. */
static int ptlogmt_is_tcb(unsigned long gs)
{
    return gs != 0 && (gs & 4095) == 0 &&
           *(volatile unsigned long *)(gs + PTLOG_TCB_MAGIC_OFF) == PTLOG_MAGIC;
}

#if PTLOG_SYNC
/* pthread cleanup handler of every wrapped thread (normal return, pthread_exit, cancellation): ptsync EXIT */
static void ptlogmt_thread_end(void *unused)
{
    (void)unused;
    ptsync_c(0, PTSYNC_OP_EXIT);
}
#endif

static void *ptlogmt_start(void *v)
{
    struct ptlogmt_arg *a = (struct ptlogmt_arg *)v;
    void *(*fn)(void *) = a->fn;
    void *arg = a->arg;
#if PTLOG_SYNC
    unsigned long force = a->force;
#endif
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
#if PTLOG_SYNC
    ptsync_c((void *)fn, PTSYNC_OP_START | (force << 8));
    void *r;
    pthread_cleanup_push(ptlogmt_thread_end, 0);
    r = fn(arg);
    pthread_cleanup_pop(1);
    return r;
#else
    return fn(arg);
#endif
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
#if PTLOG_SYNC
    ptsync_fsgs = (getauxval(AT_HWCAP2) & 2) != 0;          /* HWCAP2_FSGSBASE */
    ptsync_resolve_all();
    const char *e = getenv("PTLOG_SYNC_EVENTS");
    if (e == 0 || e[0] != '0')
    {
        if (ptsync_fsgs)
            __atomic_store_n(&ptsync_on, 1, __ATOMIC_RELEASE);
        else
        {
            static const char m[] = "[ptlogmt] sync events off: no FSGSBASE\n";
            ssize_t w = write(2, m, sizeof m - 1); (void)w;
        }
    }
#endif
}

#if PTLOG_SYNC
/* The exported pthread_create is ptsync.S's wrapper; it calls this at the program's own %rsp. */
__attribute__((visibility("hidden"), noinline)) int ptlogmt_pthread_create(pthread_t *thread,
    const pthread_attr_t *attr, void *(*fn)(void *), void *arg)
#else
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*fn)(void *), void *arg)
#endif
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
#if PTLOG_SYNC
    a->force = 0;
    if (ptsync_fsgs)
    {
        unsigned long g;
        __asm__ __volatile__("rdgsbase %0" : "=r"(g));
        a->force = g != 0;
    }
#endif
    int r = real(thread, attr, ptlogmt_start, a);
    if (r != 0)
        free(a);
    return r;
}
