// jithook.cc -- PTracer v2 JIT PoC: in-process instrumentation of V8 JIT code.
//
// A Node native addon that installs a v8::Isolate JitCodeEventHandler and, per
// mode, (1) counts events, (2) additionally dumps every code object's bytes to
// a jitdump-like file, (3) additionally patches a 5-byte jmp at the code
// object's entry into an RWX trampoline that executes `ptwrite %rdi`, the
// displaced instruction(s), and jumps back.
//
// Build: see build.sh.  Load: require('./jithook.node').
#include <node_api.h>
#include <v8.h>

#include <dlfcn.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

extern "C" {
#include <Zydis/Zydis.h>
}
#include "jitsites.h"
#include "jitmode.h"
#include "jitanchor.h"
#include "jitpatch.h"
#include "jitdump.h"
#define PT_PIN_DEFINE_MARKER
#include "../pinjit/jitbridge.h"

// ---------------------------------------------------------------- knobs -----
enum Mode {
  MODE_OFF = 0,
  MODE_COUNT = 1,   // empty-ish handler: counters only
  MODE_DUMP = 2,    // + copy code bytes into an arena, flushed at exit
  MODE_PATCH = 3,   // + patch entry with jmp -> trampoline (ptwrite)
  MODE_FULL = 4,    // + the analyzer's full critical value set (all three site kinds)
};

static int g_mode = MODE_OFF;
static int g_pinbridge = 0;
static int g_stack_anchor = 0;
static uint64_t g_stack_anchor_objects = 0, g_stack_anchor_values = 0;
static uint64_t g_pin_objects = 0, g_pin_sites = 0;
static void pin_publish(uint64_t operation, uint8_t *code, size_t len,
                        PtjSite *sites = nullptr, int n = 0, uint8_t *to = nullptr) {
  PtPinRequest request = {1, operation, (uint64_t)code, len, (uint64_t)sites,
                         (uint64_t)n, (uint64_t)to, 0};
  ptj_pin_publish(&request);
  if (request.acknowledged != 1) {
    fprintf(stderr, "PTJIT: Pin JIT bridge unavailable; use hifitool -jitbridge 1\n");
    _exit(71);
  }
}
static int g_verbose = 0;
static int g_use_ptwrite = 1;      // 0 = trampoline stores to a buffer instead
static uint64_t g_max_patch = ~0ull;
static uint64_t g_min_patch = 0;
static uint64_t g_attempt = 0;
static int g_no_imm64 = 0;

// ------------------------------------------------------------- counters -----
struct Counters {
  uint64_t added, moved, removed, other;
  uint64_t added_bytecode, added_jitcode, added_wasm;
  uint64_t bytes_added, bytes_moved;
  uint64_t handler_ns;
  uint64_t off_thread;      // events not on the thread that installed the hook
  uint64_t patched, patch_skipped_decode, patch_skipped_small,
      patch_skipped_ro, patch_skipped_type, patch_skipped_cap,
      patch_skipped_range;
  // --- implicit exceptions, V8 half -----------------------------
  // `patch_skipped_bytecode'/`patch_skipped_wasm' split the old `patch_skipped_type' bucket,
  // so the WASM_CODE gate is visible in every run's stats; `patch_skipped_wasm_range' is the
  // second, forward-proof gate (a code object V8 registered with its wasm trap handler, no
  // matter what code_type it was reported as).  `wasmprot_*' is the standing audit.
  uint64_t patch_skipped_bytecode, patch_skipped_wasm, patch_skipped_wasm_range;
  uint64_t wasmprot_objs, wasmprot_live, wasmprot_insns, wasmprot_overlap,
      wasmprot_overlap_insn, wasmprot_recovered, wasm_probe_sites;
  // V8 frees a NativeModule's handler data when the module dies (`ReleaseHandlerData`), so
  // the exit-time table can be empty even though wasm ran.  These are lifetime independent:
  // every WASM_CODE range V8 ever reported, and the peak size of the live table.
  uint64_t wasm_ranges, wasmprot_overlap_reported, wasmprot_live_max, wasmprot_insns_max;
  uint64_t dump_bytes, dump_truncated;
  // Data-gate audit (the Node analogue of the Java agent's data gate): JIT_CODE objects whose linear decode
  // from their FIRST byte hits an undecodable byte before any jmp/ret/ud2/hlt, i.e. objects that
  // may be data reported as code.  `obj_data_gate_checked' = objects examined.
  uint64_t obj_data_skips, obj_data_gate_checked;
};
static Counters g_c;
// PTJIT_DATA_GATE: 0 = off (no scan, zero cost), 1 = COUNT ONLY (default; no behaviour change),
// 2 = count and do NOT patch such objects.
static int g_data_gate = -1;
static int data_gate_mode() {
  if (g_data_gate < 0) { const char *v = getenv("PTJIT_DATA_GATE"); g_data_gate = v ? atoi(v) : 1; }
  return g_data_gate;
}
static pid_t g_install_tid;
static int g_diag = 0;
static int g_diag_every = 200;
static uint64_t g_diag_worst = 0;
static uint64_t g_verify_unmapped = 0;
static FILE *g_hashdump = nullptr;
static uint64_t verify_pass(FILE *, int, uint64_t *, uint64_t *, uint64_t *);

static inline uint64_t now_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

// ============================================================================
//  V8's wasm trap handler: the one pc-keyed fault-recovery table V8 has
//  (implicit exceptions, V8 front end)
//
//  HotSpot turns a fault in compiled code into a Java exception by looking the FAULTING PC
//  up in the nmethod's implicit-exception table, so displacing such an instruction into our
//  slab is fatal.  V8 has exactly one table of that
//  shape and it belongs to WebAssembly.  `v8::internal::trap_handler` keeps
//
//      size_t gNumCodeObjects;                        // _ZN2v88internal12trap_handler15gNumCodeObjectsE
//      CodeProtectionInfoListEntry *gCodeObjects;     // {CodeProtectionInfo *info; int next_free;}, 16-byte stride
//      struct CodeProtectionInfo { uintptr_t base; size_t size; size_t ninstr; uint32_t off[]; };
//
//  and `IsFaultAddressCovered(pc)` -- whose argument is `uc_mcontext.gregs[REG_RIP]`, read at
//  offset 0xa8 of the ucontext by `TryHandleSignal` -- requires `base <= pc < base+size` AND
//  `pc - base` to appear EXACTLY in `off[]` before it rewrites RIP to `gLandingPad`.  The
//  layout above is read off node 22.23 (V8 12.4) with objdump; the live table is the evidence.
//  A relocated copy of a protected instruction satisfies neither test, so displacing one
//  would turn a wasm OOB trap into a fatal SIGSEGV.
//
//  Only `wasm::WasmCode::RegisterTrapHandlerData()` (called from
//  `NativeModule::PublishCode[Locked]`) ever fills that table: no JS/TurboFan path does.  And
//  V8 reports wasm code to a JitCodeEventHandler as `WASM_CODE`, which the type gate in
//  `handler()` never patches.  The counters below are the standing proof: in every run
//  `wasmprot_overlap` must be 0.
// ============================================================================
struct V8ProtInfo { uint64_t base, size, ninstr; uint32_t off[1]; };
static uint64_t *g_v8_nobj = nullptr;       // &gNumCodeObjects
static uint8_t **g_v8_objs = nullptr;       // &gCodeObjects (the variable, not the array)
static uint64_t *g_v8_recovered = nullptr;  // &gRecoveredTrapCount
static int g_v8_syms = 0;
// V8 mutates the table under `trap_handler::MetadataLock` (a spinlock) from its wasm
// compilation threads, and `ReleaseHandlerData` FREES a CodeProtectionInfo, so reading the
// table unlocked is a use-after-free waiting to happen.  Both the ctor and the dtor are
// exported, so we take V8's own lock; if they cannot be resolved we do not read the table
// at all (`wasmprot_locked = 0`) rather than race with it.
static void (*g_v8_mlock)(void *) = nullptr;
static void (*g_v8_munlock)(void *) = nullptr;
static int g_patch_wasm = 0;                // PTJIT_PATCH_WASM=1: let WASM_CODE through (A/B)
static int g_wasm_probe = 0;                // PTJIT_WASM_PROBE=1: displace a PROTECTED insn
// Every WASM_CODE range V8 ever reported to us, so the exit audit does not depend on the
// module still being alive (V8 calls ReleaseHandlerData when a NativeModule dies).
#define PTJ_WASMRNG_MAX 8192
static uint64_t g_wasmrng_lo[PTJ_WASMRNG_MAX], g_wasmrng_hi[PTJ_WASMRNG_MAX];
static uint32_t g_nwasmrng = 0;
static void wasm_range_note(uint64_t lo, uint64_t hi) {
  for (uint32_t i = 0; i < g_nwasmrng; i++)
    if (lo < g_wasmrng_hi[i] && hi > g_wasmrng_lo[i]) {      // V8 reuses wasm code space
      if (lo < g_wasmrng_lo[i]) g_wasmrng_lo[i] = lo;
      if (hi > g_wasmrng_hi[i]) g_wasmrng_hi[i] = hi;
      return;
    }
  if (g_nwasmrng < PTJ_WASMRNG_MAX) {
    g_wasmrng_lo[g_nwasmrng] = lo; g_wasmrng_hi[g_nwasmrng] = hi; g_nwasmrng++;
  }
}
static int in_wasm_range(uint64_t a) {
  for (uint32_t i = 0; i < g_nwasmrng; i++)
    if (a >= g_wasmrng_lo[i] && a < g_wasmrng_hi[i]) return 1;
  return 0;
}

static void v8_trap_syms(void) {
  g_v8_nobj = (uint64_t *)dlsym(RTLD_DEFAULT,
                               "_ZN2v88internal12trap_handler15gNumCodeObjectsE");
  g_v8_objs = (uint8_t **)dlsym(RTLD_DEFAULT,
                               "_ZN2v88internal12trap_handler12gCodeObjectsE");
  g_v8_recovered = (uint64_t *)dlsym(RTLD_DEFAULT,
                               "_ZN2v88internal12trap_handler19gRecoveredTrapCountE");
  g_v8_mlock = (void (*)(void *))dlsym(RTLD_DEFAULT,
                               "_ZN2v88internal12trap_handler12MetadataLockC1Ev");
  g_v8_munlock = (void (*)(void *))dlsym(RTLD_DEFAULT,
                               "_ZN2v88internal12trap_handler12MetadataLockD1Ev");
  g_v8_syms = (g_v8_nobj && g_v8_objs && g_v8_mlock && g_v8_munlock) ? 1 : 0;
}
// RAII over V8's own metadata spinlock.
struct V8MetaLock {
  char self[8];
  V8MetaLock() { memset(self, 0, sizeof self); g_v8_mlock(self); }
  ~V8MetaLock() { g_v8_munlock(self); }
};
static inline uint64_t v8_prot_nobj(void) {
  return (g_v8_syms && g_v8_nobj) ? *(volatile uint64_t *)g_v8_nobj : 0;
}
static inline const V8ProtInfo *v8_prot_at(uint64_t i) {
  uint8_t *tab = *(uint8_t *volatile *)g_v8_objs;
  if (!tab) return nullptr;
  return *(const V8ProtInfo *volatile *)(tab + 16 * i);   // entry = {info*, int next_free}
}
// pc inside ANY code object V8 registered with the trap handler (i.e. wasm code).
static int v8_in_wasm_code(uint64_t pc) {
  if (!g_v8_syms) return 0;
  V8MetaLock lk;
  uint64_t n = v8_prot_nobj();
  for (uint64_t i = 0; i < n; i++) {
    const V8ProtInfo *p = v8_prot_at(i);
    if (p && pc >= p->base && pc - p->base < p->size) return 1;
  }
  return 0;
}
// ... and exactly ON a protected instruction, the test IsFaultAddressCovered() applies.
static int v8_is_protected(uint64_t pc) {
  if (!g_v8_syms) return 0;
  V8MetaLock lk;
  uint64_t n = v8_prot_nobj();
  for (uint64_t i = 0; i < n; i++) {
    const V8ProtInfo *p = v8_prot_at(i);
    if (!p || pc < p->base || pc - p->base >= p->size) continue;
    uint32_t o = (uint32_t)(pc - p->base);
    for (uint64_t k = 0; k < p->ninstr; k++) if (p->off[k] == o) return 1;
  }
  return 0;
}
// A snapshot of the registered code objects' ranges, so the exit audit can compare tens of
// thousands of displaced instructions without holding V8's spinlock across the whole loop.
static uint32_t v8_snapshot_ranges(uint64_t *lo, uint64_t *hi, uint32_t max,
                                   uint64_t *live, uint64_t *ninsn) {
  *live = 0; *ninsn = 0;
  if (!g_v8_syms) return 0;
  V8MetaLock lk;
  uint64_t n = v8_prot_nobj(); uint32_t m = 0;
  for (uint64_t i = 0; i < n; i++) {
    const V8ProtInfo *p = v8_prot_at(i);
    if (!p) continue;
    (*live)++; *ninsn += p->ninstr;
    if (m < max) { lo[m] = p->base; hi[m] = p->base + p->size; m++; }
  }
  return m;
}

// ---------------------------------------------------------- code dumping ----
// The jitdump (`JTR1` records) and the site map are `jitdump.h`, shared with the HotSpot
// front end (`java/jvmtiagent.cc`).
static PtjArena g_dump;
static inline uint64_t rdtscp_now() { return ptj_rdtscp(); }
static inline void arena_init(size_t cap) { ptj_arena_init(&g_dump, cap); }
static inline void arena_record(uint8_t evt, uint8_t code_type, const void *addr,
                                const void *new_addr, size_t code_len,
                                const char *name, size_t name_len, int with_code) {
  size_t before = g_dump.truncated;
  ptj_arena_record(&g_dump, evt, code_type, addr, new_addr, addr, code_len,
                   name, name_len, with_code);
  if (g_dump.truncated != before) g_c.dump_truncated++;
  else g_c.dump_bytes += code_len;
}

// -------------------------------------------------------- trampolines -------
// One RWX slab; trampolines are bump-allocated. Each is:
//     [ptwrite %rdi | mov %rdi,(%rbx-ish buffer)]   ; the critical value sink
//     <relocated original bytes>
//     jmp back
static uint8_t *g_tslab = nullptr;
static size_t g_tslab_cap = 0, g_tslab_used = 0;

static size_t g_tslab_want = 0;

// The entry patch is a 5-byte `jmp rel32`, so the slab must be within +-2GB of
// every code object it serves.  Allocate it lazily, hinted just below the first
// JIT code address we see (V8's code space is one reserved 128MB-ish cage).
static size_t g_tslab_live = 0;            // bytes of the slab that are RWX
// Keyframes and the same-run ground truth.
static uint32_t g_keyframe = 1024;         // PTJIT_KEYFRAME: analyzer `"keyframe": K'
// PTJIT_NOROOTS=1 -- ablation of the restart-roots seeding: ask the analyzer exactly as usual
// but do NOT seed the patcher's own recursive descent with the reply's `restart_roots`.  Every
// site beyond the first restart then lands on an offset this sweep never decoded and is
// dropped as `no_insn_boundary'.
static int g_noroots = 0;   // roots seeded by default
static uint32_t g_kfctr_max = 16384;       // PTJIT_KFCTR: countdown cells (1 MiB of slab)
static PtjGtRing g_gt;                     // PTJIT_GT=1: the process's own address log
static const size_t TSLAB_CHUNK = 1u << 20;

// Only the part of the slab that actually holds trampolines is executable; the
// rest stays PROT_NONE.  A stale detour (one that survived a move we did not
// repair) then faults immediately and diagnosably instead of running into a
// zero-filled RWX page and corrupting memory.
static void tslab_commit(size_t upto) {
  if (upto <= g_tslab_live || !g_tslab) return;
  size_t want = (upto + TSLAB_CHUNK - 1) & ~(TSLAB_CHUNK - 1);
  if (want > g_tslab_cap) want = g_tslab_cap;
  if (mprotect(g_tslab + g_tslab_live, want - g_tslab_live,
               PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
    memset(g_tslab + g_tslab_live, 0xcc, want - g_tslab_live);   // int3 padding
    g_tslab_live = want;
  }
}

static void tslab_init_near(uint64_t near, size_t cap) {
  if (g_tslab) return;
  if (!cap) cap = 64u << 20;
  int guard = getenv("PTJIT_SLABGUARD") ? atoi(getenv("PTJIT_SLABGUARD")) : 1;
  int prot = guard ? PROT_NONE : (PROT_READ | PROT_WRITE | PROT_EXEC);
  for (int64_t off = 1 << 20; off < (int64_t)1 << 31; off <<= 1) {
    uint64_t hint = (near - (uint64_t)off) & ~(uint64_t)0xfffff;
    void *m = mmap((void *)hint, cap, prot,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) continue;
    int64_t d1 = (int64_t)((uint64_t)m - near);
    int64_t d2 = (int64_t)((uint64_t)m + cap - near);
    if (d1 > -0x7f000000LL && d2 < 0x7f000000LL) {
      g_tslab = (uint8_t *)m; g_tslab_cap = cap; g_tslab_used = 0;
      g_tslab_live = guard ? 0 : cap;
      tslab_commit(TSLAB_CHUNK);
      return;
    }
    munmap(m, cap);
  }
}

// Make [addr, addr+len) writable. V8 12.4 maps code space RWX (PKU-protected
// when --memory-protection-keys is on); we do not mprotect (that would drop
// V8's pkey) but open the pkey window instead if needed.
static int g_pkey_open_done = 0;

#if defined(__x86_64__)
static inline uint32_t rd_pkru() {
  uint32_t eax, edx, ecx = 0;
  __asm__ volatile(".byte 0x0f,0x01,0xee" : "=a"(eax), "=d"(edx) : "c"(ecx));
  (void)edx;
  return eax;
}
static inline void wr_pkru(uint32_t v) {
  __asm__ volatile(".byte 0x0f,0x01,0xef" ::"a"(v), "c"(0), "d"(0));
}
#endif

static uint32_t g_saved_pkru = 0;
static int g_have_pkru = 0;

static void open_all_pkeys() {
#if defined(__x86_64__)
  if (g_pkey_open_done) return;
  g_pkey_open_done = 1;
  // Only touch PKRU if the CPU/kernel actually has it; probing via a read is
  // safe only when OSPKE is set, so gate on CPUID.7.0:ECX.PKU/OSPKE.
  uint32_t a, b, c, d;
  __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
  if (!(c & (1u << 4))) return;  // OSPKE not enabled -> no PKRU register
  g_have_pkru = 1;
  g_saved_pkru = rd_pkru();
  // Do NOT open the keys globally: V8's RwxMemoryWriteScope owns PKRU and we
  // must leave its value alone outside our own writes.  patch_entry opens and
  // restores the window around each write instead.
#endif
}

// ------------------------------------------- file-backed mapping table -----
// Code objects reported by kJitCodeEventEnumExisting that live inside the node
// binary (V8's embedded builtin blob) are in a read-only file mapping; they are
// AOT code and belong to the E9Patch substrate, not to this in-process patcher.
struct Range { uint64_t lo, hi; };
static Range g_file_ranges[4096];
static int g_nfile = 0;

static void load_file_ranges() {
  g_nfile = 0;
  FILE *f = fopen("/proc/self/maps", "r");
  if (!f) return;
  char line[1024];
  while (fgets(line, sizeof(line), f) && g_nfile < 4096) {
    uint64_t lo, hi; char perm[8]; char rest[512];
    rest[0] = 0;
    if (sscanf(line, "%lx-%lx %7s %*s %*s %*s %511[^\n]", &lo, &hi, perm, rest) < 3)
      continue;
    // Any mapping backed by a path (or a special region) is not JIT heap.
    const char *p = rest;
    while (*p == ' ') p++;
    if (*p == 0) continue;               // anonymous -> candidate JIT memory
    g_file_ranges[g_nfile].lo = lo;
    g_file_ranges[g_nfile].hi = hi;
    g_nfile++;
  }
  fclose(f);
}

static inline int in_file_mapping(uint64_t a) {
  for (int i = 0; i < g_nfile; i++)
    if (a >= g_file_ranges[i].lo && a < g_file_ranges[i].hi) return 1;
  return 0;
}

// ---------------------------------------------------- per-object counters ---
static uint64_t *g_ctrs = nullptr;
static const size_t CTR_MAX = 1 << 20;
struct PatchRec {
  uint64_t addr, tramp;
  uint32_t code_len;
  uint16_t dlen;
  uint16_t name_len;
  char name[96];
  // --- diagnostics ---
  uint64_t obj_addr;        // the code object this window belongs to
  uint32_t obj_len;
  uint32_t obj_idx;
  uint32_t off;             // window offset inside the object
  uint64_t tsc;             // when the patch was installed
  uint8_t  orig[32];        // the window's bytes BEFORE we patched
  uint8_t  want[32];        // the window's bytes AFTER we patched (jmp rel32 + nops)
  uint8_t  wlen;
  uint8_t  dead;            // 1 = retired (object superseded), do not verify
};
static PatchRec *g_prec = nullptr;
static uint64_t g_nprec = 0;

// V8 never issues CODE_REMOVED ("removal events are not currently issued",
// v8-callbacks.h), so a dead code object's address can be reused by a new one.
// Count how often a CODE_ADDED overlaps a range we already know about.
static uint64_t g_readd = 0, g_readd_patched = 0;
static uint64_t g_seen_lo[1 << 16], g_seen_hi[1 << 16];
static uint32_t g_nseen = 0;

static void note_range(uint64_t lo, uint64_t hi, int was_patched) {
  for (uint32_t i = 0; i < g_nseen; i++) {
    if (lo < g_seen_hi[i] && hi > g_seen_lo[i]) {
      g_readd++;
      if (was_patched) g_readd_patched++;
      g_seen_lo[i] = lo; g_seen_hi[i] = hi;
      return;
    }
  }
  if (g_nseen < (1 << 16)) { g_seen_lo[g_nseen] = lo; g_seen_hi[g_nseen] = hi; g_nseen++; }
}

// -------------------------------------------------------- x86 patching ------
static ZydisDecoder g_dec;
static int g_dec_ready = 0;

// The PoC's own site selection, trampoline emitter and entry patcher lived here; they
// are superseded by jitpatch.h (all three spec v2 site kinds, recursive-descent decoding,
// inline-data detection).  `eval/jit-poc/jithook.cc` keeps the original as the record.
static int g_count_sink = 0;
static int g_scan_site = 0;
static int g_sparkplug = 0;

// ======================================================================
//  Full critical-value-set instrumentation of JIT code (PTracer v2, D9)
//  analyzer service client + content-hash cache + all-site patching
// ======================================================================
static PtjClient g_cli;
static PtjPatchCtx g_cx;
static int g_have_cli = 0;
static uint64_t g_pause_ns = 0;        // total time spent in the handler (mode >= 3)
static uint64_t g_hash_ns = 0;
static uint64_t g_patch_ns = 0;
static uint64_t g_objs_analyzed = 0, g_objs_patched = 0;
static uint64_t g_sites_req = 0, g_sites_patched = 0;
static uint32_t g_space = 3;
typedef PtjObjRec ObjRec;      // jitdump.h
static ObjRec *g_objs = nullptr;

// ---- live objects we have patched, so a CODE_MOVED can be repaired -----------
// V8's mark-compact GC *evacuates* InstructionStream objects: the code bytes are
// memcpy'd to a new page and our position-dependent `jmp rel32` travels with the
// copy, where it now points `delta` bytes past the trampoline it was written for.
// Executing it would fault.  V8 does report the move
// (JitCodeEvent::CODE_MOVED), so we un-patch the moved copy and re-install the
// trampolines against the new base.
struct LiveObj {
  uint64_t addr; uint32_t len;
  PtjSite *sites; int nsites;      // the resolved site list (owned)
  uint32_t *rroots; int nrroots;   // The analyzer's restart roots (owned) -- the
                                   // re-install after a move must decode the same boundaries
  uint64_t first_prec; uint32_t n_prec;
  uint8_t dead;
};
static LiveObj *g_live = nullptr;
static uint32_t g_nlive = 0;
static const uint32_t LIVECAP = 1u << 20;
static uint64_t g_moved_repaired = 0, g_moved_windows = 0, g_moved_unknown = 0;
static uint64_t g_live_superseded = 0;
static uint64_t g_sites_repatched = 0, g_objs_repatched = 0;
static const uint32_t OBJCAP = 1u << 20;
static void obj_note(uint64_t a, uint32_t l, uint64_t tsc, uint32_t ns, uint32_t np) {
  if (g_objs && g_cx.obj < OBJCAP) {
    ObjRec *o = &g_objs[g_cx.obj];
    o->addr = a; o->len = l; o->tsc = tsc; o->nsites = ns; o->npatched = np;
  }
}

#define PTJ_MAXSITES 4096
#define PTJ_MAXROOTS 1024
static PtjSite g_sitebuf[PTJ_MAXSITES + 1]; // one optional post-cache stack-anchor site
// The analyzer's `restart_roots' for the object being patched.
static uint32_t g_rrootbuf[PTJ_MAXROOTS];
static uint64_t g_restart_roots = 0, g_restart_objs = 0;

// ---- in-memory cache: masked content hash -> site list ----
// The reply's `restart_roots' are part of the answer -- without them the patcher's own
// sweep does not reach the boundaries the sites sit on -- so they are cached with the sites.
struct CacheEnt { char key[65]; PtjSite *sites; int n; uint32_t *rroots; int nrroots; };
static CacheEnt *g_mem = nullptr;
static const uint32_t MEMCAP = 1u << 16;
static uint32_t g_memn = 0;
static uint32_t key_hash(const char *k) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 64; i++) { h ^= (uint8_t)k[i]; h *= 16777619u; }
  return h;
}
static CacheEnt *mem_find(const char *k, int insert) {
  if (!g_mem) return nullptr;
  uint32_t i = key_hash(k) & (MEMCAP - 1);
  for (uint32_t n = 0; n < MEMCAP; n++) {
    CacheEnt *c = &g_mem[(i + n) & (MEMCAP - 1)];
    if (!c->key[0]) { if (!insert) return nullptr; memcpy(c->key, k, 65); g_memn++; return c; }
    if (!memcmp(c->key, k, 64)) return c;
  }
  return nullptr;
}

// ---- cache key: the code bytes with every relocatable field masked ----
static uint8_t *g_maskbuf = nullptr; static size_t g_maskcap = 0;
static void masked_hash(const uint8_t *code, size_t len, char out[65], uint32_t data_from) {
  if (len > g_maskcap) { g_maskbuf = (uint8_t *)realloc(g_maskbuf, len * 2); g_maskcap = len * 2; }
  if (!g_maskbuf) { out[0] = 0; return; }
  memcpy(g_maskbuf, code, len);
  size_t off = 0;
  while (off < len) {
    ZydisDecodedInstruction ins;
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&g_dec, code + off, len - off, &ins, ops,
                                             ZYDIS_MAX_OPERAND_COUNT, 0))) break;
    for (int k = 0; k < 2; k++)
      if (ins.raw.imm[k].size >= 32)
        memset(g_maskbuf + off + ins.raw.imm[k].offset, 0, ins.raw.imm[k].size / 8);
    if (ins.raw.disp.size >= 32)
      memset(g_maskbuf + off + ins.raw.disp.offset, 0, ins.raw.disp.size / 8);
    off += ins.length;
  }
  PtjSha sh; ptj_sha_init(&sh);
  uint64_t l = len; ptj_sha_update(&sh, &l, 8);
  ptj_sha_update(&sh, g_maskbuf, len);
  // `data_from' is derived from a rip-relative disp32, which the mask above zeroes: two code
  // objects can mask to the same bytes and still have their inline data start elsewhere, and
  // the analyzer's answer depends on it (it clips the restart fill).  So it is part of the key.
  ptj_sha_update(&sh, &data_from, 4);
  ptj_sha_hex(&sh, out);
}
// The same key and the same data_from from ONE linear sweep without operand decoding
// (the masked fields are raw.imm/raw.disp, which ZydisDecoderDecodeInstruction fills);
// every decode is kept in `sw' (when non-NULL) for the patcher.  Returns data_from.
static uint64_t g_keychk_objs = 0, g_keychk_bad = 0;
static uint32_t masked_hash_fast(const uint8_t *code, size_t len, char out[65], PtjSweep *sw) {
  if (len > g_maskcap) { g_maskbuf = (uint8_t *)realloc(g_maskbuf, len * 2); g_maskcap = len * 2; }
  if (sw && !ptj_sweep_begin(sw, code, len)) sw = nullptr;
  if (!g_maskbuf) { out[0] = 0; return ptj_scan_fused(&g_dec, code, len, nullptr, sw); }
  memcpy(g_maskbuf, code, len);
  uint32_t data_from = ptj_scan_fused(&g_dec, code, len, g_maskbuf, sw);
  PtjSha sh; ptj_sha_init(&sh);
  uint64_t l = len; ptj_sha_update(&sh, &l, 8);
  ptj_sha_update(&sh, g_maskbuf, len);
  ptj_sha_update(&sh, &data_from, 4);
  ptj_sha_hex(&sh, out);
  if (g_ptj_deccheck) {                   // PTJIT_DECCHECK=1: the full-decode key, compared
    uint32_t df2 = ptj_scan_data_from(&g_dec, code, len);
    char k2[65]; masked_hash(code, len, k2, df2);
    g_keychk_objs++;
    if (df2 != data_from || strcmp(k2, out)) {
      g_keychk_bad++;
      fprintf(stderr, "PTJIT DECCHECK key MISMATCH addr=%p len=%zu df=%u/%u\n", (void *)code, len, data_from, df2);
    }
  }
  return data_from;
}
static int g_fastdec = 2;                 // PTJIT_FASTDEC; 0 = five full decodes

// ---- the PKRU window used while writing to V8's code space ----
static void wr_open(void) {
#if defined(__x86_64__)
  if (g_have_pkru) { g_saved_pkru = rd_pkru(); if (g_saved_pkru) wr_pkru(0); }
#endif
}
static void wr_close(void) {
#if defined(__x86_64__)
  if (g_have_pkru && g_saved_pkru) wr_pkru(g_saved_pkru);
#endif
}

// ---- jitdump records for a patch (evt 3 = patched code bytes, 4 = trampoline) ----
static void hexdump(FILE *f, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) fprintf(f, "%02x", p[i]);
}
static void note_tramp(PtjPatchCtx *cx, uint8_t *code, size_t clen, uint8_t *t, size_t tlen) {
  if (g_prec && g_nprec < CTR_MAX) {
    PatchRec *r = &g_prec[g_nprec];
    r->addr = (uint64_t)code; r->tramp = (uint64_t)t;
    r->code_len = (uint32_t)clen; r->dlen = (uint16_t)clen; r->name[0] = 0; r->name_len = 0;
    r->obj_addr = cx->last_obj_addr; r->obj_len = cx->last_obj_len;
    r->obj_idx = cx->obj; r->off = cx->last_off; r->tsc = cx->tsc; r->dead = 0;
    r->wlen = (uint8_t)(clen > 32 ? 32 : clen);
    memcpy(r->orig, cx->last_orig, r->wlen);
    memcpy(r->want, code, r->wlen);
    g_nprec++;
  }
  g_c.patched++;
  if (g_verbose >= 2) {
    fprintf(stderr, "PTJITTRAMP obj=%u at=%p dlen=%zu tramp=%p tlen=%zu bytes=",
            cx->obj, (void *)code, clen, (void *)t, tlen);
    hexdump(stderr, t, tlen);
    fprintf(stderr, "\n");
  }
  if (g_mode >= MODE_DUMP) {
    arena_record(3, 1, code, nullptr, clen, nullptr, 0, 1);
    arena_record(4, 1, t, (void *)code, tlen, nullptr, 0, 1);
  }
}

// V8 reuses the address of a dead code object (it never issues CODE_REMOVED), so a
// LiveObj that overlaps a range we are about to patch again describes memory that no
// longer exists.  Retire it, or a later CODE_MOVED of the *new* object would restore
// the *old* object's window bytes into the moved copy.
static void live_supersede(uint64_t lo, uint64_t hi) {
  if (!g_live) return;
  for (uint32_t i = 0; i < g_nlive; i++) {
    LiveObj *L = &g_live[i];
    if (L->dead) continue;
    if (lo < L->addr + L->len && hi > L->addr) {
      L->dead = 1;
      for (uint64_t k = L->first_prec; k < L->first_prec + L->n_prec && g_prec; k++)
        g_prec[k].dead = 1;
      g_live_superseded++;
    }
  }
}

// Remember a patched object so a later CODE_MOVED can be repaired.
static void live_note(uint64_t addr, uint32_t len, PtjSite *sites, int n, uint64_t first,
                      const uint32_t *rroots = nullptr, int nrr = 0) {
  if (!g_live || g_nlive >= LIVECAP) return;
  if (g_nprec == first) return;                 // nothing was patched: nothing to repair
  LiveObj *L = &g_live[g_nlive++];
  L->addr = addr; L->len = len;
  L->first_prec = first; L->n_prec = (uint32_t)(g_nprec - first); L->dead = 0;
  L->nsites = n;
  L->sites = (PtjSite *)malloc(sizeof(PtjSite) * (n > 0 ? n : 1));
  if (L->sites && n > 0) memcpy(L->sites, sites, sizeof(PtjSite) * n);
  else L->nsites = 0;
  L->rroots = nullptr; L->nrroots = 0;
  if (rroots && nrr > 0) {
    L->rroots = (uint32_t *)malloc(sizeof(uint32_t) * nrr);
    if (L->rroots) { memcpy(L->rroots, rroots, sizeof(uint32_t) * nrr); L->nrroots = nrr; }
  }
}

// Re-install every window of an object V8 has just evacuated.  Called from the
// JitCodeEvent handler on CODE_MOVED, i.e. inside the GC pause: the copy at
// `to` is complete and nothing is executing it yet.
static void install_at(uint8_t *code, size_t len, PtjSite *sites, int n,
                       const uint32_t *rroots, int nrr);

static void on_moved(uint8_t *from, uint8_t *to, size_t len) {
  if (!g_live || from == to) return;
  int hit = 0;
  uint32_t nlive0 = g_nlive;              // install_at appends; do not rescan what it adds
  for (uint32_t i = 0; i < nlive0; i++) {
    LiveObj *L = &g_live[i];
    if (L->dead) continue;
    if (L->addr + L->len <= (uint64_t)from || L->addr >= (uint64_t)from + len) continue;
    int64_t shift = (int64_t)L->addr - (int64_t)(uintptr_t)from;   // 0 in the usual case
    // 1. restore our windows in the *moved copy* (they carry a stale rel32)
    if (wr_open) wr_open();
    for (uint64_t k = L->first_prec; k < L->first_prec + L->n_prec && g_prec; k++) {
      PatchRec *r = &g_prec[k];
      if (!r->wlen || (int64_t)r->off + shift < 0 ||
          (size_t)((int64_t)r->off + shift) + r->wlen > len) continue;
      uint8_t *dst = to + (int64_t)r->off + shift;
      if (!memcmp(dst, r->want, r->wlen)) {        // the copy still holds our detour
        memcpy(dst, r->orig, r->wlen);
        g_moved_windows++;
      }
      r->dead = 1;
    }
    if (wr_close) wr_close();
    L->dead = 1;
    // 2. re-install against the new base (no analyzer call: the sites are cached)
    if (L->sites && L->nsites > 0 && shift == 0) install_at(to, len, L->sites, L->nsites, L->rroots, L->nrroots);
    g_moved_repaired++; hit = 1;
    if (g_diag > 1) fprintf(stderr, "PTJITMOVE ok from=0x%llx to=0x%llx len=%zu windows=%u shift=%lld\n",
                      (unsigned long long)(uintptr_t)from, (unsigned long long)(uintptr_t)to, len,
                      L->n_prec, (long long)shift);
  }
  if (hit) return;
  g_moved_unknown++;
  if (g_diag) fprintf(stderr, "PTJITMOVE UNMATCHED from=0x%llx to=0x%llx len=%zu\n",
                      (unsigned long long)(uintptr_t)from, (unsigned long long)(uintptr_t)to, len);
}

static void plan_install(uint8_t *code, size_t len, const char *name, size_t name_len,
                         PtjSite *sites, int n, uint32_t *rroots, int nrr);
static int async_enqueue(uint8_t *code, size_t len, const char *key, uint32_t data_from,
                         const char *name, size_t name_len);
static int g_async = 0;                    // PTJIT_ASYNC: analyse cache misses off the JS thread
static pthread_mutex_t g_memlock = PTHREAD_MUTEX_INITIALIZER;   // g_mem: JS thread + analysis worker
static uint64_t g_js_analysis_ns = 0, g_js_analysis_calls = 0;  // analyzer time ON the JS thread

// Analyze (or recall) one code object and patch every site it yields.
static void full_patch_body(uint8_t *code, size_t len, const char *name, size_t name_len);
static void full_patch(uint8_t *code, size_t len, const char *name, size_t name_len) {
  full_patch_body(code, len, name, name_len);
  g_cx.data_from_hint = 0; g_ptjsweep.valid = 0;   // the sweep/hint never outlive this object
}
static void full_patch_body(uint8_t *code, size_t len, const char *name, size_t name_len) {
  if (!len || (!g_pinbridge && len < 5)) { g_cx.drops[PTJ_DROP_WINDOW_TRUNCATED]++; return; }
  // Pin does not need a five-byte detour or writable application text. Include
  // V8's file-backed builtins as well as dynamically generated code in this mode.
  if (!g_pinbridge && in_file_mapping((uint64_t)code)) { g_c.patch_skipped_ro++; return; }
  uint64_t att = g_attempt++;
  if (att < g_min_patch || att >= g_max_patch) { g_c.patch_skipped_cap++; return; }
  if (!g_pinbridge) {
    if (!g_tslab) tslab_init_near((uint64_t)code, g_tslab_want);
    if (!g_tslab) { g_c.patch_skipped_cap++; return; }
    // The keyframe counters and the ground-truth cursor are carved out of the FRONT of the
    // slab, so `dec CNT(%rip)' and `lock xadd %rcx,CUR(%rip)' reach them with a rel32.
    if (!g_cx.gt_cur) {
      uint32_t nctr = g_keyframe ? g_kfctr_max : 0;
      size_t db = ptj_slab_data_bytes(nctr);
      if (db + TSLAB_CHUNK < g_tslab_cap) {
        tslab_commit(db + TSLAB_CHUNK);
        if (g_tslab_used < db) g_tslab_used = db;
        ptj_slab_data_init(&g_cx, g_tslab, nctr);
      }
    }
    if (g_cx.gt) ptj_gt_arm(&g_gt, g_cx.gt_cur);
    g_cx.slab = g_tslab; g_cx.slab_cap = g_tslab_cap; g_cx.slab_used = &g_tslab_used;
    live_supersede((uint64_t)code, (uint64_t)code + len);
  }

  char key[65];
  uint64_t h0 = ptj_ns();
  // The analyzer needs the same inline-data boundary the patcher uses, and it is part
  // of the cache key (see masked_hash).
  uint32_t data_from;
  if (g_fastdec) {                        // one no-operand sweep, kept for the patcher
    data_from = masked_hash_fast(code, len, key, g_pinbridge ? nullptr : &g_ptjsweep);
    g_cx.data_from_hint = data_from;
  } else {
    data_from = ptj_scan_data_from(&g_dec, code, len);
    masked_hash(code, len, key, data_from);
  }
  g_hash_ns += ptj_ns() - h0;

  if (g_hashdump && key[0])
    fprintf(g_hashdump, "%s %llu %zu %.*s\n", key, (unsigned long long)(uintptr_t)code, len,
            (int)(name_len > 90 ? 90 : name_len), name ? name : "");
  PtjSite *sites = g_sitebuf; int n = -1;
  uint32_t *rroots = g_rrootbuf; int nrr = 0;
  pthread_mutex_lock(&g_memlock);
  CacheEnt *ce = key[0] ? mem_find(key, 0) : nullptr;
  if (ce && !ce->sites) ce = nullptr;          // slot reserved, never filled
  if (ce) { sites = ce->sites; n = ce->n; rroots = ce->rroots; nrr = ce->nrroots; g_cli.mem_hits++; }
  pthread_mutex_unlock(&g_memlock);
  if (!ce) {
    if (key[0]) n = ptj_cache_load(&g_cli, key, g_sitebuf, PTJ_MAXSITES, g_rrootbuf, PTJ_MAXROOTS, &nrr);
    // A miss is analysed on the worker; the object runs unpatched until its plan lands
    // (installed on this thread from a V8 interrupt, after a staleness check).
    if (n < 0 && g_have_cli && g_async && key[0]) {   // also under the Pin bridge (PTJIT_PIN_ASYNC=1)
      async_enqueue(code, len, key, data_from, name, name_len);
      return;
    }
    if (n < 0 && g_have_cli) {
      char nm[128]; size_t nl = name_len > 120 ? 120 : name_len;
      if (name && nl) memcpy(nm, name, nl); nl = name ? nl : 0; nm[nl] = 0;
      uint64_t a0 = ptj_ns();
      n = ptj_analyze(&g_cli, code, len, (uint64_t)code, nm, g_sitebuf, PTJ_MAXSITES,
                      nullptr, 0, nullptr, 0, data_from, g_rrootbuf, PTJ_MAXROOTS, &nrr);
      g_js_analysis_ns += ptj_ns() - a0; g_js_analysis_calls++;
      if (n >= 0 && key[0]) ptj_cache_store(&g_cli, key, g_sitebuf, n, g_rrootbuf, nrr);
    }
    if (n >= 0 && key[0]) {
      pthread_mutex_lock(&g_memlock);
      CacheEnt *c2 = mem_find(key, 1);
      if (c2 && !c2->sites) {
        c2->sites = (PtjSite *)malloc(sizeof(PtjSite) * (n ? n : 1));
        if (c2->sites) { memcpy(c2->sites, g_sitebuf, sizeof(PtjSite) * n); c2->n = n; }
        c2->rroots = (uint32_t *)malloc(sizeof(uint32_t) * (nrr ? nrr : 1));
        if (c2->rroots) { memcpy(c2->rroots, g_rrootbuf, sizeof(uint32_t) * nrr); c2->nrroots = nrr; }
      }
      pthread_mutex_unlock(&g_memlock);
    }
  }
  if (n < 0) {
    if (g_pinbridge) { fprintf(stderr, "PTJIT: Pin plan analysis failed\n"); _exit(71); }
    return;
  }
  plan_install(code, len, name, name_len, sites, n, rroots, nrr);
}

// The install half of full_patch: a site list (from the cache, a synchronous analysis or an
// asynchronous job) is placed on the live object.  JS thread only.
static void plan_install(uint8_t *code, size_t len, const char *name, size_t name_len,
                         PtjSite *sites, int n, uint32_t *rroots, int nrr) {
  g_restart_roots += (uint64_t)nrr;
  if (nrr) g_restart_objs++;
  g_objs_analyzed++;
  if (sites != g_sitebuf) { memcpy(g_sitebuf, sites, sizeof(PtjSite) * n); }
  if (rroots != g_rrootbuf && nrr) memcpy(g_rrootbuf, rroots, sizeof(uint32_t) * nrr);
  if (g_stack_anchor) {
    int added = ptj_stack_anchor(g_sitebuf, &n, PTJ_MAXSITES + 1, g_stack_anchor);
    if (added < 0) { fprintf(stderr, "PTJIT: cannot preserve plan and add stack anchor\n"); _exit(71); }
    if (added) { g_stack_anchor_objects++; g_stack_anchor_values += (uint64_t)added; }
  }
  g_sites_req += (uint64_t)n;
  if (g_pinbridge) {
    pin_publish(PT_PIN_ADD, code, len, g_sitebuf, n);
    g_pin_objects++; g_pin_sites += (uint64_t)n;
    return; // Pin inserts the logging code; no application bytes are rewritten.
  }
  // Seed the patcher's recursive descent with the analyzer's restart roots, so both
  // sides decode the same instruction boundaries.  They are also treated as forbidden window
  // interiors (`fbd'): a restart root is a place the descent could NOT reach, i.e. a plausible
  // indirect entry (OSR / deopt / exception handler), and a window straddling one would put a
  // thread entering there in the middle of our `jmp rel32'.
  g_cx.roots = (nrr && !g_noroots) ? g_rrootbuf : nullptr;
  g_cx.nroots = (uint32_t)(g_noroots ? 0 : nrr);
  PtjObjStat ost; memset(&ost, 0, sizeof(ost));
  g_cx.tsc = rdtscp_now();
  if (g_verbose >= 2) {
    fprintf(stderr, "PTJITOBJ %u addr=%p len=%zu sites=%d name=%.*s code=",
            g_cx.obj, (void *)code, len, n, (int)(name_len > 60 ? 60 : name_len), name ? name : "");
    hexdump(stderr, code, len);
    fprintf(stderr, "\n");
    // Diagnosis: the restart roots this object was patched with, as OFFSETS, so a
    // crash dump can be replayed against `static/cfgrec.py' without guessing the base.
    fprintf(stderr, "PTJITROOTS %u addr=%p len=%zu noroots=%d nrr=%d", g_cx.obj,
            (void *)code, len, g_noroots, g_noroots ? 0 : nrr);
    for (int i = 0; !g_noroots && i < nrr; i++) fprintf(stderr, " 0x%x", g_rrootbuf[i]);
    fprintf(stderr, "\n");
  }
  uint64_t p0 = ptj_ns();
  uint64_t first = g_nprec;
  tslab_commit(g_tslab_used + (1u << 20));
  ptj_patch_object(&g_cx, code, len, g_sitebuf, n, &ost, wr_open, wr_close, note_tramp);
  g_patch_ns += ptj_ns() - p0;
  g_sites_patched += ost.patched;
  if (ost.patched) g_objs_patched++;
  obj_note((uint64_t)code, (uint32_t)len, g_cx.tsc, (uint32_t)n, ost.patched);
  live_note((uint64_t)code, (uint32_t)len, g_sitebuf, n, first, g_rrootbuf, nrr);
  g_cx.obj++;
}

// ======================================================================
//  Asynchronous analysis (PTJIT_ASYNC=1)
//  The Node counterpart of the Java agent's queue.  A code object whose masked hash is in
//  neither cache is SNAPSHOTTED in the JitCodeEvent handler and queued; one worker thread (the
//  only user of the analyzer socket) analyses it and stores the plan in both caches, then asks
//  V8 for an interrupt.  The plan is installed on the JS thread from that interrupt callback
//  (or from drain()): the JS thread is then inside a stack-check call, so every JIT frame on
//  its stack is stopped at a call's return address, and no window ever contains a call, so no
//  thread is inside a window being written (V8 runs JS code on this one thread only;
//  `off_thread' == 0).  The object runs unpatched until then -- exactly the Java behaviour.
//  Staleness: V8 never reports CODE_REMOVED and reuses addresses, so a job is dropped when a
//  later CODE_ADDED overlaps it, follows a CODE_MOVED, and before install the LIVE bytes must be
//  mapped and hash (masked, = Java's stalemask) to the job's key; otherwise it is dropped.
// ======================================================================
static int safe_read(const void *src, void *dst, size_t n);
struct AJob {
  uint64_t addr; uint32_t len; uint32_t data_from;
  char key[65]; char name[128];
  uint8_t *snap;
  int state;                 // 0 queued, 1 analysing, 2 ready, 3 failed/skipped
  int stale;                 // 1 overlap, 2 partial move (JS thread sets, under g_qlock)
  int n, nrr; PtjSite *sites; uint32_t *rroots;
  uint64_t t_enq, t_ready;
  AJob *qnext;
};
static pthread_mutex_t g_qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_qcv = PTHREAD_COND_INITIALIZER, g_qdone = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_irqlock = PTHREAD_MUTEX_INITIALIZER;  // g_qstop + RequestInterrupt
static AJob *g_qhead = nullptr, *g_qtail = nullptr;
static AJob **g_pend = nullptr; static uint32_t g_npend = 0, g_pendcap = 0;   // every job not yet retired
static int g_qstop = 0, g_worker_up = 0, g_irq_req = 0, g_qbusy = 0;
static v8::Isolate *g_iso = nullptr;
// PTJIT_WORKERS=N (default 4; 1 if the socket is not `...sock.0'; fewer if a service is missing): N analysis workers, worker i talking to its own analyzer service
// (PTJIT_SOCK's trailing `.0' replaced by `.i', the socket naming of jitwp.py/repslice.py).
// Worker 0 uses g_cli itself so the existing analysis_* counters keep their meaning.
static const int AJ_MAXW = 8;
static int g_nworkers = 1;
static PtjClient *g_wcli[AJ_MAXW];
static pthread_t g_worker[AJ_MAXW];
static struct {
  uint64_t queued, installed, install_same, stale_overlap, stale_moved, stale_hash, stale_unmapped, failed,
           moved, worker_cache_hits, irq_calls, drain_calls, install_ns, irq_ns,
           lat_sum_ns, lat_max_ns, pend_max, worker_analysis_ns, worker_calls;
} g_aj;

static void irq_cb(v8::Isolate *, void *);
static void *aworker(void *arg) {
  PtjClient *cli = g_wcli[(intptr_t)arg];
  PtjSite *sb = (PtjSite *)malloc(sizeof(PtjSite) * (PTJ_MAXSITES + 1));
  uint32_t *rb = (uint32_t *)malloc(sizeof(uint32_t) * PTJ_MAXROOTS);
  pthread_mutex_lock(&g_qlock);
  for (;;) {
    while (!g_qhead && !g_qstop) pthread_cond_wait(&g_qcv, &g_qlock);
    if (g_qstop) break;
    AJob *j = g_qhead; g_qhead = j->qnext; if (!g_qhead) g_qtail = nullptr;
    j->state = 1; g_qbusy++;
    int stale = j->stale;
    pthread_mutex_unlock(&g_qlock);
    int n = -1, nrr = 0;
    if (!stale && sb && rb) {
      pthread_mutex_lock(&g_memlock);                  // an earlier job with the same bytes
      CacheEnt *ce = mem_find(j->key, 0);
      if (ce && ce->sites) {
        n = ce->n; memcpy(sb, ce->sites, sizeof(PtjSite) * n);
        nrr = ce->nrroots; if (nrr) memcpy(rb, ce->rroots, sizeof(uint32_t) * nrr);
      }
      pthread_mutex_unlock(&g_memlock);
      if (n >= 0) __atomic_fetch_add(&g_aj.worker_cache_hits, 1, __ATOMIC_RELAXED);
      else {
        uint64_t a0 = ptj_ns();
        n = ptj_analyze(cli, j->snap, j->len, j->addr, j->name, sb, PTJ_MAXSITES,
                        nullptr, 0, nullptr, 0, j->data_from, rb, PTJ_MAXROOTS, &nrr);
        __atomic_fetch_add(&g_aj.worker_analysis_ns, ptj_ns() - a0, __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_aj.worker_calls, 1, __ATOMIC_RELAXED);
        if (n >= 0) {
          ptj_cache_store(cli, j->key, sb, n, rb, nrr);
          pthread_mutex_lock(&g_memlock);
          CacheEnt *c2 = mem_find(j->key, 1);
          if (c2 && !c2->sites) {
            PtjSite *cs = (PtjSite *)malloc(sizeof(PtjSite) * (n ? n : 1));
            uint32_t *cr = (uint32_t *)malloc(sizeof(uint32_t) * (nrr ? nrr : 1));
            if (cs && cr) {
              memcpy(cs, sb, sizeof(PtjSite) * n); if (nrr) memcpy(cr, rb, sizeof(uint32_t) * nrr);
              c2->n = n; c2->rroots = cr; c2->nrroots = nrr; c2->sites = cs;
            } else { free(cs); free(cr); }
          }
          pthread_mutex_unlock(&g_memlock);
        }
      }
    }
    PtjSite *js = nullptr; uint32_t *jr = nullptr;
    if (n >= 0) {
      js = (PtjSite *)malloc(sizeof(PtjSite) * (n + 1));
      jr = (uint32_t *)malloc(sizeof(uint32_t) * (nrr ? nrr : 1));
      if (js && jr) { memcpy(js, sb, sizeof(PtjSite) * n); if (nrr) memcpy(jr, rb, sizeof(uint32_t) * nrr); }
      else { free(js); free(jr); js = nullptr; jr = nullptr; n = -1; }
    }
    pthread_mutex_lock(&g_qlock);
    j->sites = js; j->rroots = jr; j->n = n; j->nrr = nrr;
    // The snapshot is kept until retirement: install_job compares the live bytes with it
    j->state = n >= 0 ? 2 : 3; j->t_ready = ptj_ns(); g_qbusy--;
    pthread_cond_broadcast(&g_qdone);
    int want_irq = !g_irq_req; g_irq_req = 1;
    pthread_mutex_unlock(&g_qlock);
    if (want_irq) {
      pthread_mutex_lock(&g_irqlock);        // not while holding g_qlock (V8 takes its own lock)
      if (!g_qstop && g_iso) g_iso->RequestInterrupt(irq_cb, nullptr);
      pthread_mutex_unlock(&g_irqlock);
    }
    pthread_mutex_lock(&g_qlock);
  }
  pthread_mutex_unlock(&g_qlock);
  return nullptr;
}

static int async_enqueue(uint8_t *code, size_t len, const char *key, uint32_t data_from,
                         const char *name, size_t name_len) {
  AJob *j = (AJob *)calloc(1, sizeof(AJob));
  if (!j) return -1;
  j->snap = (uint8_t *)malloc(len);
  if (!j->snap) { free(j); return -1; }
  memcpy(j->snap, code, len);
  j->addr = (uint64_t)code; j->len = (uint32_t)len; j->data_from = data_from;
  memcpy(j->key, key, 65);
  size_t nl = name_len > 120 ? 120 : name_len; if (name && nl) memcpy(j->name, name, nl);
  j->name[name ? nl : 0] = 0;
  j->t_enq = ptj_ns();
  pthread_mutex_lock(&g_qlock);
  if (g_npend == g_pendcap) {
    uint32_t nc = g_pendcap ? g_pendcap * 2 : 1024;
    AJob **np = (AJob **)realloc(g_pend, sizeof(AJob *) * nc);
    if (!np) { pthread_mutex_unlock(&g_qlock); free(j->snap); free(j); return -1; }
    g_pend = np; g_pendcap = nc;
  }
  g_pend[g_npend++] = j;
  if (g_npend > g_aj.pend_max) g_aj.pend_max = g_npend;
  if (g_qtail) g_qtail->qnext = j; else g_qhead = j;
  g_qtail = j;
  g_aj.queued++;
  pthread_cond_signal(&g_qcv);
  pthread_mutex_unlock(&g_qlock);
  return 0;
}

// JS thread, from the JitCodeEvent handler: a new object at [lo,hi) retires every pending job
// it overlaps (V8 reused the address of a dead object).
static void async_note_added(uint64_t lo, uint64_t hi) {
  if (!g_npend) return;
  pthread_mutex_lock(&g_qlock);
  for (uint32_t i = 0; i < g_npend; i++) {
    AJob *j = g_pend[i];
    if (!j->stale && lo < j->addr + j->len && hi > j->addr) j->stale = 1;
  }
  pthread_mutex_unlock(&g_qlock);
}
// JS thread, CODE_MOVED (inside the GC pause): follow the object.
static void async_note_moved(uint64_t from, uint64_t to, uint64_t len) {
  if (!g_npend || from == to) return;
  pthread_mutex_lock(&g_qlock);
  for (uint32_t i = 0; i < g_npend; i++) {
    AJob *j = g_pend[i];
    if (j->stale || !(from < j->addr + j->len && from + len > j->addr)) continue;
    if (j->addr == from && j->len == len) { j->addr = to; g_aj.moved++; }
    else j->stale = 2;
  }
  pthread_mutex_unlock(&g_qlock);
}

static void install_job(AJob *j) {
  uint8_t *live = (uint8_t *)malloc(j->len);
  if (!live) { g_aj.failed++; return; }
  if (!safe_read((const void *)j->addr, live, j->len)) { g_aj.stale_unmapped++; free(live); return; }
  uint32_t df;
  if (g_fastdec && j->snap && !memcmp(live, j->snap, j->len) && !g_ptj_deccheck) {
    df = j->data_from;                    // byte-identical to the hashed snapshot -> same key
    g_aj.install_same++;
  } else {
    char k[65];
    if (g_fastdec) df = masked_hash_fast(live, j->len, k, nullptr);
    else { df = ptj_scan_data_from(&g_dec, live, j->len); masked_hash(live, j->len, k, df); }
    if (!k[0] || strcmp(k, j->key)) { free(live); g_aj.stale_hash++; return; }
  }
  free(live);
  if (!g_pinbridge) {                    // Pin needs no slab; plan_install publishes to Pin
    if (!g_tslab) return;
    live_supersede(j->addr, j->addr + j->len);
  }
  uint64_t lat = ptj_ns() - j->t_enq;
  g_aj.lat_sum_ns += lat; if (lat > g_aj.lat_max_ns) g_aj.lat_max_ns = lat;
  memcpy(g_sitebuf, j->sites, sizeof(PtjSite) * j->n);
  if (j->nrr) memcpy(g_rrootbuf, j->rroots, sizeof(uint32_t) * j->nrr);
  if (g_fastdec) g_cx.data_from_hint = df;   // computed on these bytes
  plan_install((uint8_t *)j->addr, j->len, j->name, strlen(j->name), g_sitebuf, j->n,
               g_rrootbuf, j->nrr);
  g_cx.data_from_hint = 0; g_ptjsweep.valid = 0;
  g_aj.installed++;
}

// JS thread only: retire every job that is no longer queued or being analysed.
static void install_ready(void) {
  if (!g_npend) return;
  uint64_t t0 = ptj_ns();
  AJob *batch[256]; uint32_t nb;
  do {
    nb = 0;
    pthread_mutex_lock(&g_qlock);
    uint32_t w = 0;
    for (uint32_t i = 0; i < g_npend; i++) {
      AJob *j = g_pend[i];
      if (j->state >= 2 && nb < 256) batch[nb++] = j; else g_pend[w++] = j;
    }
    g_npend = w;
    pthread_mutex_unlock(&g_qlock);
    for (uint32_t i = 0; i < nb; i++) {
      AJob *j = batch[i];
      if (j->stale == 1) g_aj.stale_overlap++;
      else if (j->stale == 2) g_aj.stale_moved++;
      else if (j->state == 3) g_aj.failed++;
      else install_job(j);
      free(j->sites); free(j->rroots); free(j->snap); free(j);
    }
  } while (nb == 256);
  g_aj.install_ns += ptj_ns() - t0;
}

static void irq_cb(v8::Isolate *, void *) {
  uint64_t t0 = ptj_ns();
  pthread_mutex_lock(&g_qlock); g_irq_req = 0; pthread_mutex_unlock(&g_qlock);
  g_aj.irq_calls++;
  install_ready();
  g_aj.irq_ns += ptj_ns() - t0;
}

static void async_start(void) {
  if (g_worker_up) return;
  g_iso = v8::Isolate::GetCurrent();
  const char *nw = getenv("PTJIT_WORKERS");
  g_nworkers = nw ? atoi(nw) : 4;         // needs services ...sock.0..3 (jitwp/repslice start them)
  if (g_nworkers < 1) g_nworkers = 1;
  if (g_nworkers > AJ_MAXW) g_nworkers = AJ_MAXW;
  g_wcli[0] = &g_cli;
  size_t sl = strlen(g_cli.sock);
  int numbered = sl > 2 && !strcmp(g_cli.sock + sl - 2, ".0");
  // A self-spawned or un-numbered service is ONE `analyze.py --serve', which serves one client at a
  // time: a second connection would wait forever behind the first, so use one worker.
  if (!numbered) g_nworkers = 1;
  for (int i = 1; i < g_nworkers; i++) {
    PtjClient *c = (PtjClient *)calloc(1, sizeof(PtjClient));
    if (!c) { g_nworkers = i; break; }
    memcpy(c->sock, g_cli.sock, sizeof c->sock); memcpy(c->cache, g_cli.cache, sizeof c->cache);
    memcpy(c->py, g_cli.py, sizeof c->py); memcpy(c->script, g_cli.script, sizeof c->script);
    memcpy(c->ver, g_cli.ver, sizeof c->ver); c->fast = g_cli.fast; c->kf = g_cli.kf;
    if (numbered) snprintf(c->sock + sl - 1, sizeof(c->sock) - (sl - 1), "%d", i);
    c->fd = ptj_connect(c);
    if (c->fd < 0) {
      fprintf(stderr, "PTJIT async: worker %d cannot reach %s -- %d worker(s)\n", i, c->sock, i);
      free(c); g_nworkers = i; break;
    }
    g_wcli[i] = c;
  }
  int up = 0;
  for (int i = 0; i < g_nworkers; i++)
    if (pthread_create(&g_worker[i], nullptr, aworker, (void *)(intptr_t)i) == 0) up++;
  if (up) { g_worker_up = up; g_nworkers = up; }
  else { fprintf(stderr, "PTJIT async: cannot start the analysis worker -- synchronous\n"); g_async = 0; }
}
static void async_stop(void) {
  pthread_mutex_lock(&g_irqlock);
  pthread_mutex_lock(&g_qlock); g_qstop = 1; pthread_cond_broadcast(&g_qcv); pthread_mutex_unlock(&g_qlock);
  pthread_mutex_unlock(&g_irqlock);
}
static void async_counts(uint64_t *queued, uint64_t *busy, uint64_t *ready) {
  uint64_t q = 0, b = 0, r = 0;
  pthread_mutex_lock(&g_qlock);
  for (uint32_t i = 0; i < g_npend; i++) {
    int s = g_pend[i]->state;
    if (s == 0) q++; else if (s == 1) b++; else r++;
  }
  pthread_mutex_unlock(&g_qlock);
  *queued = q; *busy = b; *ready = r;
}

// Entry-only instrumentation through the same emitter (mode 3): one synthetic
// `reg` site at offset 0 logging %rdi (the JSFunction at a V8 JS entry).
static void entry_patch(uint8_t *code, size_t len, const char *name, size_t name_len) {
  (void)name; (void)name_len;
  if (len < 5) { g_c.patch_skipped_small++; return; }
  if (in_file_mapping((uint64_t)code)) { g_c.patch_skipped_ro++; return; }
  uint64_t att = g_attempt++;
  if (att < g_min_patch || att >= g_max_patch) { g_c.patch_skipped_cap++; return; }
  if (!g_tslab) tslab_init_near((uint64_t)code, g_tslab_want);
  if (!g_tslab) { g_c.patch_skipped_cap++; return; }
  g_cx.slab = g_tslab; g_cx.slab_cap = g_tslab_cap; g_cx.slab_used = &g_tslab_used;
  live_supersede((uint64_t)code, (uint64_t)code + len);
  PtjSite s; memset(&s, 0, sizeof(s));
  s.off = 0; s.id = 0; s.when = PTJ_WHEN_BEFORE; s.kind = PTJ_KIND_REG; s.size = 8;
  s.nregs = 1; s.regs[0] = 7;                        // %rdi
  g_cx.roots = nullptr; g_cx.nroots = 0;             // entry-only: no analyzer, no restart roots
  PtjObjStat ost; memset(&ost, 0, sizeof(ost));
  g_cx.tsc = rdtscp_now();
  g_sites_req++;
  uint64_t p0 = ptj_ns();
  uint64_t first = g_nprec;
  tslab_commit(g_tslab_used + (1u << 20));
  ptj_patch_object(&g_cx, code, len, &s, 1, &ost, wr_open, wr_close, note_tramp);
  g_patch_ns += ptj_ns() - p0;
  g_sites_patched += ost.patched;
  if (ost.patched) g_objs_patched++;
  obj_note((uint64_t)code, (uint32_t)len, g_cx.tsc, 1, ost.patched);
  live_note((uint64_t)code, (uint32_t)len, &s, 1, first);
  g_cx.obj++;
}

// PTJIT_WASM_PROBE=1 -- a negative control for the wasm trap-table checks.
// Deliberately displace an instruction V8 registered with its wasm trap handler: one
// synthetic `reg' site at the object's FIRST protected-instruction offset, so the window
// relocates exactly that instruction into the slab.  The next time it faults by design (a
// wasm out-of-bounds access) `IsFaultAddressCovered(RIP)` fails and the trap becomes a fatal
// SIGSEGV instead of a WebAssembly.RuntimeError.  This is never on in a measurement run; it
// exists to show that the hazard is real in V8 and that the WASM_CODE gate is what stops it.
static void wasm_probe_patch(uint8_t *code, size_t len) {
  uint64_t base = (uint64_t)code;
  uint64_t n = 0;
  int found = 0; uint32_t off = 0;
  if (g_v8_syms) {
    V8MetaLock lk;                       // copy what we need out from under V8's lock
    n = v8_prot_nobj();
    for (uint64_t i = 0; i < n && !found; i++) {
      const V8ProtInfo *p = v8_prot_at(i);
      if (!p || p->base != base || !p->ninstr) continue;
      found = 1; off = p->off[0];
      for (uint64_t k = 1; k < p->ninstr; k++) if (p->off[k] < off) off = p->off[k];
    }
  }
  if (!found) {
    fprintf(stderr, "PTJIT wasm-probe: obj %p len %zu NOT in the trap table (n=%llu)\n",
            (void *)code, len, (unsigned long long)n);
    return;
  }
  if (off + 5 > len) return;
  // wasm code lives in its own code space, far from V8's JS code range, so the run's
  // trampoline slab is not within the +-2 GB a `jmp rel32' reaches (it would
  // report `out_of_range').  Give the probe its own slab next to the wasm code.
  static uint8_t *pslab = nullptr; static size_t pslab_cap = 0, pslab_used = 0;
  if (!pslab) {
    for (int64_t o = 1 << 20; o < (int64_t)1 << 31 && !pslab; o <<= 1) {
      uint64_t hint = (base - (uint64_t)o) & ~(uint64_t)0xfffff;
      void *m = mmap((void *)hint, 1u << 20, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (m == MAP_FAILED) continue;
      int64_t d1 = (int64_t)((uint64_t)m - base), d2 = d1 + (1 << 20);
      if (d1 > -0x7f000000LL && d2 < 0x7f000000LL) { pslab = (uint8_t *)m; pslab_cap = 1u << 20; }
      else munmap(m, 1u << 20);
    }
    if (!pslab) { fprintf(stderr, "PTJIT wasm-probe: no slab near %p\n", (void *)base); return; }
  }
  uint8_t *sv_slab = g_cx.slab; size_t sv_cap = g_cx.slab_cap, *sv_used = g_cx.slab_used;
  g_cx.slab = pslab; g_cx.slab_cap = pslab_cap; g_cx.slab_used = &pslab_used;
  live_supersede(base, base + len);
  PtjSite s; memset(&s, 0, sizeof(s));
  s.off = off; s.id = 0; s.when = PTJ_WHEN_BEFORE; s.kind = PTJ_KIND_REG; s.size = 8;
  s.nregs = 1; s.regs[0] = 7;                        // %rdi -- the value is irrelevant here
  g_cx.roots = nullptr; g_cx.nroots = 0;
  PtjObjStat ost; memset(&ost, 0, sizeof(ost));
  g_cx.tsc = rdtscp_now();
  g_sites_req++;
  uint64_t first = g_nprec;
  tslab_commit(g_tslab_used + (1u << 20));
  uint64_t d0[PTJ_DROP_N];
  memcpy(d0, g_cx.drops, sizeof d0);
  ptj_patch_object(&g_cx, code, len, &s, 1, &ost, wr_open, wr_close, note_tramp);
  g_sites_patched += ost.patched;
  if (ost.patched) { g_objs_patched++; g_c.wasm_probe_sites += ost.patched; }
  fprintf(stderr, "PTJIT wasm-probe: obj %p len %zu protected@0x%x patched=%llu",
          (void *)code, len, off, (unsigned long long)ost.patched);
  for (int k = 0; k < PTJ_DROP_N; k++)
    if (g_cx.drops[k] != d0[k])
      fprintf(stderr, " %s=%llu", PTJ_DROP_NAME[k], (unsigned long long)(g_cx.drops[k] - d0[k]));
  fprintf(stderr, "\n");
  g_cx.slab = sv_slab; g_cx.slab_cap = sv_cap; g_cx.slab_used = sv_used;
  obj_note(base, (uint32_t)len, g_cx.tsc, 1, ost.patched);
  live_note(base, (uint32_t)len, &s, 1, first);
  g_cx.obj++;
}

// ======================================================================
//  Crash diagnostics
//  A SIGSEGV/SIGBUS/SIGILL handler that classifies the faulting IP against the
//  slab, the trampolines and the patched code objects, plus a patch verifier
//  that says *what* overwrote a patch (V8 restored the original bytes / wrote
//  something else / the object was re-added at the same address).
// ======================================================================
#include <signal.h>
#include <ucontext.h>
#include <sys/uio.h>

// Read our own memory without risking a SIGSEGV: V8 releases code pages, so an
// address we patched earlier in the run may no longer be mapped at all.
static int safe_read(const void *src, void *dst, size_t n) {
  struct iovec l = {dst, n}, r = {(void *)src, n};
  return process_vm_readv(getpid(), &l, 1, &r, 1, 0) == (ssize_t)n;
}

static uint64_t g_events = 0;          // JIT events seen so far

// nearest patch record whose trampoline starts at or below `a`
static int64_t prec_by_tramp(uint64_t a) {
  int64_t best = -1; uint64_t bd = ~0ull;
  for (uint64_t i = 0; i < g_nprec && g_prec; i++) {
    if (g_prec[i].tramp <= a && a - g_prec[i].tramp < bd) { bd = a - g_prec[i].tramp; best = (int64_t)i; }
  }
  return best;
}
static int64_t prec_by_code(uint64_t a) {
  for (uint64_t i = 0; i < g_nprec && g_prec; i++)
    if (a >= g_prec[i].obj_addr && a < g_prec[i].obj_addr + g_prec[i].obj_len) return (int64_t)i;
  return -1;
}
static void classify(FILE *f, const char *tag, uint64_t a) {
  fprintf(f, "PTJITDIAG %s=0x%llx", tag, (unsigned long long)a);
  if (g_tslab && a >= (uint64_t)g_tslab && a < (uint64_t)g_tslab + g_tslab_cap) {
    int64_t i = prec_by_tramp(a);
    fprintf(f, " in=SLAB off=0x%llx used=0x%llx",
            (unsigned long long)(a - (uint64_t)g_tslab), (unsigned long long)g_tslab_used);
    if (i >= 0) fprintf(f, " tramp#%lld(+0x%llx) serves code=0x%llx obj=%u",
                        (long long)i, (unsigned long long)(a - g_prec[i].tramp),
                        (unsigned long long)g_prec[i].addr, g_prec[i].obj_idx);
  } else {
    int64_t i = prec_by_code(a);
    if (i >= 0) fprintf(f, " in=CODEOBJ obj=%u base=0x%llx len=%u off=0x%llx",
                        g_prec[i].obj_idx, (unsigned long long)g_prec[i].obj_addr,
                        g_prec[i].obj_len, (unsigned long long)(a - g_prec[i].obj_addr));
    else if (in_file_mapping(a)) fprintf(f, " in=FILE_MAPPING");
    else fprintf(f, " in=?");
  }
  fprintf(f, "\n");
}

// One verification pass.  Returns the number of clobbered patches; if `report`
// is set, prints the first `report` of them with the three byte strings.
static uint64_t verify_pass(FILE *f, int report, uint64_t *out_restored,
                            uint64_t *out_partial, uint64_t *out_other) {
  uint64_t bad = 0, restored = 0, partial = 0, other = 0; int printed = 0;
  uint64_t gone = 0;
  for (uint64_t i = 0; i < g_nprec && g_prec; i++) {
    PatchRec *r = &g_prec[i];
    if (r->dead || !r->wlen) continue;
    uint8_t cur[32];
    if (!safe_read((const void *)r->addr, cur, r->wlen)) { gone++; continue; }
    if (!memcmp(cur, r->want, r->wlen)) continue;
    bad++;
    const uint8_t *now = cur;
    int is_restored = !memcmp(now, r->orig, r->wlen);
    int keeps_jmp = (now[0] == 0xe9) && !memcmp(now + 1, r->want + 1, 4);
    if (is_restored) restored++;
    else if (keeps_jmp) partial++;      // jmp intact, the nop tail was rewritten
    else other++;
    if (f && printed < report) {
      printed++;
      fprintf(f, "PTJITCLOB #%llu obj=%u code=0x%llx off=0x%x len=%u tramp=0x%llx %s\n",
              (unsigned long long)i, r->obj_idx, (unsigned long long)r->obj_addr,
              r->off, r->wlen, (unsigned long long)r->tramp,
              is_restored ? "RESTORED_ORIGINAL" : keeps_jmp ? "TAIL_ONLY" : "OVERWRITTEN");
      fprintf(f, "   orig="); hexdump(f, r->orig, r->wlen);
      fprintf(f, "\n   want="); hexdump(f, r->want, r->wlen);
      fprintf(f, "\n   now ="); hexdump(f, now, r->wlen);
      uint8_t ob[32];
      uint32_t on = r->obj_len < 32 ? r->obj_len : 32;
      if (safe_read((const void *)r->obj_addr, ob, on)) { fprintf(f, "\n   obj_now="); hexdump(f, ob, on); }
      fprintf(f, "\n");
    }
  }
  if (out_restored) *out_restored = restored;
  if (out_partial) *out_partial = partial;
  if (out_other) *out_other = other;
  g_verify_unmapped = gone;
  return bad;
}

static struct sigaction g_old_segv, g_old_bus, g_old_ill;
static volatile sig_atomic_t g_in_handler = 0;

static void diag_handler(int sig, siginfo_t *si, void *uc) {
  if (g_in_handler) _exit(98);
  g_in_handler = 1;
  ucontext_t *u = (ucontext_t *)uc;
  uint64_t rip = (uint64_t)u->uc_mcontext.gregs[REG_RIP];
  uint64_t rsp = (uint64_t)u->uc_mcontext.gregs[REG_RSP];
  FILE *f = stderr;
  fprintf(f, "\nPTJITDIAG ==== signal %d si_addr=%p si_code=%d events=%llu patches=%llu ====\n",
          sig, si->si_addr, si->si_code, (unsigned long long)g_events,
          (unsigned long long)g_nprec);
  classify(f, "rip", rip);
  classify(f, "fault", (uint64_t)si->si_addr);
  fprintf(f, "PTJITDIAG rsp=0x%llx\n", (unsigned long long)rsp);
  // bytes around RIP (may itself fault; we accept that -- _exit(98) above)
  fprintf(f, "PTJITDIAG rip[-16..+16]=");
  hexdump(f, (const uint8_t *)(rip - 16), 32);
  fprintf(f, "\n");
  static const char *RN[16] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
                               "r8","r9","r10","r11","r12","r13","r14","r15"};
  static const int GI[16] = {REG_RAX,REG_RCX,REG_RDX,REG_RBX,REG_RSP,REG_RBP,REG_RSI,REG_RDI,
                             REG_R8,REG_R9,REG_R10,REG_R11,REG_R12,REG_R13,REG_R14,REG_R15};
  for (int i = 0; i < 16; i++)
    fprintf(f, "PTJITDIAG %s=0x%llx\n", RN[i], (unsigned long long)u->uc_mcontext.gregs[GI[i]]);
  // return addresses on the stack that land in our slab or a patched object
  for (int i = 0; i < 64; i++) {
    uint64_t w = ((uint64_t *)rsp)[i];
    if (g_tslab && w >= (uint64_t)g_tslab && w < (uint64_t)g_tslab + g_tslab_cap) {
      fprintf(f, "PTJITDIAG stack[%d] ", i); classify(f, "ret", w);
    }
  }
  uint64_t rest = 0, part = 0, oth = 0;
  uint64_t bad = verify_pass(f, 8, &rest, &part, &oth);
  fprintf(f, "PTJITDIAG clobbered=%llu (restored=%llu tail_only=%llu overwritten=%llu) of %llu\n",
          (unsigned long long)bad, (unsigned long long)rest, (unsigned long long)part,
          (unsigned long long)oth, (unsigned long long)g_nprec);
  fflush(f);
  _exit(97);
}

static void diag_install(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = diag_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, &g_old_segv);
  sigaction(SIGTRAP, &sa, nullptr);
  sigaction(SIGBUS, &sa, &g_old_bus);
  sigaction(SIGILL, &sa, &g_old_ill);
}

// Re-install a cached site list at a new base (used by the CODE_MOVED repair).
static void install_at(uint8_t *code, size_t len, PtjSite *sites, int n,
                       const uint32_t *rroots, int nrr) {
  if (!g_tslab) return;
  g_cx.slab = g_tslab; g_cx.slab_cap = g_tslab_cap; g_cx.slab_used = &g_tslab_used;
  // The moved copy must be swept with the same restart roots, or the sites beyond the
  // first restart land on offsets this sweep never decoded and are dropped.
  g_cx.roots = (!g_noroots && rroots && nrr > 0) ? rroots : nullptr;
  g_cx.nroots = (uint32_t)(!g_noroots && nrr > 0 ? nrr : 0);
  tslab_commit(g_tslab_used + (1u << 20));
  PtjObjStat ost; memset(&ost, 0, sizeof(ost));
  g_cx.tsc = rdtscp_now();
  uint64_t first = g_nprec;
  ptj_patch_object(&g_cx, code, len, sites, n, &ost, wr_open, wr_close, note_tramp);
  // A re-install after a move is not a *new* site: count it separately so that
  // sites_patched / sites_requested stays the coverage of the analyzer's site set.
  g_sites_repatched += ost.patched;
  g_objs_repatched++;
  obj_note((uint64_t)code, (uint32_t)len, g_cx.tsc, (uint32_t)n, ost.patched);
  live_note((uint64_t)code, (uint32_t)len, sites, n, first, rroots, nrr);
  g_cx.obj++;
}

// ------------------------------------------------------------- handler ------
// 1 if a linear decode from the object's first byte reaches an undecodable byte before any
// unconditional control transfer that ends straight-line code (jmp/ret/ud2/hlt); 0 otherwise
// (a terminator first, or the whole object decodes).  Mirrors the Java agent's stub rule.
static int obj_looks_like_data(const uint8_t *code, size_t len) {
  for (size_t o = 0; o < len; ) {
    ZydisDecodedInstruction pi;
    ZydisDecodedOperand po[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&g_dec, code + o, len - o, &pi, po,
                                             ZYDIS_MAX_OPERAND_COUNT, 0))) return 1;
    switch (pi.mnemonic) {
      case ZYDIS_MNEMONIC_JMP: case ZYDIS_MNEMONIC_RET: case ZYDIS_MNEMONIC_UD2:
      case ZYDIS_MNEMONIC_HLT: return 0;
      default: break;
    }
    o += pi.length;
  }
  return 0;
}

static void handler(const v8::JitCodeEvent *e) {
  uint64_t t0 = 0;
  if (g_mode >= MODE_PATCH) t0 = ptj_ns();
  else if (g_verbose) t0 = now_ns();
  pid_t tid = (pid_t)syscall(SYS_gettid);
  if (tid != g_install_tid) g_c.off_thread++;

  switch (e->type) {
    case v8::JitCodeEvent::CODE_ADDED: {
      g_c.added++;
      g_c.bytes_added += e->code_len;
      switch (e->code_type) {
        case v8::JitCodeEvent::BYTE_CODE: g_c.added_bytecode++; break;
        case v8::JitCodeEvent::JIT_CODE: g_c.added_jitcode++; break;
        default: g_c.added_wasm++; break;
      }
      if (g_mode >= MODE_DUMP)
        arena_record(0, (uint8_t)e->code_type, e->code_start, nullptr,
                     e->code_len, e->name.str, e->name.len, 1);
      if (g_mode >= MODE_PATCH) {
        // ---- the two pc-identity gates ----------------
        // (1) code_type: V8 reports wasm as WASM_CODE and only wasm code contains
        //     instructions that fault BY DESIGN (trap_handler protected loads/stores and
        //     WasmGC null checks).  Never patch it.  (2) belt and braces, and forward proof
        //     against a future V8 that reports wasm as JIT_CODE: never patch a code object
        //     that is registered in `trap_handler::gCodeObjects`.
        int is_jit = (e->code_type == v8::JitCodeEvent::JIT_CODE);
        int is_wasm = (e->code_type == v8::JitCodeEvent::WASM_CODE);
        if (g_async) async_note_added((uint64_t)e->code_start, (uint64_t)e->code_start + e->code_len);
        if (is_wasm) {
          wasm_range_note((uint64_t)e->code_start, (uint64_t)e->code_start + e->code_len);
          uint64_t live = 0, ins = 0; uint64_t rl[1], rh[1];
          v8_snapshot_ranges(rl, rh, 0, &live, &ins);
          if (live > g_c.wasmprot_live_max) g_c.wasmprot_live_max = live;
          if (ins > g_c.wasmprot_insns_max) g_c.wasmprot_insns_max = ins;
        }
        int gated = 0;
        if (!is_jit && !(g_patch_wasm && is_wasm)) {
          gated = 1;
          if (is_wasm) g_c.patch_skipped_wasm++; else g_c.patch_skipped_bytecode++;
          g_c.patch_skipped_type++;
        } else if (!g_patch_wasm && v8_prot_nobj() &&
                   v8_in_wasm_code((uint64_t)e->code_start)) {
          gated = 1;
          g_c.patch_skipped_wasm_range++;
        }
        if (!gated && is_jit && data_gate_mode() > 0) {
          g_c.obj_data_gate_checked++;
          if (obj_looks_like_data((const uint8_t *)e->code_start, e->code_len)) {
            g_c.obj_data_skips++;
            if (data_gate_mode() >= 2) gated = 1;
          }
        }
        if (!gated) {
          note_range((uint64_t)e->code_start,
                     (uint64_t)e->code_start + e->code_len, 0);
          if (g_mode >= MODE_FULL)
            full_patch((uint8_t *)e->code_start, e->code_len, e->name.str, e->name.len);
          else
            entry_patch((uint8_t *)e->code_start, e->code_len, e->name.str, e->name.len);
        } else if (g_wasm_probe && is_wasm) {
          wasm_probe_patch((uint8_t *)e->code_start, e->code_len);
        }
      }
      break;
    }
    case v8::JitCodeEvent::CODE_MOVED:
      g_c.moved++;
      g_c.bytes_moved += e->code_len;
      if (g_mode >= MODE_DUMP)
        arena_record(1, (uint8_t)e->code_type, e->code_start, e->new_code_start,
                     e->code_len, nullptr, 0, 0);
      // V8 evacuated the code object: our position-dependent detour travelled with
      // the copy.  Repair it before anything can execute the new address.
      if (g_mode >= MODE_PATCH && e->code_type == v8::JitCodeEvent::JIT_CODE) {
        if (g_pinbridge) {
          if (g_async) async_note_moved((uint64_t)e->code_start, (uint64_t)e->new_code_start, e->code_len);
          pin_publish(PT_PIN_MOVE, (uint8_t *)e->code_start, e->code_len,
                      nullptr, 0, (uint8_t *)e->new_code_start);
        } else {
          if (g_async) async_note_moved((uint64_t)e->code_start, (uint64_t)e->new_code_start, e->code_len);
          on_moved((uint8_t *)e->code_start, (uint8_t *)e->new_code_start, e->code_len);
        }
      }
      break;
    case v8::JitCodeEvent::CODE_REMOVED:
      g_c.removed++;
      if (g_pinbridge && e->code_len)
        pin_publish(PT_PIN_REMOVE, (uint8_t *)e->code_start, e->code_len);
      if (g_mode >= MODE_DUMP)
        arena_record(2, (uint8_t)e->code_type, e->code_start, nullptr,
                     e->code_len, nullptr, 0, 0);
      break;
    default:
      g_c.other++;
      break;
  }
  if (g_mode >= MODE_PATCH) { uint64_t d = ptj_ns() - t0; g_pause_ns += d; g_c.handler_ns += d; }
  else if (g_verbose) g_c.handler_ns += now_ns() - t0;
  g_events++;
  if (g_diag > 1 && (g_events % (uint64_t)g_diag_every) == 0) {
    uint64_t rest = 0, part = 0, oth = 0;
    uint64_t bad = verify_pass(g_diag > 2 ? stderr : nullptr, g_diag > 2 ? 2 : 0,
                               &rest, &part, &oth);
    if (bad > g_diag_worst) {
      g_diag_worst = bad;
      fprintf(stderr, "PTJITDIAG ev=%llu clobbered=%llu (restored=%llu tail=%llu other=%llu)"
                      " of %llu\n", (unsigned long long)g_events, (unsigned long long)bad,
              (unsigned long long)rest, (unsigned long long)part, (unsigned long long)oth,
              (unsigned long long)g_nprec);
    }
  }
}

// --------------------------------------------------------------- N-API ------
#define NAPI_CALL(env, call) do { if ((call) != napi_ok) return nullptr; } while (0)

static napi_value Enable(napi_env env, napi_callback_info info) {
  v8_trap_syms();                 // V8's wasm protected-insn table
  size_t argc = 9;
  napi_value argv[9];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  int32_t mode = 1, verbose = 0, ptw = 1;
  int64_t arena_mb = 512, tslab_mb = 64, maxpatch = -1;
  if (argc > 0) napi_get_value_int32(env, argv[0], &mode);
  if (argc > 1) napi_get_value_int64(env, argv[1], &arena_mb);
  if (argc > 2) napi_get_value_int32(env, argv[2], &verbose);
  if (argc > 3) napi_get_value_int32(env, argv[3], &ptw);
  if (argc > 4) napi_get_value_int64(env, argv[4], &maxpatch);
  int32_t enum_existing = 0;
  if (argc > 5) napi_get_value_int32(env, argv[5], &enum_existing);
  int32_t count_sink = 0;
  if (argc > 6) napi_get_value_int32(env, argv[6], &count_sink);
  g_count_sink = count_sink;
  int32_t no_imm64 = 0;
  if (argc > 7) napi_get_value_int32(env, argv[7], &no_imm64);
  g_no_imm64 = no_imm64 & 1;
  g_scan_site = (no_imm64 >> 1) & 1;   // JITPOC_NOIMM64=3 -> scan mode
  g_sparkplug = (no_imm64 >> 2) & 1;   // JITPOC_NOIMM64=5 -> +Sparkplug rule
  int64_t minpatch = 0;
  if (argc > 8) napi_get_value_int64(env, argv[8], &minpatch);
  g_min_patch = (uint64_t)minpatch;
  g_mode = mode;
  const char *pin = getenv("PTJIT_PIN");
  if (pin && strcmp(pin, "0") && strcmp(pin, "1")) {
    fprintf(stderr, "PTJIT: PTJIT_PIN must be 0 or 1\n"); _exit(71);
  }
  g_pinbridge = pin && !strcmp(pin, "1");
  const char *anchor = getenv("PTJIT_STACK_ANCHOR");
  if (anchor && strcmp(anchor, "0") && strcmp(anchor, "1") && strcmp(anchor, "2")) {
    fprintf(stderr, "PTJIT: PTJIT_STACK_ANCHOR must be 0, 1 (rsp), or 2 (rsp+rbp)\n"); _exit(71);
  }
  g_stack_anchor = anchor ? atoi(anchor) : 0;
  if (g_stack_anchor && (mode != MODE_FULL || g_pinbridge)) {
    fprintf(stderr, "PTJIT: stack anchors currently require native MODE=4\n"); _exit(71);
  }
  if (g_pinbridge && (mode != MODE_FULL || !getenv("PTJIT_KEYFRAME") ||
      strcmp(getenv("PTJIT_KEYFRAME"), "0") || getenv("PTJIT_GT") || getenv("PTJIT_SINK"))) {
    fprintf(stderr, "PTJIT: Pin bridge requires MODE=4, KEYFRAME=0 and no GT/SINK override\n");
    _exit(71);
  }
  g_verbose = verbose;
  g_use_ptwrite = ptw;
  if (maxpatch >= 0) g_max_patch = (uint64_t)maxpatch;
  g_install_tid = (pid_t)syscall(SYS_gettid);
  if (mode >= MODE_DUMP) arena_init((size_t)arena_mb << 20);
  if (mode >= MODE_PATCH) {
    g_tslab_want = (size_t)tslab_mb << 20;
    load_file_ranges();
    if (!g_ctrs)
      g_ctrs = (uint64_t *)mmap(nullptr, CTR_MAX * 8, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (g_ctrs == MAP_FAILED) g_ctrs = nullptr;
    if (!g_prec)
      g_prec = (PatchRec *)mmap(nullptr, CTR_MAX * sizeof(PatchRec),
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (g_prec == MAP_FAILED) g_prec = nullptr;
    if (!g_dec_ready) {
      ZydisDecoderInit(&g_dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
      g_dec_ready = 1;
    }
    open_all_pkeys();
    g_diag = getenv("PTJIT_DIAG") ? atoi(getenv("PTJIT_DIAG")) : 0;
    if (getenv("PTJIT_HASHDUMP")) g_hashdump = fopen(getenv("PTJIT_HASHDUMP"), "w");
    g_diag_every = getenv("PTJIT_DIAG_EVERY") ? atoi(getenv("PTJIT_DIAG_EVERY")) : 200;
    if (g_diag) diag_install();
    // --- analyzer service, cache and patch context (modes 3 and 4) ---
    memset(&g_cx, 0, sizeof(g_cx));
    g_cx.dec = &g_dec;
    g_fastdec = getenv("PTJIT_FASTDEC") ? atoi(getenv("PTJIT_FASTDEC")) : 2;   // 1 = decode once, 2 = + lazy operands
    g_cx.fastdec = g_fastdec;
    g_ptj_deccheck = getenv("PTJIT_DECCHECK") ? atoi(getenv("PTJIT_DECCHECK")) : 0;
    g_cx.use_ptwrite = g_use_ptwrite;
    const char *sp = getenv("PTJIT_SPACE");
    g_space = sp ? (uint32_t)atoi(sp) : 3;
    g_cx.space = (int)g_space;
    const char *spk = getenv("PTJIT_SPARKPLUG");
    g_cx.sparkplug = spk ? atoi(spk) : 1;
    g_cx.single = getenv("PTJIT_SINGLE") ? atoi(getenv("PTJIT_SINGLE")) : 0;
    // ---- BUFFER SINK ---------------------------------
    // `PTJIT_SINK=buffer': a logged value is STORED into this thread's %gs ring instead of
    // being PTWRITEd.  The ring, its guard-page fault handler, the cv files and the sync
    // markers are `runtime/rt/ptlogrt.c' -- the same runtime the E9Patch buffer sink uses --
    // so `ptrecon' consumes the run with no JIT-specific code.  The runtime must already be
    // in the process (LD_PRELOAD=runtime/rt/ptlogrt.so, which `preload.js' does for us):
    // without it the stores fault at address 0 with nothing to catch them, so refuse loudly.
    { const char *sk = getenv("PTJIT_SINK");
      if (sk && !strcmp(sk, "buffer")) {
        unsigned long (*abi)(void) = (unsigned long (*)(void))dlsym(RTLD_DEFAULT, "ptlog_jit_abi");
        void (*arm)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "ptlog_jit_arm");
        if (!abi || !arm || abi() == 0) {
          fprintf(stderr, "PTJIT sink=buffer: runtime/rt/ptlogrt.so is not loaded "
                          "(LD_PRELOAD it) -- refusing to run\n");
          _exit(71);
        }
        arm();                                  // our handler in front of V8's wasm trap handler
        g_cx.sink_buffer = 1;
        const char *sy = getenv("PTLOG_SYNC");   // MUST equal the runtime's own period
        g_cx.sync = sy ? (uint32_t)strtoul(sy, nullptr, 0) : 4096u;
        const char *sc = getenv("PTLOG_SYNC_CARRIER");   // `tnt': no PTWRITE (rewrite.py --sync-carrier)
        g_cx.sync_tnt = (sc && !strcmp(sc, "tnt")) ? 1 : 0;
        if (g_cx.sync_tnt) {
          g_cx.maxtnts = 3u << 21;
          g_cx.tnts = (PtjTntMark *)mmap(nullptr, (size_t)g_cx.maxtnts * sizeof(PtjTntMark),
              PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
          if (g_cx.tnts == MAP_FAILED) { g_cx.tnts = nullptr; g_cx.maxtnts = 0; }
        }
        g_cx.maxsyncs = 1u << 21;
        g_cx.syncs = (PtjSyncMark *)mmap(nullptr, (size_t)g_cx.maxsyncs * sizeof(PtjSyncMark),
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (g_cx.syncs == MAP_FAILED) { g_cx.syncs = nullptr; g_cx.maxsyncs = 0; }
      } else if (sk && strcmp(sk, "ptwrite")) {
        fprintf(stderr, "PTJIT: unknown PTJIT_SINK=%s (want `ptwrite' or `buffer')\n", sk);
        _exit(71);
      } }
    // ---- keyframes -------------------------
    // V8 tiers a function up WHILE it is running and enters the new code object by OSR at a
    // loop header, so the object's one frame anchor (a `reg' site at its first byte) never
    // executes and 79-91 % of its records had an unknown address.  `PTJIT_KEYFRAME=K' asks
    // the analyzer for `resync' sites at the outermost loop back-edge headers, logged every
    // K-th execution.  0 turns them off.
    // NOROOTS=0 (default) seeds the restart roots; NOROOTS=1 is an explicit ablation.
    { const char *nr = getenv("PTJIT_NOROOTS"); g_noroots = nr ? atoi(nr) : 0; }
    // ---- pc identity ---------------------------------
    // The knobs for implicit-exception translation.  Both default OFF: a measurement run never
    // patches wasm code, and the audit below proves it did not.
    g_patch_wasm = getenv("PTJIT_PATCH_WASM") ? atoi(getenv("PTJIT_PATCH_WASM")) : 0;
    g_wasm_probe = getenv("PTJIT_WASM_PROBE") ? atoi(getenv("PTJIT_WASM_PROBE")) : 0;
    { const char *kf = getenv("PTJIT_KEYFRAME");
      g_keyframe = kf ? (uint32_t)strtoul(kf, nullptr, 0) : 1024;
      const char *kc = getenv("PTJIT_KFCTR");
      if (kc) g_kfctr_max = (uint32_t)strtoul(kc, nullptr, 0);
      g_cx.kf_flags_live = getenv("PTJIT_KF_FLAGS_LIVE") ? atoi(getenv("PTJIT_KF_FLAGS_LIVE")) : 0;
      // The engine-neutral placement rules shared with the Java front end.
      // `slide' starts a failed window up to 4 instructions earlier, `shift' moves a reg/before
      // site forward past instructions that write none of its registers, `kfflagsave' wraps a
      // live-EFLAGS keyframe guard in pushfq/popfq.  None displaces an instruction the V8 rules
      // refuse (imm32 = V8 RelocInfo, calls, CODE_TARGET branches); pcsoft/uep/immrule/vmentries
      // are HotSpot-specific and have no V8 counterpart.  Default on; =0 disables.
      g_cx.slide = getenv("PTJIT_SLIDE") ? atoi(getenv("PTJIT_SLIDE")) : 1;
      g_cx.shift = getenv("PTJIT_SHIFT") ? atoi(getenv("PTJIT_SHIFT")) : 1;
      g_cx.kf_flags_save = getenv("PTJIT_KF_FLAGS_SAVE") ? atoi(getenv("PTJIT_KF_FLAGS_SAVE")) : 1; }
    // ---- same-run ground truth -----------------------------------------
    if (getenv("PTJIT_GT") && atoi(getenv("PTJIT_GT"))) {
      size_t mb = getenv("PTJIT_GT_MB") ? (size_t)strtoul(getenv("PTJIT_GT_MB"), nullptr, 0) : 4096;
      const char *dir = getenv("PTJIT_GT_DIR");
      if (ptj_gt_open(&g_gt, dir ? dir : ".", (int)getpid(), mb << 20)) {
        g_cx.gt = 1;                       // the cursor cell is armed with the slab
        g_cx.maxgtents = 1u << 22;
        g_cx.gtents = (PtjGtEnt *)mmap(nullptr, (size_t)g_cx.maxgtents * sizeof(PtjGtEnt),
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (g_cx.gtents == MAP_FAILED) { g_cx.gtents = nullptr; g_cx.maxgtents = 0; }
        fprintf(stderr, "PTJIT gt: %s (%zu MB window)\n", g_gt.path, mb);
      } else {
        fprintf(stderr, "PTJIT gt: FAILED to open the ground-truth ring -- refusing to run\n");
        _exit(70);
      }
    }
    g_cx.maxentries = 1u << 21; g_cx.maxrelocs = 1u << 21; g_cx.maxtramps = 1u << 20;
    g_cx.entries = (PtjMapEntry *)mmap(nullptr, (size_t)g_cx.maxentries * sizeof(PtjMapEntry),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    g_cx.relocs = (PtjReloc *)mmap(nullptr, (size_t)g_cx.maxrelocs * sizeof(PtjReloc),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    g_cx.tramps = (uint64_t *)mmap(nullptr, (size_t)g_cx.maxtramps * 8,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    g_objs = (ObjRec *)mmap(nullptr, (size_t)OBJCAP * sizeof(ObjRec),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    g_live = (LiveObj *)mmap(nullptr, (size_t)LIVECAP * sizeof(LiveObj),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (g_live == MAP_FAILED) g_live = nullptr;
    if (g_cx.entries == MAP_FAILED) g_cx.entries = nullptr, g_cx.maxentries = 0;
    if (g_cx.relocs == MAP_FAILED) g_cx.relocs = nullptr, g_cx.maxrelocs = 0;
    if (g_cx.tramps == MAP_FAILED) g_cx.tramps = nullptr, g_cx.maxtramps = 0;
    if (g_objs == MAP_FAILED) g_objs = nullptr;
  }
  if (mode >= MODE_FULL) {
    memset(&g_cli, 0, sizeof(g_cli));
    g_cli.fd = -1;
    const char *sk = getenv("PTJIT_SOCK");
    if (sk) snprintf(g_cli.sock, sizeof(g_cli.sock), "%s", sk);
    else snprintf(g_cli.sock, sizeof(g_cli.sock), "/tmp/ptjit.%d.sock", (int)getpid());
    const char *cd = getenv("PTJIT_CACHE");
    snprintf(g_cli.cache, sizeof(g_cli.cache), "%s",
             cd ? cd : "cache");
    mkdir(g_cli.cache, 0755);
    const char *py = getenv("PTJIT_PY");
    snprintf(g_cli.py, sizeof(g_cli.py), "%s", py ? py : "python3");
    const char *cv = getenv("PTJIT_CACHE_VER");
    int cvlen = snprintf(g_cli.ver, sizeof(g_cli.ver), "%s", cv ? cv : "v2.24");
    if (cvlen < 0 || (size_t)cvlen >= sizeof(g_cli.ver) ||
        ptj_objective(g_cli.ver, sizeof(g_cli.ver), getenv("PTJIT_FAST"), &g_cli.fast)) {
      fprintf(stderr, "PTJIT: invalid PTJIT_FAST (use 0/1) or oversized cache version\n");
      _exit(71);
    }
    // K is part of the on-disk cache key: a k0 site list and a k1024 one describe the same
    // bytes, and serving one for the other would silently undo the keyframe build.
    g_cli.kf = g_keyframe;
    const char *an = getenv("PTJIT_ANALYZE");
    snprintf(g_cli.script, sizeof(g_cli.script), "%s",
             an ? an : "analyze.py");
    if (!getenv("PTJIT_NOSPAWN")) {
      int to = getenv("PTJIT_SPAWN_MS") ? atoi(getenv("PTJIT_SPAWN_MS")) : 20000;
      ptj_start_service(&g_cli, to);
    } else {
      g_cli.fd = ptj_connect(&g_cli);
    }
    g_have_cli = (g_cli.fd >= 0);
    // Asynchronous analysis (the Java queue design, one worker = the only socket user).
    // Default on; PTJIT_ASYNC=0 analyses inside the JIT event.
    { const char *as = getenv("PTJIT_ASYNC"); g_async = as ? atoi(as) : 1; }
    { const char *pa = getenv("PTJIT_PIN_ASYNC");     // async analysis under the Pin bridge (Node HiFi)
      if (g_async && ((g_pinbridge && !(pa && !strcmp(pa, "1"))) || !g_have_cli)) g_async = 0; }
    if (g_async) async_start();
    g_mem = (CacheEnt *)mmap(nullptr, (size_t)MEMCAP * sizeof(CacheEnt),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (g_mem == MAP_FAILED) g_mem = nullptr;
  }
  v8::Isolate *iso = v8::Isolate::GetCurrent();
  iso->SetJitCodeEventHandler(enum_existing ? v8::kJitCodeEventEnumExisting
                                            : v8::kJitCodeEventDefault,
                              handler);
  napi_value r;
  napi_create_int32(env, mode, &r);
  return r;
}

static napi_value Disable(napi_env env, napi_callback_info info) {
  v8::Isolate *iso = v8::Isolate::GetCurrent();
  iso->SetJitCodeEventHandler(v8::kJitCodeEventDefault, nullptr);
  g_mode = MODE_OFF;
  napi_value r;
  napi_get_undefined(env, &r);
  return r;
}

static napi_value Flush(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  char path[512] = {0};
  size_t n = 0;
  if (argc > 0) napi_get_value_string_utf8(env, argv[0], path, sizeof(path), &n);
  if (g_mode >= MODE_DUMP && g_tslab && g_tslab_used)
    arena_record(5, 1, g_tslab, nullptr, g_tslab_used, nullptr, 0, 1);
  if (g_hashdump) { fflush(g_hashdump); }
  int64_t written = 0;
  if (n) written = ptj_arena_flush(&g_dump, path);
  napi_value r;
  napi_create_int64(env, written, &r);
  return r;
}

#define SETNUM(name, val) do { \
  napi_value v; napi_create_double(env, (double)(val), &v); \
  napi_set_named_property(env, obj, name, v); } while (0)

// The standing audit: NOTHING we displaced may be inside a
// code object V8 registered with its wasm trap handler, and nothing may be ON one of its
// protected instructions.  `relocs[].orig_addr` is every instruction we moved out of guest
// code, so this is the exact question implicit-exception translation asks, asked of the live V8 table.
static void wasm_prot_audit(void) {
  if (!g_v8_syms) return;
  static uint64_t lo[PTJ_WASMRNG_MAX], hi[PTJ_WASMRNG_MAX];
  uint64_t live = 0, ins = 0;
  uint32_t m = v8_snapshot_ranges(lo, hi, PTJ_WASMRNG_MAX, &live, &ins);
  g_c.wasmprot_objs = v8_prot_nobj();     // SLOTS in the table (free ones included)
  g_c.wasmprot_live = live; g_c.wasmprot_insns = ins;
  if (live > g_c.wasmprot_live_max) g_c.wasmprot_live_max = live;
  if (ins > g_c.wasmprot_insns_max) g_c.wasmprot_insns_max = ins;
  if (g_v8_recovered) g_c.wasmprot_recovered = *(volatile uint64_t *)g_v8_recovered;
  g_c.wasm_ranges = g_nwasmrng;
  if ((!m && !g_nwasmrng) || !g_cx.relocs) return;   // no wasm here: nothing to check
  for (uint32_t r = 0; r < g_cx.nrelocs; r++) {
    uint64_t a = g_cx.relocs[r].orig_addr;
    if (in_wasm_range(a)) g_c.wasmprot_overlap_reported++;
    int hit = 0;
    for (uint32_t i = 0; i < m && !hit; i++) if (a >= lo[i] && a < hi[i]) hit = 1;
    if (!hit) continue;
    g_c.wasmprot_overlap++;
    if (v8_is_protected(a)) g_c.wasmprot_overlap_insn++;   // rare: takes the lock
  }
}

static napi_value Stats(napi_env env, napi_callback_info info) {
  wasm_prot_audit();
  napi_value obj;
  napi_create_object(env, &obj);
  SETNUM("added", g_c.added);
  SETNUM("moved", g_c.moved);
  SETNUM("removed", g_c.removed);
  SETNUM("other", g_c.other);
  SETNUM("added_bytecode", g_c.added_bytecode);
  SETNUM("added_jitcode", g_c.added_jitcode);
  SETNUM("added_wasm", g_c.added_wasm);
  SETNUM("bytes_added", g_c.bytes_added);
  SETNUM("bytes_moved", g_c.bytes_moved);
  SETNUM("handler_ns", g_c.handler_ns);
  SETNUM("off_thread", g_c.off_thread);
  SETNUM("patched", g_c.patched);
  SETNUM("obj_data_skips", g_c.obj_data_skips);
  SETNUM("obj_data_gate_checked", g_c.obj_data_gate_checked);
  SETNUM("data_gate_mode", data_gate_mode());
  SETNUM("nprec", g_nprec);
  SETNUM("readd", g_readd);
  SETNUM("nseen", g_nseen);
  SETNUM("patch_skipped_decode", g_c.patch_skipped_decode);
  SETNUM("patch_skipped_small", g_c.patch_skipped_small);
  SETNUM("patch_skipped_ro", g_c.patch_skipped_ro);
  SETNUM("patch_skipped_range", g_c.patch_skipped_range);
  SETNUM("patch_skipped_type", g_c.patch_skipped_type);
  // pc identity: the two gates and the standing audit.
  SETNUM("patch_skipped_bytecode", g_c.patch_skipped_bytecode);
  SETNUM("patch_skipped_wasm", g_c.patch_skipped_wasm);
  SETNUM("patch_skipped_wasm_range", g_c.patch_skipped_wasm_range);
  SETNUM("wasmprot_syms", g_v8_syms);
  SETNUM("wasmprot_locked", (g_v8_mlock && g_v8_munlock) ? 1 : 0);
  SETNUM("wasmprot_objs", g_c.wasmprot_objs);
  SETNUM("wasmprot_live", g_c.wasmprot_live);
  SETNUM("wasmprot_live_max", g_c.wasmprot_live_max);
  SETNUM("wasmprot_insns_max", g_c.wasmprot_insns_max);
  SETNUM("wasm_ranges", g_c.wasm_ranges);
  SETNUM("wasmprot_overlap_reported", g_c.wasmprot_overlap_reported);
  SETNUM("wasmprot_insns", g_c.wasmprot_insns);
  SETNUM("wasmprot_overlap", g_c.wasmprot_overlap);
  SETNUM("wasmprot_overlap_insn", g_c.wasmprot_overlap_insn);
  SETNUM("wasmprot_recovered", g_c.wasmprot_recovered);
  SETNUM("wasm_probe_sites", g_c.wasm_probe_sites);
  SETNUM("patch_wasm", g_patch_wasm);
  SETNUM("patch_skipped_cap", g_c.patch_skipped_cap);
  SETNUM("keyframe", g_keyframe);
  SETNUM("kf_requested", g_cx.kf_requested);
  SETNUM("kf_sites", g_cx.kf_sites);
  SETNUM("kf_values", g_cx.kf_values);
  SETNUM("kf_counters", g_cx.kfctr_used);
  SETNUM("kf_dropped_flags_live", g_cx.drops[PTJ_DROP_KF_FLAGS]);
  SETNUM("kf_dropped_no_counter", g_cx.drops[PTJ_DROP_KF_NOCTR]);
  SETNUM("slide", g_cx.slide); SETNUM("shift", g_cx.shift); SETNUM("kf_flags_save", g_cx.kf_flags_save);
  SETNUM("slid", g_cx.slid); SETNUM("shifted", g_cx.shifted); SETNUM("kf_flags_saved", g_cx.kf_flags_saved);
  SETNUM("gt_sites", g_cx.ngtents);
  // BUFFER SINK
  SETNUM("sink_buffer", (uint64_t)g_cx.sink_buffer);
  SETNUM("buf_sync", g_cx.sync);
  SETNUM("buf_sites", g_cx.buf_sites);
  SETNUM("buf_sites_flags_live", g_cx.buf_sites_flags_live);
  SETNUM("buf_values", g_cx.buf_values);
  SETNUM("wide_memop_sites", g_cx.wide_memop_sites);   // > 8-byte memop sites (logged as 8 bytes)
  SETNUM("buf_markers", g_cx.nsyncs);
  SETNUM("restart_roots", g_restart_roots);      // Analyzer restart roots seeded into the sweep
  SETNUM("restart_objs", g_restart_objs);
  SETNUM("gt_refused", g_cx.gt_refused);
  SETNUM("gt_records", g_gt.base ? (ptj_gt_used(&g_gt) - 32) / 16 : 0);
  SETNUM("gt_window_bytes", g_gt.bytes);
  SETNUM("dump_bytes", g_c.dump_bytes);
  SETNUM("dump_truncated", g_c.dump_truncated);
  SETNUM("arena_used", g_dump.used);
  SETNUM("tslab_used", g_tslab_used);
  SETNUM("tslab_base", (uint64_t)g_tslab);
  SETNUM("pause_ns", g_pause_ns);
  { uint64_t cn = g_cli.call_ns, cc = g_cli.calls, ce = g_cli.errors;   // + workers 1..N-1
    for (int i = 1; i < g_nworkers && g_worker_up; i++)
      if (g_wcli[i]) { cn += g_wcli[i]->call_ns; cc += g_wcli[i]->calls; ce += g_wcli[i]->errors; }
    SETNUM("analysis_ns", cn);
    SETNUM("analysis_calls", cc);
    SETNUM("analysis_errors", ce); }
  SETNUM("aj_workers", g_async ? g_nworkers : 0);
  SETNUM("hash_ns", g_hash_ns);
  SETNUM("fastdec", g_fastdec);
  SETNUM("deccheck_key_objs", g_keychk_objs); SETNUM("deccheck_key_bad", g_keychk_bad);
  SETNUM("deccheck_dec_objs", g_ptj_deccheck_objs); SETNUM("deccheck_dec_bad", g_ptj_deccheck_bad);
  SETNUM("aj_install_same", g_aj.install_same);
  SETNUM("patch_ns", g_patch_ns);
  SETNUM("cache_mem_hits", g_cli.mem_hits);
  SETNUM("cache_disk_hits", g_cli.disk_hits);
  SETNUM("cache_disk_writes", g_cli.disk_writes);
  SETNUM("objs_analyzed", g_objs_analyzed);
  SETNUM("objs_patched", g_objs_patched);
  SETNUM("sites_requested", g_sites_req);
  SETNUM("sites_patched", g_sites_patched);
  SETNUM("values", g_cx.n_values);
  SETNUM("map_entries", g_cx.nentries);
  SETNUM("map_relocated", g_cx.nrelocs);
  SETNUM("trampolines", g_cx.ntramps);
  SETNUM("have_service", g_have_cli);
  SETNUM("analysis_fast", g_cli.fast);
  SETNUM("noroots", g_noroots);
  SETNUM("stack_anchor", g_stack_anchor);
  SETNUM("stack_anchor_objects", g_stack_anchor_objects);
  SETNUM("stack_anchor_values", g_stack_anchor_values);
  SETNUM("pin_bridge", g_pinbridge);
  SETNUM("pin_objects", g_pin_objects);
  SETNUM("pin_sites", g_pin_sites);
  SETNUM("moved_repaired", g_moved_repaired);
  SETNUM("moved_windows_restored", g_moved_windows);
  SETNUM("moved_unpatched_obj", g_moved_unknown);
  SETNUM("live_superseded", g_live_superseded);
  SETNUM("sites_repatched", g_sites_repatched);
  SETNUM("objs_repatched", g_objs_repatched);
  SETNUM("tslab_live", g_tslab_live);
  {
    napi_value dr; napi_create_object(env, &dr);
    for (int i = 0; i < PTJ_DROP_N; i++) {
      if (!g_cx.drops[i]) continue;
      napi_value v; napi_create_double(env, (double)g_cx.drops[i], &v);
      napi_set_named_property(env, dr, PTJ_DROP_NAME[i], v);
    }
    napi_set_named_property(env, obj, "dropped", dr);
    napi_value la; napi_create_array(env, &la);
    for (uint32_t i = 0; i < g_cli.nlat; i++) {
      napi_value v; napi_create_double(env, (double)g_cli.lat_ns[i] / 1e6, &v);
      napi_set_element(env, la, i, v);
    }
    napi_set_named_property(env, obj, "analysis_ms", la);
  }
  { uint64_t q = 0, b = 0, r = 0; async_counts(&q, &b, &r);
    SETNUM("async", g_async);
    SETNUM("aj_pending", q + b + r); SETNUM("aj_queued_now", q); SETNUM("aj_busy_now", b); SETNUM("aj_ready_now", r);
    SETNUM("aj_queued", g_aj.queued); SETNUM("aj_installed", g_aj.installed);
    SETNUM("aj_stale_overlap", g_aj.stale_overlap); SETNUM("aj_stale_moved", g_aj.stale_moved);
    SETNUM("aj_stale_hash", g_aj.stale_hash); SETNUM("aj_stale_unmapped", g_aj.stale_unmapped);
    SETNUM("aj_failed", g_aj.failed); SETNUM("aj_moved", g_aj.moved);
    SETNUM("aj_worker_cache_hits", g_aj.worker_cache_hits); SETNUM("aj_worker_calls", g_aj.worker_calls);
    SETNUM("aj_worker_analysis_ns", g_aj.worker_analysis_ns);
    SETNUM("aj_irq_calls", g_aj.irq_calls); SETNUM("aj_drain_calls", g_aj.drain_calls);
    SETNUM("aj_install_ns", g_aj.install_ns); SETNUM("aj_irq_ns", g_aj.irq_ns);
    SETNUM("aj_lat_mean_ms", g_aj.installed ? (double)g_aj.lat_sum_ns / g_aj.installed / 1e6 : 0);
    SETNUM("aj_lat_max_ms", (double)g_aj.lat_max_ns / 1e6); SETNUM("aj_pend_max", g_aj.pend_max);
    SETNUM("js_analysis_ns", g_js_analysis_ns); SETNUM("js_analysis_calls", g_js_analysis_calls); }
  SETNUM("have_pkru", g_have_pkru);
  SETNUM("saved_pkru", g_saved_pkru);
  return obj;
}

// Return the per-patched-object hit counters as an array of
// {addr, tramp, len, dlen, name, count} -- used to prove that a patch was
// installed BEFORE the code object's first execution.
static napi_value CountersFn(napi_env env, napi_callback_info info) {
  napi_value arr;
  napi_create_array(env, &arr);
  uint32_t n = 0;
  for (uint64_t i = 0; i < g_nprec && g_prec; i++) {
    if (g_ctrs && g_ctrs[i] == 0) continue;   // only executed ones
    napi_value o, v;
    napi_create_object(env, &o);
    napi_create_double(env, (double)g_prec[i].addr, &v);
    napi_set_named_property(env, o, "addr", v);
    napi_create_double(env, (double)g_prec[i].tramp, &v);
    napi_set_named_property(env, o, "tramp", v);
    napi_create_double(env, (double)g_prec[i].code_len, &v);
    napi_set_named_property(env, o, "len", v);
    napi_create_double(env, (double)g_prec[i].dlen, &v);
    napi_set_named_property(env, o, "dlen", v);
    napi_create_double(env, (double)(g_ctrs ? g_ctrs[i] : 0), &v);
    napi_set_named_property(env, o, "count", v);
    napi_create_string_utf8(env, g_prec[i].name, g_prec[i].name_len, &v);
    napi_set_named_property(env, o, "name", v);
    napi_set_element(env, arr, n++, o);
  }
  return arr;
}

// Re-read every patched entry and report how many no longer hold our
// `jmp rel32` -- i.e. how many patches V8 (GC relocation of embedded pointers,
// IC/deopt patching, code compaction) has overwritten behind our back.
static napi_value Verify(napi_env env, napi_callback_info info) {
  uint64_t ok = 0, clobbered = 0, first_clobber = ~0ull, retired = 0, unmapped = 0;
  for (uint64_t i = 0; i < g_nprec && g_prec; i++) {
    if (g_prec[i].dead) { retired++; continue; }
    uint8_t cur[5];
    if (!safe_read((const void *)g_prec[i].addr, cur, 5)) { unmapped++; continue; }
    uint8_t want[5];
    int64_t rel = (int64_t)(g_prec[i].tramp - (g_prec[i].addr + 5));
    int32_t r32 = (int32_t)rel;
    want[0] = 0xe9; memcpy(want + 1, &r32, 4);
    if (memcmp(cur, want, 5) == 0) ok++;
    else { clobbered++; if (first_clobber == ~0ull) first_clobber = i; }
  }
  napi_value o, v;
  napi_create_object(env, &o);
  napi_create_double(env, (double)ok, &v);
  napi_set_named_property(env, o, "intact", v);
  napi_create_double(env, (double)clobbered, &v);
  napi_set_named_property(env, o, "clobbered", v);
  napi_create_double(env, (double)(first_clobber == ~0ull ? -1 : (int64_t)first_clobber), &v);
  napi_set_named_property(env, o, "first", v);
  napi_create_double(env, (double)retired, &v);
  napi_set_named_property(env, o, "retired", v);
  napi_create_double(env, (double)unmapped, &v);
  napi_set_named_property(env, o, "unmapped", v);
  return o;
}

// Execute one `ptwrite` from JS so we can check PTW delivery independent of
// the JIT patching path.
static napi_value Ptwrite(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  int64_t v = 0;
  if (argc > 0) napi_get_value_int64(env, argv[0], &v);
  uint64_t x = (uint64_t)v;
  __asm__ volatile(".byte 0xf3,0x48,0x0f,0xae,0xe7" ::"D"(x));
  napi_value r;
  napi_get_undefined(env, &r);
  return r;
}


// Write the site map (docs/SPEC_FORMAT.md section 2, JIT profile: absolute addresses,
// plus per-record `obj`/`tsc` because a JIT address is only valid for a time interval).
static napi_value Sitemap(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  char path[512] = {0}; size_t n = 0;
  if (argc > 0) napi_get_value_string_utf8(env, argv[0], path, sizeof(path), &n);
  int64_t written = 0;
  if (n)
    written = ptj_write_sitemap(path, "v8", (int)getpid(), &g_cx, g_objs, g_cx.obj,
                                g_tslab, g_tslab_cap, g_tslab_used, g_use_ptwrite, g_space,
                                g_keyframe);
  ptj_gt_close(&g_gt);      // truncate the ground-truth ring to what was written
  napi_value r; napi_create_int64(env, written, &r); return r;
}

// drain(timeout_ms) -- wait (JS thread) until no job is queued or being analysed, then
// install every finished plan.  Returns the jobs still pending.  NOT used inside a timed window.
static napi_value Drain(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value argv[1]; int64_t to = 60000;
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc > 0) napi_get_value_int64(env, argv[0], &to);
  g_aj.drain_calls++;
  if (g_async) {
    struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += to / 1000; dl.tv_nsec += (to % 1000) * 1000000;
    if (dl.tv_nsec >= 1000000000) { dl.tv_sec++; dl.tv_nsec -= 1000000000; }
    pthread_mutex_lock(&g_qlock);
    for (;;) {
      int busy = g_qbusy || g_qhead;
      if (!busy) break;
      if (pthread_cond_timedwait(&g_qdone, &g_qlock, &dl)) break;
    }
    pthread_mutex_unlock(&g_qlock);
    install_ready();
  }
  uint64_t q = 0, b = 0, r = 0; async_counts(&q, &b, &r);
  napi_value v; napi_create_double(env, (double)(q + b + r), &v); return v;
}
static napi_value Stop(napi_env env, napi_callback_info info) {
  if (g_worker_up) async_stop();
  napi_value r; napi_get_undefined(env, &r); return r;
}

static napi_value Init(napi_env env, napi_value exports) {
  napi_value fn;
  napi_create_function(env, nullptr, 0, Enable, nullptr, &fn);
  napi_set_named_property(env, exports, "enable", fn);
  napi_create_function(env, nullptr, 0, Disable, nullptr, &fn);
  napi_set_named_property(env, exports, "disable", fn);
  napi_create_function(env, nullptr, 0, Stats, nullptr, &fn);
  napi_set_named_property(env, exports, "stats", fn);
  napi_create_function(env, nullptr, 0, Flush, nullptr, &fn);
  napi_set_named_property(env, exports, "flush", fn);
  napi_create_function(env, nullptr, 0, Verify, nullptr, &fn);
  napi_set_named_property(env, exports, "verify", fn);
  napi_create_function(env, nullptr, 0, CountersFn, nullptr, &fn);
  napi_set_named_property(env, exports, "counters", fn);
  napi_create_function(env, nullptr, 0, Ptwrite, nullptr, &fn);
  napi_set_named_property(env, exports, "ptwrite", fn);
  napi_create_function(env, nullptr, 0, Sitemap, nullptr, &fn);
  napi_set_named_property(env, exports, "sitemap", fn);
  napi_create_function(env, nullptr, 0, Drain, nullptr, &fn);
  napi_set_named_property(env, exports, "drain", fn);
  napi_create_function(env, nullptr, 0, Stop, nullptr, &fn);
  napi_set_named_property(env, exports, "stop", fn);
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
