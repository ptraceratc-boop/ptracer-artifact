/*
 * ptlog_abi.h -- the few facts about the Stage-2 runtime's per-thread control
 * block (`struct ptlog_tcb', runtime/rt/ptlogrt.c) and its process-wide
 * control block (`struct ptlog_ctl') that OTHER programs need without sharing
 * code with the runtime:
 *
 *   * runtime/rt/ptlogmt.c, the pthread shim (an LD_PRELOAD object; the runtime
 *     itself is injected into the rewritten image by E9Patch and exports no
 *     dynamic symbols) -- it meets the runtime only through the thread's %gs
 *     base;
 *   * offline/pt_capture2.c, the ptracer -- it meets the runtime only through
 *     PTRACE_PEEKDATA/POKEDATA on the traced process and its registers.
 *
 * ptlogrt.c pins every offset here with a _Static_assert against the real
 * structs, so a layout change cannot go unnoticed.
 *
 * Why pt_capture2 needs this.
 * Linux copies the GS base into a new thread at clone(2), and glibc blocks ALL
 * signals around the clone (creator side until the clone returns, child side
 * until start_thread restores the mask), so a SIGSEGV taken there is fatal by
 * kernel rule (force_sig_info_to_task: blocked -> SIG_DFL).  The child therefore
 * cannot obtain its own %gs region by faulting, and the creator cannot be put
 * on %gs = 0 either.  The only party that can act on the child BEFORE its first
 * instruction, without a fault, is a ptracer: with PTRACE_O_TRACECLONE the new
 * thread stops once before it runs.  So the runtime keeps a pool of SPARE,
 * fully initialised TCBs in `ptlog_ctl' (a ring: `spare[put % MAX]' produced by
 * the runtime, consumed by the tracer through `spare_get'), and pt_capture2, at
 * the creator's PTRACE_EVENT_CLONE, pops one through the creator's %gs base,
 * writes the child's tid into it and sets the child's GS base to it at the
 * child's first stop (PTRACE_ARCH_PRCTL).  The child then logs into its own
 * cv/gt/count areas from its very first instrumented instruction.  A spare's cv
 * and gt files are named when the tid becomes known (`pending').
 */
#ifndef PTLOG_ABI_H
#define PTLOG_ABI_H

#define PTLOG_MAGIC             0x50544c4f47544342UL    /* "PTLOGTCB" at +392 */
#define PTLOG_ABI               2                       /* spare-TCB protocol */

/* struct ptlog_tcb */
#define PTLOG_TCB_TID_OFF       48      /* long   owner tid (0 = spare, not yet assigned) */
#define PTLOG_TCB_MAGIC_OFF     392     /* u64    PTLOG_MAGIC                              */
#define PTLOG_TCB_CTL_OFF       400     /* ptr    struct ptlog_ctl *                       */
#define PTLOG_TCB_CALLREC_OFF   568     /* ptr    record function of the --call-sink trampolines */
#define PTLOG_TCB_ABI_OFF       576     /* u64    PTLOG_ABI (0 in a pre-protocol runtime)  */
#define PTLOG_TCB_SYNC_NEXT_OFF 608     /* u64    reserved                                 */
#define PTLOG_TCB_SYNC_BIAS_OFF 616     /* u64    reserved                                 */

/* struct ptlog_ctl */
#define PTLOG_SPARE_MAX         64      /* ring capacity                                   */
#define PTLOG_CTL_SPARE_PUT_OFF 8264    /* long   spares produced (runtime writes)          */
#define PTLOG_CTL_SPARE_GET_OFF 8272    /* long   spares consumed (tracer writes)           */
#define PTLOG_CTL_SPARE_ARR_OFF 8296    /* ptr[PTLOG_SPARE_MAX] spare[i % MAX]              */

#endif
