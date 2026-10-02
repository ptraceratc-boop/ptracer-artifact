/*
 * ptlogrt.c -- PTracer v2 Stage-2 runtime support for the *buffer* value sink.
 *
 * The trampolines emitted by runtime/e9plugin/ptlog.cpp in `sink buffer' mode
 * store 8-byte critical values through a per-thread cursor held in the %gs
 * segment base.  Offsets 0..16 are the ABI the emitted code knows; everything
 * after that belongs to this file:
 *
 *      gs:[0]  cursor   -- next 8-byte slot                       (ABI)
 *      gs:[8]  countdown to the next PTWRITE sync marker          (ABI)
 *      gs:[16] running total of values written by this thread     (ABI)
 *      gs:[24] arena start        gs:[32] arena end
 *      gs:[40] output fd          gs:[48] thread id
 *      ... see `struct ptlog_tcb'
 *
 * There is NO bounds check in the trampoline (a check would need a compare,
 * which would clobber EFLAGS, and a branch on every logged value).  Instead
 * every buffer is followed by a PROT_NONE guard page: the store
 *
 *      mov %val,disp(%cur)
 *
 * faults exactly when the buffer is full, and the SIGSEGV handler below hands
 * the full buffer to the writer thread, points the cursor register (and
 * gs:[0]) at the next buffer and returns so the faulting store re-executes.
 *
 * LIVENESS-AWARE TRAMPOLINES.  The cursor is
 * whichever register the analyzer proved DEAD at the site, and one cursor load
 * can serve N stores at `disp' = 0, 8, 16 ..., so the handler DECODES the
 * faulting `mov %val,disp(%cur)' to find the cursor register and recovers the
 * displacement from `si_addr' instead of assuming either.  Everything below
 * si_addr in the buffer has been stored; the retry address is
 * `newbuffer - (si_addr - cursor)', which makes the trampoline's own
 * `add $8N,%gs:0' (or `lea 8N(%cur),%cur; mov %cur,%gs:0') land on
 * `newbuffer + 8*(N-k)' -- exactly the values that ended up in the new buffer.
 *
 * ASYNCHRONOUS DRAIN
 * -------------------
 * Writing the full buffer to the thread's cv file *inside* the handler would
 * stop the target for the whole write(2), and a whole-program run produces
 * gigabytes.  Instead each thread owns a ring of `PTLOG_NBUF' buffers and a single
 * writer thread drains them:
 *
 *   target thread  fills buffer h, faults on its guard page, publishes h
 *                  (state FULL) and continues in buffer h+1 -- no syscall, no
 *                  copy, ~1 us;
 *   writer thread  walks every registered thread's ring IN ORDER and write(2)s
 *                  each FULL buffer, so the cv file stays in program order.
 *
 * The target only blocks when it laps the writer (all `PTLOG_NBUF' buffers
 * full).  Even then it does not deadlock: after `PTLOG_SPINS' yields it claims
 * the oldest buffer itself and writes it inline, which is also what keeps the
 * sink correct in a fork(2)ed child, where the writer thread does not exist.
 * Claiming is a CAS on the buffer's state word, so the writer and the handler
 * can never write the same buffer, and only the CAS winner advances `tail', so
 * buffers reach the file strictly in order.
 *
 * The writer thread is created with a raw clone(2) (CLONE_VM|CLONE_THREAD|...)
 * and its own dummy TLS: it must not touch libc, because in the E9Patch-
 * injected build it starts before libc is initialised.
 *
 * BUILD (two flavours, same source)
 *
 *  1. Injected into the rewritten binary by E9Patch (primary; also works for
 *     statically-linked targets, no LD_PRELOAD needed):
 *
 *       cd <e9patch checkout>
 *       NO_SIMD_CHECK=1 ./e9compile.sh \
 *           <path>/rt/ptlogrt.c -DPTLOG_E9RT
 *
 *     produces the PIE object `ptlogrt'; ptlog.cpp loads it with
 *     sendELFFileMessage() so that its init()/fini() run at process start/exit.
 *
 *  2. As an ordinary LD_PRELOAD library (handy for debugging):
 *
 *       gcc -shared -fPIC -O2 -o ptlogrt.so ptlogrt.c
 *
 * ENVIRONMENT
 *   PTLOG_DIR      directory for cv.<pid>.<tid>.bin files (default ".").
 *                  The literal value "/dev/null" opens /dev/null instead, which
 *                  is the buffer sink's analogue of `pt_capture2 --aux-out
 *                  /dev/null': the drain runs, the bytes are discarded.
 *   PTLOG_BUFVALS  values per buffer (default 1<<20 = 8 MiB)
 *   PTLOG_NBUF     buffers per thread (default 4, max PTLOG_MAX_BUF)
 *   PTLOG_SYNC     sync-marker period; must match the plugin's `sync' (4096)
 *   PTLOG_STATS    if set, print a one-line drain report to stderr at exit
 *   PTLOG_SPARES   spare TCBs kept ready for the ptracer to hand to new threads
 *                  (ptlog_abi.h, default 32, 0 = off)
 *   PTLOG_RING_MB  per-THREAD ring size in MiB (0 = PTLOG_NBUF x PTLOG_BUFVALS)
 *   PTLOG_RING_TOTAL_MB
 *                  The PER-PROCESS ring budget in MiB (default 512,
 *                  0 = off).  Each TCB -- live thread or spare -- gets
 *                  budget / (threads now + PTLOG_SPARES), clamped to
 *                  PTLOG_RING_MB above and PTLOG_RING_FLOOR_KB below and
 *                  clipped to what the pool has left.  Nothing is ever lost by
 *                  shrinking a ring: a full buffer stalls its thread until the
 *                  writer frees one.  Reported as `ring_mb=used/budget'.
 *   PTLOG_RING_FLOOR_KB
 *                  per-thread floor for the above (default 256)
 *   PTLOG_RING_MIN_TCBS
 *                  how many TCBs the fair share plans for even when the process
 *                  is still single-threaded (default 64 = 32 threads + the
 *                  32-deep spare pool).  Without it the spares built at startup
 *                  take the whole budget and every later thread gets the floor.
 */

#ifdef PTLOG_E9RT
#include "stdlib.c"
#define PTLOG_ENV_FROM_ENVP 1
#else
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#define PTLOG_ENV_FROM_ENVP 0
#endif

#ifndef ARCH_SET_GS
#define ARCH_SET_GS     0x1001
#define ARCH_GET_GS     0x1004
#endif

#include "ptlog_abi.h"      /* offsets shared with the pthread shim rt/ptlogmt.c */
#include "ptgt_format.h"

#define PTLOG_MAX_THREADS   1024
/*
 * `tcbs[]' is PINNED: ptlog_abi.h fixes the offsets of
 * everything AFTER it (spare_put/spare_get/spare[]) and offline/pt_capture2.c
 * POKES those offsets into the traced process, so widening `tcbs[]' itself
 * would need PTLOG_ABI bumped and the tracer rebuilt with it.  The OVERFLOW
 * table `tcbs2[]' is appended past every pinned offset instead -- exactly where
 * the recycle ring and the ring-budget counters already live -- so the registry
 * holds PTLOG_TCB_SLOTS threads with the ptracer's ABI bit-for-bit unchanged.
 *
 * Why it matters: the registry is what the reaper and the teardown walk, and it
 * is also what NAMES a spare TCB's cv/gt files (`ptlog_finalise' through
 * `pending').  A thread past the ceiling therefore loses BOTH -- its cv stream
 * is truncated at its last full buffer AND its ground-truth ring keeps the
 * provisional `gt.<pid>.s<serial>.bin' name, so the oracle cannot find it at
 * all.
 *
 * The recycle path (`ptlog_recycle_tcb') bounds `ntcb' by CONCURRENCY rather
 * than by threads-ever-created, but it refuses outright on a ground-truth build
 * (`tcb->gt_fd >= 0'), so it does not replace the wider registry there.
 */
#define PTLOG_MAX_THREADS2  7168
#define PTLOG_TCB_SLOTS     (PTLOG_MAX_THREADS + PTLOG_MAX_THREADS2)
#define PTLOG_GUARD         4096

/* ------------------------------------------------------------------------
 * GROUND-TRUTH ADDRESS RING
 *
 * A `--gt-all' build (runtime/rewrite.py) puts a SECOND logging sequence in
 * every trampoline: it computes the ORIGINAL instruction's effective address
 * and stores the 16-byte record { effective address, original ip } through a
 * per-thread cursor at %gs:PTLOG_GT_OFF.  It is a separate channel from the
 * critical-value buffer above and never intentionally drops records: the cursor
 * walks a window of a MAP_SHARED file, and when it reaches the window's guard
 * page the SIGSEGV handler synchronously extends the file and maps the next
 * window -- the target thread waits for the mmap. This backpressure does not
 * establish asynchronous-signal safety.
 * PTGT v2 also supports completed REP STOS descriptors (ptgt_format.h).
 * There is no ring, no drain thread and no back-pressure heuristic: the
 * writeback is the kernel's, and the target blocks only for the two syscalls
 * per window.  Overhead is irrelevant here (this build exists to MEASURE the
 * reconstruction, never to be the tracer).
 * ------------------------------------------------------------------------ */
#define PTLOG_GT_OFF        512     /* %gs offset of the gt cursor (ABI)     */
#define PTLOG_COUNT_DATA    4096    /* %gs offset where the count array begins (ABI) */
#define PTLOG_GT_REC        16      /* bytes per gt record (addr, ip)        */
#define PTLOG_GT_HDR        32      /* file header, see `gt_hdr' below       */
#define PTLOG_GT_MAGIC      0x54475450u     /* "PTGT" little-endian          */
#define PTLOG_MAX_BUF       16      /* buffers per thread (ring length)      */
#define PTLOG_SPINS         2000    /* yields before the handler writes itself */

/* Per-buffer state.  Only ever changed with __sync_bool_compare_and_swap or a
 * release store, so the writer thread and the faulting target thread cannot
 * both write the same buffer. */
/* PTLOG_MAGIC ("PTLOGTCB", ptlog_abi.h) recognises one of our own TCBs through
 * an inherited %gs base -- see ptlog_setup()'s "one runtime per PROCESS" note. */

#define PTLOG_FREE      0
#define PTLOG_FULL      1
#define PTLOG_WRITING   2

/* Offsets 0..16 are the ABI shared with the emitted trampolines. */
struct ptlog_ctl;

struct ptlog_tcb
{
    unsigned long cursor;       /* +0   next 8-byte slot                     */
    unsigned long count;        /* +8   sync-marker countdown                */
    unsigned long total;        /* +16  values written by this thread         */
    unsigned long arena;        /* +24  first buffer                          */
    unsigned long arena_end;    /* +32  end of the last guard page            */
    long          fd;           /* +40                                        */
    long          tid;          /* +48                                        */
    long          pid;          /* +56  the pid the fd was opened for         */
    long          nbuf;         /* +64                                        */
    unsigned long stride;       /* +72  bufbytes + guard                      */
    unsigned long bufbytes;     /* +80                                        */
    long          head;         /* +88  buffer being filled (target thread)   */
    long          tail;         /* +96  oldest unwritten buffer (CAS winner)  */
    long          st[PTLOG_MAX_BUF];    /* +104 per-buffer state              */
    unsigned long len[PTLOG_MAX_BUF];   /*      bytes to write                */
    /* statistics (not on any hot path) */
    unsigned long n_flush;      /* buffers handed to the drain               */
    unsigned long n_inline;     /* buffers the faulting thread wrote itself  */
    unsigned long n_stall;      /* faults that had to wait for a free buffer */
    unsigned long bytes;        /* bytes written for this thread             */
    unsigned long magic;        /* PTLOG_MAGIC: "this TCB is ours"            */
    struct ptlog_ctl *ctl;      /* the process-wide control block             */
    /* --- ground-truth address channel; gt_cursor MUST land on
     *     %gs:PTLOG_GT_OFF, which the static assert below checks. --- */
    unsigned long gt_pad[13];
    unsigned long gt_cursor;    /* +512 next 16-byte record slot        (ABI) */
    unsigned long gt_map;       /* +520 start of the mapped window            */
    unsigned long gt_end;       /* +528 first byte of the window's guard page */
    long          gt_fd;        /* +536                                       */
    unsigned long gt_off;       /* +544 file offset of the current window     */
    unsigned long gt_win;       /* +552 window size in bytes                  */
    unsigned long gt_rot;       /* +560 windows mapped so far                 */
    /* --- count sink: the 8-byte per-address
     *     execution counters live IN this thread's %gs region, starting at
     *     %gs:PTLOG_COUNT_DATA (one page in, past this struct).  `count_arr'
     *     caches that address (== (char*)tcb + PTLOG_COUNT_DATA) for the dump. */
    unsigned long count_arr;    /* base of this thread's embedded counter array */
    /* --- spare TCBs (ptlog_abi.h).  `abi' tells the
     *     ptracer (offline/pt_capture2.c) that this runtime keeps a pool of
     *     SPARE TCBs in `ptlog_ctl' which it may hand to a new thread at the
     *     thread's first ptrace stop, before its first instruction.  A spare is
     *     allocated with tid 0; the tracer writes the tid.  Its cv file cannot
     *     be opened and its gt file cannot be given its final name until then:
     *     `pending' = 1 says so, and ptlog_finalise() does it once the tid is
     *     known (from the writer thread, any fault of ours, or the teardown).
     *     `serial' names the provisional gt file (gt.<pid>.s<serial>.bin). --- */
    unsigned long abi;          /* +576 PTLOG_ABI                              */
    long          pending;      /* +584 1 = files not yet named, 2 = naming    */
    long          serial;       /* +592 spare serial                           */
    /* 1 once this thread has
     * been found DEAD and its partly filled buffer published by the writer.
     * Without it a thread that dies without the process dying keeps its tail in
     * memory until teardown -- and a process killed by a signal never reaches
     * teardown at all. */
    long          reaped;       /* +600 the owner thread is gone and flushed   */
    /* Cursor-derived synchronization (experimental emitter, default off).
     * Keep these valid with the default emitter too: its images also work
     * with this runtime, but one process must not mix the two sync emitters.
     * (cursor - sync_bias) / 8 is the exact number of committed value slots.
     * Rotation rebases both pointers; it does not change that count. */
    unsigned long sync_next;    /* +608 post-store cursor that triggers sync  */
    unsigned long sync_bias;    /* +616 cursor corresponding to stream byte 0 */
};

_Static_assert(__builtin_offsetof(struct ptlog_tcb, sync_next) == PTLOG_TCB_SYNC_NEXT_OFF,
    "ptlog_abi.h: PTLOG_TCB_SYNC_NEXT_OFF is stale");
_Static_assert(__builtin_offsetof(struct ptlog_tcb, sync_bias) == PTLOG_TCB_SYNC_BIAS_OFF,
    "ptlog_abi.h: PTLOG_TCB_SYNC_BIAS_OFF is stale");

_Static_assert(__builtin_offsetof(struct ptlog_tcb, tid) == PTLOG_TCB_TID_OFF,
    "ptlog_abi.h: PTLOG_TCB_TID_OFF is stale");
_Static_assert(__builtin_offsetof(struct ptlog_tcb, magic) == PTLOG_TCB_MAGIC_OFF,
    "ptlog_abi.h: PTLOG_TCB_MAGIC_OFF is stale");
_Static_assert(__builtin_offsetof(struct ptlog_tcb, abi) == PTLOG_TCB_ABI_OFF,
    "ptlog_abi.h: PTLOG_TCB_ABI_OFF is stale");
_Static_assert(__builtin_offsetof(struct ptlog_tcb, ctl) == PTLOG_TCB_CTL_OFF,
    "ptlog_abi.h: PTLOG_TCB_CTL_OFF is stale");

_Static_assert(__builtin_offsetof(struct ptlog_tcb, gt_cursor) == PTLOG_GT_OFF,
    "the gt cursor must sit at %gs:PTLOG_GT_OFF -- adjust gt_pad[]");
_Static_assert(sizeof(struct ptlog_tcb) <= PTLOG_COUNT_DATA,
    "the TCB header must fit below the embedded count array at %gs:PTLOG_COUNT_DATA");

/*
 * Everything that must be visible to EVERY injected copy of this runtime lives
 * in one shared control block, not in statics: `libc.so.6' has no DT_FINI at
 * all, so the copy injected into it -- the FIRST to initialise, and therefore
 * the one that owns the state -- can never be given a fini(); the teardown has
 * to be done by whichever other copy's fini() runs.  Every TCB carries a
 * pointer to the block, so a copy that finds an existing TCB through %gs also
 * finds the registry, the writer-thread flags and the stats flag.
 */
struct ptlog_ctl
{
    long ntcb;
    volatile long stop;         /* tell the writer thread to leave           */
    volatile long writer;       /* 1 while the writer thread is up           */
    long done;                  /* the teardown has already run              */
    long stats;                 /* PTLOG_STATS                               */
    long warned;                /* the inherited-GS warning has been printed */
    /* The MULTIPLEXED cv (PTLOG_CVMUX) writes every thread's buffers
     * into ONE process file `cv.<pid>.bin' tagged per block with the tid, so a
     * thread-per-connection server spends ONE fd, not one per thread (no
     * EMFILE with many threads).  `mux_fd' is that shared
     * fd; `mux_lock' serialises the block-header+data write so blocks never
     * interleave; `mux_pid' re-opens after fork(2). */
    volatile long mux_lock;     /* 0 = free, 1 = a writer holds it           */
    long mux_fd;                /* the shared cv.<pid>.bin fd (-1 = not open) */
    long mux_pid;               /* the pid mux_fd was opened for             */
    struct ptlog_tcb *tcbs[PTLOG_MAX_THREADS];
    /* The SPARE TCB ring (ptlog_abi.h).  The runtime PRODUCES fully
     * initialised, tid-less TCBs into spare[put % MAX] and bumps `put'; the
     * ptracer (pt_capture2) is the only CONSUMER: at a creator's clone event it
     * reads spare[get % MAX], bumps `get' (PTRACE_POKEDATA) and gives that TCB
     * to the new thread before its first instruction.  Single producer at a
     * time (`spare_lock'), single consumer -- no other synchronisation needed. */
    volatile long spare_put;    /* +8264 */
    volatile long spare_get;    /* +8272 */
    long spare_lock;            /* +8280 CAS lock among producing threads     */
    long spare_serial;          /* +8288 provisional gt file names            */
    struct ptlog_tcb *spare[PTLOG_SPARE_MAX];   /* +8296 */
    /* The ring budget is PER PROCESS.
     * Appended AFTER spare[] because everything above it is the ptracer's ABI
     * (ptlog_abi.h asserts the offsets).  `ring_used' is the bytes of ring +
     * guard pages handed out so far, `ring_over' the number of threads that had
     * to be given the floor because the budget was exhausted. */
    volatile long warned_ntcb;  /* The registry overflowed, said once */
    volatile long exit_sig;     /* The signal that ran the teardown  */
    /* The RECYCLE ring -- TCBs whose owner has died and whose buffers
     * have all been drained, waiting to be handed to the next thread.  It is
     * DELIBERATELY NOT the ptracer's `spare[]': that ring's `get' is advanced by
     * offline/pt_capture2.c with PTRACE_POKEDATA while other threads run, so a
     * second in-process consumer would race it.  Both consumers of this one are
     * the runtime itself, under `rec_lock'.  Appended here, past every offset
     * ptlog_abi.h pins, so the layout the ptracer knows is unchanged. */
    volatile long recycled;     /* TCBs returned to the ring, ever              */
    volatile long rec_put;
    volatile long rec_get;
    volatile long rec_lock;
    struct ptlog_tcb *rec[PTLOG_SPARE_MAX];
    volatile long ring_used;
    volatile long ring_over;
    volatile long ring_threads;
    /* The registry OVERFLOW table, slots PTLOG_MAX_THREADS ..
     * PTLOG_TCB_SLOTS-1.  Appended last, past every offset ptlog_abi.h pins. */
    struct ptlog_tcb *tcbs2[PTLOG_MAX_THREADS2];
    /* THE SIGSEGV
     * CHAIN LIVES HERE, not in a copy's static `ptlog_old_segv'.  A JIT VM installs
     * its own SIGSEGV handler AFTER ours and does not chain (node: SA_RESETHAND +
     * raise; HotSpot: chains, but first runs libc -- which in a whole-program build
     * is INSTRUMENTED and logs into the very ring whose guard page just faulted,
     * re-faulting with SIGSEGV blocked -> the kernel kills the process without a
     * word).  The LD_PRELOADed flavour of this runtime therefore interposes
     * sigaction(2): while `segv_handler' is the installed action, an application
     * request to replace it is recorded HERE as the chain target instead, and
     * ours stays first.  The PRIMARY copy (embedded in the rewritten image, or
     * the .so) reads the chain from here when a fault is not its own.  Layout-
     * neutral (u64s, not a struct sigaction: the E9Patch mini-libc's differs
     * from glibc's).  Appended past every offset ptlog_abi.h pins. */
    volatile long segv_chain_abi;       /* 1 = the primary reads the fields below */
    unsigned long segv_handler;         /* the primary's ptlog_segv               */
    volatile long segv_chain_lock;
    unsigned long segv_chain_handler;   /* 0 = SIG_DFL, 1 = SIG_IGN, else address */
    unsigned long segv_chain_flags;     /* its sa_flags (SA_SIGINFO/SA_RESETHAND) */
};

/* Slot `i' of the registry: the pinned array below the ceiling, the appended
 * overflow table above it.  `i' must be < PTLOG_TCB_SLOTS. */
static struct ptlog_tcb **ptlog_tcb_slot(struct ptlog_ctl *c, long i)
{
    return i < PTLOG_MAX_THREADS ? &c->tcbs[i]
                                 : &c->tcbs2[i - PTLOG_MAX_THREADS];
}

_Static_assert(__builtin_offsetof(struct ptlog_ctl, spare_put) == PTLOG_CTL_SPARE_PUT_OFF,
    "ptlog_abi.h: PTLOG_CTL_SPARE_PUT_OFF is stale");
_Static_assert(__builtin_offsetof(struct ptlog_ctl, spare_get) == PTLOG_CTL_SPARE_GET_OFF,
    "ptlog_abi.h: PTLOG_CTL_SPARE_GET_OFF is stale");
_Static_assert(__builtin_offsetof(struct ptlog_ctl, spare) == PTLOG_CTL_SPARE_ARR_OFF,
    "ptlog_abi.h: PTLOG_CTL_SPARE_ARR_OFF is stale");

static struct ptlog_ctl *ptlog_ctl = 0;
static long  ptlog_bufvals = 1L << 20;
static long  ptlog_nbuf    = 4;
static long  ptlog_ring_mb = 0;     /* Per-thread ring size in MiB (0 = use nbuf*bufvals) */
/* The budget that actually bounds the process.  PTLOG_RING_MB is PER
 * THREAD, so a 30-thread process at 64 MB would ask for 1.9 GB.
 * PTLOG_RING_TOTAL_MB is the whole
 * process's ring budget; each new thread gets budget/threads_now, clamped to
 * [PTLOG_RING_FLOOR_KB, the per-thread size PTLOG_RING_MB/PTLOG_BUFVALS asks
 * for], and the pool is accounted so the sum can never exceed it.  ON BY
 * DEFAULT (512 MiB): a process with few threads is unaffected (the per-thread
 * cap binds first), a thread-rich one shrinks instead of being killed. */
static long  ptlog_ring_total_mb = 512;
/* The per-thread floor.
 * A floored thread gets `floor/PTLOG_NBUF' bytes per buffer; small buffers are expensive
 * because the guard-page fault and the drain `write(2)` then happen every few thousand
 * values.  1 MiB (256 KiB buffers) is close to the larger ring sizes in cost and still keeps a
 * ~500-thread process inside the 512 MiB budget. */
static long  ptlog_ring_floor_kb = 1024;
/* The share is budget/(threads now + spare pool), but "threads now" is 1 at the
 * moment the 32 spares are built, so a thread-rich process would hand the whole
 * budget to the first 33 TCBs and floor every later one.  Plan for at least this many TCBs from the start. */
static long  ptlog_ring_min_tcbs = 64;
/* PTLOG_HUGE=1: back the ring with 2 MiB transparent
 * huge pages.  The ring is 8 MiB of PURELY SEQUENTIAL stores per buffer, which
 * on 4 KiB pages is one dTLB miss every 512 values.  The mapping is aligned to
 * 2 MiB and each buffer is rounded UP to a whole number of huge pages, with the
 * PROT_NONE guard page taken from a separate trailing 2 MiB of slack -- so the
 * `mprotect' that arms the full-buffer fault splits only the slack and never
 * the buffer itself.  OFF by default. */
static int   ptlog_huge = 0;
/* PER-THREAD KEYFRAME COUNTERS (PTLOG_KF_N).
 * A `--kf-gs' build puts the keyframe
 * countdown of every resync site in the THREAD's own %gs region, in the same
 * array the count sink uses (the two sinks are mutually exclusive), at
 * %gs:(PTLOG_COUNT_DATA + 8*i).  PTLOG_KF_N is the total number of counters the
 * whole process needs -- the sum over images of what `rewrite.py --kf-plan'
 * reported -- and every cell must start at 1, not 0: a countdown of 1 makes the
 * FIRST execution of a site a keyframe (so it anchors immediately), and a
 * zero-filled cell would `dec' to -1 and not fire again for 2^64 executions.
 * 0 = the build has no per-thread keyframe counters. */
static long  ptlog_kf_n = 0;
/* SIGNAL-TERMINATED RUNS.
 * A per-thread ring reaches the cv file only when a buffer FILLS (the guard
 * page faults) or at library finalisation.  A service stopped with SIGTERM runs
 * no finalisation, so without help every thread's partly filled buffer would
 * be dropped.
 *   PTLOG_SIGEXIT=1 (default) installs ptlog_sigexit() on the catchable
 * terminating signals, which runs the SAME ptlog_teardown() the PTLOG_GT_MAX
 * path already calls and then re-raises with the default disposition, so the
 * process still dies with the status it would have had.
 *   PTLOG_REAP=1 (default) makes the writer thread notice threads that have
 * EXITED while the process lives and publish their tails, so a server's
 * per-connection streams are complete as the connections close. */
static int   ptlog_sigexit_on = 1;
static int   ptlog_reap = 1;
/* Registry-overflow mitigation, PTLOG_RECYCLE (default OFF -- see below).
 *
 * `ptlog_ctl.tcbs[]' has PTLOG_MAX_THREADS = 1024 entries and `ntcb' only ever
 * grows, so a process that CREATES more than 1024 threads over its life stops
 * registering them -- their buffers are then invisible to the reaper and to the
 * teardown, and their streams are silently truncated.  A thread-per-connection
 * server can cross that line quickly.
 *
 * The registry itself holds PTLOG_TCB_SLOTS = 8 192 through the appended
 * overflow table `tcbs2[]' (see PTLOG_MAX_THREADS2), and RECYCLE is refused on
 * a GROUND-TRUTH build (`ptlog_recycle_tcb': `tcb->gt_fd >= 0').  Widening the
 * registry therefore serves a ground-truth run and recycling serves a
 * production one; they are complementary, not alternatives.
 *
 * Widening `tcbs[]' IN PLACE would move every offset after it in `struct
 * ptlog_ctl', which ptlog_abi.h pins and offline/pt_capture2.c pokes, so that
 * would need PTLOG_ABI bumped and the tracer rebuilt; appending `tcbs2[]' past
 * every pinned offset does not.  Recycling does not either: a TCB whose owner has
 * DIED and whose buffers have all been drained is returned to the SPARE POOL,
 * where `ptlog_spare_refill' would otherwise have built a brand new one.  The
 * ceiling then stops being "threads created" and becomes "threads ALIVE AT
 * ONCE", which for a thread-per-connection server is its concurrency, not its
 * connection count.  Nothing about the layout, the pinned offsets or the
 * ptracer's spare protocol changes -- the tracer pops a TCB pointer out of
 * `spare[]' and cannot tell a recycled one from a new one.
 *
 * OFF BY DEFAULT; turning it on is one environment variable.  REFUSED for a TCB that has a
 * ground-truth ring (an accuracy build): the gt window and its file would have
 * to be re-established too, and no measurement needs that. */
static int   ptlog_recycle = 0;
#define PTLOG_HPAGE     (2UL << 20)
#ifndef MADV_HUGEPAGE
#define MADV_HUGEPAGE   14
#endif
static int   ptlog_cvmux   = 0;     /* One tid-tagged cv file per process */
static long  ptlog_sync    = 4096;
static char  ptlog_dir[256];
static int   ptlog_ready   = 0;
static int   ptlog_devnull = 0;
static int   ptlog_secondary = 0;   /* another copy of this runtime is in charge */
static volatile int ptlog_self_sigaction = 0;   /* Our own sigaction() calls pass through */
/* ground-truth address channel (PTLOG_GT / PTLOG_GT_DIR / PTLOG_GT_WIN) */
static int   ptlog_gt      = 0;         /* the build logs gt addresses      */
/* PTLOG_SEGV_DIAG=1: print sig/addr/rip and the TCB window bounds when a
 * SIGSEGV is NOT one of ours and is about to be chained (the
 * only way to see where a --gt-all build dies, since the default action
 * kills the process without a word). */
static int   ptlog_segv_diag = 0;
static char  ptlog_gt_dir[256];
static long  ptlog_gt_win  = 256L << 20;
/* PTLOG_GT_MAX: hard cap on the bytes one thread writes to its gt file.  The gt
 * ring is 16 bytes per DYNAMIC memory access, so a whole CPython run is tens of
 * GB -- more than the machine has to spare and far more than any comparison
 * needs.  When the cap is reached the runtime flushes the value buffers and
 * exits the process, so the gt log, the PT AUX file and the compared region all
 * end at the same point: the comparison is then "the first N memory accesses of
 * the run", which is the same kind of bounded window the two-process table
 * used.  0 = no cap. */
static long  ptlog_gt_max  = 2L << 30;
/* COUNT SINK.  A `--sink count' build has no cv file,
 * no PT and no guard pages: every trampoline just bumps a per-thread 8-byte
 * counter in an array whose base sits at %gs:PTLOG_COUNT_OFF.  Per-thread means
 * lock-free and exact; the whole-run count is the SUM over threads, taken at
 * exit and written as `index,count' CSV to PTLOG_COUNT_DIR/count.<pid>.csv. */
static int   ptlog_count     = 0;       /* the build is a count-sink build   */
static long  ptlog_count_n   = 1L << 20;/* counters per thread (PTLOG_COUNT_N) */
static char  ptlog_count_dir[256];

/* Spare TCBs kept ready for the ptracer to hand to new threads
 * (PTLOG_SPARES, default 32, 0 = off; the ring holds PTLOG_SPARE_MAX). */
static long  ptlog_spares    = 32;
#define PTLOG_SPARE_LOW     8       /* refill when fewer than this are unconsumed */

static struct ptlog_tcb *ptlog_new_tcb(void);
static struct ptlog_tcb *ptlog_make_tcb(long tid, int spare);
static void ptlog_spare_refill(int in_handler);
static void ptlog_finalise(struct ptlog_tcb *tcb);
static void ptlog_gt_init(struct ptlog_tcb *tcb, long tid, long pid, long serial);
static void ptlog_gt_path(char *path, long pid, long tid, long serial);
static unsigned long ptlog_gt_rotate(struct ptlog_tcb *tcb);
static void ptlog_gt_close(struct ptlog_tcb *tcb);
static void ptlog_teardown(void);
static void ptlog_install_sigexit(void);
static int ptlog_recycle_tcb(struct ptlog_tcb *tcb);
static int ptlog_store_base(const unsigned char *rip);
static char *ptlog_utoa(char *p, unsigned long v);
static char *ptlog_hexa(char *p, unsigned long v);
static void ptlog_warn(const char *msg);

static long ptlog_syscall6(long n, long a, long b, long c, long d, long e,
    long f)
{
    /* A RAW syscall in BOTH flavours, returning -errno.
     * glibc's syscall(3) writes errno through %fs on failure, and the writer
     * thread (raw clone, dummy 4 KiB TLS, ptlog_start_writer) has no errno slot
     * there, so a failing syscall (e.g. a futex wait returning EAGAIN) would
     * fault in the writer.  The E9Patch flavour's mini-libc syscall() is raw
     * as well.  Every caller
     * tests `< 0', never errno or -1 (see the tgkill note in ptlog_reap_dead). */
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

static long ptlog_arch_prctl(int code, unsigned long addr)
{
    return ptlog_syscall6(SYS_arch_prctl, code, (long)addr, 0, 0, 0, 0);
}

/*
 * NON-TEMPORAL STORES.  A `--nt-store' build writes
 * the ring with `movnti', which is WEAKLY ORDERED with respect to every other
 * store: the write-combining buffers holding the tail of a buffer may still be
 * in flight when the buffer is published.  An `sfence' therefore has to retire
 * them BEFORE the buffer becomes visible to anyone else -- on the guard-page
 * hand-off, on an inline drain, and at teardown.  It is executed
 * unconditionally (once per 8 MiB buffer, i.e. once per ~10^6 values, so it is
 * free) rather than under a flag, because the flag lives in the REWRITTEN IMAGE
 * and the runtime cannot see it.
 */
static inline void ptlog_sfence(void)
{
    __asm__ __volatile__ ("sfence" ::: "memory");
}

static void *ptlog_mmap_rw(unsigned long len)
{
    long r = ptlog_syscall6(SYS_mmap, 0, (long)len,
        PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (r < 0 && r > -4096)
        return 0;
    return (void *)r;
}

/*
 * PTLOG_HUGE: an anonymous mapping of `len' bytes whose base is a multiple of
 * `align' (2 MiB), with the slack at both ends unmapped.  Transparent huge
 * pages only back a 2 MiB-aligned, 2 MiB-sized range, so the alignment is not
 * optional -- MADV_HUGEPAGE on a misaligned mapping silently does nothing.
 */
static void *ptlog_mmap_rw_aligned(unsigned long len, unsigned long align)
{
    unsigned long total = len + align;
    long r = ptlog_syscall6(SYS_mmap, 0, (long)total,
        PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (r < 0 && r > -4096)
        return 0;
    unsigned long base = (unsigned long)r;
    unsigned long a = (base + align - 1) & ~(align - 1);
    if (a > base)
        ptlog_syscall6(SYS_munmap, (long)base, (long)(a - base), 0, 0, 0, 0);
    unsigned long tail = (base + total) - (a + len);
    if (tail > 0)
        ptlog_syscall6(SYS_munmap, (long)(a + len), (long)tail, 0, 0, 0, 0);
    return (void *)a;
}

static char *ptlog_utoa(char *p, unsigned long v)
{
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v != 0) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) *p++ = tmp[--n];
    return p;
}

static char *ptlog_hexa(char *p, unsigned long v)
{
    static const char d[] = "0123456789abcdef";
    int i, lead = 0;
    *p++ = '0'; *p++ = 'x';
    for (i = 60; i >= 0; i -= 4)
    {
        unsigned long nib = (v >> i) & 0xf;
        if (nib != 0 || lead != 0 || i == 0) { *p++ = d[nib]; lead = 1; }
    }
    return p;
}

static void ptlog_warn(const char *msg)
{
    long n = 0;
    while (msg[n] != '\0') n++;
    ptlog_syscall6(SYS_write, 2, (long)msg, n, 0, 0, 0);
}

/*
 * Open (truncating) this thread's critical-value file and write the
 * SPEC_FORMAT section-3 header.  PTLOG_DIR=/dev/null opens /dev/null, so the
 * drain runs at full speed and the bytes are discarded -- the buffer sink's
 * equivalent of `pt_capture2 --aux-out /dev/null'.
 */
static long ptlog_open_cv(long tid, long pid, int header)
{
    char path[512], *p = path;
    if (ptlog_devnull)
    {
        const char *s = "/dev/null";
        while (*s != '\0') *p++ = *s++;
        *p = '\0';
    }
    else
    {
        const char *d = ptlog_dir;
        while (*d != '\0' && (p - path) < 400) *p++ = *d++;
        if (p != path && p[-1] != '/') *p++ = '/';
        *p++ = 'c'; *p++ = 'v'; *p++ = '.';
        p = ptlog_utoa(p, (unsigned long)pid);
        *p++ = '.';
        p = ptlog_utoa(p, (unsigned long)tid);
        *p++ = '.'; *p++ = 'b'; *p++ = 'i'; *p++ = 'n'; *p = '\0';
    }

    long fd = ptlog_syscall6(SYS_open, (long)path,
        O_WRONLY|O_CREAT|O_TRUNC, 0644, 0, 0, 0);
    if (fd < 0)
    {
        /* Some kernels only expose openat(2). */
        fd = ptlog_syscall6(SYS_openat, -100 /*AT_FDCWD*/, (long)path,
            O_WRONLY|O_CREAT|O_TRUNC, 0644, 0, 0);
    }
    if (fd < 0)
        return -1;
    if (header)
    {
        unsigned int hdr[4];
        hdr[0] = 0x56435450u;               /* "PTCV" little-endian */
        hdr[1] = 2u;                        /* version */
        hdr[2] = (unsigned int)tid;
        hdr[3] = (unsigned int)ptlog_sync;  /* sync-marker period, 0 = off */
        ptlog_syscall6(SYS_write, fd, (long)hdr, sizeof(hdr), 0, 0, 0);
    }
    return fd;
}

static unsigned long ptlog_buf(struct ptlog_tcb *tcb, long i)
{
    return tcb->arena + (unsigned long)i * tcb->stride;
}

/*
 * The shared multiplexed cv file `cv.<pid>.bin' (SPEC_FORMAT §3, format
 * version 3).  ONE fd per process: every thread's buffers drain here as blocks
 * tagged with the thread id, so a thread-per-connection server never runs out of
 * file descriptors.  The 16-byte header is { "PTCW", version=3, sync, reserved }.
 * Opened lazily under a CAS lock; re-opened after fork(2).
 */
static long ptlog_mux_open(void)
{
    struct ptlog_ctl *c = ptlog_ctl;
    long pid = ptlog_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
    if (c->mux_fd >= 0 && c->mux_pid == pid)
        return c->mux_fd;
    while (!__sync_bool_compare_and_swap(&c->mux_lock, 0, 1))
        ptlog_syscall6(SYS_sched_yield, 0, 0, 0, 0, 0, 0);
    if (c->mux_fd < 0 || c->mux_pid != pid)
    {
        char path[512], *p = path;
        if (ptlog_devnull)
        {
            const char *s = "/dev/null"; while (*s) *p++ = *s++; *p = '\0';
        }
        else
        {
            const char *d = ptlog_dir;
            while (*d != '\0' && (p - path) < 400) *p++ = *d++;
            if (p != path && p[-1] != '/') *p++ = '/';
            *p++ = 'c'; *p++ = 'v'; *p++ = '.';
            p = ptlog_utoa(p, (unsigned long)pid);
            *p++ = '.'; *p++ = 'b'; *p++ = 'i'; *p++ = 'n'; *p = '\0';
        }
        long fd = ptlog_syscall6(SYS_openat, -100, (long)path,
            O_WRONLY|O_CREAT|O_TRUNC, 0644, 0, 0);
        if (fd >= 0 && !ptlog_devnull)
        {
            unsigned int hdr[4];
            hdr[0] = 0x57435450u;               /* "PTCW" little-endian */
            hdr[1] = 3u;                        /* multiplexed format version */
            hdr[2] = (unsigned int)ptlog_sync;
            hdr[3] = 0u;
            ptlog_syscall6(SYS_write, fd, (long)hdr, sizeof(hdr), 0, 0, 0);
        }
        c->mux_fd = fd;
        c->mux_pid = pid;
    }
    __atomic_store_n(&c->mux_lock, 0, __ATOMIC_RELEASE);
    return c->mux_fd;
}

/* Append one buffer as a tid-tagged block { u32 tid, u32 nbytes, <nbytes> } to
 * the shared mux fd, under the mux lock so blocks never interleave. */
static void ptlog_mux_write(long tid, unsigned long base, unsigned long n)
{
    struct ptlog_ctl *c = ptlog_ctl;
    long fd = ptlog_mux_open();
    if (fd < 0 || n == 0)
        return;
    while (!__sync_bool_compare_and_swap(&c->mux_lock, 0, 1))
        ptlog_syscall6(SYS_sched_yield, 0, 0, 0, 0, 0, 0);
    unsigned int bh[2]; bh[0] = (unsigned int)tid; bh[1] = (unsigned int)n;
    if (!ptlog_devnull)
    {
        ptlog_syscall6(SYS_write, fd, (long)bh, sizeof(bh), 0, 0, 0);
        unsigned long off = 0;
        while (off < n)
        {
            long w = ptlog_syscall6(SYS_write, fd, (long)(base + off),
                (long)(n - off), 0, 0, 0);
            if (w <= 0) break;
            off += (unsigned long)w;
        }
    }
    __atomic_store_n(&c->mux_lock, 0, __ATOMIC_RELEASE);
}

/*
 * Write one FULL buffer, whoever gets there first.  Returns 1 if this call
 * wrote a buffer.  The CAS makes the writer thread and the faulting target
 * thread mutually exclusive, and only the winner advances `tail', so buffers
 * reach the file in order.
 */
static int ptlog_drain_one(struct ptlog_tcb *tcb)
{
    long i = tcb->tail;
    if (i < 0 || i >= tcb->nbuf)
        return 0;
    if (tcb->pending != 0)
    {
        ptlog_finalise(tcb);            /* a spare: its cv file needs the tid */
        if (tcb->pending != 0)
            return 0;                   /* still unassigned: nothing to drain */
    }
    if (!__sync_bool_compare_and_swap(&tcb->st[i], PTLOG_FULL, PTLOG_WRITING))
        return 0;
    ptlog_sfence();                     /* --nt-store: see ptlog_sfence() */
    unsigned long base = ptlog_buf(tcb, i), n = tcb->len[i], off = 0;
    if (ptlog_cvmux)
    {
        /* One tid-tagged block into the shared process file. */
        ptlog_mux_write(tcb->tid, base, n);
        off = n;
    }
    else if (tcb->fd >= 0)
    {
        while (off < n)
        {
            long w = ptlog_syscall6(SYS_write, tcb->fd, (long)(base + off),
                (long)(n - off), 0, 0, 0);
            if (w <= 0)
                break;
            off += (unsigned long)w;
        }
    }
    tcb->bytes += off;
    tcb->tail = (i + 1) % tcb->nbuf;
    __atomic_store_n(&tcb->st[i], PTLOG_FREE, __ATOMIC_RELEASE);
    return 1;
}

/*
 * Publish the partly filled buffer of a thread that will never write
 * again (it has exited, or the process is going down).  Returns 1 if anything
 * was published.
 *
 * Setting `cursor' back to the buffer's base afterwards is what makes this safe
 * to call twice: the drain returns the buffer to PTLOG_FREE when it has written
 * it, so a second call -- ptlog_teardown() after the reaper, say -- would
 * otherwise see `cursor > base' with a FREE buffer and write the SAME bytes a
 * second time.  The owner is gone, so nothing reads `cursor' again.
 *
 * The `sfence' is for `--nt-store': the caller is
 * NOT the thread whose write-combining buffers hold the tail, so this fence is
 * not the one that retires them -- that one is in ptlogmt_start() and in the
 * kernel's own context switch.  It is here because it is free at this rate and
 * because a caller that IS the owner (teardown on the main thread) needs it.
 */
static int ptlog_publish_partial(struct ptlog_tcb *tcb)
{
    if (tcb == 0 || tcb->tid == 0 || tcb->arena == 0)
        return 0;
    long h = tcb->head;
    if (h < 0 || h >= tcb->nbuf)
        return 0;
    unsigned long base = ptlog_buf(tcb, h);
    if (!(tcb->cursor > base && tcb->cursor <= base + tcb->bufbytes))
        return 0;
    if (__atomic_load_n(&tcb->st[h], __ATOMIC_ACQUIRE) != PTLOG_FREE)
        return 0;
    tcb->len[h] = tcb->cursor - base;
    tcb->cursor = base;                 /* idempotent: see above */
    ptlog_sfence();
    __atomic_store_n(&tcb->st[h], PTLOG_FULL, __ATOMIC_RELEASE);
    return 1;
}

/*
 * Publish the tail of every thread that has EXITED while the process
 * is still running.  `tgkill(pid, tid, 0)' is the cheapest liveness test there
 * is, and a thread that is gone cannot race us: it will never execute another
 * value store, so its `head' buffer and its `cursor' are final.
 *
 * Called from the writer loop, rate-limited, so a thread-per-connection server
 * has every closed connection's values in the file rather than in memory.
 * Without it those tails wait for teardown -- and if the process is killed
 * rather than exiting, teardown never runs.
 */
static int ptlog_reap_dead(void)
{
    struct ptlog_ctl *c = ptlog_ctl;
    int did = 0;
    if (c == 0 || !ptlog_reap || ptlog_count)
        return 0;
    long n = c->ntcb;
    if (n > PTLOG_TCB_SLOTS) n = PTLOG_TCB_SLOTS;
    long pid = ptlog_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
    for (long i = 0; i < n; i++)
    {
        struct ptlog_tcb *tcb = *ptlog_tcb_slot(c, i);
        if (tcb == 0 || tcb->tid == 0)
            continue;
        if (tcb->reaped)
        {
            /* Published on an earlier sweep: recycle it once the drain has
             * finished with its ring. */
            did |= ptlog_recycle_tcb(tcb);
            continue;
        }
        if (tcb->tid == pid)            /* the main thread: alive until exit */
            continue;
        /* `tgkill(pid, tid, 0)' fails ONLY if that thread is gone: EPERM cannot
         * happen inside our own thread group and signal 0 is always valid.  The
         * test is `< 0' rather than `== -ESRCH' on purpose -- the E9Patch
         * runtime's raw syscall returns -errno while the LD_PRELOAD build goes
         * through glibc, which returns -1; comparing against -ESRCH silently
         * never matched in the second one. */
        if (ptlog_syscall6(SYS_tgkill, pid, tcb->tid, 0, 0, 0, 0) >= 0)
            continue;
        ptlog_finalise(tcb);            /* a spare that got a tid needs its fd */
        did |= ptlog_publish_partial(tcb);
        __atomic_store_n(&tcb->reaped, 1L, __ATOMIC_RELEASE);
    }
    return did;
}

/*
 * Hand a reaped TCB back to the spare pool, so the NEXT thread reuses
 * it instead of `ptlog_spare_refill' building one and `ntcb' growing past
 * PTLOG_TCB_SLOTS.  Returns 1 if the TCB was recycled.
 *
 * PRECONDITIONS, all of them necessary:
 *   * the owner is gone (`reaped', set only after tgkill(2) said ESRCH) and its
 *     tail has been published;
 *   * EVERY buffer is back to PTLOG_FREE, i.e. the drain has finished with the
 *     ring.  Recycling one instruction earlier would give the new owner a
 *     buffer the writer still considers the old thread's;
 *   * there is no ground-truth ring (accuracy builds are refused outright).
 *
 * The reset has to make the TCB INDISTINGUISHABLE from a fresh spare, and two
 * parts of it are correctness, not tidiness:
 *   * `count'/`total' go back to a fresh TCB's values (1 and 1 - sync).  Leaving the
 *     dead thread's running total would make the new thread's PTWRITE sync
 *     markers continue someone else's numbering, and the reconstructor's cv
 *     cursor is POSITIONAL -- the same class of problem as the shared keyframe
 *     countdown.
 *   * the per-thread KEYFRAME countdowns at %gs:PTLOG_COUNT_DATA go back to 1,
 *     or the new owner inherits the dead thread's keyframe phase.
 * The cv file is closed and re-opened under the NEW tid through the existing
 * `pending' protocol, so a recycled slot never appends to a dead thread's file.
 */
static int ptlog_recycle_tcb(struct ptlog_tcb *tcb)
{
    struct ptlog_ctl *c = ptlog_ctl;
    if (c == 0 || !ptlog_recycle || tcb == 0 || !tcb->reaped || tcb->tid == 0)
        return 0;
    if (tcb->gt_fd >= 0 || tcb->gt_map != 0)
        return 0;                       /* accuracy build: not worth the risk */
    for (long i = 0; i < tcb->nbuf; i++)
        if (__atomic_load_n(&tcb->st[i], __ATOMIC_ACQUIRE) != PTLOG_FREE)
            return 0;                   /* the drain has not finished */
    if (tcb->pending != 0)
        return 0;                       /* mid-naming; next sweep */
    /* Room in the ring?  If not, leave it reaped. */
    if (!__sync_bool_compare_and_swap(&c->rec_lock, 0, 1))
        return 0;
    long put = c->rec_put;
    long get = c->rec_get;
    if (put - get >= PTLOG_SPARE_MAX)
    {
        __atomic_store_n(&c->rec_lock, 0, __ATOMIC_RELEASE);
        return 0;
    }
    if (!ptlog_cvmux && tcb->fd >= 0)
        ptlog_syscall6(SYS_close, tcb->fd, 0, 0, 0, 0, 0);
    tcb->fd     = -1;
    tcb->head   = 0;
    tcb->tail   = 0;
    tcb->cursor = tcb->arena;
    tcb->sync_next = tcb->arena + 8;
    tcb->sync_bias = tcb->arena;
    tcb->count  = ptlog_sync > 0 ? 1UL : 0UL;
    tcb->total  = ptlog_sync > 0 ? (unsigned long)(1 - ptlog_sync) : 0UL;
    for (long i = 0; i < tcb->nbuf; i++) { tcb->st[i] = PTLOG_FREE; tcb->len[i] = 0; }
    if (ptlog_kf_n > 0)
    {
        unsigned long *kf =
            (unsigned long *)((unsigned char *)tcb + PTLOG_COUNT_DATA);
        for (long i = 0; i < ptlog_kf_n; i++)
            kf[i] = 1UL;
    }
    tcb->serial  = __sync_fetch_and_add(&c->spare_serial, 1);
    tcb->reaped  = 0;
    __atomic_store_n(&tcb->tid, 0L, __ATOMIC_RELEASE);      /* unowned again */
    __atomic_store_n(&tcb->pending, 1L, __ATOMIC_RELEASE);  /* name on assign */
    c->rec[put % PTLOG_SPARE_MAX] = tcb;
    __atomic_store_n(&c->rec_put, put + 1, __ATOMIC_RELEASE);
    __atomic_store_n(&c->rec_lock, 0, __ATOMIC_RELEASE);
    __sync_fetch_and_add(&c->recycled, 1);
    return 1;
}

/* Take a recycled TCB, or 0.  Both callers are in this file. */
static struct ptlog_tcb *ptlog_recycle_pop(void)
{
    struct ptlog_ctl *c = ptlog_ctl;
    struct ptlog_tcb *t = 0;
    if (c == 0 || !ptlog_recycle)
        return 0;
    if (__atomic_load_n(&c->rec_put, __ATOMIC_ACQUIRE) == c->rec_get)
        return 0;
    if (!__sync_bool_compare_and_swap(&c->rec_lock, 0, 1))
        return 0;
    long get = c->rec_get;
    if (get != c->rec_put)
    {
        t = c->rec[get % PTLOG_SPARE_MAX];
        __atomic_store_n(&c->rec_get, get + 1, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&c->rec_lock, 0, __ATOMIC_RELEASE);
    return t;
}

/* Drain everything that is currently FULL, for every registered thread. */
static int ptlog_drain_all(void)
{
    int did = 0;
    struct ptlog_ctl *c = ptlog_ctl;
    if (c == 0) return 0;
    long n = c->ntcb;
    if (n > PTLOG_TCB_SLOTS) n = PTLOG_TCB_SLOTS;
    for (long i = 0; i < n; i++)
    {
        struct ptlog_tcb *tcb = *ptlog_tcb_slot(c, i);
        if (tcb == 0) continue;
        while (ptlog_drain_one(tcb))
            did = 1;
    }
    return did;
}

/* ---- the writer thread ------------------------------------------------- */

#define PTLOG_STACK     (64 * 1024)

static void ptlog_nsleep(long ns)
{
    struct { long sec; long nsec; } ts = { 0, ns };
    ptlog_syscall6(SYS_nanosleep, (long)&ts, 0, 0, 0, 0, 0);
}

/*
 * Keep the writer OFF the traced thread's core.
 *
 * clone(2) inherits the CPU affinity, so the writer would land on exactly the
 * core the benchmark is pinned to -- it would both steal cycles from the timed
 * thread and, worse, DESCHEDULE it, which puts a gap in the Intel PT stream and
 * costs the reconstructor a decoder resync.
 * The complement of the inherited mask is intersected with the cpuset the task
 * is allowed anyway, so this only ever moves the writer to a core the process
 * could already use; if that set is empty the call fails and the inherited
 * affinity stands.  This is what `pt_capture2 --child-core' does for its own
 * drain thread, for the same reason.
 */
static void ptlog_writer_affinity(void)
{
    unsigned long cur[16], want[16];
    for (int i = 0; i < 16; i++) { cur[i] = 0; want[i] = 0; }
    if (ptlog_syscall6(SYS_sched_getaffinity, 0, sizeof(cur), (long)cur,
            0, 0, 0) < 0)
        return;
    int any = 0;
    for (int i = 0; i < 16; i++)
    {
        want[i] = ~cur[i];
        if (want[i] != 0) any = 1;
    }
    if (any)
        ptlog_syscall6(SYS_sched_setaffinity, 0, sizeof(want), (long)want,
            0, 0, 0);
}

static void ptlog_writer_main(void)
{
    struct ptlog_ctl *c = ptlog_ctl;
    ptlog_writer_affinity();
    c->writer = 1;
    long ticks = 0;
    for (;;)
    {
        ptlog_spare_refill(0);              /* Keep spares ready */
        /* Look for exited threads about every 5 ms of idle loop.
         * One tgkill(2) per registered TCB is cheap, but not at 50 kHz.
         * Once the registry is half full the ceiling is in sight and
         * a TCB recycled late is a TCB not recycled at all, so sweep every
         * iteration -- by then the scan is the cheapest thing in the loop
         * compared with losing a thread's whole stream. */
        long urgent = (c->ntcb * 2 > PTLOG_TCB_SLOTS);
        if (urgent || (++ticks & 0xff) == 0)
            ptlog_reap_dead();
        if (!ptlog_drain_all())
        {
            if (c->stop)
                break;
            ptlog_nsleep(20 * 1000);        /* 20 us */
        }
    }
    ptlog_reap_dead();
    c->writer = 0;
    ptlog_syscall6(SYS_exit, 0, 0, 0, 0, 0, 0);
}

#ifndef CLONE_VM
#define CLONE_VM        0x00000100
#define CLONE_FS        0x00000200
#define CLONE_FILES     0x00000400
#define CLONE_SIGHAND   0x00000800
#define CLONE_THREAD    0x00010000
#define CLONE_SYSVSEM   0x00040000
#define CLONE_SETTLS    0x00080000
#endif

/*
 * Raw clone(2).  The child cannot return through C (its stack is fresh), so it
 * jumps straight into ptlog_writer_main.  A dummy TLS block is installed with
 * CLONE_SETTLS so the writer never shares the creating thread's TCB.
 */
static void ptlog_start_writer(void)
{
    unsigned char *stack = (unsigned char *)ptlog_mmap_rw(PTLOG_STACK);
    unsigned long *tls   = (unsigned long *)ptlog_mmap_rw(4096);
    if (stack == 0 || tls == 0)
        return;                             /* fall back to inline writes */
    tls[0] = (unsigned long)tls;            /* TCB self-pointer at fs:0    */
    void *sp = (void *)((unsigned long)(stack + PTLOG_STACK) & ~15UL);
    long flags = CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|
                 CLONE_SYSVSEM|CLONE_SETTLS;
    /* This is an internal raw-clone worker, NOT an application
     * pthread. It has dummy TLS and inherits our GS base. A process-directed
     * application signal here could execute instrumented handlers against our
     * TCB and corrupt both streams. Block before clone (not in the child: that
     * leaves a delivery race), then restore the parent's exact original mask.
     * Linux x86-64's kernel sigset is 8 bytes, unlike libc's sigset_t. */
    unsigned long blocked = ~0UL, saved_mask = 0;
    if (ptlog_syscall6(SYS_rt_sigprocmask, 2 /* SIG_SETMASK */,
            (long)&blocked, (long)&saved_mask, sizeof(blocked), 0, 0) < 0)
        return;                            /* safe inline-drain fallback */
    register long r10 __asm__("r10") = 0;           /* child_tid           */
    register long r8  __asm__("r8")  = (long)tls;   /* newtls              */
    register void (*fn)(void) __asm__("r12") = ptlog_writer_main;
    long ret;
    __asm__ __volatile__ (
        "syscall\n\t"
        "testl %%eax,%%eax\n\t"
        "jnz 1f\n\t"
        "xorl %%ebp,%%ebp\n\t"
        "callq *%%r12\n\t"
        "movl $60,%%eax\n\t"                        /* SYS_exit            */
        "xorl %%edi,%%edi\n\t"
        "syscall\n\t"
        "1:\n\t"
        : "=a"(ret)
        : "a"((long)SYS_clone), "D"(flags), "S"(sp), "d"(0L),
          "r"(r10), "r"(r8), "r"(fn)
        : "rcx", "r11", "memory");
    (void)ret;
    ptlog_syscall6(SYS_rt_sigprocmask, 2 /* SIG_SETMASK */,
        (long)&saved_mask, 0, sizeof(saved_mask), 0, 0);
    /* Deliberately NO wait for the writer to come up.  It is not needed -- the
     * guard-page handler writes the buffer itself if the writer never appears --
     * and sleeping here deschedules the thread that Intel PT is tracing, which
     * puts a gap in the trace right at start-up. */
}

/* ---- per-thread setup -------------------------------------------------- */

/*
 * Allocate and initialise one TCB (its ring + guard pages, gt window, or count
 * array) for thread `tid' and register it.  `spare' = 1 allocates on behalf of
 * a thread that does not exist yet: tid 0, no cv fd (the file name
 * needs the tid), gt file under a provisional name -- ptlog_finalise() fixes
 * both once the tracer has written the tid.  Does NOT touch %gs.
 */
/*
 * How many threads does this process have RIGHT NOW?  /proc/self/status's
 * `Threads:' line, read with raw syscalls into a stack buffer -- this runs from
 * the SIGSEGV fixup path that creates a TCB lazily, so it must not allocate and
 * must not take a lock.  Returns 1 if /proc is unreadable.
 */
static long ptlog_nthreads(void)
{
    char buf[4096];
    long fd = ptlog_syscall6(SYS_openat, -100, (long)"/proc/self/status",
                             O_RDONLY | O_CLOEXEC, 0, 0, 0);
    if (fd < 0)
        return 1;
    long n = 0, got;
    while (n < (long)sizeof(buf) - 1 &&
           (got = ptlog_syscall6(SYS_read, fd, (long)(buf + n),
                                 (long)sizeof(buf) - 1 - n, 0, 0, 0)) > 0)
        n += got;
    ptlog_syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
    buf[n < 0 ? 0 : n] = '\0';
    for (long i = 0; i + 8 < n; i++)
        if (buf[i] == 'T' && strncmp(buf + i, "Threads:", 8) == 0)
        {
            long v = 0, j = i + 8;
            while (j < n && (buf[j] == ' ' || buf[j] == '\t')) j++;
            while (j < n && buf[j] >= '0' && buf[j] <= '9') v = v * 10 + (buf[j++] - '0');
            return v > 0 ? v : 1;
        }
    return 1;
}

/*
 * This thread's share of the process-wide ring budget, in BYTES of
 * value buffer (guard pages excluded -- ptlog_make_tcb charges the real figure
 * once it has rounded).  0 = no budget in force, use the global ptlog_bufvals.
 */
static long ptlog_ring_share(void)
{
    if (ptlog_ring_total_mb <= 0 || ptlog_ctl == 0)
        return 0;
    long nb      = ptlog_nbuf > 0 ? ptlog_nbuf : 1;
    long budget  = ptlog_ring_total_mb * (1L << 20);
    long floorb  = ptlog_ring_floor_kb * 1024;
    long percap  = ptlog_bufvals * 8 * nb;           /* what PTLOG_RING_MB asks for */
    /* How many TCBs will exist?  The live threads PLUS the spare pool: a spare
     * is a fully built TCB waiting for a thread that does not exist yet, and it owns a
     * ring exactly like a live one.  Counting only the live threads would give the first
     * spares large rings and leave the later ones on the floor. */
    long nt      = ptlog_nthreads() + (ptlog_spares > 0 ? ptlog_spares : 0);
    long ntcb    = ptlog_ctl->ntcb + 1;              /* the one being made */
    if (nt < ptlog_ring_min_tcbs) nt = ptlog_ring_min_tcbs;
    if (nt < ntcb) nt = ntcb;
    long share = budget / (nt > 0 ? nt : 1);
    if (share > percap) share = percap;              /* never MORE than asked for */
    if (share < floorb) share = floorb;
    long used = ptlog_ctl->ring_used, rem = budget - used;
    if (share > rem)
    {
        share = rem > floorb ? rem : floorb;
        if (rem < floorb)
            __sync_fetch_and_add(&ptlog_ctl->ring_over, 1);
    }
    if (share < 4096 * 8) share = 4096 * 8;          /* the 4096-value minimum */
    return share;
}

static struct ptlog_tcb *ptlog_make_tcb(long tid, int spare)
{
    long nbuf = ptlog_nbuf;
    /* Size THIS thread's ring out of the process-wide budget. */
    long myshare = ptlog_count ? 0 : ptlog_ring_share();
    long mybufvals = myshare > 0 ? myshare / (nbuf * 8) : ptlog_bufvals;
    if (mybufvals < 4096) mybufvals = 4096;
    unsigned long bufbytes = (unsigned long)mybufvals * 8;
    bufbytes = (bufbytes + 4095) & ~4095UL;
    unsigned long stride = bufbytes + PTLOG_GUARD;
    if (ptlog_huge && !ptlog_count)
    {
        /* Whole huge pages per buffer, and a whole huge page of slack after it
         * to hold the 4 KiB guard page: the `mprotect' then splits only the
         * slack, so every byte the trampoline stores to is inside a 2 MiB-
         * aligned, 2 MiB-multiple range that THP can back. */
        bufbytes = (bufbytes + PTLOG_HPAGE - 1) & ~(PTLOG_HPAGE - 1);
        if (bufbytes == 0) bufbytes = PTLOG_HPAGE;
        stride = bufbytes + PTLOG_HPAGE;
    }
    long pid = ptlog_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
    long serial = spare ? __sync_fetch_and_add(&ptlog_ctl->spare_serial, 1) : 0;

    if (ptlog_count)
    {
        /* Count sink: no buffer arena, no guard pages, no cv file, no gt ring.
         * The counters are EMBEDDED in the %gs region at PTLOG_COUNT_DATA, so
         * the trampoline addresses them as `incq %gs:disp' with no pointer to
         * load -- exactly like the buffer cursor, so an invalid %gs faults the
         * same way and is fixed up lazily.  One mmap holds the TCB header
         * (page 0) and the N counters after it (MAP_ANONYMOUS is zero-filled). */
        unsigned long rlen =
            (PTLOG_COUNT_DATA + (unsigned long)ptlog_count_n * 8 + 4095) & ~4095UL;
        unsigned char *region = (unsigned char *)ptlog_mmap_rw(rlen);
        if (region == 0)
            return 0;
        struct ptlog_tcb *ctcb = (struct ptlog_tcb *)region;
        ctcb->count_arr = (unsigned long)(region + PTLOG_COUNT_DATA);
        ctcb->tid   = tid;
        ctcb->pid   = pid;
        ctcb->fd    = -1;
        ctcb->gt_fd = -1;
        ctcb->magic = PTLOG_MAGIC;
        ctcb->ctl   = ptlog_ctl;
        ctcb->abi   = PTLOG_ABI;
        ctcb->pending = 0;          /* nothing to name for the count sink */
        ctcb->serial  = serial;
        long slot = __sync_fetch_and_add(&ptlog_ctl->ntcb, 1);
        if (slot < PTLOG_TCB_SLOTS)
            *ptlog_tcb_slot(ptlog_ctl, slot) = ctcb;
        return ctcb;
    }

    /* The TCB page, plus this thread's own keyframe countdowns when the build
     * has them (--kf-gs).  MAP_ANONYMOUS is zero-filled and a keyframe cell
     * must start at 1, so the array is initialised explicitly. */
    unsigned long tcblen = 4096;
    if (ptlog_kf_n > 0)
        tcblen = (PTLOG_COUNT_DATA + (unsigned long)ptlog_kf_n * 8 + 4095)
                 & ~4095UL;
    struct ptlog_tcb *tcb = (struct ptlog_tcb *)ptlog_mmap_rw(tcblen);
    if (tcb == 0)
        return 0;
    if (ptlog_kf_n > 0)
    {
        unsigned long *kf =
            (unsigned long *)((unsigned char *)tcb + PTLOG_COUNT_DATA);
        for (long i = 0; i < ptlog_kf_n; i++)
            kf[i] = 1UL;
    }
    unsigned char *arena = (unsigned char *)(
        (ptlog_huge && !ptlog_count)
            ? ptlog_mmap_rw_aligned(stride * (unsigned long)nbuf, PTLOG_HPAGE)
            : ptlog_mmap_rw(stride * (unsigned long)nbuf));
    if (arena == 0)
        return 0;
    /* Charge the BUDGET only for memory that is actually touched: in
     * PTLOG_HUGE mode `stride' carries a whole 2 MiB of slack per buffer of
     * which only the 4 KiB guard page is ever mapped-and-faulted, and charging
     * it would floor every later thread for nothing. */
    __sync_fetch_and_add(&ptlog_ctl->ring_used,
        (long)((bufbytes + PTLOG_GUARD) * (unsigned long)nbuf));
    __sync_fetch_and_add(&ptlog_ctl->ring_threads, 1);
    /* One PROT_NONE guard page after each buffer: the store faults there
     * exactly when that buffer is full. */
    for (long i = 0; i < nbuf; i++)
        ptlog_syscall6(SYS_mprotect,
            (long)(arena + (unsigned long)i * stride + bufbytes),
            PTLOG_GUARD, PROT_NONE, 0, 0, 0);
    /* ... and, in PTLOG_HUGE mode, ask for 2 MiB pages for the buffers
     * themselves.  AFTER the mprotect, so the advice lands only on the buffer
     * VMAs and is not lost in the split. */
    if (ptlog_huge && !ptlog_count)
        for (long i = 0; i < nbuf; i++)
            ptlog_syscall6(SYS_madvise,
                (long)(arena + (unsigned long)i * stride),
                (long)bufbytes, MADV_HUGEPAGE, 0, 0, 0);

    tcb->arena     = (unsigned long)arena;
    tcb->arena_end = (unsigned long)arena + stride * (unsigned long)nbuf;
    tcb->stride    = stride;
    tcb->bufbytes  = bufbytes;
    tcb->nbuf      = nbuf;
    tcb->head      = 0;
    tcb->tail      = 0;
    tcb->cursor    = (unsigned long)arena;
    tcb->sync_next = (unsigned long)arena + 8;
    tcb->sync_bias = (unsigned long)arena;
    /* A fresh TCB fires its FIRST
     * sync marker at its first logged value -- count 1, total 1 - SYNC, so the
     * marker's `TOTAL += SYNC - COUNT' yields exactly N for a first site that
     * logs N -- and every later interval is unchanged.  The reconstructor's
     * positional cv cursor is thereby aligned before the first value-dependent
     * address (code that ran unpatched before the E9Patch loader installed the
     * image's pages makes the decoder consume phantom slots until a marker). */
    tcb->count     = ptlog_sync > 0 ? 1UL : 0UL;
    tcb->total     = ptlog_sync > 0 ? (unsigned long)(1 - ptlog_sync) : 0UL;
    tcb->tid       = tid;
    tcb->pid       = pid;
    /* In mux mode there is ONE shared process file, opened lazily by
     * the drain -- no per-thread fd.  A spare's file waits for its tid. */
    tcb->fd        = (ptlog_cvmux || spare) ? -1 : ptlog_open_cv(tid, pid, 1);
    tcb->magic     = PTLOG_MAGIC;
    tcb->ctl       = ptlog_ctl;
    tcb->abi       = PTLOG_ABI;
    tcb->pending   = spare ? 1 : 0;
    tcb->serial    = serial;
    ptlog_gt_init(tcb, tid, pid, spare ? serial : -1);
    for (long i = 0; i < nbuf; i++) { tcb->st[i] = PTLOG_FREE; tcb->len[i] = 0; }

    long slot = __sync_fetch_and_add(&ptlog_ctl->ntcb, 1);
    if (slot < PTLOG_TCB_SLOTS)
        *ptlog_tcb_slot(ptlog_ctl, slot) = tcb;
    else if (__sync_bool_compare_and_swap(&ptlog_ctl->warned_ntcb, 0, 1))
        /* Past PTLOG_TCB_SLOTS a TCB is not in the
         * registry, so neither the reaper nor the teardown can ever find its
         * buffers: that thread's cv stream is silently truncated at its last
         * FULL buffer.  Raising the constant moves every offset in
         * struct ptlog_ctl, so ptlog_abi.h and offline/pt_capture2.c have to
         * move with it and PTLOG_ABI has to be bumped -- hence a LOUD WARNING
         * here rather than a quiet change. */
        ptlog_warn("[ptlogrt] *** more than PTLOG_TCB_SLOTS threads: this "
                   "thread's TCB is NOT registered, so its buffers are never "
                   "drained and its cv stream is TRUNCATED. ***\n");
    return tcb;
}

/* The calling thread's own TCB, installed as its %gs base. */
static struct ptlog_tcb *ptlog_new_tcb(void)
{
    long tid = ptlog_syscall6(SYS_gettid, 0, 0, 0, 0, 0, 0);
    /* Reuse a dead thread's TCB before building another one, so a
     * process that creates thousands of short-lived threads keeps `ntcb' at its
     * CONCURRENCY rather than its thread count. */
    struct ptlog_tcb *tcb = ptlog_recycle_pop();
    if (tcb != 0)
    {
        tcb->tid = tid;
        ptlog_finalise(tcb);            /* names the cv file under the NEW tid */
    }
    else
        tcb = ptlog_make_tcb(tid, 0);
    if (tcb != 0)
        ptlog_arch_prctl(ARCH_SET_GS, (unsigned long)tcb);
    return tcb;
}

/*
 * Keep PTLOG_SPARE_LOW spare TCBs ready in the ring for the ptracer
 * (see struct ptlog_ctl).  Called from init, from the writer thread's loop and
 * from the SIGSEGV handler (`in_handler': never wait for the lock there).
 */
static void ptlog_spare_refill(int in_handler)
{
    struct ptlog_ctl *c = ptlog_ctl;
    if (c == 0 || !ptlog_ready || ptlog_spares <= 0)
        return;
    long want = ptlog_spares < PTLOG_SPARE_MAX ? ptlog_spares : PTLOG_SPARE_MAX;
    long avail = __atomic_load_n(&c->spare_put, __ATOMIC_ACQUIRE) -
                 __atomic_load_n(&c->spare_get, __ATOMIC_ACQUIRE);
    if (avail >= (in_handler ? PTLOG_SPARE_LOW : want))
        return;
    if (!__sync_bool_compare_and_swap(&c->spare_lock, 0, 1))
        return;                             /* another thread is refilling */
    for (;;)
    {
        long put = c->spare_put;
        long get = __atomic_load_n(&c->spare_get, __ATOMIC_ACQUIRE);
        if (put - get >= want || put - get >= PTLOG_SPARE_MAX)
            break;
        struct ptlog_tcb *t = ptlog_recycle_pop();   /* recycled TCBs first */
        if (t == 0)
            t = ptlog_make_tcb(0, 1);
        if (t == 0)
            break;
        c->spare[put % PTLOG_SPARE_MAX] = t;
        __atomic_store_n(&c->spare_put, put + 1, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&c->spare_lock, 0, __ATOMIC_RELEASE);
}

/*
 * A spare TCB has been given to a thread (the tracer wrote `tid'): open
 * its cv file and give its gt file the final name.  Idempotent and safe to call
 * from any thread (CAS on `pending'); called by the writer loop, by the drain
 * (which needs the fd), by any fault of ours and by the teardown.
 */
static void ptlog_finalise(struct ptlog_tcb *tcb)
{
    if (tcb == 0 || tcb->pending != 1 || tcb->tid == 0)
        return;
    if (!__sync_bool_compare_and_swap(&tcb->pending, 1, 2))
        return;
    long tid = tcb->tid, pid = tcb->pid;
    if (!ptlog_count && !ptlog_cvmux && tcb->fd < 0)
        tcb->fd = ptlog_open_cv(tid, pid, 1);
    if (tcb->gt_fd >= 0)
    {
        char oldp[512], newp[512];
        ptlog_gt_path(oldp, pid, -1, tcb->serial);
        ptlog_gt_path(newp, pid, tid, 0);
        ptlog_syscall6(SYS_renameat, -100 /*AT_FDCWD*/, (long)oldp,
            -100, (long)newp, 0, 0);
        unsigned int t32 = (unsigned int)tid;   /* header word 2: tid */
        ptlog_syscall6(SYS_pwrite64, tcb->gt_fd, (long)&t32, 4, 8, 0, 0);
    }
    __atomic_store_n(&tcb->pending, 0, __ATOMIC_RELEASE);
}

/*
 * The guard page faulted: publish the buffer that just filled up and move to
 * the next one.  Returns the address the store must be retried at, or 0 if the
 * fault was not ours.
 */
static unsigned long ptlog_rotate(struct ptlog_tcb *tcb, unsigned long cur)
{
    /* Linux INHERITS the GS base across clone(2)
     * (copy_thread(): `p->thread.gsbase = me->thread.gsbase'), so a thread that
     * was not started through rt/ptlogmt.so's pthread_create shim arrives here
     * still pointing at its creator's TCB -- it has been storing values into
     * another thread's buffer, unsynchronised, since it started.  Give it its
     * own TCB so it stops making things worse, and say so once: the cv files of
     * this run are NOT trustworthy. */
    if (ptlog_syscall6(SYS_gettid, 0, 0, 0, 0, 0, 0) != tcb->tid)
    {
        if (__sync_bool_compare_and_swap(&tcb->ctl->warned, 0, 1))
            ptlog_warn("[ptlogrt] *** a thread reached the buffer sink with an "
                       "INHERITED %gs base: its values went into another "
                       "thread's buffer.  The cv files of this run are "
                       "interleaved and incomplete.  Preload "
                       "runtime/rt/ptlogmt.so.\n");
        struct ptlog_tcb *mine = ptlog_new_tcb();
        if (mine != 0)
            return mine->cursor;
        return 0;
    }

    long h = tcb->head;
    unsigned long base = ptlog_buf(tcb, h);
    if (cur < base || cur > base + tcb->bufbytes)
        cur = base + tcb->bufbytes;         /* be conservative */

    /* A fork(2)ed child inherits the parent's fd and would interleave its
     * values into the parent's file.  Re-open under the new pid instead; this
     * costs one getpid(2) per FULL buffer, i.e. once per PTLOG_BUFVALS values. */
    long pid = ptlog_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
    if (pid != tcb->pid)
    {
        tcb->pid = pid;
        if (!ptlog_cvmux)               /* mux re-opens the shared fd itself */
            tcb->fd = ptlog_open_cv(tcb->tid, pid, 1);
    }

    tcb->len[h] = cur - base;
    tcb->n_flush++;
    /* --nt-store: retire this thread's write-combining buffers before the
     * drain (another thread) may read the buffer.  A release store is a plain
     * store on x86 and does NOT order non-temporal stores. */
    ptlog_sfence();
    __atomic_store_n(&tcb->st[h], PTLOG_FULL, __ATOMIC_RELEASE);

    long n = (h + 1) % tcb->nbuf;
    long spins = 0;
    while (__atomic_load_n(&tcb->st[n], __ATOMIC_ACQUIRE) != PTLOG_FREE)
    {
        if (spins == 0)
            tcb->n_stall++;
        if (++spins > PTLOG_SPINS)
        {
            /* No writer thread (or it cannot keep up): write the oldest buffer
             * here.  ptlog_drain_one CASes, so this never races the writer. */
            if (ptlog_drain_one(tcb))
                tcb->n_inline++;
            else
                ptlog_syscall6(SYS_sched_yield, 0, 0, 0, 0, 0, 0);
        }
        else
            ptlog_syscall6(SYS_sched_yield, 0, 0, 0, 0, 0, 0);
    }
    const unsigned long next = ptlog_buf(tcb, n);
    /* At a guard fault, cur-base bytes have really been filled, including
     * stores earlier in a multi-value site whose GS cursor is still stale.
     * Use the fault boundary, NEVER that unpublished GS cursor. Unsigned
     * arithmetic deliberately also handles wrap from the last ring buffer. */
    const unsigned long rebase = next - cur;
    tcb->sync_next += rebase;
    tcb->sync_bias += rebase;
    tcb->head   = n;
    tcb->cursor = next;
    return tcb->cursor;
}


/* ---- ground-truth address channel -------------------------------------- */

/*
 * Open (truncating) this thread's gt file and write its 32-byte header.
 *   u32 magic "PTGT" | u32 version | u32 tid | u32 record bytes | 16 reserved
 * The records that follow are pairs of little-endian 64-bit words
 * { effective address, original instruction address }, in program order.
 */
static void ptlog_gt_path(char *path, long pid, long tid, long serial)
{
    char *p = path;
    const char *d = ptlog_gt_dir;
    while (*d != '\0' && (p - path) < 400) *p++ = *d++;
    if (p != path && p[-1] != '/') *p++ = '/';
    *p++ = 'g'; *p++ = 't'; *p++ = '.';
    p = ptlog_utoa(p, (unsigned long)pid);
    *p++ = '.';
    if (tid < 0)                        /* a spare: gt.<pid>.s<serial>.bin */
        { *p++ = 's'; p = ptlog_utoa(p, (unsigned long)serial); }
    else
        p = ptlog_utoa(p, (unsigned long)tid);
    *p++ = '.'; *p++ = 'b'; *p++ = 'i'; *p++ = 'n'; *p = '\0';
}

static long ptlog_open_gt(long tid, long pid, long serial)
{
    char path[512];
    ptlog_gt_path(path, pid, serial >= 0 ? -1 : tid, serial);
    long fd = ptlog_syscall6(SYS_openat, -100 /*AT_FDCWD*/, (long)path,
        O_RDWR|O_CREAT|O_TRUNC, 0644, 0, 0);
    return fd;
}

/*
 * Map the window of the gt file that starts at `off'.  The window is followed
 * by a PROT_NONE guard page, so the FIRST store past its end faults and lands
 * in ptlog_segv() -> ptlog_gt_rotate().  Returns the mapping, or 0.
 */
static unsigned long ptlog_gt_mapwin(struct ptlog_tcb *tcb, unsigned long off)
{
    unsigned long len = tcb->gt_win;
    if (ptlog_syscall6(SYS_ftruncate, tcb->gt_fd,
            (long)(off + len), 0, 0, 0, 0) < 0)
        return 0;
    long r = ptlog_syscall6(SYS_mmap, 0, (long)(len + PTLOG_GUARD), PROT_NONE,
        MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (r < 0 && r > -4096)
        return 0;
    unsigned long base = (unsigned long)r;
    r = ptlog_syscall6(SYS_mmap, (long)base, (long)len, PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_FIXED, tcb->gt_fd, (long)off);
    if (r < 0 && r > -4096)
    {
        ptlog_syscall6(SYS_munmap, (long)base, (long)(len + PTLOG_GUARD),
            0, 0, 0, 0);
        return 0;
    }
    tcb->gt_map = base;
    tcb->gt_end = base + len;
    tcb->gt_off = off;
    tcb->gt_rot++;
    return base;
}

/* `serial' >= 0: a spare TCB -- provisional file name, header tid 0. */
static void ptlog_gt_init(struct ptlog_tcb *tcb, long tid, long pid, long serial)
{
    tcb->gt_cursor = 0; tcb->gt_map = 0; tcb->gt_end = 0;
    tcb->gt_fd = -1; tcb->gt_off = 0; tcb->gt_rot = 0;
    tcb->gt_win = (unsigned long)ptlog_gt_win;
    if (!ptlog_gt)
        return;
    tcb->gt_fd = ptlog_open_gt(tid, pid, serial);
    if (tcb->gt_fd < 0)
    {
        ptlog_warn("[ptlogrt] gt: cannot create the gt file; "
                   "the gt trampolines will fault\n");
        return;
    }
    if (ptlog_gt_mapwin(tcb, 0) == 0)
    {
        ptlog_warn("[ptlogrt] gt: cannot map the gt window\n");
        return;
    }
    unsigned int *h = (unsigned int *)tcb->gt_map;
    h[0] = PTLOG_GT_MAGIC; h[1] = PTGT_VERSION;
    h[2] = (unsigned int)tid; h[3] = PTLOG_GT_REC;
    for (int i = 4; i < 8; i++) h[i] = 0;
    tcb->gt_cursor = tcb->gt_map + PTLOG_GT_HDR;
}

/*
 * The gt window's guard page faulted: unmap it (which schedules the writeback)
 * and map the next one.  Returns the address the store must be retried at.
 */
static unsigned long ptlog_gt_rotate(struct ptlog_tcb *tcb)
{
    unsigned long off = tcb->gt_off + tcb->gt_win;
    if (ptlog_gt_max > 0 && off >= (unsigned long)ptlog_gt_max)
    {
        /* The window the comparison covers is full.  Flush everything (the
         * value buffers included -- a `--sink buffer' build would otherwise
         * lose its last buffer) and stop the process here, so the PT capture
         * ends at the same instruction the ground truth does. */
        ptlog_warn("[ptlogrt] gt: PTLOG_GT_MAX reached; flushing and exiting\n");
        ptlog_teardown();
        ptlog_syscall6(SYS_exit_group, 0, 0, 0, 0, 0, 0);
    }
    ptlog_syscall6(SYS_munmap, (long)tcb->gt_map,
        (long)(tcb->gt_win + PTLOG_GUARD), 0, 0, 0, 0);
    if (ptlog_gt_mapwin(tcb, off) == 0)
        return 0;
    tcb->gt_cursor = tcb->gt_map;
    return tcb->gt_cursor;
}

/*
 * The gt channel is deliberately NOT torn down.
 *
 * The teardown runs from a DT_FINI, and in a whole-program build the code that
 * runs after it -- glibc's own exit path -- is instrumented too, so it keeps
 * executing gt trampolines.  Closing the channel here (unmap the window, zero
 * the cursor) made every one of those stores fault: the smoke test printed its
 * output correctly and then died with SIGSEGV in `exit'.  Truncating the file
 * while leaving it mapped is no better -- a store past the new EOF is SIGBUS.
 *
 * So the window stays mapped and the cursor stays valid until the process
 * really dies, and the file keeps the tail of its last window.  Those bytes are
 * never written, so the file is SPARSE (its apparent size is a multiple of
 * PTLOG_GT_WIN) and costs no disk; consumers stop at the first record with a
 * zero `ip', which is what SPEC_FORMAT section 3 says and what
 * `offline/ptrecon --gt-in' does.  MAP_SHARED pages are the page cache, so
 * nothing has to be flushed either.
 */
static void ptlog_gt_close(struct ptlog_tcb *tcb)
{
    (void)tcb;
}

/* ucontext_t on x86-64: uc_flags(8) uc_link(8) uc_stack(24) then gregs[23]. */
#ifdef PTLOG_E9RT
#define PTLOG_SI_ADDR(si)   ((si)->_sifields._sigfault.si_addr)
#else
#define PTLOG_SI_ADDR(si)   ((si)->si_addr)
#endif
#define PTLOG_GREGS(uc)     ((long *)((char *)(uc) + 40))
#define PTLOG_REG_R11       3
#define PTLOG_REG_RIP       16

/*
 * x86 register encoding (0=rax .. 15=r15) -> index into ucontext gregs[]
 * (REG_R8=0 .. REG_R15=7, RDI=8, RSI=9, RBP=10, RBX=11, RDX=12, RAX=13,
 * RCX=14, RSP=15).
 */
static const signed char ptlog_greg_of[16] =
{
    13, 14, 12, 11, 15, 10,  9,  8,     /* rax rcx rdx rbx rsp rbp rsi rdi */
     0,  1,  2,  3,  4,  5,  6,  7      /* r8 .. r15                        */
};

/*
 * Decode the faulting value store and return the x86 encoding of the base
 * register (the cursor), or -1 if this is not one of ours.
 *
 *   mov    %val,disp(%cur)    REX.W 89 /r          -- the ordinary store
 *   movnti %val,disp(%cur)    REX.W 0F C3 /r       -- `--nt-store' builds
 *   movq   $imm,disp(%cur)    REX.W C7 /0 id       -- `--log-blocks' builds
 *
 * The C7 form stores a COMPILE-TIME CONSTANT -- a basic-block identifier --
 * and so needs no source register; its reg field is the /0 opcode extension
 * rather than a register number, which costs this decoder nothing because the
 * only thing it reports is the BASE.  The trailing imm32 is never read: the
 * displacement comes from `si_addr', not from the instruction.
 *
 * mod 0 or 1, no index.  The three share their ModRM/SIB/disp encoding exactly
 * (runtime/e9plugin/ptlog.cpp, emitMovToMemDisp / emitMovNtToMemDisp), so only
 * the opcode test differs, and `si_addr' still gives the displacement.  The
 * emitter never emits any other addressing form for a value store.
 */
static int ptlog_store_base(const unsigned char *rip)
{
    int rex = 0, modrm, mod, rm, base;
    if (rip == 0)
        return -1;
    if ((rip[0] & 0xf0) == 0x40)
        rex = *rip++;
    if (rip[0] == 0x0f && rip[1] == 0xc3)   /* movnti r64 -> m64 (--nt-store) */
        rip += 2;
    else if (rip[0] == 0xc7)            /* movq imm32 -> m64 (--log-blocks)  */
        rip++;
    else if (*rip++ != 0x89)            /* mov r64 -> r/m64                  */
        return -1;
    modrm = *rip++;
    mod = (modrm >> 6) & 3;
    rm  = modrm & 7;
    if (mod == 3)                       /* register destination: not a store */
        return -1;
    if (rm == 4)                        /* SIB: base only, index must be none */
    {
        int sib = *rip++;
        if (((sib >> 3) & 7) != 4)
            return -1;
        base = sib & 7;
    }
    else if (rm == 5 && mod == 0)       /* rip-relative                      */
        return -1;
    else
        base = rm;
    return base | ((rex & 1) << 3);
}

static struct sigaction ptlog_old_segv;

/* The SIGSEGV chain target -- (handler, flags), where handler 0/1 is
 * SIG_DFL/SIG_IGN -- read from / written to the shared control block when the
 * primary publishes one, else this copy's static `ptlog_old_segv'. */
static void ptlog_chain_get(unsigned long *h, unsigned long *f)
{
    if (ptlog_ctl != 0 && ptlog_ctl->segv_chain_abi != 0)
    {
        *h = ptlog_ctl->segv_chain_handler;
        *f = ptlog_ctl->segv_chain_flags;
        return;
    }
    *f = (unsigned long)ptlog_old_segv.sa_flags;
    *h = (*f & SA_SIGINFO) != 0 ? (unsigned long)ptlog_old_segv.sa_sigaction
                                : (unsigned long)ptlog_old_segv.sa_handler;
}

static void ptlog_chain_set(unsigned long h, unsigned long f)
{
    if (ptlog_ctl != 0 && ptlog_ctl->segv_chain_abi != 0)
    {
        ptlog_ctl->segv_chain_handler = h;
        ptlog_ctl->segv_chain_flags = f;
        return;
    }
    memset(&ptlog_old_segv, 0, sizeof(ptlog_old_segv));
    ptlog_old_segv.sa_flags = f;
    if ((f & SA_SIGINFO) != 0)
        ptlog_old_segv.sa_sigaction = (void (*)(int, siginfo_t *, void *))h;
    else
        ptlog_old_segv.sa_handler = (void (*)(int))h;
}

static void ptlog_segv(int sig, siginfo_t *si, void *ucv);

/* The primary publishes its handler and the action it displaced, so that the
 * LD_PRELOADed copy's sigaction() interposer can keep the chain current. */
static void ptlog_chain_publish(void)
{
    unsigned long f = (unsigned long)ptlog_old_segv.sa_flags;
    if (ptlog_ctl == 0)
        return;
    ptlog_ctl->segv_handler = (unsigned long)ptlog_segv;
    ptlog_ctl->segv_chain_flags = f;
    ptlog_ctl->segv_chain_handler = (f & SA_SIGINFO) != 0
        ? (unsigned long)ptlog_old_segv.sa_sigaction
        : (unsigned long)ptlog_old_segv.sa_handler;
    ptlog_ctl->segv_chain_abi = 1;
}

static void ptlog_segv(int sig, siginfo_t *si, void *ucv)
{
    /* Rotation publishes a full buffer before switching head/cursor. An
     * asynchronous application handler during that interval can enter the
     * same ring and publish it twice. Defer async delivery until sigreturn,
     * which restores the interrupted context's mask. Leave synchronous fault
     * signals alone, and undo this change before chaining a non-runtime fault.
     * This does NOT close the trampoline's load/store/commit signal window. */
    unsigned long async_mask = ~((1UL << (SIGSEGV-1)) | (1UL << (SIGBUS-1)) |
        (1UL << (SIGILL-1)) | (1UL << (SIGFPE-1)) | (1UL << (SIGTRAP-1)));
    unsigned long entry_mask = 0;
    long masked = ptlog_syscall6(SYS_rt_sigprocmask, 0 /* SIG_BLOCK */,
        (long)&async_mask, (long)&entry_mask, sizeof(async_mask), 0, 0);
    unsigned long gs = 0;
    ptlog_arch_prctl(ARCH_GET_GS, (unsigned long)&gs);
    long *gregs = PTLOG_GREGS(ucv);
    unsigned long addr = (unsigned long)PTLOG_SI_ADDR(si);
    if (gs == 0)
    {
        /* Thread with no %gs base yet: the fault is a %gs-relative access
         * (`mov %gs:0,%r11' for the buffer sink at addr < 4096, or the count
         * sink's `incq %gs:disp' at addr up to PTLOG_COUNT_DATA + N*8).  A
         * leading 0x65 (or 0xf0 0x65 for `lock') is the %gs override, which only
         * our trampolines emit here, so give the thread its own region.
         *
         * NOTE: this path is NOT how a pthread gets its region in a
         * whole-program build -- glibc has every signal blocked while the new
         * thread runs start_thread, so a fault there would be fatal.  The
         * ptracer hands the thread a spare TCB before its first instruction
         * instead (ptlog_abi.h); this path serves the shim's fallback (a thread
         * whose creator had no spare) and the count sink's early start-up. */
        const unsigned char *rip = (const unsigned char *)gregs[PTLOG_REG_RIP];
        unsigned long lim = ptlog_count
            ? (unsigned long)(PTLOG_COUNT_DATA + ptlog_count_n * 8 + 4096)
            : (ptlog_kf_n > 0
               ? (unsigned long)(PTLOG_COUNT_DATA + ptlog_kf_n * 8 + 4096)
               : 4096UL);
        int is_gs = (rip != 0 && (rip[0] == 0x65 ||
                                  (rip[0] == 0xf0 && rip[1] == 0x65)));
        if (ptlog_ready && addr < lim && is_gs)
        {
            if (ptlog_new_tcb() != 0)
            {
                ptlog_spare_refill(1);
                return;                 /* re-execute the faulting access */
            }
        }
    }
    else
    {
        struct ptlog_tcb *tcb = (struct ptlog_tcb *)gs;
        /* A spare handed to this thread by the tracer: name its files now
         * (cheap no-op once done), and keep the pool topped up. */
        ptlog_finalise(tcb);
        if (tcb->gt_map != 0 && addr >= tcb->gt_end &&
                addr < tcb->gt_end + PTLOG_GUARD)
        {
            /* The gt window is full: map the next one and retry the store.
             * The record is 16 bytes and the window is page-aligned, so the
             * FIRST of a record's two stores is always the one that faults
             * and its displacement is 0. */
            int b = ptlog_store_base(
                (const unsigned char *)gregs[PTLOG_REG_RIP]);
            unsigned long next = ptlog_gt_rotate(tcb);
            if (next != 0 && b >= 0)
            {
                unsigned long cur = (unsigned long)gregs[ptlog_greg_of[b]];
                unsigned long off = (addr >= cur? addr - cur: 0);
                gregs[ptlog_greg_of[b]] = (long)(next - off);
                tcb->gt_cursor = next - off;
                ptlog_spare_refill(1);
                return;                 /* re-execute the faulting store */
            }
        }
        if (addr >= tcb->arena && addr < tcb->arena_end)
        {
            /* Which register holds the cursor, and at what displacement did
             * this store address it?  (Liveness-aware trampolines: the cursor
             * is any dead register, and one cursor load can serve N stores.) */
            int b = ptlog_store_base(
                (const unsigned char *)gregs[PTLOG_REG_RIP]);
            int gi = (b >= 0? ptlog_greg_of[b]: PTLOG_REG_R11);
            unsigned long cur = (unsigned long)gregs[gi];
            /* SIGNED.  Under `reserve 1' with LIVE flags the cursor register
             * points PAST the run and the stores address it through negative
             * displacements, so `addr - cur' is negative and clamping it to 0
             * would send the rest of the run below the new buffer. */
            long off = (long)(addr - cur);
            /* Which shape faulted?  In the fill-then-advance shape %gs:CURSOR is still the
             * run base, so it is <= the faulting slot.  Under reserve-before-fill
             * it has ALREADY been advanced past the whole run, so it is strictly
             * greater -- and the bytes still to be stored are exactly
             * `cursor - addr'.  No flag is needed: the two are distinguishable
             * by construction, so one handler serves both kinds of image. */
            unsigned long reserved = (tcb->cursor > addr? tcb->cursor - addr: 0);
            unsigned long next = ptlog_rotate(tcb, addr);
            if (next != 0)
            {
                /* `next - off' is where the cursor register must point so that
                 * THIS store lands at the start of the new buffer and the rest
                 * of the run follows it. */
                gregs[gi] = (long)((unsigned long)next - (unsigned long)off);
                if (off != 0 || reserved != 0)
                {
                    /* Re-read %gs: ptlog_rotate may have handed this thread a
                     * TCB of its own. */
                    unsigned long gs2 = 0;
                    ptlog_arch_prctl(ARCH_GET_GS, (unsigned long)&gs2);
                    if (gs2 != 0)
                        ((struct ptlog_tcb *)gs2)->cursor = reserved != 0
                            /* reserve-before-fill: re-reserve what is left of
                             * the run in the NEW buffer.  The trampoline will
                             * not bump the cursor again. */
                            ? next + reserved
                            /* fill-then-advance: the trampoline's own `add $8N' still has
                             * to run, and lands on next + 8*(N-k). */
                            : (unsigned long)((unsigned long)next - (unsigned long)off);
                }
                ptlog_spare_refill(1);
                return;                 /* re-execute the faulting store */
            }
        }
    }

    if (ptlog_segv_diag)
    {
        char b[512], *p = b;
        const unsigned char *rip = (const unsigned char *)gregs[PTLOG_REG_RIP];
        int i;
        const char *m = "[ptlogrt] SEGV not ours: sig=";
        while (*m) *p++ = *m++;
        p = ptlog_utoa(p, (unsigned long)sig);
        m = " addr="; while (*m) *p++ = *m++;
        p = ptlog_hexa(p, addr);
        m = " rip="; while (*m) *p++ = *m++;
        p = ptlog_hexa(p, (unsigned long)rip);
        m = " gs="; while (*m) *p++ = *m++;
        p = ptlog_hexa(p, gs);
        if (gs != 0)
        {
            struct ptlog_tcb *t = (struct ptlog_tcb *)gs;
            m = " gt_map="; while (*m) *p++ = *m++;
            p = ptlog_hexa(p, t->gt_map);
            m = " gt_end="; while (*m) *p++ = *m++;
            p = ptlog_hexa(p, t->gt_end);
            m = " gt_cur="; while (*m) *p++ = *m++;
            p = ptlog_hexa(p, t->gt_cursor);
            m = " arena="; while (*m) *p++ = *m++;
            p = ptlog_hexa(p, t->arena);
        }
        m = " code="; while (*m) *p++ = *m++;
        if (rip != 0)
            for (i = 0; i < 12; i++)
            {
                unsigned long by = rip[i];
                *p++ = "0123456789abcdef"[by >> 4];
                *p++ = "0123456789abcdef"[by & 0xf];
            }
        *p++ = '\n';
        ptlog_syscall6(SYS_write, 2, (long)b, (long)(p - b), 0, 0, 0);
    }

    /* Not ours: chain.  The chain target comes from the shared control
     * block when it carries one (the sigaction() interposer keeps it current),
     * else from the action we displaced at install time.  SIGSEGV is NOT
     * blocked while the target runs, whatever its own SA_NODEFER says: in a
     * whole-program build the target executes instrumented code and may fill
     * the ring, and that guard fault must reach us again (a genuine crash
     * inside the target's handler recurses instead of dying by kernel rule --
     * it dies either way).  Its sa_mask is not applied either. */
    if (masked == 0)
        ptlog_syscall6(SYS_rt_sigprocmask, 2 /* SIG_SETMASK */,
            (long)&entry_mask, 0, sizeof(entry_mask), 0, 0);
    {
        unsigned long ch, cf;
        ptlog_chain_get(&ch, &cf);
        if ((cf & SA_RESETHAND) != 0)
            ptlog_chain_set(0, 0);      /* one-shot: the target expects SIG_DFL after it */
        if (ch > 1 && (cf & SA_SIGINFO) != 0)
            ((void (*)(int, siginfo_t *, void *))ch)(sig, si, ucv);
        else if (ch > 1)
            ((void (*)(int))ch)(sig);
        else
        {
            struct sigaction dfl;
            memset(&dfl, 0, sizeof(dfl));
            dfl.sa_handler = (void (*)(int))0;      /* SIG_DFL */
            ptlog_self_sigaction = 1;
            sigaction(SIGSEGV, &dfl, 0);
            ptlog_self_sigaction = 0;
        }
    }
}

static long ptlog_atol(const char *s)
{
    long v = 0;
    while (*s >= '0' && *s <= '9') { v = v*10 + (*s - '0'); s++; }
    return v;
}

static int ptlog_streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

static void ptlog_setup(char **envp)
{
    int want_stats = 0;
    ptlog_dir[0] = '.'; ptlog_dir[1] = '\0';
#if PTLOG_ENV_FROM_ENVP
    for (char **e = envp; e != 0 && *e != 0; e++)
    {
        const char *s = *e;
        if (strncmp(s, "PTLOG_DIR=", 10) == 0)
        {
            int i = 0;
            for (const char *p = s+10; *p != '\0' && i < 250; p++)
                ptlog_dir[i++] = *p;
            ptlog_dir[i] = '\0';
        }
        else if (strncmp(s, "PTLOG_BUFVALS=", 14) == 0)
            ptlog_bufvals = ptlog_atol(s+14);
        else if (strncmp(s, "PTLOG_NBUF=", 11) == 0)
            ptlog_nbuf = ptlog_atol(s+11);
        else if (strncmp(s, "PTLOG_RING_MB=", 14) == 0)
            ptlog_ring_mb = ptlog_atol(s+14);
        else if (strncmp(s, "PTLOG_RING_TOTAL_MB=", 20) == 0)
            ptlog_ring_total_mb = ptlog_atol(s+20);
        else if (strncmp(s, "PTLOG_RING_FLOOR_KB=", 20) == 0)
            ptlog_ring_floor_kb = ptlog_atol(s+20);
        else if (strncmp(s, "PTLOG_RING_MIN_TCBS=", 20) == 0)
            ptlog_ring_min_tcbs = ptlog_atol(s+20);
        else if (strncmp(s, "PTLOG_HUGE=", 11) == 0)
            ptlog_huge = (s[11] != '0');
        else if (strncmp(s, "PTLOG_KF_N=", 11) == 0)
            ptlog_kf_n = ptlog_atol(s+11);
        else if (strncmp(s, "PTLOG_SIGEXIT=", 14) == 0)
            ptlog_sigexit_on = (s[14] != '0');
        else if (strncmp(s, "PTLOG_REAP=", 11) == 0)
            ptlog_reap = (s[11] != '0');
        else if (strncmp(s, "PTLOG_RECYCLE=", 14) == 0)
            ptlog_recycle = (s[14] != '0');
        else if (strncmp(s, "PTLOG_CVMUX=", 12) == 0)
            ptlog_cvmux = (s[12] != '0');
        else if (strncmp(s, "PTLOG_SYNC=", 11) == 0)
            ptlog_sync = ptlog_atol(s+11);
        else if (strncmp(s, "PTLOG_STATS=", 12) == 0)
            want_stats = (int)ptlog_atol(s+12);
        else if (strncmp(s, "PTLOG_GT=", 9) == 0)
            ptlog_gt = (s[9] != '0');
        else if (strncmp(s, "PTLOG_GT_WIN=", 13) == 0)
            ptlog_gt_win = ptlog_atol(s+13);
        else if (strncmp(s, "PTLOG_GT_MAX=", 13) == 0)
            ptlog_gt_max = ptlog_atol(s+13);
        else if (strncmp(s, "PTLOG_GT_DIR=", 13) == 0)
        {
            int i = 0;
            for (const char *p = s+13; *p != '\0' && i < 250; p++)
                ptlog_gt_dir[i++] = *p;
            ptlog_gt_dir[i] = '\0';
        }
        else if (strncmp(s, "PTLOG_COUNT=", 12) == 0)
            ptlog_count = (s[12] != '0');
        else if (strncmp(s, "PTLOG_COUNT_N=", 14) == 0)
            ptlog_count_n = ptlog_atol(s+14);
        else if (strncmp(s, "PTLOG_SPARES=", 13) == 0)
            ptlog_spares = ptlog_atol(s+13);
        else if (strncmp(s, "PTLOG_COUNT_DIR=", 16) == 0)
        {
            int i = 0;
            for (const char *p = s+16; *p != '\0' && i < 250; p++)
                ptlog_count_dir[i++] = *p;
            ptlog_count_dir[i] = '\0';
        }
    }
#else
    (void)envp;
    const char *v;
    if ((v = getenv("PTLOG_DIR")) != 0)
    {
        int i = 0;
        for (; v[i] != '\0' && i < 250; i++) ptlog_dir[i] = v[i];
        ptlog_dir[i] = '\0';
    }
    if ((v = getenv("PTLOG_BUFVALS")) != 0) ptlog_bufvals = ptlog_atol(v);
    if ((v = getenv("PTLOG_NBUF")) != 0)    ptlog_nbuf = ptlog_atol(v);
    if ((v = getenv("PTLOG_RING_MB")) != 0) ptlog_ring_mb = ptlog_atol(v);
    if ((v = getenv("PTLOG_RING_TOTAL_MB")) != 0) ptlog_ring_total_mb = ptlog_atol(v);
    if ((v = getenv("PTLOG_RING_FLOOR_KB")) != 0) ptlog_ring_floor_kb = ptlog_atol(v);
    if ((v = getenv("PTLOG_RING_MIN_TCBS")) != 0) ptlog_ring_min_tcbs = ptlog_atol(v);
    if ((v = getenv("PTLOG_HUGE")) != 0)    ptlog_huge = (v[0] != '0');
    if ((v = getenv("PTLOG_KF_N")) != 0)    ptlog_kf_n = ptlog_atol(v);
    if ((v = getenv("PTLOG_SIGEXIT")) != 0) ptlog_sigexit_on = (v[0] != '0');
    if ((v = getenv("PTLOG_REAP")) != 0)    ptlog_reap = (v[0] != '0');
    if ((v = getenv("PTLOG_RECYCLE")) != 0) ptlog_recycle = (v[0] != '0');
    if ((v = getenv("PTLOG_CVMUX")) != 0)   ptlog_cvmux = (v[0] != '0');
    if ((v = getenv("PTLOG_SYNC")) != 0)    ptlog_sync = ptlog_atol(v);
    if ((v = getenv("PTLOG_STATS")) != 0)   want_stats = (int)ptlog_atol(v);
    if ((v = getenv("PTLOG_SEGV_DIAG")) != 0) ptlog_segv_diag = (v[0] != '0');
    if ((v = getenv("PTLOG_GT")) != 0)      ptlog_gt = (v[0] != '0');
    if ((v = getenv("PTLOG_GT_WIN")) != 0)  ptlog_gt_win = ptlog_atol(v);
    if ((v = getenv("PTLOG_GT_MAX")) != 0)  ptlog_gt_max = ptlog_atol(v);
    if ((v = getenv("PTLOG_GT_DIR")) != 0)
    {
        int i = 0;
        for (; v[i] != '\0' && i < 250; i++) ptlog_gt_dir[i] = v[i];
        ptlog_gt_dir[i] = '\0';
    }
    if ((v = getenv("PTLOG_COUNT")) != 0)     ptlog_count = (v[0] != '0');
    if ((v = getenv("PTLOG_COUNT_N")) != 0)   ptlog_count_n = ptlog_atol(v);
    if ((v = getenv("PTLOG_SPARES")) != 0)    ptlog_spares = ptlog_atol(v);
    if ((v = getenv("PTLOG_COUNT_DIR")) != 0)
    {
        int i = 0;
        for (; v[i] != '\0' && i < 250; i++) ptlog_count_dir[i] = v[i];
        ptlog_count_dir[i] = '\0';
    }
#endif
    /* ---- one runtime per PROCESS, not per IMAGE --------------------------
     * A whole-program build injects this object into EVERY rewritten image
     * (34 of them for CPython), so init() runs 34 times.  Each copy has its own
     * statics, so without this check each would allocate its own TCB, call
     * arch_prctl(ARCH_SET_GS) over the previous one, and re-open
     * cv.<pid>.<tid>.bin with O_TRUNC -- destroying everything written so far,
     * every time a rewritten extension module is dlopen()ed.
     *
     * The copies do not need to cooperate, only to stand back: the trampolines
     * of every image address the same three %gs slots, and the SIGSEGV handler
     * the FIRST copy installs works on whatever TCB %gs points at, whichever
     * image's trampoline faulted.  So the first copy to run init() owns
     * everything and the rest do nothing at all.
     *
     * "Is %gs already one of ours?" is answered by the magic word: the TCB is a
     * page-aligned mmap, so a %gs base that is not page-aligned is certainly
     * not ours, and the magic then distinguishes our TCB from a target that
     * uses %gs for its own purposes (Go, Wine -- unsupported either way). */
    unsigned long gs = 0;
    ptlog_arch_prctl(ARCH_GET_GS, (unsigned long)&gs);
    if (gs != 0 && (gs & 4095) == 0 &&
            ((struct ptlog_tcb *)gs)->magic == PTLOG_MAGIC)
    {
        ptlog_secondary = 1;
        ptlog_ctl = ((struct ptlog_tcb *)gs)->ctl;   /* for the teardown */
        return;
    }

    /* A single per-process knob for the ring size.  PTLOG_RING_MB is
     * the TOTAL bytes of one thread's ring; split it across PTLOG_NBUF buffers.
     * Overrides PTLOG_BUFVALS when set.  A thread-per-connection server wants a
     * few MiB per thread, not the 32 MiB default (100 threads x 32 MiB
     * = 3.3 GB). */
    if (ptlog_ring_mb > 0)
    {
        long nb = ptlog_nbuf > 0 ? ptlog_nbuf : 1;
        ptlog_bufvals = (ptlog_ring_mb * (1L << 20)) / (nb * 8);
    }
    if (ptlog_bufvals < 4096) ptlog_bufvals = 4096;
    if (ptlog_nbuf < 1) ptlog_nbuf = 1;
    if (ptlog_nbuf > PTLOG_MAX_BUF) ptlog_nbuf = PTLOG_MAX_BUF;
    ptlog_devnull = ptlog_streq(ptlog_dir, "/dev/null");
    if (ptlog_gt_dir[0] == '\0')
    {
        int i = 0;
        for (; ptlog_dir[i] != '\0' && i < 250; i++)
            ptlog_gt_dir[i] = ptlog_dir[i];
        ptlog_gt_dir[i] = '\0';
        if (ptlog_streq(ptlog_gt_dir, "/dev/null"))
            { ptlog_gt_dir[0] = '.'; ptlog_gt_dir[1] = '\0'; }
    }
    ptlog_gt_win = (ptlog_gt_win + 4095) & ~4095L;
    if (ptlog_gt_win < 4096) ptlog_gt_win = 4096;  /* page minimum; permits bounded guard tests */
    if (ptlog_count_n < 1) ptlog_count_n = 1;
    if (ptlog_count_dir[0] == '\0')
    {
        int i = 0;
        for (; ptlog_dir[i] != '\0' && i < 250; i++)
            ptlog_count_dir[i] = ptlog_dir[i];
        ptlog_count_dir[i] = '\0';
        /* /dev/null makes no sense for the count CSV; fall back to cwd. */
        if (ptlog_count_dir[0] == '\0' || ptlog_streq(ptlog_count_dir, "/dev/null"))
            { ptlog_count_dir[0] = '.'; ptlog_count_dir[1] = '\0'; }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ptlog_segv;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    ptlog_self_sigaction = 1;
    sigaction(SIGSEGV, &sa, &ptlog_old_segv);
    ptlog_self_sigaction = 0;

    /* Run the teardown when the process is TERMINATED BY A SIGNAL.
     * Installed below, after ptlog_ctl exists. */

    ptlog_ctl = (struct ptlog_ctl *)ptlog_mmap_rw(
        (sizeof(struct ptlog_ctl) + 4095) & ~4095UL);
    if (ptlog_ctl == 0)
        return;
    ptlog_chain_publish();
    ptlog_ctl->stats = want_stats;
    ptlog_ctl->mux_fd = -1;
    ptlog_ctl->mux_pid = 0;
    ptlog_ctl->mux_lock = 0;
    ptlog_ready = 1;
    ptlog_new_tcb();
    if (ptlog_nbuf > 1 && !ptlog_count)      /* count sink drains nothing */
        ptlog_start_writer();
    /* After the ctl and the writer exist, so a signal that arrives
     * the very next instant finds a runtime that can actually drain. */
    ptlog_install_sigexit();
    /* The spare pool for the ptracer, created AFTER the writer thread's
     * clone so that the writer -- a raw clone of ours that logs nothing -- is
     * not given one. */
    if (ptlog_spares > PTLOG_SPARE_MAX) ptlog_spares = PTLOG_SPARE_MAX;
    ptlog_spare_refill(0);
}

static void ptlog_report(void)
{
    unsigned long flush = 0, inl = 0, stall = 0, bytes = 0, vals = 0, spare = 0;
    long n = ptlog_ctl->ntcb, threads = 0;
    if (n > PTLOG_TCB_SLOTS) n = PTLOG_TCB_SLOTS;
    for (long i = 0; i < n; i++)
    {
        struct ptlog_tcb *t = *ptlog_tcb_slot(ptlog_ctl, i);
        if (t == 0) continue;
        if (t->tid == 0) { spare++; continue; }     /* an unconsumed spare */
        threads++;
        flush += t->n_flush; inl += t->n_inline; stall += t->n_stall;
        bytes += t->bytes;   vals += t->total;
    }
    n = threads;
    char buf[512], *p = buf;
    const char *s = "[ptlogrt] threads=";
    while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, (unsigned long)n);
    s = " bytes=";           while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, bytes);
    s = " buffers=";         while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, flush);
    s = " inline=";          while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, inl);
    s = " stalls=";          while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, stall);
    s = " syncmarks=";       while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, vals);
    s = " spares_unused=";   while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, spare);
    s = " spares_taken=";    while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, (unsigned long)ptlog_ctl->spare_get);
    { const char *m = " ntcb="; while (*m) *p++ = *m++; }
    p = ptlog_utoa(p, (unsigned long)ptlog_ctl->ntcb);
    { const char *m = " recycled="; while (*m) *p++ = *m++; }
    p = ptlog_utoa(p, (unsigned long)ptlog_ctl->recycled);
    s = " ring_mb=";        while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, (unsigned long)(ptlog_ctl->ring_used >> 20));
    s = "/";                while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, (unsigned long)ptlog_ring_total_mb);
    s = " ring_threads=";   while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, (unsigned long)ptlog_ctl->ring_threads);
    s = " ring_floored=";   while (*s != '\0') *p++ = *s++;
    p = ptlog_utoa(p, (unsigned long)ptlog_ctl->ring_over);
    *p++ = '\n';
    ptlog_syscall6(SYS_write, 2, (long)buf, (long)(p - buf), 0, 0, 0);
}

/*
 * COUNT SINK dump.  Sum every thread's per-address
 * counter array and write the nonzero rows as `index,count' CSV to
 * PTLOG_COUNT_DIR/count.<pid>.csv.  eval/ptw_budget_count.py joins the index
 * against the site maps (which carry each address's count index) to size the
 * mixed-sink PTWRITE budget from EXACT whole-run counts.
 */
static void ptlog_count_dump(void)
{
    struct ptlog_ctl *c = ptlog_ctl;
    if (c == 0)
        return;
    long nt = c->ntcb, nthreads = 0;
    if (nt > PTLOG_TCB_SLOTS) nt = PTLOG_TCB_SLOTS;
    for (long t = 0; t < nt; t++)           /* spares never handed out count for nothing */
        if (*ptlog_tcb_slot(c, t) != 0 && (*ptlog_tcb_slot(c, t))->tid != 0) nthreads++;
    long pid = ptlog_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);

    char path[512], *p = path;
    const char *d = ptlog_count_dir;
    while (*d != '\0' && (p - path) < 400) *p++ = *d++;
    if (p != path && p[-1] != '/') *p++ = '/';
    const char *s = "count."; while (*s) *p++ = *s++;
    p = ptlog_utoa(p, (unsigned long)pid);
    s = ".csv"; while (*s) *p++ = *s++;
    *p = '\0';
    long fd = ptlog_syscall6(SYS_openat, -100 /*AT_FDCWD*/, (long)path,
        O_WRONLY|O_CREAT|O_TRUNC, 0644, 0, 0);
    if (fd < 0)
    {
        ptlog_warn("[ptlogrt] count: cannot create the count CSV\n");
        return;
    }

    char buf[8192]; int bl = 0;
    #define CT_FLUSH() do { if (bl) { ptlog_syscall6(SYS_write, fd, (long)buf, bl, 0,0,0); bl = 0; } } while (0)
    char *hp = buf;
    s = "# ptcount v1 pid="; while (*s) *hp++ = *s++;
    hp = ptlog_utoa(hp, (unsigned long)pid);
    s = " n=";  while (*s) *hp++ = *s++;  hp = ptlog_utoa(hp, (unsigned long)ptlog_count_n);
    s = " threads="; while (*s) *hp++ = *s++; hp = ptlog_utoa(hp, (unsigned long)nthreads);
    s = "\nindex,count\n"; while (*s) *hp++ = *s++;
    bl = (int)(hp - buf);

    unsigned long total = 0;
    for (long i = 0; i < ptlog_count_n; i++)
    {
        unsigned long sum = 0;
        for (long t = 0; t < nt; t++)
        {
            struct ptlog_tcb *tcb = *ptlog_tcb_slot(c, t);
            if (tcb == 0 || tcb->count_arr == 0) continue;
            sum += ((unsigned long *)tcb->count_arr)[i];
        }
        if (sum == 0) continue;
        total += sum;
        if (bl > (int)sizeof(buf) - 64) CT_FLUSH();
        char *lp = buf + bl;
        lp = ptlog_utoa(lp, (unsigned long)i);
        *lp++ = ',';
        lp = ptlog_utoa(lp, sum);
        *lp++ = '\n';
        bl = (int)(lp - buf);
    }
    CT_FLUSH();
    #undef CT_FLUSH
    ptlog_syscall6(SYS_close, fd, 0, 0, 0, 0, 0);

    if (c->stats)
    {
        char rep[128], *rp = rep;
        s = "[ptlogrt] count: threads="; while (*s) *rp++ = *s++;
        rp = ptlog_utoa(rp, (unsigned long)nthreads);
        s = " total_executions="; while (*s) *rp++ = *s++;
        rp = ptlog_utoa(rp, total);
        *rp++ = '\n';
        ptlog_syscall6(SYS_write, 2, (long)rep, (long)(rp - rep), 0, 0, 0);
    }
}

/*
 * TEARDOWN ON A SIGNAL-TERMINATED RUN.
 *
 * `ptlog_teardown()' publishes every registered thread's partly filled buffer
 * and drains the lot; it runs from DT_FINI, and a process killed with SIGTERM
 * never gets there.  This handler runs exactly the same teardown -- the one the
 * PTLOG_GT_MAX path has always called from inside a signal handler -- and then
 * restores the DEFAULT disposition and re-raises, so the process dies with
 * precisely the status it would have had (WIFSIGNALED, 128+signo): a service
 * manager cannot tell the difference, and no exit code changes.
 *
 * ONLY installed where the target's current disposition is SIG_DFL.  If the
 * target already handles the signal we leave it alone and say so once: its
 * handler may do a graceful shutdown and RETURN, and tearing the sink down
 * underneath a process that then keeps running would turn a truncated trace
 * into a wrong one.  Such a target loses nothing anyway as long as its handler
 * ends in exit(3) (which runs DT_FINI); it loses the tails only if it ends in
 * _exit(2) or re-raises.  A target that installs its own handler AFTER us
 * simply replaces ours -- also safe, also silent, also covered by the
 * `ptlog.<pid>.done' marker below.
 *
 * Async-signal-safety: the teardown is built on raw syscalls and takes two of
 * our own spin locks.  If this thread was interrupted inside `ptlog_mux_write'
 * it would deadlock on `mux_lock'; that window is a few hundred nanoseconds
 * once per buffer, and the alternative -- deferring to another thread -- needs
 * that thread not to hold the lock either.  The same trade is already made by
 * the PTLOG_GT_MAX path.  PTLOG_SIGEXIT=0 turns the whole thing off.
 */
static struct sigaction ptlog_old_sig[5];
static const int PTLOG_EXIT_SIGS[5] = { SIGTERM, SIGINT, SIGQUIT, SIGHUP, SIGPIPE };

static void ptlog_sigexit(int sig)
{
    if (ptlog_ctl != 0)
        ptlog_ctl->exit_sig = sig;
    ptlog_teardown();
    /* Die as we would have: default disposition, then re-raise on THIS thread. */
    struct sigaction d;
    memset(&d, 0, sizeof(d));
    d.sa_handler = (void (*)(int))0;            /* SIG_DFL */
    sigaction(sig, &d, 0);
    long tid = ptlog_syscall6(SYS_gettid, 0, 0, 0, 0, 0, 0);
    long pid = ptlog_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
    ptlog_syscall6(SYS_tgkill, pid, tid, sig, 0, 0, 0);
}

static void ptlog_install_sigexit(void)
{
    if (!ptlog_sigexit_on)
        return;
    int skipped = 0;
    for (int i = 0; i < 5; i++)
    {
        struct sigaction old;
        memset(&old, 0, sizeof(old));
        if (sigaction(PTLOG_EXIT_SIGS[i], 0, &old) != 0)
            continue;
        /* SIG_DFL only.  SIG_IGN means the signal will not terminate anything,
         * and a target-installed handler is not ours to displace. */
        if (old.sa_handler != 0 || (old.sa_flags & SA_SIGINFO) != 0)
        {
            /* Warn only for a REAL handler.  An inherited SIG_IGN (any signal:
             * bash ignores SIGINT/SIGQUIT in `&' jobs of a non-interactive
             * shell, nohup ignores SIGHUP) cannot terminate the process, so no
             * ring can be lost, and a note on stderr would only disturb
             * background runs whose stderr is compared. */
            if ((old.sa_flags & SA_SIGINFO) != 0 ||
                old.sa_handler != (void (*)(int))1 /* SIG_IGN */)
                skipped++;
            continue;
        }
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = ptlog_sigexit;
        sa.sa_flags = 0;                        /* restartable, no SA_SIGINFO */
        sigaction(PTLOG_EXIT_SIGS[i], &sa, &ptlog_old_sig[i]);
    }
    if (skipped != 0)
        ptlog_warn("[ptlogrt] note: the target already handles a terminating "
                   "signal, so PTLOG_SIGEXIT left it alone.  If that handler "
                   "ends in _exit(2) rather than exit(3), every live thread's "
                   "partly filled ring is lost -- check for "
                   "`ptlog.<pid>.done'.\n");
}

/*
 * `<PTLOG_DIR>/ptlog.<pid>.done' -- written only by a teardown that
 * ran to completion.  Its absence beside a set of cv files means the run was
 * cut short and every live thread's partial ring was lost.
 */
static void ptlog_write_done(void)
{
    if (ptlog_devnull || ptlog_dir[0] == '\0')
        return;
    char path[512], *p = path;
    const char *d = ptlog_dir;
    while (*d != '\0' && (p - path) < 400) *p++ = *d++;
    if (p != path && p[-1] != '/') *p++ = '/';
    *p++ = 'p'; *p++ = 't'; *p++ = 'l'; *p++ = 'o'; *p++ = 'g'; *p++ = '.';
    long pid = ptlog_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
    p = ptlog_utoa(p, (unsigned long)pid);
    *p++ = '.'; *p++ = 'd'; *p++ = 'o'; *p++ = 'n'; *p++ = 'e'; *p = '\0';
    long fd = ptlog_syscall6(SYS_openat, -100 /*AT_FDCWD*/, (long)path,
        O_RDWR|O_CREAT|O_TRUNC, 0644, 0, 0);
    if (fd < 0)
        return;
    char b[160], *q = b;
    const char *m = "ptlog done pid=";
    while (*m) *q++ = *m++;
    q = ptlog_utoa(q, (unsigned long)pid);
    m = " tcbs="; while (*m) *q++ = *m++;
    q = ptlog_utoa(q, (unsigned long)ptlog_ctl->ntcb);
    m = " sig="; while (*m) *q++ = *m++;
    q = ptlog_utoa(q, (unsigned long)ptlog_ctl->exit_sig);
    *q++ = '\n';
    ptlog_syscall6(SYS_write, fd, (long)b, (long)(q - b), 0, 0, 0);
    ptlog_syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
}

static void ptlog_teardown(void)
{
    /* ANY copy of the runtime may run this -- see `struct ptlog_ctl'.  The CAS
     * makes sure only the first one does. */
    if (ptlog_ctl == 0)
        return;
    if (!__sync_bool_compare_and_swap(&ptlog_ctl->done, 0, 1))
        return;
    if (ptlog_count)             /* count sink: no buffers, just dump the CSV */
    {
        ptlog_count_dump();
        return;
    }
    long n = ptlog_ctl->ntcb;
    if (n > PTLOG_TCB_SLOTS) n = PTLOG_TCB_SLOTS;
    ptlog_sfence();                     /* --nt-store: see ptlog_sfence() */
    /* Publish every thread's partly filled buffer, then drain everything.  A
     * thread that has already exited is included: its TCB and its buffers are
     * still mapped, so nothing is lost. */
    for (long i = 0; i < n; i++)
    {
        struct ptlog_tcb *tcb = *ptlog_tcb_slot(ptlog_ctl, i);
        if (tcb == 0) continue;
        if (tcb->tid == 0)
        {
            /* A spare no thread ever received -- drop its provisional
             * gt file so the consumers never see a tid-less ring. */
            if (tcb->gt_fd >= 0)
            {
                char path[512];
                ptlog_gt_path(path, tcb->pid, -1, tcb->serial);
                ptlog_syscall6(SYS_unlinkat, -100, (long)path, 0, 0, 0, 0);
                ptlog_syscall6(SYS_close, tcb->gt_fd, 0, 0, 0, 0, 0);
                tcb->gt_fd = -1;
            }
            continue;
        }
        ptlog_finalise(tcb);
        ptlog_publish_partial(tcb);     /* Idempotent */
    }
    ptlog_ctl->stop = 1;
    for (int i = 0; i < 100000 && ptlog_ctl->writer; i++)
        ptlog_nsleep(10 * 1000);
    ptlog_drain_all();
    if (ptlog_ctl->stats)
        ptlog_report();
    for (long i = 0; i < n; i++)
    {
        struct ptlog_tcb *tcb = *ptlog_tcb_slot(ptlog_ctl, i);
        if (tcb == 0) continue;
        ptlog_gt_close(tcb);
        if (tcb->fd >= 0)
            ptlog_syscall6(SYS_close, tcb->fd, 0, 0, 0, 0, 0);
        tcb->fd = -1;
    }
    /* The CLEAN-SHUTDOWN MARKER.  A cv file gives an offline consumer
     * no way to tell "this thread logged nothing more" from "this run was cut
     * short and its tails were dropped".  One small file per process, written
     * only on the path that has actually published and drained every buffer,
     * does: `offline/cvcheck.py' reports its absence as a TRUNCATED capture.
     * A process killed with SIGKILL (or SIGSTOP'd and never resumed) cannot
     * write it, which is exactly the point -- that is a known
     * limitation. */
    ptlog_write_done();
    if (ptlog_ctl->mux_fd >= 0)          /* The one shared process fd */
    {
        ptlog_syscall6(SYS_close, ptlog_ctl->mux_fd, 0, 0, 0, 0, 0);
        ptlog_ctl->mux_fd = -1;
    }
}

#ifdef PTLOG_E9RT
void init(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv;
    ptlog_setup(envp);
}
#ifndef PTLOG_NO_FINI
/*
 * e9tool asks E9Patch to hook the image's DT_FINI only because this object
 * exports `fini'.  `libc.so.6' has NO DT_FINI/DT_FINI_ARRAY, so the rewrite of
 * libc fails with "failed to replace finalization point" unless it is given the
 * -DPTLOG_NO_FINI flavour of this object.  That is safe because the teardown is
 * driven off `struct ptlog_ctl', so any other image's fini() does it.
 */
void fini(void)
{
    ptlog_teardown();
}
#endif
#else
/* ------------------------------------------------------------------------
 * JIT front ends.  `runtime/jit/jitpatch.h'
 * emits exactly the stores this runtime's %gs layout and guard-page handler
 * expect, so a JIT buffer-sink run needs nothing here except that the runtime
 * be IN the process (LD_PRELOAD) and that its SIGSEGV handler be reached.
 *
 * A JIT VM installs its own SIGSEGV handler after ours: V8 for the WebAssembly
 * trap handler, HotSpot for implicit null checks and the safepoint polling
 * page.  V8's handler chains to the one it displaced, HotSpot's does not
 * unless libjsig is preloaded -- and either way the VM's handler runs first on
 * every buffer-full fault, which is a needless detour through a VM that will
 * decide the fault is not its own.  `ptlog_jit_arm' puts ours back in front,
 * keeping whatever is installed now as the chain target.  The JIT front end
 * calls it once, after the VM is up and before it patches anything.
 *
 * `ptlog_jit_abi' is the presence check: a front end asked for the buffer sink
 * must refuse to patch when this runtime is not loaded, because the stores it
 * would emit fault at address 0 with nothing to catch them.
 */
unsigned long ptlog_jit_abi(void)
{
    /* In a whole-program build this .so is usually a
     * SECONDARY copy (the primary is embedded in the rewritten libc); the ring it would
     * report is the primary's, reached through the shared control block, so the JIT front
     * end may use it. */
    return (ptlog_ready || (ptlog_secondary && ptlog_ctl != 0)) ? (unsigned long)PTLOG_ABI : 0UL;
}

void ptlog_jit_arm(void)
{
    struct sigaction sa, old;
    if (!ptlog_ready)
        return;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ptlog_segv;
    /* SA_ONSTACK, unlike the constructor's arming: a JIT VM gives every one of its
     * threads a sigaltstack and relies on the SEGV handler running there (HotSpot's
     * stack-overflow banging faults on the thread's own guard page).  The kernel
     * ignores the flag on a thread that has registered no alternate stack. */
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
    ptlog_self_sigaction = 1;
    if (sigaction(SIGSEGV, &sa, &old) != 0)
    {
        ptlog_self_sigaction = 0;
        return;
    }
    ptlog_self_sigaction = 0;
    /* Do not chain to ourselves if we are already first.  (With the
     * interposer in the process we always are, and this is a no-op.) */
    if ((void *)old.sa_sigaction != (void *)ptlog_segv)
    {
        ptlog_old_segv = old;
        ptlog_chain_publish();
    }
}

#ifndef PTLOG_E9RT
/* sigaction(2) INTERPOSER (LD_PRELOAD flavour only).  While the
 * installed SIGSEGV action is the runtime's, an application request to replace
 * it is answered from the chain slot instead: the previous chain target is
 * returned as the old action, the new one becomes the chain target, and the
 * runtime's handler stays FIRST.  Every other signal, and every request made
 * while the runtime is not (yet) in charge, passes through unchanged.  This is
 * what libjsig does for HotSpot, generalised to any VM. */
typedef int (*ptlog_sigaction_fn)(int, const struct sigaction *, struct sigaction *);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    static ptlog_sigaction_fn real = 0;
    if (real == 0)
        real = (ptlog_sigaction_fn)dlsym(RTLD_NEXT, "sigaction");
    if (sig == SIGSEGV && !ptlog_self_sigaction && ptlog_ctl != 0 &&
            ptlog_ctl->segv_chain_abi != 0)
    {
        struct sigaction cur;
        if (real(SIGSEGV, 0, &cur) == 0 &&
                (unsigned long)cur.sa_sigaction == ptlog_ctl->segv_handler)
        {
            while (__sync_lock_test_and_set(&ptlog_ctl->segv_chain_lock, 1) != 0)
                ;
            if (oact != 0)
            {
                unsigned long f = ptlog_ctl->segv_chain_flags;
                unsigned long h = ptlog_ctl->segv_chain_handler;
                memset(oact, 0, sizeof(*oact));
                oact->sa_flags = (int)f;
                if ((f & SA_SIGINFO) != 0)
                    oact->sa_sigaction = (void (*)(int, siginfo_t *, void *))h;
                else
                    oact->sa_handler = (void (*)(int))h;
            }
            if (act != 0)
            {
                unsigned long f = (unsigned long)(unsigned int)act->sa_flags;
                ptlog_ctl->segv_chain_flags = f;
                ptlog_ctl->segv_chain_handler = (f & SA_SIGINFO) != 0
                    ? (unsigned long)act->sa_sigaction
                    : (unsigned long)act->sa_handler;
            }
            __sync_lock_release(&ptlog_ctl->segv_chain_lock);
            return 0;
        }
    }
    return real(sig, act, oact);
}
#endif

/* THREAD-EXIT HEADROOM.
 * glibc's start_thread blocks EVERY signal before a thread's exit path
 * (__libc_signal_block_all; then __nptl_free_tcb -> queue_stack for a detached
 * thread, madvise, _dl_deallocate_tls ...), and in a whole-program build that
 * path is instrumented libc: if its values cross the buffer's guard page there,
 * the SIGSEGV is blocked and the kernel kills the process (force_sig_info_to_task:
 * blocked -> SIG_DFL), with no hs_err.  Programs with many short-lived detached
 * threads hit this.  rt/ptlogmt.so's start-routine wrapper calls this after the
 * routine returns (and from a cleanup handler for pthread_exit), i.e. while
 * signals are still unblocked: if less than `min_bytes' are left in the current
 * buffer it is published now (exactly as a guard fault at the cursor would: len =
 * cursor - base, sync bias rebased) and the thread continues in the next, empty
 * buffer, so the exit path can never reach a guard page unless it logs more than
 * one buffer.  The values stay positional: nothing is written, only a boundary is
 * moved.  Returns 1 if it rotated, 0 if not needed or not possible, -1 if the
 * runtime is not active.  It never drains inline (this copy may be the SECONDARY
 * one): if the next buffer is not free within the bounded wait it does nothing. */
int ptlog_thread_headroom(unsigned long min_bytes)
{
    if (!(ptlog_ready || (ptlog_secondary && ptlog_ctl != 0)))
        return -1;
    unsigned long gs = 0;
    ptlog_arch_prctl(ARCH_GET_GS, (unsigned long)&gs);
    if (gs == 0 || (gs & 4095) != 0 ||
            *(volatile unsigned long *)(gs + PTLOG_TCB_MAGIC_OFF) != PTLOG_MAGIC)
        return 0;
    struct ptlog_tcb *tcb = (struct ptlog_tcb *)gs;
    if (ptlog_syscall6(SYS_gettid, 0, 0, 0, 0, 0, 0) != tcb->tid || tcb->nbuf < 2)
        return 0;
    /* Async signals off while the boundary moves (the handler does the same). */
    unsigned long async_mask = ~((1UL << (SIGSEGV-1)) | (1UL << (SIGBUS-1)) |
        (1UL << (SIGILL-1)) | (1UL << (SIGFPE-1)) | (1UL << (SIGTRAP-1)));
    unsigned long old_mask = 0;
    long masked = ptlog_syscall6(SYS_rt_sigprocmask, 0 /* SIG_BLOCK */,
        (long)&async_mask, (long)&old_mask, sizeof(async_mask), 0, 0);
    int r = 0;
    unsigned long base = ptlog_buf(tcb, tcb->head);
    unsigned long cur = tcb->cursor;
    if (cur >= base && cur <= base + tcb->bufbytes &&
            base + tcb->bufbytes - cur < min_bytes && cur > base)
    {
        long n = (tcb->head + 1) % tcb->nbuf;
        long spins = 0;
        while (__atomic_load_n(&tcb->st[n], __ATOMIC_ACQUIRE) != PTLOG_FREE &&
                ++spins < 200000)
            ptlog_syscall6(SYS_sched_yield, 0, 0, 0, 0, 0, 0);
        if (__atomic_load_n(&tcb->st[n], __ATOMIC_ACQUIRE) == PTLOG_FREE)
        {
            ptlog_rotate(tcb, cur);
            r = 1;
        }
    }
    if (masked == 0)
        ptlog_syscall6(SYS_rt_sigprocmask, 2 /* SIG_SETMASK */,
            (long)&old_mask, 0, sizeof(old_mask), 0, 0);
    return r;
}

__attribute__((constructor(101))) static void ptlog_ctor(void)
{
    ptlog_setup(0);
}
__attribute__((destructor(101))) static void ptlog_dtor(void)
{
    ptlog_teardown();
}
#endif
