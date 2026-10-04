/*
 * ptsync.S -- synchronization-event logging for the Fast (E9Patch) buffer sink, part of ptlogmt_sync.so
 * (ptlogmt.c built with -DPTLOG_SYNC=1; preloaded instead of ptlogmt.so).  Off unless that build is used.
 *
 * Every wrapper here interposes one libc entry point (pthread_mutex_lock, sem_post, fork, ...).
 * With PTLOG_SYNC_EVENTS unset/0 a wrapper is a pure tail jump to the real function.  With it on,
 * an event is ONE 16-byte record written into the calling thread's ordinary value stream through
 * the %gs cursor (the same store path, guard page and drain as every trampoline value):
 *
 *      w0 = 1<<63 | op<<56 | res<<48 | (call-site return address & 2^48-1)
 *      w1 = object address / peer (pthread_t, pid)
 *
 * and `%gs:TOTAL' += 2, so the image's sync-marker payloads stay exact.
 *
 * RECONSTRUCTION CONTRACT (offline/recon.cpp, ptsync_scan()).  All code in section .ptsync_text is
 * treated by ptrecon like E9Patch trampoline code: it is never interpreted and produces no records,
 * EXCEPT the two stores ptsync_site0/ptsync_site1, which are buffer-sink value sites consuming w0/w1.
 * For that to keep the replayed machine state exact this code must be TRANSPARENT to the program:
 *   - a wrapper calls the real function with EXACTLY the stack pointer the program's own call had
 *     (it pops the program's return address into a thread-local frame first), so the callee's
 *     stack accesses replay at the right addresses;
 *   - argument registers reach the callee unchanged; the callee's %rax reaches the program;
 *     callee-saved registers are never touched;
 *   - code in the shim's ordinary .text (ptlogmt.c) enters the emitter with a `jmp', never a
 *     `call' (an unbalanced push would shift the replayed %rsp).
 * The section .ptsync_desc tells ptrecon where the sites and the block entries are.
 */
#define OP_CREATE      1
#define OP_START       2
#define OP_EXIT        3
#define OP_JOIN        4
#define OP_FORK        5
#define OP_FORK_CHILD  6
#define OP_VFORK       7
#define OP_EXEC        8
#define OP_WAIT        9
#define OP_LOCK        10
#define OP_TRYLOCK     11
#define OP_UNLOCK      12
#define OP_TIMEDLOCK   13
#define OP_CWAIT_BEGIN 14
#define OP_CWAIT_END   15
#define OP_CSIGNAL     16
#define OP_CBROADCAST  17
#define OP_RDLOCK      18
#define OP_WRLOCK      19
#define OP_TRYRDLOCK   20
#define OP_TRYWRLOCK   21
#define OP_RWUNLOCK    22
#define OP_SEMWAIT     23
#define OP_SEMTRYWAIT  24
#define OP_SEMTIMEDWAIT 25
#define OP_SEMPOST     26
#define OP_SPAWN       27

#define TAG(op) ((1 << 63) | ((op) << 56))
#define NFRAME 16

/* thread-local frame stack (ptlogmt.c: struct ptsync_tls, initial-exec):
 *   +0 n   +8 flags (byte 0: in a vfork child)   +16 + 32*i: { ret, a0, a1, a2 } */

        .section .ptsync_desc, "a"
        .balign 8
ptsync_desc:
        .ascii "PTSYNCD2"
        .quad ptsync_anchor - .           /* link-time PC64: anchor = &this + value */
        .quad (ptsync_sites_end - ptsync_sites_begin) / 8      /* value sites: pairs {w0 store, w1 store} */
        .quad (ptsync_ents_end - ptsync_ents_begin) / 8        /* block entries */
ptsync_sites_begin:
        .pushsection .ptsync_desc, 2
ptsync_sites_end:
ptsync_ents_begin:
        .popsection
        .pushsection .ptsync_desc, 4
ptsync_ents_end:
        .popsection

/* ENTRY: record a block entry (reached from program code by a jump, a call or a return) */
.macro ENTRY lbl
        .pushsection .ptsync_desc, 3
        .quad \lbl - ptsync_anchor
        .popsection
.endm

/* EMIT scr: write the event {%rax = w0, %r11 = w1} through the %gs cursor, using \scr (rcx or r10) as the
 * cursor register (the runtime's guard-page handler decodes it).  Skipped when the thread has no TCB or is a vfork
 * child.  Clobbers \scr and flags.  Both stores are value sites (listed in .ptsync_desc). */
.macro EMIT scr, force=0
        /* Never fault a TCB into existence here: a thread may run with SIGSEGV blocked (node creates threads
         * with every signal blocked), and a fault with SIGSEGV blocked kills the process.  A thread without a
         * TCB -- no pt_capture2 --sideband spare yet -- logs nothing. */
        rdgsbase %\scr
        test %\scr, %\scr
        jz .Lemit_skip\@
        movq ptsync_tls@GOTTPOFF(%rip), %\scr
        cmpb $0, %fs:8(%\scr)
        jne .Lemit_skip\@
        mov %gs:0, %\scr
.Lsite_a\@:
        mov %rax, (%\scr)
.Lsite_b\@:
        mov %r11, 8(%\scr)
        lea 16(%\scr), %\scr             /* cursor += 16: a store, not a %gs read-modify-write */
        mov %\scr, %gs:0
        addq $2, %gs:16                    /* TOTAL: keeps the sync-marker payloads exact */
        .pushsection .ptsync_desc, 1
        .quad .Lsite_a\@ - ptsync_anchor, .Lsite_b\@ - ptsync_anchor
        .popsection
.Lemit_skip\@:
.endm

        .section .ptsync_text, "ax", @progbits
        .balign 16
ptsync_anchor:

/* ptsync_emit_c: entered from ptlogmt.c (by jmp) in BOTH modes, so that file's own instruction stream does
 * not depend on the switch.  %rax = start routine, %r11 = op | force<<8, %r10 = continuation; w1 =
 * pthread_self() (= %fs:16).  Clobbers %rax, %r11, flags; the 128-byte red zone of the C caller is kept. */
        .globl ptsync_emit_c
        .hidden ptsync_emit_c
ptsync_emit_c:
        ENTRY ptsync_emit_c
        cmpb $0, ptsync_on(%rip)
        je 9f
        lea -128(%rsp), %rsp
        push %rcx
        push %r10
        shl $16, %rax
        shr $16, %rax
        mov %r11, %rcx
        and $0xff, %r11d
        shl $56, %r11
        or %r11, %rax
        mov $1, %r11d
        shl $63, %r11
        or %r11, %rax
        mov %fs:16, %r11
        test $0x100, %ecx
        jnz 5f
        EMIT rcx
        jmp 6f
5:      EMIT rcx, 1
6:      pop %r10
        pop %rcx
        lea 128(%rsp), %rsp
9:      jmp *%r10

/* ---------------------------------------------------------------------------------------------
 * Lazy resolution of the real functions (normally done by ptlogmt.c's constructor).
 * In: %r10 = where to retry.  Preserves every argument register.
 * ------------------------------------------------------------------------------------------- */
ptsync_resolve_j:
        push %rdi
        push %rsi
        push %rdx
        push %rcx
        push %r8
        push %r9
        push %rax
        push %r10
        sub $8, %rsp
        call ptsync_resolve_all
        add $8, %rsp
        pop %r10
        pop %rax
        pop %r9
        pop %r8
        pop %rcx
        pop %rdx
        pop %rsi
        pop %rdi
        jmp *%r10

/* w0 for the before-style wrappers: (%rsp) is the program's return address */
.macro W0_ENTRY op
        mov (%rsp), %rax
        shl $16, %rax
        shr $16, %rax
        movabs $TAG(\op), %r11
        or %r11, %rax
.endm

/* ---------------------------------------------------------------------------------------------
 * BEFORE: log (op, obj = first argument or 0), then tail-jump to the real function.
 * ------------------------------------------------------------------------------------------- */
.macro BEFORE name, op, obj
        .globl \name
        .type \name, @function
\name:
.Ltop_\name:
        ENTRY .Ltop_\name
        cmpb $0, ptsync_on(%rip)
        je .Lpass_\name
        W0_ENTRY \op
        .ifc \obj,a0
        mov %rdi, %r11
        .else
        xor %r11d, %r11d
        .endif
        EMIT r10
.Lpass_\name:
        mov ptsync_real_\name(%rip), %r11
        test %r11, %r11
        jz .Lslow_\name
        jmp *%r11
.Lslow_\name:
        lea .Lpass_\name(%rip), %r10
        jmp ptsync_resolve_j
        .size \name, . - \name
        .data
        .balign 8
        .globl ptsync_real_\name
        .hidden ptsync_real_\name
ptsync_real_\name: .quad 0
        .section .ptsync_text, "ax", @progbits
.endm

/* ---------------------------------------------------------------------------------------------
 * AFTER: call the real function at the program's own %rsp, then log (op, obj, res).
 *   obj: a0 | a1 | a0p64 (*(u64*)a0 if ret == 0) | a0p32 (*(int*)a0 if ret == 0) | ret | waitid
 *   res: rc (min(ret,255) as unsigned) | neg (255 if ret < 0 else 0)
 *   pre: none | cond (log UNLOCK(a1), CWAIT_BEGIN(a0) first, LOCK(a1) after)
 *   kind: plain | fork | vfork
 * ------------------------------------------------------------------------------------------- */
.macro AFTER name, op, obj, res, pre=none, kind=plain, real=0
        .globl \name
        .type \name, @function
\name:
.Ltop_\name:
        ENTRY .Ltop_\name
        cmpb $0, ptsync_on(%rip)
        je .Lpass_\name
        .ifc \real,0
        mov ptsync_real_\name(%rip), %r11
        test %r11, %r11
        jz .Lslow_\name
        .endif
        movq ptsync_tls@GOTTPOFF(%rip), %r10
        cmpq $NFRAME, %fs:(%r10)
        jae .Lpass_\name
        .ifc \pre,cond
        W0_ENTRY OP_UNLOCK
        mov %rsi, %r11
        EMIT r10
        W0_ENTRY OP_CWAIT_BEGIN
        mov %rdi, %r11
        EMIT r10
        movq ptsync_tls@GOTTPOFF(%rip), %r10
        .endif
        mov %fs:(%r10), %rax
        addq $1, %fs:(%r10)                /* claim the frame before filling it (signal-safe) */
        shl $5, %rax
        add %rax, %r10
        .ifc \kind,fork
        rdgsbase %rax                      /* fork: the parent's pid if it has a TCB, for the child */
        test %rax, %rax
        jz 3f
        mov %gs:56, %rax
3:      mov %rax, %fs:40(%r10)
        .else
        mov %rdx, %fs:40(%r10)
        .endif
        pop %rax                           /* the program's return address; %rsp = its own again */
        mov %rax, %fs:16(%r10)
        mov %rdi, %fs:24(%r10)
        mov %rsi, %fs:32(%r10)
        .ifc \real,0
        call *ptsync_real_\name(%rip)
        .else
        call \real
        .endif
.Lafter_\name:
        ENTRY .Lafter_\name
        movq ptsync_tls@GOTTPOFF(%rip), %r8
        mov %fs:(%r8), %rcx
        dec %rcx
        mov %rcx, %r9
        shl $5, %r9
        add %r8, %r9
        mov %fs:16(%r9), %rdx              /* return address */
        mov %fs:24(%r9), %rsi              /* a0 */
        mov %fs:32(%r9), %rdi              /* a1 */
        mov %fs:40(%r9), %r9               /* a2 (fork: parent pid) */
        .ifc \kind,vfork
        test %eax, %eax
        jnz 4f
        movb $1, %fs:8(%r8)                /* child: peek only -- the parent pops this frame */
        push %rdx
        ret
4:      movb $0, %fs:8(%r8)
        .endif
        mov %rcx, %fs:(%r8)                /* pop */
        mov %rax, %r8                      /* the result, restored at the end */
        .ifc \kind,fork
        test %eax, %eax
        jnz 5f
        /* the child: FORK_CHILD(parent pid), forcing a TCB if the parent had one */
        mov %rdx, %rax
        shl $16, %rax
        shr $16, %rax
        movabs $TAG(OP_FORK_CHILD), %r11
        or %r11, %rax
        mov %r9, %r11
        test %r9, %r9
        jz 4f
        EMIT rcx, 1
        jmp 7f
4:      EMIT rcx
        jmp 7f
5:
        .endif
        /* object -> %r11 */
        .ifc \obj,a0
        mov %rsi, %r11
        .endif
        .ifc \obj,a1
        mov %rdi, %r11
        .endif
        .ifc \obj,ret
        movslq %r8d, %r11
        .endif
        .ifc \obj,a0p64
        xor %r11d, %r11d
        test %r8d, %r8d
        jnz 6f
        test %rsi, %rsi
        jz 6f
        mov (%rsi), %r11
6:
        .endif
        .ifc \obj,a0p32
        xor %r11d, %r11d
        test %r8d, %r8d
        jnz 6f
        test %rsi, %rsi
        jz 6f
        movslq (%rsi), %r11
6:
        .endif
        .ifc \obj,waitid
        xor %r11d, %r11d
        test %r8d, %r8d
        jnz 6f
        test %r9, %r9
        jz 6f
        movslq 16(%r9), %r11               /* infop->si_pid */
6:
        .endif
        /* result byte -> %rcx */
        .ifc \res,rc
        mov %r8d, %ecx
        cmp $255, %ecx
        jbe 6f
        mov $255, %ecx
6:
        .else
        xor %ecx, %ecx
        test %r8d, %r8d
        jns 6f
        mov $255, %ecx
6:
        .endif
        shl $48, %rcx
        mov %rdx, %rax
        shl $16, %rax
        shr $16, %rax
        or %rcx, %rax
        movabs $TAG(\op), %rcx
        or %rcx, %rax
        EMIT rcx
7:
        .ifc \pre,cond
        mov %rdx, %rax
        shl $16, %rax
        shr $16, %rax
        movabs $TAG(OP_LOCK), %rcx
        or %rcx, %rax
        mov %rdi, %r11
        EMIT rcx
        .endif
        mov %r8, %rax
        push %rdx                          /* return to the program by `ret': PT compresses it (no TIP) */
        ret
.Lpass_\name:
        ENTRY .Lpass_\name
        .ifc \real,0
        mov ptsync_real_\name(%rip), %r11
        test %r11, %r11
        jz .Lslow_\name
        jmp *%r11
.Lslow_\name:
        lea .Ltop_\name(%rip), %r10
        jmp ptsync_resolve_j
        .else
        jmp \real
        .endif
        .size \name, . - \name
        .ifc \real,0
        .data
        .balign 8
        .globl ptsync_real_\name
        .hidden ptsync_real_\name
ptsync_real_\name: .quad 0
        .section .ptsync_text, "ax", @progbits
        .endif
.endm

/* ---- threads ---- */
        AFTER pthread_create, OP_CREATE, a0p64, rc, real=ptlogmt_pthread_create
        AFTER pthread_join, OP_JOIN, a0, rc
        AFTER pthread_tryjoin_np, OP_JOIN, a0, rc
        AFTER pthread_timedjoin_np, OP_JOIN, a0, rc
/* ---- processes ---- */
        AFTER fork, OP_FORK, ret, neg, kind=fork
        AFTER vfork, OP_VFORK, ret, neg, kind=vfork
        AFTER posix_spawn, OP_SPAWN, a0p32, rc
        AFTER posix_spawnp, OP_SPAWN, a0p32, rc
        BEFORE execve, OP_EXEC, 0
        BEFORE execv, OP_EXEC, 0
        BEFORE execvp, OP_EXEC, 0
        BEFORE execvpe, OP_EXEC, 0
        BEFORE fexecve, OP_EXEC, 0
        AFTER wait, OP_WAIT, ret, neg
        AFTER waitpid, OP_WAIT, ret, neg
        AFTER wait4, OP_WAIT, ret, neg
        AFTER waitid, OP_WAIT, waitid, neg
/* ---- mutexes ---- */
#if PTSYNC_LOCK_REQUEST
        /* -DPTSYNC_LOCK_REQUEST=1: the cheaper variant -- pthread_mutex_lock logs LOCK at entry (a request, no
         * result) by a tail jump.  The acquisition order of a mutex is still exact: it is the order of its
         * UNLOCK events (critical sections are disjoint). */
        BEFORE pthread_mutex_lock, OP_LOCK, a0
#else
        AFTER pthread_mutex_lock, OP_LOCK, a0, rc
#endif
        AFTER pthread_mutex_trylock, OP_TRYLOCK, a0, rc
        AFTER pthread_mutex_timedlock, OP_TIMEDLOCK, a0, rc
        AFTER pthread_mutex_clocklock, OP_TIMEDLOCK, a0, rc
        BEFORE pthread_mutex_unlock, OP_UNLOCK, a0
/* ---- condition variables ---- */
        AFTER pthread_cond_wait, OP_CWAIT_END, a0, rc, pre=cond
        AFTER pthread_cond_timedwait, OP_CWAIT_END, a0, rc, pre=cond
        AFTER pthread_cond_clockwait, OP_CWAIT_END, a0, rc, pre=cond
        BEFORE pthread_cond_signal, OP_CSIGNAL, a0
        BEFORE pthread_cond_broadcast, OP_CBROADCAST, a0
/* ---- reader-writer locks ---- */
        AFTER pthread_rwlock_rdlock, OP_RDLOCK, a0, rc
        AFTER pthread_rwlock_wrlock, OP_WRLOCK, a0, rc
        AFTER pthread_rwlock_tryrdlock, OP_TRYRDLOCK, a0, rc
        AFTER pthread_rwlock_trywrlock, OP_TRYWRLOCK, a0, rc
        AFTER pthread_rwlock_timedrdlock, OP_RDLOCK, a0, rc
        AFTER pthread_rwlock_timedwrlock, OP_WRLOCK, a0, rc
        BEFORE pthread_rwlock_unlock, OP_RWUNLOCK, a0
/* ---- semaphores ---- */
        AFTER sem_wait, OP_SEMWAIT, a0, rc
        AFTER sem_trywait, OP_SEMTRYWAIT, a0, rc
        AFTER sem_timedwait, OP_SEMTIMEDWAIT, a0, rc
        AFTER sem_clockwait, OP_SEMTIMEDWAIT, a0, rc
        BEFORE sem_post, OP_SEMPOST, a0

        .section .note.GNU-stack, "", @progbits
