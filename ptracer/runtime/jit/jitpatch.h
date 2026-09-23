// jitpatch.h -- PTracer v2 runtime/jit: E9Patch-equivalent trampoline emission for
// JIT code objects.  Implements the three spec v2 site kinds (docs/SPEC_FORMAT.md §1)
// with the same encodings the e9tool `ptlog` plugin uses the design notes:
//   reg   before/after : `ptwrite %reg` per register, >=3 non-PTWRITE instructions apart
//   load  after        : the displaced load, then `ptwrite %dst`
//   memop before       : `ptwrite <mem>` with the memory operand re-encoded verbatim
// Windows are chosen so that displacing them is safe in *live V8 code*: see
// the design notes ("what may be displaced").
#ifndef PTJIT_PATCH_H
#define PTJIT_PATCH_H
#include <stdint.h>
#include <string.h>
#include "jitsites.h"
extern "C" {
#include <Zydis/Zydis.h>
}

// ------------------------------------------------------------ drop reasons --
enum {
  PTJ_DROP_NO_BOUNDARY = 0,   // site address is not an instruction boundary
  PTJ_DROP_UNSUP_REG,         // fs_base/gs_base or an unknown register name
  PTJ_DROP_IMM32,             // the site instruction carries a >=32-bit immediate (V8 RelocInfo)
  PTJ_DROP_CALL,              // a call in the window: its return address would be in the slab
  PTJ_DROP_EXTERN_BRANCH,     // a direct branch out of the code object == a CODE_TARGET reloc
  PTJ_DROP_NARROW_BRANCH,     // jrcxz/loop: no rel32 form to relocate into
  PTJ_DROP_WINDOW_IMM32,
  PTJ_DROP_WINDOW_CALL,
  PTJ_DROP_WINDOW_EXTERN_BRANCH,
  PTJ_DROP_WINDOW_NARROW_BRANCH,
  PTJ_DROP_WINDOW_TRUNCATED,  // fewer than 5 displaceable bytes before the object ends
  PTJ_DROP_BRANCH_TARGET,     // an interior boundary of the window is a direct branch target
  PTJ_DROP_SEGMENT,           // memop with an fs/gs prefix
  PTJ_DROP_ENCODE,            // the trampoline could not be encoded
  PTJ_DROP_SLAB_FULL,
  PTJ_DROP_OUT_OF_RANGE,      // slab not within +-2GB
  PTJ_DROP_DECODE,            // Zydis could not decode the code object
  PTJ_DROP_TOO_MANY_SITES,
  PTJ_DROP_DATA_REGION,       // the site is in (or its window reaches) an inline data table
  PTJ_DROP_WINDOW_MULTI,      // PTJIT_SINGLE: the 5-byte window needs more than one instruction
  PTJ_DROP_KF_FLAGS,          // resync keyframe site whose EFLAGS the analyzer could not prove dead
  PTJ_DROP_KF_NOCTR,          // the keyframe counter arena is full / out of rip-relative reach
  PTJ_DROP_N
};
static const char *PTJ_DROP_NAME[PTJ_DROP_N] = {
  "no_insn_boundary","unsupported_reg","site_imm32","site_call","site_extern_branch",
  "site_narrow_branch","window_imm32","window_call","window_extern_branch",
  "window_narrow_branch","window_truncated","window_branch_target","segment_prefix",
  "encode_failed","slab_full","out_of_range","decode_failed","too_many_sites",
  "data_region","window_multi_insn","keyframe_flags_live","keyframe_no_counter"};

// ------------------------------------------------------------- site map -----
struct PtjMapEntry {
  uint64_t tramp_addr, orig_addr, tsc;
  uint32_t obj;
  uint16_t site;
  uint8_t kind, when, size, reg, half, payload_bits;
  // KEYFRAME guard the design notes; ELF equivalent the design notes.
  // Non-zero `kf_period' means this value is logged only when the site's countdown reaches
  // zero.  `kf_branch'/`kf_join' are what Stage 3 actually consumes: it sees the branch, then
  // decides from the NEXT instruction's ip whether the logging path ran.  So the counter's
  // arithmetic is never replayed offline and its exact period is not load-bearing.
  uint64_t counter, kf_branch, kf_join;
  uint32_t kf_period;
  uint8_t resync;
  // BUFFER SINK the design notes: this value is a `mov %val,disp(%cur)' store
  // into the thread's %gs ring (runtime/rt/ptlogrt.c), not a `ptwrite'.  `tramp_addr' is the
  // store; Stage 3 takes the value positionally from cv.<pid>.<tid>.bin when it sees that ip.
  uint8_t buffer;
};
// A buffer-sink SYNC MARKER: `ptwrite %gs:16' of the thread's running value count, emitted
// 1-in-`sync' values so the reconstructor can realign its positional cv cursor after a PT
// state loss (SPEC_FORMAT section 3, `sync_markers').
struct PtjSyncMark { uint64_t tramp_addr, orig_addr; };
// A same-run ground-truth record site (`role: "gt"', the design notes: the
// trampoline logs the effective address of ONE memory access into the process's own gt ring.
// It is not a critical value -- Stage 3 drops it at load time and it consumes no payload.
struct PtjGtEnt {
  uint64_t tramp_addr, orig_addr, tsc;
  // D-J9: the LOCKSTEP KEY this record is written with.  `orig_addr' is not a dynamic
  // instance -- a JIT address is reused by a later code object (V8 CODE_MOVED, a HotSpot
  // nmethod sweep), and inside a loop the "next ground-truth record with the same ip" is a
  // different iteration -- so the ground-truth record carries the address of the RELOCATED
  // COPY of the instruction inside this trampoline instead.  The slab is bump-allocated and
  // never reused, so that address identifies the patch (SPEC_FORMAT section 2b), and
  // `ptrecon' knows it: it is exactly the `relocated[].tramp_addr' it is executing when it
  // produces the record.  Published as `gt_site_tramps' beside `gt_site_addrs'.
  uint64_t key_addr;
  uint32_t obj;
  uint16_t size;
  uint8_t op;                 // 0 load, 1 store, 2 read-modify-write (diagnostic only)
};
struct PtjReloc {
  uint64_t tramp_addr, orig_addr;
  uint32_t obj;
  uint16_t len, tramp_len;
};

struct PtjPatchCtx {
  ZydisDecoder *dec;
  uint8_t *slab; size_t slab_cap, *slab_used;
  int use_ptwrite;          // 0 = bare detour (A/B control)
  int space;                // non-PTWRITE instructions between consecutive ptwrites
  // --- BUFFER SINK the design notes ------------------------------------
  // `sink_buffer' = 1: a logged value is STORED through the per-thread cursor at %gs:0 into
  // the ring `runtime/rt/ptlogrt.c' owns (same %gs layout, cv record format and sync markers
  // as the E9Patch buffer sink, so `ptrecon' consumes it unchanged).  `sync' is the marker
  // period and MUST equal the runtime's PTLOG_SYNC: the marker's arithmetic re-arms the
  // countdown with it.  0 = no markers.  `syncs' collects every marker's `ptwrite' address.
  int sink_buffer;
  uint32_t sync;
  PtjSyncMark *syncs; uint32_t nsyncs, maxsyncs;
  uint64_t buf_sites, buf_sites_flags_live, buf_values, buf_markers;
  PtjMapEntry *entries; uint32_t nentries, maxentries;
  PtjReloc *relocs; uint32_t nrelocs, maxrelocs;
  uint64_t *tramps; uint32_t ntramps, maxtramps;
  uint64_t drops[PTJ_DROP_N];
  uint64_t n_values;        // logged values emitted
  uint32_t obj;             // current code object index
  uint64_t tsc;
  int sparkplug;            // trust the V8-12.4 Sparkplug prologue shape (see jit_runtime.md)
  int single;               // only displace a SINGLE instruction (no interior boundary at all)
  // --- VM-supplied structure (HotSpot; all optional, 0/NULL == the V8 behaviour) --------
  // `roots`   extra offsets the code object can be *entered at* indirectly, seeded into
  //           the recursive-descent sweep (JVMTI's jvmtiAddrLocationMap, the interpreter's
  //           dispatch tables).  Without them a linear region reachable only by an indirect
  //           jump is invisible; with them it is decoded *and* protected.
  // `forbid`  offsets that must not fall strictly inside a displaced window -- the same
  //           rule as a direct branch target, for entries we cannot see in the code.
  // `lo/hi`   only windows fully inside [lo, hi) may be displaced (hi == 0 -> the object's
  //           end).  HotSpot patches the 5 bytes at the verified entry itself when it makes
  //           an nmethod not-entrant, so `lo` keeps us out of the prologue.
  const uint32_t *roots; uint32_t nroots;
  const uint32_t *forbid; uint32_t nforbid;
  uint32_t lo, hi;
  // `install` writes the finished window bytes over live code.  NULL = a plain store (the
  // V8 path: the code object cannot be executing yet).  HotSpot nmethods *are* live when
  // the event arrives, so the Java front end supplies the int3-then-jmp protocol here.
  // `at` is the window start, `bytes`/`n` the finished window (jmp rel32 + nop padding).
  int (*install)(uint8_t *at, const uint8_t *bytes, uint32_t n, uint64_t tramp);
  // `unpatched` collects the OFFSET of every site this pass could not place a trampoline
  // for, so the caller can re-ask the analyzer with those addresses in `avoid` and get an
  // alternative covering set (analyzer v2.18) rather than losing the value.  NULL = do not
  // collect, which is the V8 behaviour.
  uint32_t *unpatched; uint32_t nunpatched, maxunpatched;
  // `ctr` (measurement only, the design notes: an array of 64-bit counters
  // reachable rip-relative from the slab.  When non-NULL every trampoline is prefixed with
  // a flags-safe `incq ctr[i](%rip)`, i being the trampoline's index, so a run reports how
  // many times each trampoline actually EXECUTED.  `ctr_orig[i]` records the window's
  // original address.  This changes the trampoline, so it is a counting configuration, not
  // a timing one.
  uint64_t *ctr; uint64_t *ctr_orig; uint32_t ctr_max;
  // --- keyframes (defect D-J1) --------------------------------------------------------
  // `kfctr' is an array of 64-bit countdown cells, one per keyframed SITE, at stride
  // PTJ_KFCTR_STRIDE (one cache line) so that two hot sites -- possibly on two threads --
  // never share a line.  It must be within +-2GB of the slab: the front end maps it right
  // after the slab and the emitter checks the reach per guard.
  uint64_t *kfctr; uint32_t kfctr_used, kfctr_max;
  int kf_flags_live;          // resync site with live EFLAGS: 0 drop (default), 1 emit anyway
  uint64_t kf_sites, kf_values, kf_requested;   // resync sites the analyzer returned / placed
  // --- same-run ground truth (`gt' role, defect D-J2) -----------------------------------
  // `gt' turns on a SECOND, independent logging sequence at every memory-accessing
  // instruction of a displaced window, writing {effective address, original ip} into the
  // process's own ring at `gt_cur'.  It contains no branch and no ptwrite, so it adds
  // nothing at all to the Intel PT stream (gt_same_process.md section 3).
  int gt;
  uint64_t *gt_cur;           // the ring cursor cell; bumped with `lock xadd' (thread safe)
  PtjGtEnt *gtents; uint32_t ngtents, maxgtents;
  uint64_t gt_refused;        // memory instructions in a window we could not encode a record for
  // --- diagnostics: the last window installed (read by the note_tramp callback) ---
  uint8_t  last_orig[32];   // the window's ORIGINAL bytes, captured just before the write
  uint32_t last_orig_len;
  uint64_t last_obj_addr; uint32_t last_obj_len; uint32_t last_off;
};

// Record a site the patcher had to give up on (deduplicated by the caller, which sorts).
static inline void ptj_note_unpatched(PtjPatchCtx *cx, uint32_t off) {
  if (!cx->unpatched || cx->nunpatched >= cx->maxunpatched) return;
  cx->unpatched[cx->nunpatched++] = off;
}
// Same, for a site the caller has in hand: a ground-truth PSEUDO-site is ours, not the
// analyzer's, so it must never enter the `avoid' re-solve list (the analyzer would be asked
// to route around an address it never proposed).
static inline void ptj_note_unpatched_s(PtjPatchCtx *cx, const PtjSite *s) {
  if (s->kind == PTJ_KIND_GT) return;
  ptj_note_unpatched(cx, s->off);
}

static inline int ptj_fits32(int64_t v) { return v >= -2147483648LL && v <= 2147483647LL; }

// ---------------------------------------------------- displaceability -------
// HotSpot only: also refuse a non-RIP-relative 32-bit DISPLACEMENT.  C1's patching stubs
// rewrite the field offset of an unresolved field access in place (`NativeMovRegMem::
// set_offset`), so such an instruction is a VM-rewritable field in exactly the way a
// >=32-bit immediate is -- which is why `masked_hash` already masks displacements too.
// Off by default (0) so V8, where nothing rewrites a displacement, is unaffected.
static int g_ptj_no_disp32 = 0;

// Returns 0 if the instruction may be copied into a trampoline, else a PTJ_DROP_* code
// (the "site" variants; the caller maps them to the "window" variants when it is not the
// site instruction itself).
static int ptj_insn_unsafe(const ZydisDecodedInstruction *ins, const ZydisDecodedOperand *ops,
                           uint64_t lo, uint64_t hi, uint64_t at) {
  ZydisInstructionCategory cat = ins->meta.category;
  if (cat == ZYDIS_CATEGORY_CALL) return PTJ_DROP_CALL;
  if (cat == ZYDIS_CATEGORY_INTERRUPT) return PTJ_DROP_CALL;
  // A >=32-bit immediate may be a V8 RelocInfo field (FULL/COMPRESSED_EMBEDDED_OBJECT,
  // EXTERNAL_REFERENCE).  Branch displacements are not immediates in that sense.
  int is_br = (cat == ZYDIS_CATEGORY_COND_BR || cat == ZYDIS_CATEGORY_UNCOND_BR);
  if (!is_br && (ins->raw.imm[0].size >= 32 || ins->raw.imm[1].size >= 32)) return PTJ_DROP_IMM32;
  // a rewritable field offset: mod != 0 or rm != 5 excludes the RIP-relative form, which is
  // not a patch site and which ptj_reloc_insn already relocates correctly
  if (g_ptj_no_disp32 && !is_br && ins->raw.disp.size >= 32 &&
      !(ins->raw.modrm.mod == 0 && ins->raw.modrm.rm == 5)) return PTJ_DROP_IMM32;
  if (is_br) {
    if (ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ops[0].imm.is_relative) {
      switch (ins->mnemonic) {
        case ZYDIS_MNEMONIC_JRCXZ: case ZYDIS_MNEMONIC_JECXZ:
        case ZYDIS_MNEMONIC_LOOP: case ZYDIS_MNEMONIC_LOOPE: case ZYDIS_MNEMONIC_LOOPNE:
          return PTJ_DROP_NARROW_BRANCH;
        default: break;
      }
      uint64_t t = at + ins->length + (uint64_t)ops[0].imm.value.s;
      if (t < lo || t >= hi) return PTJ_DROP_EXTERN_BRANCH;   // CODE_TARGET relocation
      return 0;                                               // internal: re-encode as rel32
    }
    return 0;                                                 // indirect: position independent
  }
  return 0;
}

// ------------------------------------------------------------- encoders -----
struct PtjEmit { uint8_t *p; int since_ptw; int any_ptw; int space; };

static inline void ptj_space(PtjEmit *e) {
  if (!e->any_ptw) return;
  while (e->since_ptw < e->space) { *e->p++ = 0x90; e->since_ptw++; }
}
static inline void ptj_did_ptw(PtjEmit *e) { e->any_ptw = 1; e->since_ptw = 0; }

// ptwrite %reg (GP)
static uint8_t *ptj_ptw_reg(PtjEmit *e, int r) {
  ptj_space(e);
  uint8_t *at = e->p;
  *e->p++ = 0xf3;
  *e->p++ = (uint8_t)(0x48 | ((r >= 8) ? 1 : 0));
  *e->p++ = 0x0f; *e->p++ = 0xae;
  *e->p++ = (uint8_t)(0xe0 | (r & 7));
  ptj_did_ptw(e);
  return at;
}

// The scratch bracket used by the xmm and narrow-memop forms: it steps over the
// System V red zone with `lea` (never touches EFLAGS) and pushes one GP register.
static void ptj_scratch_open(PtjEmit *e, int scr) {
  uint8_t *p = e->p;
  *p++ = 0x48; *p++ = 0x8d; *p++ = 0xa4; *p++ = 0x24;      // lea -0x80(%rsp),%rsp
  *(int32_t *)p = -0x80; p += 4;
  if (scr >= 8) *p++ = 0x41;
  *p++ = (uint8_t)(0x50 | (scr & 7));                      // push %scr
  e->p = p; e->since_ptw += 2;
}
static void ptj_scratch_close(PtjEmit *e, int scr) {
  uint8_t *p = e->p;
  if (scr >= 8) *p++ = 0x41;
  *p++ = (uint8_t)(0x58 | (scr & 7));                      // pop %scr
  *p++ = 0x48; *p++ = 0x8d; *p++ = 0xa4; *p++ = 0x24;      // lea 0x80(%rsp),%rsp
  *(int32_t *)p = 0x80; p += 4;
  e->p = p; e->since_ptw += 2;
}

// Measurement-only trampoline prologue: `incq <slot>(%rip)`, bracketed so that neither
// EFLAGS nor any GP register is disturbed (the System V red zone is stepped over with the
// same `lea` the scratch bracket uses).  20 bytes.
static void ptj_emit_counter(PtjEmit *e, uint64_t *slot) {
  uint8_t *p = e->p;
  *p++ = 0x48; *p++ = 0x8d; *p++ = 0xa4; *p++ = 0x24;      // lea -0x80(%rsp),%rsp
  *(int32_t *)p = -0x80; p += 4;
  *p++ = 0x9c;                                             // pushfq
  *p++ = 0x48; *p++ = 0xff; *p++ = 0x05;                   // incq disp32(%rip)
  { uint8_t *dp = p; p += 4;
    int64_t rel = (int64_t)(uintptr_t)slot - (int64_t)(uintptr_t)p;
    *(int32_t *)dp = (int32_t)rel; }
  *p++ = 0x9d;                                             // popfq
  *p++ = 0x48; *p++ = 0x8d; *p++ = 0xa4; *p++ = 0x24;      // lea 0x80(%rsp),%rsp
  *(int32_t *)p = 0x80; p += 4;
  e->p = p; e->since_ptw += 5;
}


// ================================================================ BUFFER SINK ===========
// the design notes  The value is STORED into the thread's ring through the
// cursor at %gs:0 -- the %gs layout, the cv file and the sync markers are those of
// runtime/rt/ptlogrt.c and runtime/e9plugin/ptlog.cpp (emitBufRun / emitSyncFast), so
// `ptrecon' consumes a JIT buffer-sink run with no change at all.  A site logging n values:
//
//      lea   -0x80(%rsp),%rsp        ; step over the red zone (writes no EFLAGS)
//      push  %A  [ push %B ]         ; the cursor register, and a value register when needed
//      [ pushfq ]                    ; only where the analyzer could NOT prove EFLAGS dead
//      [ mov   <mem>,%B ]            ; memop: the value, BEFORE %A is clobbered (it may be a base)
//      mov   %gs:0,%A                ; the cursor        <- the lazy-%gs fault when %gs is 0
//      mov   %v0,(%A) ... mov %v(n-1),8(n-1)(%A)      <- the `entries' rows; faults on the guard
//      addq  $8n,%gs:0               ; cursor update (flag-free `lea/mov' when EFLAGS are live
//                                    ;   and there is no pushfq bracket)
//      subq  $n,%gs:8                ; the sync countdown          \  `sync' > 0 only
//      jg    1f                      ;                              |
//      mov   %gs:8,%A                ; <= 0: how far it overshot    |  the marker, 1-in-`sync'
//      sub   $SYNC,%A                ; A = COUNT - SYNC = -(values since the last marker)
//      sub   %A,%gs:16               ; TOTAL += values since the last marker  => EXACT (D-M4-16)
//      movq  $SYNC,%gs:8             ; ABSOLUTE re-arm (D-M4-16)
//      ptwrite %gs:16                ; the marker payload = values this thread has written
//   1: [ popfq ] [ pop %B ] pop %A ; lea 0x80(%rsp),%rsp
//
// WHY PUSH/POP AND NOT A DEAD REGISTER (what the E9Patch emitter does): the analyzer's
// `dead_regs' are dead along the CFG it recovered, and a JIT deoptimisation reads register
// state the CFG does not show (HotSpot's scope descriptors and V8's translations name
// registers at a call/deopt point).  `ptwrite' clobbers nothing, which is why the PTWRITE
// path never had to decide this; the buffer path does, and it takes the safe answer.
// WHY THE FAULT-BASED DESIGN IS SOUND INSIDE A VM: the store has no bounds check (a compare
// would clobber EFLAGS); the ring's guard page faults exactly when a buffer is full and the
// runtime's SIGSEGV handler rotates the ring and re-executes the store, decoding the base
// register out of the `mov' (ptlog_store_base) -- hence the store MUST be `REX.W 89 /r' with
// mod 0/1 and no index, which is all this emitter produces.  With %gs == 0 the first
// `mov %gs:0,%A' of a thread faults at address 0 and the same handler gives the thread its
// own region (the lazy-%gs path); the front ends keep every thread at %gs == 0 until then, so
// no thread ever inherits its creator's ring .
static inline void ptj_lea_rsp(PtjEmit *e, int32_t d) {
  uint8_t *p = e->p;
  *p++ = 0x48; *p++ = 0x8d; *p++ = 0xa4; *p++ = 0x24; memcpy(p, &d, 4); p += 4;
  e->p = p; e->since_ptw++;
}
static inline void ptj_push(PtjEmit *e, int r) {
  if (r >= 8) *e->p++ = 0x41; *e->p++ = (uint8_t)(0x50 | (r & 7)); e->since_ptw++;
}
static inline void ptj_pop(PtjEmit *e, int r) {
  if (r >= 8) *e->p++ = 0x41; *e->p++ = (uint8_t)(0x58 | (r & 7)); e->since_ptw++;
}
// mov %gs:disp32,%r64      65 REX.W(R) 8b /r  (mod 00, rm 100, SIB 25 = disp32, no base)
static inline void ptj_mov_from_gs(PtjEmit *e, int r, int32_t disp) {
  uint8_t *p = e->p;
  *p++ = 0x65; *p++ = (uint8_t)(0x48 | ((r >= 8) ? 4 : 0)); *p++ = 0x8b;
  *p++ = (uint8_t)(0x04 | ((r & 7) << 3)); *p++ = 0x25; memcpy(p, &disp, 4); p += 4;
  e->p = p; e->since_ptw++;
}
// mov %r64,%gs:disp32      65 REX.W(R) 89 /r
static inline void ptj_mov_to_gs(PtjEmit *e, int r, int32_t disp) {
  uint8_t *p = e->p;
  *p++ = 0x65; *p++ = (uint8_t)(0x48 | ((r >= 8) ? 4 : 0)); *p++ = 0x89;
  *p++ = (uint8_t)(0x04 | ((r & 7) << 3)); *p++ = 0x25; memcpy(p, &disp, 4); p += 4;
  e->p = p; e->since_ptw++;
}
// addq/subq $imm,%gs:disp32   65 48 83|81 /0|/5  (ext 0 = add, 5 = sub).  WRITES EFLAGS.
static inline void ptj_arith_imm_gs(PtjEmit *e, int ext, int32_t imm, int32_t disp) {
  uint8_t *p = e->p;
  *p++ = 0x65; *p++ = 0x48;
  int i8 = (imm >= -128 && imm <= 127);
  *p++ = i8 ? 0x83 : 0x81; *p++ = (uint8_t)(0x04 | (ext << 3)); *p++ = 0x25;
  memcpy(p, &disp, 4); p += 4;
  if (i8) *p++ = (uint8_t)(int8_t)imm; else { memcpy(p, &imm, 4); p += 4; }
  e->p = p; e->since_ptw++;
}
// movq $imm32,%gs:disp32   65 48 c7 /0
static inline void ptj_mov_imm_gs(PtjEmit *e, int32_t imm, int32_t disp) {
  uint8_t *p = e->p;
  *p++ = 0x65; *p++ = 0x48; *p++ = 0xc7; *p++ = 0x04; *p++ = 0x25;
  memcpy(p, &disp, 4); p += 4; memcpy(p, &imm, 4); p += 4;
  e->p = p; e->since_ptw++;
}
// sub %r64,%gs:disp32      65 REX.W(R) 29 /r      WRITES EFLAGS
static inline void ptj_sub_reg_gs(PtjEmit *e, int r, int32_t disp) {
  uint8_t *p = e->p;
  *p++ = 0x65; *p++ = (uint8_t)(0x48 | ((r >= 8) ? 4 : 0)); *p++ = 0x29;
  *p++ = (uint8_t)(0x04 | ((r & 7) << 3)); *p++ = 0x25; memcpy(p, &disp, 4); p += 4;
  e->p = p; e->since_ptw++;
}
// sub $imm32,%r64          REX.W(B) 81 /5 id      WRITES EFLAGS
static inline void ptj_sub_imm_reg(PtjEmit *e, int32_t imm, int r) {
  uint8_t *p = e->p;
  *p++ = (uint8_t)(0x48 | ((r >= 8) ? 1 : 0)); *p++ = 0x81; *p++ = (uint8_t)(0xe8 | (r & 7));
  memcpy(p, &imm, 4); p += 4;
  e->p = p; e->since_ptw++;
}
// lea disp32(%r),%r        REX.W(RB) 8d /r mod 10  (flag-free add; r is never rsp/r12 here)
static inline void ptj_lea_add_reg(PtjEmit *e, int r, int32_t d) {
  uint8_t *p = e->p;
  *p++ = (uint8_t)(0x48 | ((r >= 8) ? 5 : 0)); *p++ = 0x8d;
  *p++ = (uint8_t)(0x80 | ((r & 7) << 3) | (r & 7)); memcpy(p, &d, 4); p += 4;
  e->p = p; e->since_ptw++;
}
// lea disp32(%rsp),%r      REX.W(R) 8d /r mod 10 rm 100 SIB 24
static inline void ptj_lea_from_rsp(PtjEmit *e, int r, int32_t d) {
  uint8_t *p = e->p;
  *p++ = (uint8_t)(0x48 | ((r >= 8) ? 4 : 0)); *p++ = 0x8d;
  *p++ = (uint8_t)(0x84 | ((r & 7) << 3)); *p++ = 0x24; memcpy(p, &d, 4); p += 4;
  e->p = p; e->since_ptw++;
}
// mov %src,disp(%base)     REX.W(RB) 89 /r, mod 0 (disp 0) or mod 1 (disp8) -- EXACTLY the
// shape runtime/rt/ptlogrt.c::ptlog_store_base decodes in the guard-page fault handler.
// `base' is never rsp/rbp/r12/r13 (see ptj_buf_pick), so no SIB and no forced disp8.
static inline uint8_t *ptj_store_to_base(PtjEmit *e, int src, int base, int disp8) {
  uint8_t *at = e->p, *p = e->p;
  *p++ = (uint8_t)(0x48 | ((src >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0)); *p++ = 0x89;
  if (disp8) { *p++ = (uint8_t)(0x40 | ((src & 7) << 3) | (base & 7)); *p++ = (uint8_t)disp8; }
  else         *p++ = (uint8_t)(((src & 7) << 3) | (base & 7));
  e->p = p; e->since_ptw++;
  return at;
}
// movq %xmmX,%r / pextrq $1,%xmmX,%r (the two halves of an xmm value)
static inline void ptj_movq_xmm_gp(PtjEmit *e, int x, int r) {
  uint8_t *p = e->p;
  *p++ = 0x66; *p++ = (uint8_t)(0x48 | ((x >= 8) ? 4 : 0) | ((r >= 8) ? 1 : 0));
  *p++ = 0x0f; *p++ = 0x7e; *p++ = (uint8_t)(0xc0 | ((x & 7) << 3) | (r & 7));
  e->p = p; e->since_ptw++;
}
static inline void ptj_pextrq_hi_gp(PtjEmit *e, int x, int r) {
  uint8_t *p = e->p;
  *p++ = 0x66; *p++ = (uint8_t)(0x48 | ((x >= 8) ? 4 : 0) | ((r >= 8) ? 1 : 0));
  *p++ = 0x0f; *p++ = 0x3a; *p++ = 0x16; *p++ = (uint8_t)(0xc0 | ((x & 7) << 3) | (r & 7)); *p++ = 1;
  e->p = p; e->since_ptw++;
}
// ptwrite %gs:disp32       65 f3 48 0f ae /4  (mod 00 rm 100 SIB 25)
static inline uint8_t *ptj_ptw_gs(PtjEmit *e, int32_t disp) {
  uint8_t *at = e->p, *p = e->p;
  *p++ = 0x65; *p++ = 0xf3; *p++ = 0x48; *p++ = 0x0f; *p++ = 0xae; *p++ = 0x24; *p++ = 0x25;
  memcpy(p, &disp, 4); p += 4;
  e->p = p;
  return at;
}
#define PTJ_GS_CURSOR 0      /* struct ptlog_tcb: +0 cursor, +8 countdown, +16 total (ABI) */
#define PTJ_GS_COUNT  8
#define PTJ_GS_TOTAL  16

// Two scratch registers the site does not log.  Never rsp (the bracket moves it), never
// rbp/r12/r13 (they would need a SIB or a forced disp8 in the store, which the runtime's
// fault handler does not decode), and callee-clobbered registers first so that the pushes
// sit on the hot path as rarely as the VM's own ABI allows.
static int ptj_buf_pick(const PtjSite *st, int *A, int *B) {
  static const int order[] = {11, 10, 9, 8, 0, 1, 2, 6, 7, 3};
  unsigned used = 0;
  for (int i = 0; i < st->nregs; i++) if (st->regs[i] < 16) used |= 1u << st->regs[i];
  int n = 0, pick[2] = {-1, -1};
  for (unsigned i = 0; i < sizeof order / sizeof order[0] && n < 2; i++)
    if (!(used & (1u << order[i]))) pick[n++] = order[i];
  if (n < 2) return 0;
  *A = pick[0]; *B = pick[1];
  return 1;
}

// Forward declarations: the buffer emitter is defined here, beside the %gs encoders it
// uses, but `ptj_emit_memop' (the memory-operand re-encoder) and `ptj_add_entry' (the
// site-map row) live further down beside the PTWRITE emitter.
static int ptj_emit_memop(PtjEmit *e, const ZydisDecodedInstruction *ins, const uint8_t *raw,
                          uint64_t orig_addr, const uint8_t *opc, int opclen, int regfield,
                          int rexW, int rsp_delta, int with_f3);
static void ptj_add_entry(PtjPatchCtx *cx, uint8_t *at, uint64_t orig, const PtjSite *st,
                          int reg, int half, int pbits);

// Emit the values of one site into the ring.  Layout in the comment above.
static int ptj_emit_site_buffer(PtjPatchCtx *cx, PtjEmit *e, const PtjSite *st,
                                const ZydisDecodedInstruction *ins,
                                const uint8_t *raw, uint64_t orig_addr) {
  int A, B;
  if (!ptj_buf_pick(st, &A, &B)) return 0;
  int nval = 0, need_b = 0;
  if (st->kind == PTJ_KIND_MEMOP) { nval = 1; need_b = 1; }
  else {
    for (int i = 0; i < st->nregs; i++) {
      int r = st->regs[i];
      if (r < 16) { nval++; if (r == 4) need_b = 1; }
      else if (r < 32) { nval += 2; need_b = 1; }
      else return 0;
    }
  }
  if (nval == 0) return 1;
  if (nval > 12) return 0;                              // 8*12 = 96 fits the disp8 stores
  const int sync = (int)cx->sync;
  // pushfq/popfq only where the analyzer could not prove EFLAGS dead AND something here
  // writes them (the countdown and the `addq' cursor update).  With flags live and no
  // markers the cursor update takes the flag-free `lea/mov' form instead.
  const int flags_ok = st->flags_dead ? 1 : 0;
  const int pushf = (!flags_ok && sync > 0) ? 1 : 0;
  const int use_arith = flags_ok || pushf;
  uint8_t *save = e->p;
  uint32_t save_entries = cx->nentries; uint64_t save_values = cx->n_values;

  ptj_lea_rsp(e, -0x80);
  ptj_push(e, A);
  if (need_b) ptj_push(e, B);
  if (pushf) *e->p++ = 0x9c;                            // pushfq
  const int rsp_delta = 0x80 + 8 * (1 + need_b + pushf);
  if (st->kind == PTJ_KIND_MEMOP) {
    // The value FIRST, while every register the operand names still holds its value.
    int size = st->size ? st->size : 8;
    static const uint8_t OPC_MOV[1] = {0x8b};
    static const uint8_t OPC_MZX[2][2] = {{0x0f, 0xb6}, {0x0f, 0xb7}};
    int ok;
    if (size == 8 || size == 4) ok = ptj_emit_memop(e, ins, raw, orig_addr, OPC_MOV, 1, B, size == 8, rsp_delta, 0);
    else                        ok = ptj_emit_memop(e, ins, raw, orig_addr, OPC_MZX[size == 2], 2, B, 1, rsp_delta, 0);
    if (!ok) { e->p = save; return 0; }
    e->since_ptw++;
  }
  ptj_mov_from_gs(e, A, PTJ_GS_CURSOR);
  int k = 0;
  if (st->kind == PTJ_KIND_MEMOP) {
    uint8_t *at = ptj_store_to_base(e, B, A, 0);
    ptj_add_entry(cx, at, orig_addr, st, 255, 0, 64); cx->entries[cx->nentries - 1].buffer = 1; k++;
  } else {
    for (int i = 0; i < st->nregs; i++) {
      int r = st->regs[i];
      if (r < 16) {
        int src = r;
        if (r == 4) { ptj_lea_from_rsp(e, B, rsp_delta); src = B; }   // the ORIGINAL %rsp
        uint8_t *at = ptj_store_to_base(e, src, A, 8 * k);
        ptj_add_entry(cx, at, orig_addr, st, r, 0, 64); cx->entries[cx->nentries - 1].buffer = 1; k++;
      } else {
        ptj_movq_xmm_gp(e, r - 16, B);
        uint8_t *lo = ptj_store_to_base(e, B, A, 8 * k); k++;
        ptj_pextrq_hi_gp(e, r - 16, B);
        uint8_t *hi = ptj_store_to_base(e, B, A, 8 * k); k++;
        ptj_add_entry(cx, lo, orig_addr, st, r, 1, 64); cx->entries[cx->nentries - 1].buffer = 1;
        ptj_add_entry(cx, hi, orig_addr, st, r, 2, 64); cx->entries[cx->nentries - 1].buffer = 1;
      }
    }
  }
  if (cx->nentries != save_entries + (uint32_t)k) { e->p = save; cx->nentries = save_entries; cx->n_values = save_values; return 0; }
  // cursor update
  if (use_arith) ptj_arith_imm_gs(e, 0, 8 * k, PTJ_GS_CURSOR);
  else { ptj_lea_add_reg(e, A, 8 * k); ptj_mov_to_gs(e, A, PTJ_GS_CURSOR); }
  // sync countdown + marker (E9Patch emitSyncFast, byte for byte in effect)
  if (sync > 0) {
    ptj_arith_imm_gs(e, 5, k, PTJ_GS_COUNT);           // subq $k,%gs:8
    uint8_t *jg = e->p; *e->p++ = 0x0f; *e->p++ = 0x8f; uint8_t *hole = e->p; e->p += 4;
    e->since_ptw++;
    ptj_mov_from_gs(e, A, PTJ_GS_COUNT);
    ptj_sub_imm_reg(e, sync, A);
    ptj_sub_reg_gs(e, A, PTJ_GS_TOTAL);
    ptj_mov_imm_gs(e, sync, PTJ_GS_COUNT);
    uint8_t *mk = ptj_ptw_gs(e, PTJ_GS_TOTAL);
    (void)jg;
    { int32_t rel = (int32_t)(int64_t)(e->p - (hole + 4)); memcpy(hole, &rel, 4); }
    if (cx->syncs && cx->nsyncs < cx->maxsyncs) {
      cx->syncs[cx->nsyncs].tramp_addr = (uint64_t)(uintptr_t)mk;
      cx->syncs[cx->nsyncs].orig_addr = orig_addr;
      cx->nsyncs++;
    }
    cx->buf_markers++;
    e->since_ptw = 1000;                                // the marker PTWRITE is 1-in-`sync'
  }
  if (pushf) *e->p++ = 0x9d;                            // popfq
  if (need_b) ptj_pop(e, B);
  ptj_pop(e, A);
  ptj_lea_rsp(e, 0x80);
  cx->buf_sites++; if (!flags_ok) cx->buf_sites_flags_live++;
  cx->buf_values += (uint64_t)k;
  return 1;
}

// ------------------------------------------------------- keyframe guard -----
// One 64-byte line per countdown cell so two hot sites (possibly on two threads) never
// share one.  Cells start at 1, so a site's FIRST execution is a keyframe: an nmethod or a
// tiered-up JS function is patched while it is ALREADY RUNNING, and must re-anchor at once.
#define PTJ_KFCTR_STRIDE 8
static uint64_t *ptj_kf_alloc(PtjPatchCtx *cx) {
  if (!cx->kfctr || cx->kfctr_used >= cx->kfctr_max) return nullptr;
  uint64_t *c = cx->kfctr + (size_t)cx->kfctr_used * PTJ_KFCTR_STRIDE;
  cx->kfctr_used++;
  *c = 1;
  return c;
}

// `dec CNT(%rip); jg join; mov $K,CNT(%rip)' -- the guard, 24 bytes.  The caller emits the
// site's logging instructions after it and calls ptj_kf_close() to fill the branch's rel32.
//
// WHY `jg' WHERE THE ELF EMITTER USES `jnz' the design notes section 1.3):
//  * a zero-filled cell is then SAFE: `dec 0' gives -1, which is <= 0, so the guard fires
//    and re-arms.  With `jnz' a cell that ever reached -1 would not log again for 2^64
//    executions, which is why the ELF emitter has to initialise its counters to 1.
//  * HotSpot runs one nmethod on MANY threads and the cell is not atomic.  A lost decrement
//    (two threads read the same value) can drive it negative; `jg' turns that into "fire
//    now" instead of "never fire again", so the counter is self-healing and needs no `lock'
//    on the hottest loop header in the program.
// Neither choice is visible to Stage 3: `offline/recon.cpp' does not replay the arithmetic,
// it reads the branch DECISION out of the PT stream (`kf_branch' then `ip != kf_join'), so a
// racy counter can change the keyframe RATE but can never change the reconstruction.
static int ptj_kf_open(PtjEmit *e, uint64_t *ctr, uint32_t k, uint8_t **branch, uint8_t **hole) {
  uint8_t *p = e->p;
  *p++ = 0x48; *p++ = 0xff; *p++ = 0x0d;                  // dec qword ptr [rip+d]
  { uint8_t *dp = p; p += 4;
    int64_t rel = (int64_t)(uintptr_t)ctr - (int64_t)(uintptr_t)p;
    if (!ptj_fits32(rel)) return 0;
    *(int32_t *)dp = (int32_t)rel; }
  *branch = p;
  *p++ = 0x0f; *p++ = 0x8f;                               // jg rel32
  *hole = p; p += 4;
  // mov qword ptr [rip+d],imm32.  The RIP-relative field is measured from the END of the
  // instruction, i.e. from past the imm32 -- the same subtlety keyframe_impl.md section 1.3
  // records for E9Patch's ENTRY_REL32.
  *p++ = 0x48; *p++ = 0xc7; *p++ = 0x05;
  { uint8_t *dp = p; p += 4; uint8_t *imm = p; p += 4;
    int64_t rel = (int64_t)(uintptr_t)ctr - (int64_t)(uintptr_t)p;
    if (!ptj_fits32(rel)) return 0;
    *(int32_t *)dp = (int32_t)rel;
    uint32_t kk = k ? k : 1; memcpy(imm, &kk, 4); }
  e->p = p; e->since_ptw += 3;
  return 1;
}
static inline void ptj_kf_close(PtjEmit *e, uint8_t *hole) {
  int32_t rel = (int32_t)(int64_t)(e->p - (hole + 4));
  memcpy(hole, &rel, 4);
}

// --------------------------------------------------- same-run ground truth --
// Is this instruction one the gt oracle can record with EXACTLY ONE {address, ip} record?
// That is the requirement for walking the reconstruction and the ground truth in lockstep
// the design notes section 5).  Returns the index of its single explicit
// memory operand, or -1 to refuse.
static int ptj_gt_operand(const ZydisDecodedInstruction *ins, const ZydisDecodedOperand *ops) {
  if (ins->attributes & (ZYDIS_ATTRIB_HAS_REP | ZYDIS_ATTRIB_HAS_REPE | ZYDIS_ATTRIB_HAS_REPNE))
    return -1;                                   // one instruction, N accesses
  switch (ins->meta.category) {
    case ZYDIS_CATEGORY_CALL: case ZYDIS_CATEGORY_RET: case ZYDIS_CATEGORY_INTERRUPT:
    case ZYDIS_CATEGORY_SYSCALL: case ZYDIS_CATEGORY_SYSRET:
    case ZYDIS_CATEGORY_PREFETCH: case ZYDIS_CATEGORY_PREFETCHWT1:
      return -1;
    default: break;
  }
  switch (ins->mnemonic) {                       // a memory OPERAND that is not an ACCESS:
    case ZYDIS_MNEMONIC_LEA: case ZYDIS_MNEMONIC_NOP:      // ptrecon emits no record, so
    case ZYDIS_MNEMONIC_CLFLUSH: case ZYDIS_MNEMONIC_CLFLUSHOPT:   // instrumenting one would
    case ZYDIS_MNEMONIC_CLWB:                              // desynchronise the two streams
      return -1;
    default: break;
  }
  if (ins->attributes & (ZYDIS_ATTRIB_HAS_SEGMENT_FS | ZYDIS_ATTRIB_HAS_SEGMENT_GS))
    return -1;                                   // ptrecon reports %gs-relative unknown by design
  int found = -1, n = 0;
  for (int i = 0; i < ins->operand_count; i++) {
    if (ops[i].type != ZYDIS_OPERAND_TYPE_MEMORY) continue;
    if (ops[i].mem.type != ZYDIS_MEMOP_TYPE_MEM) continue;         // AGEN / MIB are not accesses
    n++;
    if (ops[i].visibility == ZYDIS_OPERAND_VISIBILITY_EXPLICIT) found = i;
  }
  // n != 1 covers `push (%rax)' / `pop (%rax)' / `movs' (two operands) and the implicit
  // stack accesses of push/pop/leave (one HIDDEN operand, found < 0), which this front end
  // does not instrument -- they come out of the comparison as `excluded'.
  if (n != 1 || found < 0) return -1;
  return found;
}

// The gt logging sequence.  `lock xadd' publishes the ring slot atomically, so several
// threads inside the same nmethod cannot overwrite each other's record (HotSpot); it is
// bracketed by pushfq/popfq because `xadd' writes EFLAGS.  This is a measurement build --
// the two extra instructions and the lock cost nothing that is being measured.
//
// It contains NO branch and NO ptwrite, so a gt build's Intel PT stream is byte for byte
// the stream of the build it measures (gt_same_process.md section 3), which is what makes
// the comparison an oracle for the ordinary build rather than for itself.
static int ptj_emit_memop(PtjEmit *e, const ZydisDecodedInstruction *ins, const uint8_t *raw,
                          uint64_t orig_addr, const uint8_t *opc, int opclen, int regfield,
                          int rexW, int rsp_delta, int with_f3);
static int ptj_emit_gt(PtjEmit *e, const ZydisDecodedInstruction *ins, const uint8_t *raw,
                       uint64_t orig_addr, uint64_t *cur, uint8_t **key_imm) {
  static const uint8_t OPC_LEA[1] = {0x8d};
  uint8_t *save = e->p;
  uint8_t *p = e->p;
  *p++ = 0x48; *p++ = 0x8d; *p++ = 0xa4; *p++ = 0x24;     // lea -0x80(%rsp),%rsp
  *(int32_t *)p = -0x80; p += 4;
  *p++ = 0x50;                                            // push %rax
  *p++ = 0x51;                                            // push %rcx
  *p++ = 0x9c;                                            // pushfq
  e->p = p;
  // The effective address FIRST, while %rax and %rcx still hold the values the original
  // instruction would see (push preserves them; the cursor load below does not).
  // %rsp has moved by 0x80 + 3*8.
  if (!ptj_emit_memop(e, ins, raw, orig_addr, OPC_LEA, 1, 0, 1, 0x98, 0)) { e->p = save; return 0; }
  p = e->p;
  *p++ = 0xb9; { uint32_t sz = 16; memcpy(p, &sz, 4); p += 4; }        // mov $16,%ecx
  *p++ = 0xf0; *p++ = 0x48; *p++ = 0x0f; *p++ = 0xc1; *p++ = 0x0d;     // lock xadd %rcx,d(%rip)
  { uint8_t *dp = p; p += 4;
    int64_t rel = (int64_t)(uintptr_t)cur - (int64_t)(uintptr_t)p;
    if (!ptj_fits32(rel)) { e->p = save; return 0; }
    *(int32_t *)dp = (int32_t)rel; }
  *p++ = 0x48; *p++ = 0x89; *p++ = 0x01;                  // mov %rax,(%rcx)
  // The record's second word is the LOCKSTEP KEY (D-J9), not the original ip: the caller
  // back-patches this immediate with the address of the relocated copy it emits next, which
  // is unique for the life of the process.  Written as `orig_addr' first so that a caller
  // that does not back-patch (and check_sitemap.py's decoder) still sees a valid sequence.
  *p++ = 0x48; *p++ = 0xb8; if (key_imm) *key_imm = p;
  memcpy(p, &orig_addr, 8); p += 8;                       // movabs $KEY,%rax
  *p++ = 0x48; *p++ = 0x89; *p++ = 0x41; *p++ = 0x08;     // mov %rax,8(%rcx)
  *p++ = 0x9d;                                            // popfq
  *p++ = 0x59;                                            // pop %rcx
  *p++ = 0x58;                                            // pop %rax
  *p++ = 0x48; *p++ = 0x8d; *p++ = 0xa4; *p++ = 0x24;     // lea 0x80(%rsp),%rsp
  *(int32_t *)p = 0x80; p += 4;
  e->p = p; e->since_ptw += 14;
  return 1;
}

// ptwrite of an xmm register: two 64-bit halves through a scratch GP register.
static void ptj_ptw_xmm(PtjEmit *e, int x, int scr, uint8_t **lo, uint8_t **hi) {
  ptj_scratch_open(e, scr);
  uint8_t *p = e->p;
  // movq %xmmX,%scr   66 REX.W 0F 7E /r
  *p++ = 0x66; *p++ = (uint8_t)(0x48 | ((x >= 8) ? 4 : 0) | ((scr >= 8) ? 1 : 0));
  *p++ = 0x0f; *p++ = 0x7e; *p++ = (uint8_t)(0xc0 | ((x & 7) << 3) | (scr & 7));
  e->p = p; e->since_ptw++;
  *lo = ptj_ptw_reg(e, scr);
  p = e->p;
  // pextrq $1,%xmmX,%scr   66 REX.W 0F 3A 16 /r ib
  *p++ = 0x66; *p++ = (uint8_t)(0x48 | ((x >= 8) ? 4 : 0) | ((scr >= 8) ? 1 : 0));
  *p++ = 0x0f; *p++ = 0x3a; *p++ = 0x16;
  *p++ = (uint8_t)(0xc0 | ((x & 7) << 3) | (scr & 7)); *p++ = 1;
  e->p = p; e->since_ptw++;
  *hi = ptj_ptw_reg(e, scr);
  ptj_scratch_close(e, scr);
}

// Re-encode the memory operand of `ins` under a new opcode / reg field.
// rsp_delta: how far %rsp has moved since the original instruction would have run.
// Returns 0 on failure.
static int ptj_emit_memop(PtjEmit *e, const ZydisDecodedInstruction *ins, const uint8_t *raw,
                          uint64_t orig_addr, const uint8_t *opc, int opclen, int regfield,
                          int rexW, int rsp_delta, int with_f3) {
  if (ins->attributes & (ZYDIS_ATTRIB_HAS_SEGMENT_FS | ZYDIS_ATTRIB_HAS_SEGMENT_GS |
                         ZYDIS_ATTRIB_HAS_SEGMENT_CS | ZYDIS_ATTRIB_HAS_SEGMENT_DS |
                         ZYDIS_ATTRIB_HAS_SEGMENT_ES | ZYDIS_ATTRIB_HAS_SEGMENT_SS)) return 0;
  int mod = ins->raw.modrm.mod, rm = ins->raw.modrm.rm;
  if (mod == 3) return 0;
  int has_sib = (rm == 4);
  // The base/index EXTENSION bits live in REX only for a legacy instruction; a VEX/EVEX/XOP
  // instruction carries them in its own prefix, **inverted**.  Reading `raw.rex` alone silently
  // dropped the high bit of a VEX operand's base: HotSpot's `vmulsd xmm0,xmm4,[r11+rcx*8+0x8]'
  // re-encoded as `lea [rbx+rcx*8+0x8]', and the ground truth then reported 0x103d where the
  // real address was 0x6170857f0 (69.6 % of a Java gt run came out "wrong" because of this).
  // It is the same defect in the `memop' site path, which shares this encoder.
  int rexX = ins->raw.rex.X, rexB = ins->raw.rex.B;
  if (ins->attributes & ZYDIS_ATTRIB_HAS_VEX)        { rexX = !ins->raw.vex.X;  rexB = !ins->raw.vex.B; }
  else if (ins->attributes & ZYDIS_ATTRIB_HAS_EVEX)  { rexX = !ins->raw.evex.X; rexB = !ins->raw.evex.B; }
  else if (ins->attributes & ZYDIS_ATTRIB_HAS_XOP)   { rexX = !ins->raw.xop.X;  rexB = !ins->raw.xop.B; }
  int rip_rel = (mod == 0 && rm == 5);
  int64_t disp = ins->raw.disp.size ? (int64_t)ins->raw.disp.value : 0;
  int base_is_rsp = has_sib && ins->raw.sib.base == 4 && rexB == 0;
  int newmod = mod;
  uint64_t rip_target = 0;
  if (rip_rel) {
    rip_target = orig_addr + ins->length + (uint64_t)disp;
  } else if (rsp_delta && base_is_rsp) {
    disp += rsp_delta;
    newmod = 2;
    if (!ptj_fits32(disp)) return 0;
  }
  uint8_t *p = e->p;
  if (with_f3) *p++ = 0xf3;
  int rex = (rexW ? 8 : 0) | ((regfield >= 8) ? 4 : 0) | (rexX ? 2 : 0) | (rexB ? 1 : 0);
  if (rex) *p++ = (uint8_t)(0x40 | rex);
  for (int i = 0; i < opclen; i++) *p++ = opc[i];
  *p++ = (uint8_t)((newmod << 6) | ((regfield & 7) << 3) | rm);
  if (has_sib) *p++ = raw[ins->raw.sib.offset];
  if (rip_rel) {
    uint8_t *dp = p; p += 4;
    int64_t rel = (int64_t)rip_target - (int64_t)(uintptr_t)p;
    if (!ptj_fits32(rel)) return 0;
    *(int32_t *)dp = (int32_t)rel;
  } else if (newmod == 1) {
    *(int8_t *)p = (int8_t)disp; p += 1;
  } else if (newmod == 2 || (newmod == 0 && has_sib && ins->raw.sib.base == 5)) {
    if (!ptj_fits32(disp)) return 0;
    *(int32_t *)p = (int32_t)disp; p += 4;
  }
  e->p = p;
  return 1;
}

// `ptwrite <mem>` for a memop site.  Widths 4 and 8 go straight to the memory form;
// 1 and 2 are zero-extended through a scratch register (spec v2 §1).
static int ptj_ptw_mem(PtjEmit *e, const ZydisDecodedInstruction *ins, const uint8_t *raw,
                       uint64_t orig_addr, int size, int scr, int *payload_bits, uint8_t **at) {
  static const uint8_t OPC_PTW[2] = {0x0f, 0xae};
  if (size == 8 || size == 4) {
    ptj_space(e);
    uint8_t *save = e->p;
    if (!ptj_emit_memop(e, ins, raw, orig_addr, OPC_PTW, 2, 4, size == 8, 0, 1)) { e->p = save; return 0; }
    ptj_did_ptw(e);
    *at = save;
    *payload_bits = size == 8 ? 64 : 32;
    return 1;
  }
  // 1/2 bytes: movzbq/movzwq <mem>,%scr ; ptwrite %scr
  static const uint8_t OPC_MZX[2][2] = {{0x0f, 0xb6}, {0x0f, 0xb7}};
  ptj_scratch_open(e, scr);
  uint8_t *save = e->p;
  // the scratch push moved %rsp by 0x80 + 8
  if (!ptj_emit_memop(e, ins, raw, orig_addr, OPC_MZX[size == 2], 2, scr, 1, 0x88, 0)) {
    e->p = save; return 0;
  }
  e->since_ptw++;
  *at = ptj_ptw_reg(e, scr);
  ptj_scratch_close(e, scr);
  *payload_bits = 64;
  return 1;
}

// Copy one original instruction into the trampoline, fixing rip-relative operands and
// re-encoding internal direct branches with a rel32 back to the original target.
static int ptj_reloc_insn(PtjEmit *e, const ZydisDecodedInstruction *ins,
                          const ZydisDecodedOperand *ops, const uint8_t *raw, uint64_t orig_addr) {
  uint8_t *p = e->p;
  ZydisInstructionCategory cat = ins->meta.category;
  if ((cat == ZYDIS_CATEGORY_COND_BR || cat == ZYDIS_CATEGORY_UNCOND_BR) &&
      ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ops[0].imm.is_relative) {
    uint64_t target = orig_addr + ins->length + (uint64_t)ops[0].imm.value.s;
    if (cat == ZYDIS_CATEGORY_UNCOND_BR) {
      uint8_t *dst = p; *dst++ = 0xe9; dst += 4;
      int64_t rel = (int64_t)target - (int64_t)(uintptr_t)dst;
      if (!ptj_fits32(rel)) return 0;
      *(int32_t *)(p + 1) = (int32_t)rel;
      e->p = dst; e->since_ptw++;
      return 1;
    }
    // conditional: find the condition code from the opcode
    uint8_t cc;
    if (ins->opcode >= 0x70 && ins->opcode <= 0x7f) cc = (uint8_t)(ins->opcode - 0x70);
    else if (ins->opcode >= 0x80 && ins->opcode <= 0x8f) cc = (uint8_t)(ins->opcode - 0x80);
    else return 0;
    uint8_t *dst = p; *dst++ = 0x0f; *dst++ = (uint8_t)(0x80 | cc); dst += 4;
    int64_t rel = (int64_t)target - (int64_t)(uintptr_t)dst;
    if (!ptj_fits32(rel)) return 0;
    *(int32_t *)(p + 2) = (int32_t)rel;
    e->p = dst; e->since_ptw++;
    return 1;
  }
  memcpy(p, raw, ins->length);
  if (ins->raw.modrm.mod == 0 && ins->raw.modrm.rm == 5 && ins->raw.disp.size == 32) {
    uint64_t target = orig_addr + ins->length + (uint64_t)(int64_t)ins->raw.disp.value;
    int64_t rel64 = (int64_t)target - (int64_t)(uintptr_t)(p + ins->length);
    if (!ptj_fits32(rel64)) return 0;
    int32_t rel = (int32_t)rel64;
    memcpy(p + ins->raw.disp.offset, &rel, 4);
  }
  e->p = p + ins->length; e->since_ptw++;
  return 1;
}

static inline int ptj_put_jmp32(uint8_t *at, const uint8_t *target) {
  int64_t rel = (int64_t)(target - (at + 5));
  if (!ptj_fits32(rel)) return 0;
  at[0] = 0xe9;
  int32_t r = (int32_t)rel;
  memcpy(at + 1, &r, 4);
  return 1;
}
// Same, but encoded into a scratch buffer for an instruction that will *live* at `at`
// (the window is assembled off to the side and then installed atomically).
static inline int ptj_enc_jmp32(uint8_t *buf, const uint8_t *at, const uint8_t *target) {
  int64_t rel = (int64_t)(target - (at + 5));
  if (!ptj_fits32(rel)) return 0;
  buf[0] = 0xe9;
  int32_t r = (int32_t)rel;
  memcpy(buf + 1, &r, 4);
  return 1;
}

// ------------------------------------------------------ the patch driver ----
// Per code object: decode it once, group the analyzer's sites into displaceable
// windows, emit one trampoline per window and write the `jmp rel32` over the window.
struct PtjObjStat { uint32_t requested, patched, windows, values; };

struct PtjDec {
  uint32_t *off; uint16_t *len; uint8_t *bad;
  ZydisDecodedInstruction *ins; ZydisDecodedOperand (*ops)[ZYDIS_MAX_OPERAND_COUNT];
  uint32_t n, cap;
  uint32_t *tgt; uint32_t ntgt, tcap;
  uint8_t *vis;          // 1 at every byte offset that starts a *reachable* instruction
  uint8_t *fbd;          // 1 at every byte offset the VM says can be entered indirectly
  uint32_t *stk; uint32_t nstk;
  uint32_t data_from;    // first byte of the object's inline data (jump table / constants)
};
static PtjDec g_ptjdec;

static int ptj_dec_reserve(PtjDec *d, uint32_t n) {
  if (n <= d->cap) return 1;
  uint32_t c = n * 2;
  void *a = realloc(d->off, c * 4); if (!a) return 0; d->off = (uint32_t *)a;
  a = realloc(d->len, c * 2); if (!a) return 0; d->len = (uint16_t *)a;
  a = realloc(d->bad, c); if (!a) return 0; d->bad = (uint8_t *)a;
  a = realloc(d->vis, c); if (!a) return 0; d->vis = (uint8_t *)a;
  a = realloc(d->fbd, c); if (!a) return 0; d->fbd = (uint8_t *)a;
  a = realloc(d->stk, c * 4); if (!a) return 0; d->stk = (uint32_t *)a;
  a = realloc(d->ins, (size_t)c * sizeof(ZydisDecodedInstruction)); if (!a) return 0;
  d->ins = (ZydisDecodedInstruction *)a;
  a = realloc(d->ops, (size_t)c * sizeof(ZydisDecodedOperand) * ZYDIS_MAX_OPERAND_COUNT);
  if (!a) return 0;
  d->ops = (ZydisDecodedOperand (*)[ZYDIS_MAX_OPERAND_COUNT])a;
  a = realloc(d->tgt, c * 4); if (!a) return 0; d->tgt = (uint32_t *)a;
  d->cap = c; d->tcap = c;
  return 1;
}

// Pass 0 of the sweep, as a standalone function: find the object's inline data.  A
// `lea reg,[rip+d]` whose target is inside the same code object addresses a table (V8
// emits `lea r10,[rip+X]; jmp [r10+r8*8]` for a switch and for irregexp dispatch, with
// 8-byte absolute in-object pointers at X).  Everything from the first such target on is
// data: it must not be decoded and must not be displaced.  A linear sweep is used
// deliberately -- a bogus decode can only LOWER the answer, never raise it.
//
// It is separate from ptj_decode_object because the ANALYZER needs the same number
// (defect D-J6): `"data_from"` clips the analyzer's restart fill, so that the fill never
// walks into a jump table the patcher would refuse to touch anyway.  Both sides must
// therefore compute it from the same bytes -- the front end passes the SNAPSHOT it hashes.
static uint32_t ptj_scan_data_from(ZydisDecoder *dec, const uint8_t *code, size_t len) {
  uint32_t data_from = (uint32_t)len;
  for (size_t o = 0; o + 1 < len; ) {
    ZydisDecodedInstruction pi;
    ZydisDecodedOperand po[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(dec, code + o, len - o, &pi, po,
                                             ZYDIS_MAX_OPERAND_COUNT, 0))) { o++; continue; }
    if (pi.mnemonic == ZYDIS_MNEMONIC_LEA && pi.raw.modrm.mod == 0 && pi.raw.modrm.rm == 5 &&
        pi.raw.disp.size == 32) {
      int64_t t = (int64_t)o + pi.length + (int64_t)pi.raw.disp.value;
      if (t > (int64_t)o && t < (int64_t)len && (uint32_t)t < data_from) data_from = (uint32_t)t;
    }
    o += pi.length;
  }
  return data_from;
}

// A **recursive-descent** sweep, not a linear one.  V8 code objects embed data in the
// instruction stream -- irregexp emits an inline jump table right after
// `lea r9,[rip+0xa]; jmp [r9+r8*8]` -- and a linear sweep decodes that table as
// instructions and happily patches a `jmp rel32` over a table entry (observed: SIGSEGV
// on `acorn`).  Only offsets reachable by falling through, by a direct branch or past a
// call are treated as instruction boundaries; everything else is data as far as we care.
//
// D-J6: an OPTIMISED code object is entered in the middle (OSR at a loop header, a deopt
// or exception entry), so a descent from offset 0 can cover 2 % of it.  The analyzer's
// v2.21 restart fill finds those entries and returns them as `restart_roots`; the front
// end puts them into `cx->roots`, so BOTH sides decode the same instruction boundaries and
// a site at an analyzer-only boundary is no longer dropped as `no_insn_boundary`.
static int ptj_decode_object(PtjPatchCtx *cx, const uint8_t *code, size_t len) {
  PtjDec *d = &g_ptjdec;
  if (!ptj_dec_reserve(d, (uint32_t)len + 8)) return 0;
  d->n = 0; d->ntgt = 0;
  memset(d->vis, 0, len);
  memset(d->fbd, 0, len);
  for (uint32_t r = 0; r < cx->nforbid; r++) if (cx->forbid[r] < len) d->fbd[cx->forbid[r]] = 1;
  for (uint32_t r = 0; r < cx->nroots;  r++) if (cx->roots[r]  < len) d->fbd[cx->roots[r]]  = 1;
  uint64_t lo = (uint64_t)(uintptr_t)code, hi = lo + len;
  d->data_from = ptj_scan_data_from(cx->dec, code, len);   // pass 0: inline data
  len = d->data_from;
  hi = lo + len;
  d->nstk = 0;
  d->stk[d->nstk++] = 0;
  // Indirect entry points the VM told us about (HotSpot: the jvmtiAddrLocationMap, the
  // interpreter dispatch tables).  They are decode roots as well as branch targets.
  for (uint32_t r = 0; r < cx->nroots; r++)
    if (cx->roots[r] < len && d->nstk < d->cap) d->stk[d->nstk++] = cx->roots[r];
  while (d->nstk) {
    uint32_t off = d->stk[--d->nstk];
    while (off < len && !d->vis[off]) {
      ZydisDecodedInstruction ins;
      ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
      if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(cx->dec, code + off, len - off, &ins, ops,
                                               ZYDIS_MAX_OPERAND_COUNT, 0))) break;
      d->vis[off] = 1;
      ZydisInstructionCategory c = ins.meta.category;
      int direct = (ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ops[0].imm.is_relative);
      uint64_t t = lo + off + ins.length + (uint64_t)ops[0].imm.value.s;
      if (c == ZYDIS_CATEGORY_UNCOND_BR) {
        if (direct && t >= lo && t < hi && d->nstk < d->cap) d->stk[d->nstk++] = (uint32_t)(t - lo);
        break;                                    // no fall-through
      }
      if (c == ZYDIS_CATEGORY_RET || c == ZYDIS_CATEGORY_INTERRUPT) break;
      if (c == ZYDIS_CATEGORY_COND_BR && direct && t >= lo && t < hi && d->nstk < d->cap)
        d->stk[d->nstk++] = (uint32_t)(t - lo);
      if (c == ZYDIS_CATEGORY_CALL && direct && t >= lo && t < hi && d->nstk < d->cap)
        d->stk[d->nstk++] = (uint32_t)(t - lo);
      off += ins.length;
    }
  }
  // second pass, in address order: build the instruction table and the branch-target set
  for (uint32_t off = 0; off < len; off++) {
    if (!d->vis[off]) continue;
    ZydisDecodedInstruction *ins = &d->ins[d->n];
    ZydisDecodedOperand *ops = d->ops[d->n];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(cx->dec, code + off, len - off, ins, ops,
                                             ZYDIS_MAX_OPERAND_COUNT, 0))) continue;
    d->off[d->n] = off;
    d->len[d->n] = ins->length;
    d->bad[d->n] = (uint8_t)ptj_insn_unsafe(ins, ops, lo, hi, lo + off);
    if (d->n == 0 && off == 0 && cx->sparkplug && len >= 20 && code[0] == 0xbb &&
        code[5] == 0x49 && code[6] == 0xbc && code[15] == 0xe8) {
      uint32_t imm; memcpy(&imm, code + 1, 4);
      if (imm < 0x10000 && !(imm & 7)) d->bad[0] = 0;  // `mov ebx,<frame size>`: a plain integer
    }
    ZydisInstructionCategory c = ins->meta.category;
    if ((c == ZYDIS_CATEGORY_COND_BR || c == ZYDIS_CATEGORY_UNCOND_BR || c == ZYDIS_CATEGORY_CALL) &&
        ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ops[0].imm.is_relative) {
      uint64_t t = lo + off + ins->length + (uint64_t)ops[0].imm.value.s;
      if (t >= lo && t < hi && d->ntgt < d->tcap) d->tgt[d->ntgt++] = (uint32_t)(t - lo);
    }
    d->n++;
  }
  return 1;
}

static int ptj_is_target(PtjDec *d, uint32_t o) {
  for (uint32_t i = 0; i < d->ntgt; i++) if (d->tgt[i] == o) return 1;
  return 0;
}
// An offset the VM says can be entered indirectly (HotSpot: a PcDesc / a dispatch-table
// entry).  Treated exactly like a direct branch target: no window may contain it strictly
// inside, or a thread entering there would land in the middle of our `jmp rel32`.
// `d->fbd` is filled from cx->forbid and cx->roots once per object.
static inline int ptj_is_forbidden(PtjDec *d, uint32_t o) { return d->fbd && d->fbd[o]; }
static int ptj_find_insn(PtjDec *d, uint32_t o) {
  int lo = 0, hi = (int)d->n - 1;
  while (lo <= hi) { int m = (lo + hi) / 2;
    if (d->off[m] == o) return m;
    if (d->off[m] < o) lo = m + 1; else hi = m - 1; }
  return -1;
}

static void ptj_sort_sites(PtjSite *s, int n) {
  for (int i = 1; i < n; i++) {
    PtjSite t = s[i]; int j = i - 1;
    while (j >= 0 && (s[j].off > t.off || (s[j].off == t.off &&
           (s[j].when > t.when || (s[j].when == t.when && s[j].id > t.id))))) { s[j+1] = s[j]; j--; }
    s[j+1] = t;
  }
}

static void ptj_add_entry(PtjPatchCtx *cx, uint8_t *at, uint64_t orig, const PtjSite *st,
                          int reg, int half, int pbits) {
  if (cx->nentries >= cx->maxentries) return;
  PtjMapEntry *m = &cx->entries[cx->nentries++];
  m->tramp_addr = (uint64_t)(uintptr_t)at; m->orig_addr = orig; m->tsc = cx->tsc;
  m->obj = cx->obj; m->site = st->id; m->kind = st->kind; m->when = st->when;
  m->size = st->size; m->reg = (uint8_t)reg; m->half = (uint8_t)half;
  m->payload_bits = (uint8_t)pbits;
  m->counter = m->kf_branch = m->kf_join = 0; m->kf_period = 0; m->resync = 0;
  m->buffer = 0;
  cx->n_values++;
}

// Emit the values of one site.  Returns 0 if the site could not be encoded.
static int ptj_emit_site_values(PtjPatchCtx *cx, PtjEmit *e, const PtjSite *st,
                                const ZydisDecodedInstruction *ins,
                                const uint8_t *raw, uint64_t orig_addr);
static int ptj_emit_site(PtjPatchCtx *cx, PtjEmit *e, const PtjSite *st,
                         const ZydisDecodedInstruction *ins, const ZydisDecodedOperand *ops,
                         const uint8_t *raw, uint64_t orig_addr) {
  (void)ops;
  if (st->kind == PTJ_KIND_GT) return 1;          // window-forcing pseudo-site: no value
  if (!cx->use_ptwrite && !cx->sink_buffer) return 1;   // bare-detour A/B control
  // A `resync' keyframe site (analyzer `"resync": true, "keyframe": K'): wrap the whole
  // site -- all of its registers together, which is the point -- in one countdown guard.
  uint8_t *kf_branch = nullptr, *kf_hole = nullptr;
  uint64_t *kf_ctr = nullptr;
  uint32_t kf_first = cx->nentries;
  if (st->kf) {
    if (!st->flags_dead && !cx->kf_flags_live) { cx->drops[PTJ_DROP_KF_FLAGS]++; return 1; }
    kf_ctr = ptj_kf_alloc(cx);
    if (!kf_ctr || !ptj_kf_open(e, kf_ctr, st->kf, &kf_branch, &kf_hole)) {
      cx->drops[PTJ_DROP_KF_NOCTR]++; return 1;   // no guard -> no site (never log unguarded:
    }                                             // a loop header would log every iteration)
    cx->kf_sites++;
  }
  int ok = ptj_emit_site_values(cx, e, st, ins, raw, orig_addr);
  if (kf_ctr && ok) {
    ptj_kf_close(e, kf_hole);
    for (uint32_t q = kf_first; q < cx->nentries; q++) {
      cx->entries[q].kf_period = st->kf;
      cx->entries[q].counter   = (uint64_t)(uintptr_t)kf_ctr;
      cx->entries[q].kf_branch = (uint64_t)(uintptr_t)kf_branch;
      cx->entries[q].kf_join   = (uint64_t)(uintptr_t)e->p;
      cx->entries[q].resync    = st->resync ? 1 : 0;
      cx->kf_values++;
    }
  }
  return ok;
}

static int ptj_emit_site_buffer(PtjPatchCtx *cx, PtjEmit *e, const PtjSite *st,
                                const ZydisDecodedInstruction *ins,
                                const uint8_t *raw, uint64_t orig_addr);
static int ptj_emit_site_values(PtjPatchCtx *cx, PtjEmit *e, const PtjSite *st,
                                const ZydisDecodedInstruction *ins,
                                const uint8_t *raw, uint64_t orig_addr) {
  if (cx->sink_buffer) return ptj_emit_site_buffer(cx, e, st, ins, raw, orig_addr);
  if (st->kind == PTJ_KIND_MEMOP) {
    int pb = 64; uint8_t *at = nullptr;
    int scr = 0;                                   // %rax
    if (!ptj_ptw_mem(e, ins, raw, orig_addr, st->size ? st->size : 8, scr, &pb, &at)) return 0;
    ptj_add_entry(cx, at, orig_addr, st, 255, 0, pb);
    return 1;
  }
  for (int i = 0; i < st->nregs; i++) {
    int r = st->regs[i];
    if (r < 16) {
      uint8_t *at = ptj_ptw_reg(e, r);
      ptj_add_entry(cx, at, orig_addr, st, r, 0, 64);
    } else if (r < 32) {
      uint8_t *l = nullptr, *h = nullptr;
      int scr = (r - 16) == 0 ? 0 : 0;             // %rax; saved and restored
      ptj_ptw_xmm(e, r - 16, scr, &l, &h);
      ptj_add_entry(cx, l, orig_addr, st, r, 1, 64);
      ptj_add_entry(cx, h, orig_addr, st, r, 2, 64);
    } else return 0;
  }
  return 1;
}

// The whole object.  `code` is the live code object; `sites` come from the analyzer
// with offsets relative to it.
static int ptj_patch_object(PtjPatchCtx *cx, uint8_t *code, size_t len,
                            PtjSite *sites, int nsites, PtjObjStat *ost,
                            void (*wr_open)(void), void (*wr_close)(void),
                            void (*note_tramp)(PtjPatchCtx *, uint8_t *, size_t, uint8_t *, size_t)) {
  PtjDec *d = &g_ptjdec;
  ost->requested = (uint32_t)nsites;
  if (!ptj_decode_object(cx, code, len)) { cx->drops[PTJ_DROP_DECODE] += nsites; return 0; }
  ptj_sort_sites(sites, nsites);
  // SAME-RUN GROUND TRUTH (defect D-J2).  `--gt-all' for a JIT: every memory-accessing
  // instruction of the object becomes a window-forcing PSEUDO-SITE, so the patcher displaces
  // it and the trampoline can record its effective address.  Without this the oracle would
  // only ever cover the handful of instructions that happen to sit inside a critical-value
  // window.  The pseudo-sites carry no value and cost no analyzer round; they are already
  // sorted by offset, so they are MERGED into the (sorted) analyzer set rather than run
  // through the O(n^2) insertion sort, which a 5000-instruction nmethod would not survive.
  for (int q = 0; q < nsites; q++) if (sites[q].kf) cx->kf_requested++;
  PtjSite *gtbuf = nullptr;
  if (cx->gt) {
    gtbuf = (PtjSite *)malloc(sizeof(PtjSite) * ((size_t)nsites + d->n + 1));
    if (gtbuf) {
      uint32_t a = 0; int b = 0, t = 0;
      PtjSite ps; memset(&ps, 0, sizeof ps);
      ps.id = 0xffff; ps.when = PTJ_WHEN_BEFORE; ps.kind = PTJ_KIND_GT; ps.size = 8;
      for (; a < d->n || b < nsites; ) {
        int take_gt = 0;
        uint32_t goff = 0;
        while (a < d->n && (d->bad[a] || ptj_gt_operand(&d->ins[a], d->ops[a]) < 0)) a++;
        if (a < d->n) { goff = d->off[a]; take_gt = (b >= nsites || goff <= sites[b].off); }
        if (take_gt) { ps.off = goff; gtbuf[t++] = ps; a++; }
        else if (b < nsites) gtbuf[t++] = sites[b++];
        else break;
      }
      sites = gtbuf; nsites = t;
    }
  }
  uint64_t lo = (uint64_t)(uintptr_t)code;
  int i = 0;
  // No window may start before `guard_off`.  For HotSpot that starts at cx->lo: the 5
  // bytes at the verified entry are the JVM's own not-entrant patch site.
  uint32_t guard_off = cx->lo;
  uint32_t limit = cx->hi ? cx->hi : (uint32_t)len;
  while (i < nsites) {
    PtjSite *s0 = &sites[i];
    if (s0->off < guard_off) { cx->drops[PTJ_DROP_BRANCH_TARGET]++; ptj_note_unpatched_s(cx, &sites[i]); i++; continue; }
    // A keyframe guard is `dec CNT(%rip)', which writes EFLAGS.  Where the analyzer could not
    // prove them dead the site is dropped (the ELF default, keyframe_impl.md section 1.4):
    // logging a loop header unconditionally would cost one PTWRITE per iteration of the
    // hottest loop in the program, and a keyframe is an accuracy bonus, not a correctness
    // requirement.  Done here as well as in ptj_emit_site so it does not consume a window.
    if (s0->kf && !s0->flags_dead && !cx->kf_flags_live) {
      cx->drops[PTJ_DROP_KF_FLAGS]++; ptj_note_unpatched_s(cx, &sites[i]); i++; continue; }
    int k = ptj_find_insn(d, s0->off);
    if (k < 0) {
      cx->drops[s0->off >= d->data_from ? PTJ_DROP_DATA_REGION : PTJ_DROP_NO_BOUNDARY]++;
      ptj_note_unpatched_s(cx, &sites[i]); i++; continue;
    }
    // unsupported registers -> drop the site, keep going
    int unsup = 0;
    for (int r = 0; r < s0->nregs; r++) if (sites[i].regs[r] >= 32) unsup = 1;
    if (unsup) { cx->drops[PTJ_DROP_UNSUP_REG]++; ptj_note_unpatched_s(cx, &sites[i]); i++; continue; }
    if (d->bad[k]) { cx->drops[d->bad[k]]++; ptj_note_unpatched_s(cx, &sites[i]); i++; continue; }
    // grow the window to >= 5 bytes
    int j = k; uint32_t end = d->off[k]; int bad = 0;
    while (end - d->off[k] < 5) {
      if (j >= (int)d->n) { bad = PTJ_DROP_WINDOW_TRUNCATED; break; }
      if (d->bad[j]) { bad = (int)d->bad[j] + (PTJ_DROP_WINDOW_IMM32 - PTJ_DROP_IMM32); break; }
      if (j > k && d->off[j] != end) { bad = PTJ_DROP_WINDOW_TRUNCATED; break; }  // a gap = data
      end = d->off[j] + d->len[j]; j++;
    }
    if (bad) { cx->drops[bad]++; ptj_note_unpatched_s(cx, &sites[i]); i++; continue; }
    if (cx->single && j > k + 1) { cx->drops[PTJ_DROP_WINDOW_MULTI]++; ptj_note_unpatched_s(cx, &sites[i]); i++; continue; }
    if (end > limit) { cx->drops[PTJ_DROP_WINDOW_TRUNCATED]++; ptj_note_unpatched_s(cx, &sites[i]); i++; continue; }
    // No interior instruction boundary may be the target of a direct branch, nor an offset
    // the VM told us can be entered indirectly (HotSpot: a PcDesc / a dispatch-table entry).
    int clash = 0;
    for (int q = k + 1; q < j; q++)
      if (ptj_is_target(d, d->off[q]) || ptj_is_forbidden(d, d->off[q])) { clash = 1; break; }
    if (clash) { cx->drops[PTJ_DROP_BRANCH_TARGET]++; ptj_note_unpatched_s(cx, &sites[i]); i++; continue; }
    // absorb every site that falls inside the window
    int m = i;
    while (m < nsites && sites[m].off < end) m++;
    // emit
    if (*cx->slab_used + 1024 + (end - d->off[k]) * 4 > cx->slab_cap) {
      cx->drops[PTJ_DROP_SLAB_FULL] += (m - i); for (int x = i; x < m; x++) ptj_note_unpatched_s(cx, &sites[x]); i = m; continue;
    }
    uint8_t *t = cx->slab + *cx->slab_used;
    int64_t reach = (int64_t)((uint64_t)(uintptr_t)t - (lo + d->off[k]));
    if (!ptj_fits32(reach)) { cx->drops[PTJ_DROP_OUT_OF_RANGE] += (m - i); for (int x = i; x < m; x++) ptj_note_unpatched_s(cx, &sites[x]); i = m; continue; }
    uint32_t saved_entries = cx->nentries, saved_relocs = cx->nrelocs, saved_gtents = cx->ngtents;
    uint64_t saved_values = cx->n_values;
    PtjEmit e; e.p = t; e.since_ptw = 0; e.any_ptw = 0; e.space = cx->space;
    int fail = 0, npatched = 0;
    if (cx->ctr && cx->ntramps < cx->ctr_max && cx->ntramps < cx->maxtramps)
      ptj_emit_counter(&e, &cx->ctr[cx->ntramps]);
    for (int q = k; q < j && !fail; q++) {
      uint64_t ia = lo + d->off[q];
      const uint8_t *raw = code + d->off[q];
      for (int x = i; x < m; x++) {
        if (sites[x].off != d->off[q] || sites[x].when != PTJ_WHEN_BEFORE) continue;
        if (!ptj_emit_site(cx, &e, &sites[x], &d->ins[q], d->ops[q], raw, ia)) { fail = 1; break; }
      }
      if (fail) break;
      uint8_t *gkey = nullptr; PtjGtEnt *gent = nullptr;
      if (cx->gt && cx->gt_cur) {                 // the ground-truth record for THIS access
        int gi = ptj_gt_operand(&d->ins[q], d->ops[q]);
        if (gi >= 0) {
          uint8_t *gat = e.p;
          if (ptj_emit_gt(&e, &d->ins[q], raw, ia, cx->gt_cur, &gkey)) {
            if (cx->gtents && cx->ngtents < cx->maxgtents) {
              gent = &cx->gtents[cx->ngtents++];
              gent->tramp_addr = (uint64_t)(uintptr_t)gat; gent->orig_addr = ia; gent->tsc = cx->tsc;
              gent->key_addr = 0;
              gent->obj = cx->obj; gent->size = (uint16_t)(d->ops[q][gi].size / 8);
              uint8_t act = (uint8_t)d->ops[q][gi].actions;
              gent->op = (act & ZYDIS_OPERAND_ACTION_MASK_WRITE)
                        ? ((act & ZYDIS_OPERAND_ACTION_MASK_READ) ? 2 : 1) : 0;
            }
          } else { gkey = nullptr; cx->gt_refused++; }
        }
      }
      uint8_t *rstart = e.p;
      if (!ptj_reloc_insn(&e, &d->ins[q], d->ops[q], raw, ia)) { fail = 1; break; }
      // D-J9: the ground-truth record's lockstep key is the address of the relocated copy
      // just emitted -- what `ptrecon' has in hand (relocated[].tramp_addr) when it produces
      // the reconstructed record for this access.
      if (gkey) { uint64_t kv = (uint64_t)(uintptr_t)rstart; memcpy(gkey, &kv, 8);
                  if (gent) gent->key_addr = kv; }
      if (cx->nrelocs < cx->maxrelocs) {
        PtjReloc *rr = &cx->relocs[cx->nrelocs++];
        rr->tramp_addr = (uint64_t)(uintptr_t)rstart; rr->orig_addr = ia;
        rr->obj = cx->obj; rr->len = d->len[q]; rr->tramp_len = (uint16_t)(e.p - rstart);
      }
      for (int x = i; x < m && !fail; x++) {
        if (sites[x].off != d->off[q] || sites[x].when != PTJ_WHEN_AFTER) continue;
        if (!ptj_emit_site(cx, &e, &sites[x], &d->ins[q], d->ops[q], raw, ia)) fail = 1;
      }
    }
    if (!fail) {
      if (!ptj_put_jmp32(e.p, code + end)) fail = 1;
      else e.p += 5;
    }
    if (fail) {
      cx->nentries = saved_entries; cx->nrelocs = saved_relocs; cx->n_values = saved_values;
      cx->ngtents = saved_gtents;
      cx->drops[PTJ_DROP_ENCODE] += (m - i); for (int x = i; x < m; x++) ptj_note_unpatched_s(cx, &sites[x]); i = m; continue;
    }
    size_t tlen = (size_t)(e.p - t);
    *cx->slab_used = (*cx->slab_used + tlen + 15) & ~(size_t)15;
    // diagnostics: remember the window's original bytes before we overwrite them
    {
      uint32_t wl = end - d->off[k];
      cx->last_orig_len = wl > 32 ? 32 : wl;
      memcpy(cx->last_orig, code + d->off[k], cx->last_orig_len);
      cx->last_obj_addr = lo; cx->last_obj_len = (uint32_t)len; cx->last_off = d->off[k];
    }
    // install the detour.  The finished window is built first, then written either with a
    // plain store (V8: the code object cannot be executing yet) or through cx->install,
    // which the HotSpot front end supplies as the int3-then-jmp cross-modifying protocol.
    uint32_t wlen = end - d->off[k];
    uint8_t wbuf[32];
    if (wlen > sizeof(wbuf)) { cx->drops[PTJ_DROP_ENCODE] += (m - i); for (int x = i; x < m; x++) ptj_note_unpatched_s(cx, &sites[x]); i = m; continue; }
    int ok = ptj_enc_jmp32(wbuf, code + d->off[k], t);
    for (uint32_t z = 5; z < wlen; z++) wbuf[z] = 0x90;
    if (wr_open) wr_open();
    if (ok) {
      if (cx->install) ok = cx->install(code + d->off[k], wbuf, wlen, (uint64_t)(uintptr_t)t);
      else memcpy(code + d->off[k], wbuf, wlen);
    }
    if (wr_close) wr_close();
    if (!ok) {
      cx->nentries = saved_entries; cx->nrelocs = saved_relocs; cx->n_values = saved_values;
      cx->ngtents = saved_gtents;
      cx->drops[PTJ_DROP_ENCODE] += (m - i); for (int x = i; x < m; x++) ptj_note_unpatched_s(cx, &sites[x]); i = m; continue;
    }
    if (cx->ntramps < cx->maxtramps) {
      if (cx->ctr_orig && cx->ntramps < cx->ctr_max)
        cx->ctr_orig[cx->ntramps] = lo + d->off[k];
      cx->tramps[cx->ntramps++] = (uint64_t)(uintptr_t)t;
    }
    npatched = m - i;
    ost->patched += (uint32_t)npatched;
    ost->windows++;
    if (note_tramp) note_tramp(cx, code + d->off[k], end - d->off[k], t, tlen);
    guard_off = end;
    i = m;
    // continue scanning from the first instruction after the window
  }
  ost->values = (uint32_t)(cx->n_values);
  free(gtbuf);
  return 1;
}
#endif
