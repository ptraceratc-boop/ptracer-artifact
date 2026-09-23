// jvmtiagent.cc -- PTracer v2, Stage 2 for HotSpot JIT code (decision D9, the Java half).
//
// A JVMTI agent (`-agentpath:`) that, on every `CompiledMethodLoad` and
// `DynamicCodeGenerated`, dumps the generated code (the `JTR1` jitdump of
// the design notes, asks the analyzer service for that code object's
// critical value set (content-hash cached, D7) and patches E9Patch-style trampolines
// into it -- exactly the pipeline `../jithook.cc` runs for V8, sharing `../jitsites.h`
// (analyzer client + cache), `../jitpatch.h` (window selection + trampoline emission)
// and `../jitdump.h` (the two output formats).
//
// The one thing that is *not* shared is how the detour is written.  V8 delivers
// `CODE_ADDED` before the code object's first execution; HotSpot posts
// `CompiledMethodLoad` on the **Service Thread after the nmethod is already reachable**
// the design notes, so an application thread may be executing the very bytes we
// are about to displace.  The install is therefore the Intel SDM cross-modifying-code
// protocol -- the same one HotSpot uses for its own not-entrant patching:
//
//     1. store `int3` over the window's first byte           (one atomic byte)
//     2. serialise every core (membarrier SYNC_CORE)         -- nobody can now start
//                                                               fetching the old window
//     3. write the remaining window bytes (rel32 + nop pad)
//     4. serialise every core
//     5. store the `jmp` opcode over the `int3`              (one atomic byte)
//     6. serialise every core
//
// Phases 1, 3 and 5 are batched over all the windows of one nmethod, so a method costs
// three `membarrier(2)` calls, not three per window.  Between phases 1 and 5 the window
// is a live breakpoint: a thread that reaches it takes `SIGTRAP`, and the agent's handler
// redirects it to the (already complete) trampoline.  The redirect is exact -- the
// trampoline logs the site's values, runs the relocated window and jumps back -- so the
// int3 phase is not a stall, it is just a slower path through the same detour.
//
// Build/run: see build.sh and the README in this directory.
#include <jvmti.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <errno.h>
#include <linux/membarrier.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unordered_map>
#include <unordered_set>

extern "C" {
#include <Zydis/Zydis.h>
}
#include "../jitsites.h"
#include "../jitpatch.h"
#include "../jitdump.h"
#include "hs_stub_entry.h"
#include "hs_cache_key.h"
#define PT_PIN_DEFINE_MARKER
#include "../../pinjit/jitbridge.h"

// ---------------------------------------------------------------- knobs -----
enum Mode { MODE_OFF = 0, MODE_COUNT = 1, MODE_DUMP = 2, MODE_ENTRY = 3, MODE_FULL = 4 };

static int g_mode = MODE_COUNT;
static int g_verbose = 0;
static int g_relative_avoid = 0; // opt-in, offsets rather than process-specific addresses
static int g_use_ptwrite = 1;
static int g_pinbridge = 0;
// Protected by g_lock, including publication versus JVMTI unload/reuse.
static uint64_t g_pin_objects = 0, g_pin_sites = 0, g_pin_retired = 0;
static uint64_t g_pin_objects_at_exit = 0, g_pin_sites_at_exit = 0;
static uint64_t g_pin_generation = 0;
static std::unordered_map<uint64_t, uint64_t> g_pin_live;
// D-J14: every jmethodID the VM has already compiled once.  Guarded by g_lock, which every
// CompiledMethodLoad already holds; jmethodIDs are stable for the life of the VM.
static std::unordered_set<uint64_t> g_seen_methods;
static void pin_publish(uint64_t operation, uint64_t base, uint64_t length,
                        PtjSite *sites = nullptr, uint64_t count = 0) {
  PtPinRequest request = {1, operation, base, length, (uint64_t)sites, count, 0, 0};
  ptj_pin_publish(&request);
  if (request.acknowledged != 1) {
    fprintf(stderr, "PTJAVA pin=1 requires the Pintool -jitbridge 1 acknowledgement\n");
    _exit(71);
  }
}
static int g_patch_interp = 0;       // native interp=1: legacy r13 probes; 2: full critical values
static int g_patch_stubs = 0;        // stubs=1: also patch the other DynamicCodeGenerated blobs
static uint64_t g_stub_secondary_entries = 0;
static int g_diag = 0;
static uint64_t g_min_patch = 0, g_max_patch = ~0ull, g_attempt = 0;
static uint32_t g_space = 3;
static const char *g_dumpfile = nullptr, *g_sitemapfile = nullptr;
static const char *g_statsfile = nullptr, *g_mapsfile = nullptr;
static size_t g_arena_mb = 1024, g_slab_mb = 16;
static FILE *g_hashdump = nullptr;   // "<masked hash> <addr> <len> <name>" per object
static uint32_t g_maxlen = 0;        // skip analysis of objects larger than this (0 = no cap)
static long g_drain_ms = 60000;      // how long VMDeath waits for the analysis pool
// Measurement-only the design notes: ctr=1 prefixes every trampoline with a
// flags-safe `incq' so a run reports how many times each trampoline EXECUTED; ctrdump=PATH
// writes `idx tramp_addr window_addr count' at VM death.  A counting configuration, not a
// timing one -- the prologue is 20 bytes of extra work per trampoline execution.
static int g_ctr_on = 0;
// instlog=PATH: `ns windows values' after every object that got at least one trampoline, so
// the harness can say WHEN patching converged (95/99 % of the windows installed) and which
// Renaissance iteration that was.  CLOCK_MONOTONIC, the same clock the harness stamps with.
static const char *g_instlog = nullptr;
#define PTJ_INSTLOG_MAX 262144
static uint64_t (*g_instlog_buf)[3] = nullptr;
static uint32_t g_instlog_n = 0;
static const char *g_ctrdump = nullptr;
static uint32_t g_ctr_max = 1u << 17;      // 128k counters = 1 MB at the head of the slab
// Keyframes (defect D-J1) and the same-run ground truth (D-J2): the design notes
static uint32_t g_keyframe = 1024;         // agent option `keyframe=K' (0 = the A/B control)
static uint32_t g_kfctr_max = 16384;       // `kfctr=N' countdown cells (1 MiB of slab)
static PtjGtRing g_gt;                     // `gt=1': the process's own address log
static const char *g_gtdir = ".";
static size_t g_gt_mb = 4096;

static inline uint64_t ns_now(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// ------------------------------------------------------------- counters -----
static struct {
  uint64_t load, unload, dyn, nmethod_bytes, dyn_bytes;
  uint64_t hook_ns, patch_ns, hash_ns, analysis_ns;
  uint64_t objs_analyzed, objs_patched, sites_req, sites_patched;
  uint64_t skipped_small, skipped_cap, skipped_type, skipped_nolo;
  uint64_t readd, superseded, unload_retired;
  uint64_t installs, int3_traps, foreign_traps, sync_calls;
  // D-J12b the design notes: the interior-quiescence phase of the install.
  uint64_t quiesce_rounds, quiesce_signals, quiesce_redirects, quiesce_timeouts,
           quiesce_unknown_rip, quiesce_ns;
  uint64_t selftest_int3, selftest_quiesce;      // 1 = passed (VM_INIT), 0 = not run/failed
  // D-J13 the design notes: HotSpot's IMPLICIT EXCEPTIONS inside a
  // DISPLACED instruction.  A relocated load/store/safepoint-poll that faults does so at a
  // pc inside our trampoline slab, which is not in the code cache, so the VM's signal
  // handler cannot map it to the nmethod's implicit-exception continuation and the process
  // dies.  `xlat_hits' counts the faults we translated back to the original instruction's
  // address before chaining to the VM's handler; `xlat_other' faults elsewhere in the slab
  // (the buffer sink's guard pages are these); `xlat_relmap' the size of the pc map.
  uint64_t xlat_hits, xlat_other, xlat_relmap, xlat_sigs;
  uint64_t ve_gt_20, ve_none;
  uint64_t locmap_entries, locmap_objs;
  uint64_t restart_roots, restart_objs;   // D-J6: analyzer restart roots seeded into the sweep
  uint64_t mprotect_calls, mprotect_fail;
  uint64_t entry_probe_tries;
  uint64_t interp_entries, interp_patched;
  uint64_t win_at_exit, val_at_exit, obj_at_exit, sites_at_exit;   // before the VMDeath drain
  // --- the `avoid` re-solve loop (analyzer v2.18) -----------------------------------
  uint64_t plan_ns, plan_passes;        // cost of the non-installing planning passes
  uint64_t avoid_objs;                  // objects that needed at least one re-solve
  uint64_t avoid_rounds_used;           // total extra analyzer rounds spent
  uint64_t avoid_addrs;                 // addresses fed back to the solver
  uint64_t avoid_recovered;             // sites placed that round 0 would have dropped
  uint64_t avoid_unplaceable;           // sites still unplaceable when the loop gave up
  uint64_t avoid_baseline_unpl;         // ... and what round 0 alone would have dropped
  uint64_t avoid_converged;             // objects that reached 0 unplaceable sites
  // --- when did patching converge?  CLOCK_MONOTONIC, comparable with the harness's
  // own time.monotonic() stamps on Renaissance's `iteration N completed' lines.
  uint64_t agent_load_ns, first_install_ns, last_install_ns;
  // --- DEFECT D-J3.2: how long after its CODE_ADDED is an nmethod actually patched?
  // A Java workload shorter than that is measured on an UNPATCHED JVM, whatever the
  // offline stage does, so this has to be a reported number and not a footnote.
  uint64_t recompiles;                     // D-J14: events for a method compiled before
  uint64_t lat_n, lat_sum, lat_max;        // CODE_ADDED -> the detour is installed
  uint64_t qlat_sum, qlat_max;             // ... of which spent waiting in the job queue
} g_c;
static uint64_t g_lat_sample[8192]; static uint32_t g_nlat = 0;
static pthread_mutex_t g_latlock = PTHREAD_MUTEX_INITIALIZER;
static void note_latency(uint64_t enq_ns, uint64_t qwait) {
  uint64_t now = ns_now();
  uint64_t lat = now - enq_ns, q = qwait;
  pthread_mutex_lock(&g_latlock);
  g_c.lat_n++; g_c.lat_sum += lat; if (lat > g_c.lat_max) g_c.lat_max = lat;
  g_c.qlat_sum += q; if (q > g_c.qlat_max) g_c.qlat_max = q;
  if (g_nlat < 8192) g_lat_sample[g_nlat++] = lat;
  pthread_mutex_unlock(&g_latlock);
}

// --- D-J14 (2026-09-22): WHERE the publication latency goes ------------------------------
// `patch_latency_ms' said only "mean 2.5 s, of which 2.4 s was queue wait".  That is not a
// fix, it is a symptom, so the path is split into the six stages a change could aim at:
//   evt     the CompiledMethodLoad handler: name lookup, the jitdump record, the snapshot
//   queue   waiting on a job queue (summed over both lanes)
//   hash    the masked content hash and the inline-data scan
//   cache   the in-memory and on-disk content-hash cache lookup
//   anal    the analyzer-service round trip -- zero when the cache answered
//   pub     pin_publish: the Pintool's map update and PIN_RemoveInstrumentationInRange
// Split by whether the CACHE or the ANALYZER answered, because those two populations have
// completely different service times and mixing them is what hid the head-of-line blocking.
struct StageTL { uint64_t hash, cache, anal, pub; };
static thread_local StageTL g_sgtl;

// D-J14 diagnosis: one line per published object, so the Pintool's `-jitmiss 3' addresses can
// be turned into names.  Written beside the stats file; diagnostic, off unless `objlog=' is set.
struct ObjRec { uint64_t addr, len, pub_ns, lat_ns, q_ns, a_ns, c_ns; char name[96]; };
static ObjRec *g_objlog = nullptr; static uint32_t g_objlog_n = 0, g_objlog_max = 0;
static pthread_mutex_t g_objloglock = PTHREAD_MUTEX_INITIALIZER;
static const char *g_objlogfile = nullptr;
static void obj_log(uint64_t addr, uint32_t len, uint64_t lat, uint64_t qw, const char *name) {
  if (!g_objlog) return;
  pthread_mutex_lock(&g_objloglock);
  if (g_objlog_n < g_objlog_max) {
    ObjRec *r = &g_objlog[g_objlog_n++];
    r->addr = addr; r->len = len; r->pub_ns = ns_now(); r->lat_ns = lat;
    r->q_ns = qw; r->a_ns = g_sgtl.anal; r->c_ns = g_sgtl.cache;
    snprintf(r->name, sizeof r->name, "%s", name ? name : "");
  }
  pthread_mutex_unlock(&g_objloglock);
}
static void write_objlog(void) {
  if (!g_objlog || !g_objlogfile) return;
  FILE *f = fopen(g_objlogfile, "w");
  if (!f) return;
  fprintf(f, "# addr len publish_ns latency_ns queue_ns analyze_ns cache_ns name   (agent_load_ns=%llu)\n",
          (unsigned long long)g_c.agent_load_ns);
  for (uint32_t i = 0; i < g_objlog_n; i++)
    fprintf(f, "%llu %llu %llu %llu %llu %llu %llu %s\n", (unsigned long long)g_objlog[i].addr,
            (unsigned long long)g_objlog[i].len, (unsigned long long)g_objlog[i].pub_ns,
            (unsigned long long)g_objlog[i].lat_ns, (unsigned long long)g_objlog[i].q_ns,
            (unsigned long long)g_objlog[i].a_ns, (unsigned long long)g_objlog[i].c_ns,
            g_objlog[i].name);
  fclose(f);
}

struct StageSum { uint64_t n, evt, queue, hash, cache, anal, pub, total; };
static StageSum g_sg_hit, g_sg_miss;
static pthread_mutex_t g_sglock = PTHREAD_MUTEX_INITIALIZER;
static void stage_reset(void) { g_sgtl.hash = g_sgtl.cache = g_sgtl.anal = g_sgtl.pub = 0; }
static void stage_note(uint64_t ev_ns, uint64_t enq_ns, uint64_t qwait) {
  uint64_t now = ns_now();
  StageSum *s = g_sgtl.anal ? &g_sg_miss : &g_sg_hit;
  pthread_mutex_lock(&g_sglock);
  s->n++;
  s->evt += (enq_ns > ev_ns) ? enq_ns - ev_ns : 0;
  s->queue += qwait;
  s->hash += g_sgtl.hash; s->cache += g_sgtl.cache;
  s->anal += g_sgtl.anal; s->pub += g_sgtl.pub;
  s->total += (now > ev_ns) ? now - ev_ns : 0;
  pthread_mutex_unlock(&g_sglock);
}
static uint64_t lat_pct(int pct) {
  if (!g_nlat) return 0;
  static uint64_t sorted[8192]; static uint32_t n = 0;
  if (n != g_nlat) { n = g_nlat; memcpy(sorted, g_lat_sample, (size_t)n * 8);
    for (uint32_t i = 1; i < n; i++) { uint64_t v = sorted[i]; uint32_t j = i;
      while (j && sorted[j-1] > v) { sorted[j] = sorted[j-1]; j--; } sorted[j] = v; } }
  uint32_t k = (uint32_t)((uint64_t)pct * (n - 1) / 100);
  return sorted[k];
}

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_event_threads = 0;
static char g_event_thread_names[8][32];


// ------------------------------------------------------ core serialisation --
// MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE exists for exactly this: it IPIs every core
// running a thread of this process and forces a context-synchronising event there, which
// is what the SDM's cross-modifying-code sequence requires between the three stores.
static int g_have_sync_core = 0;
static void sync_cores(void) {
  if (!g_have_sync_core) return;
  syscall(__NR_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0);
  g_c.sync_calls++;
}
static void sync_core_init(void) {
  long q = syscall(__NR_membarrier, MEMBARRIER_CMD_QUERY, 0, 0);
  if (q > 0 && (q & MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE) &&
      syscall(__NR_membarrier, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0) == 0)
    g_have_sync_core = 1;
}

// ------------------------------------------------------------- the arena ----
static PtjArena g_dump;

// ------------------------------------------------------- trampoline slab ----
static uint8_t *g_tslab = nullptr;
static size_t g_tslab_cap = 0, g_tslab_used = 0, g_tslab_live = 0;
static const size_t TSLAB_CHUNK = 1u << 20;

// Only the pages that hold trampolines are RWX; the rest stays PROT_NONE and the live part
// is int3-filled, so a detour that ever pointed at unused slab faults immediately instead
// of running into zeros (the safety net of jit_runtime.md §6b-fix).
static void tslab_commit(size_t upto) {
  if (upto <= g_tslab_live || !g_tslab) return;
  size_t want = (upto + TSLAB_CHUNK - 1) & ~(TSLAB_CHUNK - 1);
  if (want > g_tslab_cap) want = g_tslab_cap;
  if (mprotect(g_tslab + g_tslab_live, want - g_tslab_live,
               PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
    memset(g_tslab + g_tslab_live, 0xcc, want - g_tslab_live);
    g_tslab_live = want;
  }
}

// The detour is a 5-byte `jmp rel32`, so the slab must be within +-2GB of every code
// object it serves.  A hinted mmap is not good enough here: the JVM reserves the heap, the
// compressed-class space and the code cache as one contiguous block, and a hint inside it
// is silently relocated to the process's mmap base, which is out of reach.  So read
// /proc/self/maps and place the slab in the first free gap within +-2GB, with
// MAP_FIXED_NOREPLACE so the kernel cannot move it.
static int tslab_try(uint64_t near, size_t cap);
static void ctr_init_slab(void);   // ctr=1: carve the counter array out of the slab head
static void kf_init_slab(void);    // keyframe counters + gt cursor, likewise (D-J1/D-J2)

// The JVM reserves the heap, the compressed-class space and the code cache as one block,
// and on some ASLR layouts there is no 64MB hole left within +-2GB of the code cache at
// all (measured: `cands=0`).  So ask for the requested size, then halve until something
// fits -- a few hundred KB of trampolines is all we ever use.
static pthread_mutex_t g_slablock = PTHREAD_MUTEX_INITIALIZER;
static void tslab_init_near(uint64_t near, size_t cap) {
  if (g_tslab) return;
  pthread_mutex_lock(&g_slablock);
  if (!g_tslab) {
    if (!cap) cap = 16u << 20;
    for (size_t c = cap; c >= (1u << 20); c >>= 1)
      if (tslab_try(near, c)) break;
  }
  pthread_mutex_unlock(&g_slablock);
}

static int tslab_try(uint64_t near, size_t cap) {
  const uint64_t REACH = 0x7e000000ull;              // stay clear of the 2GB rel32 limit
  uint64_t win_lo = (near > REACH) ? near - REACH : 0x10000;
  uint64_t win_hi = near + REACH;
  FILE *f = fopen("/proc/self/maps", "r");
  if (!f) return 0;
  char line[512];
  uint64_t prev_end = win_lo;
  uint64_t cand[64]; int ncand = 0;
  while (fgets(line, sizeof line, f) && ncand < 64) {
    uint64_t lo, hi;
    if (sscanf(line, "%lx-%lx", &lo, &hi) != 2) continue;
    if (hi <= win_lo) continue;
    if (lo >= win_hi) break;
    if (lo > prev_end && lo - prev_end >= cap + (1u << 20)) {
      uint64_t a = (prev_end + 0xfffff) & ~(uint64_t)0xfffff;
      if (a + cap <= lo) cand[ncand++] = a;
    }
    if (hi > prev_end) prev_end = hi;
  }
  if (prev_end < win_hi && win_hi - prev_end >= cap + (1u << 20) && ncand < 64)
    cand[ncand++] = (prev_end + 0xfffff) & ~(uint64_t)0xfffff;
  fclose(f);
  if (g_verbose) fprintf(stderr, "PTJAVA slab search near=%#lx win=[%#lx,%#lx) cands=%d\n",
                         near, win_lo, win_hi, ncand);
  // Prefer the gap nearest the code object (shortest rel32, and it keeps the slab out of
  // the way of whatever the VM maps next).
  for (int pass = 0; pass < ncand; pass++) {
    int best = -1; uint64_t bd = ~0ull;
    for (int i = 0; i < ncand; i++) {
      if (!cand[i]) continue;
      uint64_t d = cand[i] > near ? cand[i] - near : near - cand[i];
      if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) break;
    uint64_t a = cand[best]; cand[best] = 0;
    void *m = mmap((void *)a, cap, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (m == MAP_FAILED) continue;
    int64_t d1 = (int64_t)((uint64_t)m - near), d2 = d1 + (int64_t)cap;
    if (g_verbose) fprintf(stderr, "PTJAVA   try %#lx -> %p d1=%lld\n", a, m, (long long)d1);
    if ((uint64_t)m == a && d1 > -0x7f000000LL && d2 < 0x7f000000LL) {
      g_tslab = (uint8_t *)m; g_tslab_cap = cap; g_tslab_used = 0; g_tslab_live = 0;
      tslab_commit(TSLAB_CHUNK < cap ? TSLAB_CHUNK : cap);
      ctr_init_slab();
      kf_init_slab();
      return 1;
    }
    munmap(m, cap);
  }
  return 0;
}

// ------------------------------------------------- writable code cache ------
// HotSpot maps the code cache RWX by default, but do not assume it: remember the
// [start,end) of every region we have made writable and mprotect the rest on demand.
struct WRange { uint64_t lo, hi; };
static WRange g_wr[64]; static int g_nwr = 0;
static int code_is_writable(uint64_t a) {
  for (int i = 0; i < g_nwr; i++) if (a >= g_wr[i].lo && a < g_wr[i].hi) return 1;
  return 0;
}
static void ensure_writable(uint64_t a, size_t len) {
  if (code_is_writable(a) && code_is_writable(a + len - 1)) return;
  FILE *f = fopen("/proc/self/maps", "r");
  if (!f) return;
  char line[512];
  while (fgets(line, sizeof line, f)) {
    uint64_t lo, hi; char perms[8];
    if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3) continue;
    if (a < lo || a >= hi) continue;
    if (perms[1] != 'w') {
      g_c.mprotect_calls++;
      if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        g_c.mprotect_fail++; fclose(f); return;
      }
    }
    if (g_nwr < 64) { g_wr[g_nwr].lo = lo; g_wr[g_nwr].hi = hi; g_nwr++; }
    break;
  }
  fclose(f);
}

// --------------------------------------------------------- patch records ----
// One per installed window: what we wrote, where, and which trampoline it goes to.
// Doubles as (a) the SIGTRAP redirect table and (b) the exit-time `verify()` table.
struct PatchRec {
  uint64_t addr, tramp, tsc;
  uint64_t obj_addr; uint32_t obj_len, obj_idx, off;
  uint8_t orig[32], want[32], wlen, dead;
};
static PatchRec *g_prec = nullptr;
static uint64_t g_nprec = 0;
static const size_t PREC_MAX = 1u << 21;

// Lock-free address -> trampoline map read by the SIGTRAP handler.  Insert-only.
static const uint32_t WMAP_CAP = 1u << 21;
static uint64_t *g_wkey = nullptr, *g_wval = nullptr;
static inline uint32_t wmap_h(uint64_t a) { return (uint32_t)((a * 0x9e3779b97f4a7c15ull) >> 43) & (WMAP_CAP - 1); }
static void wmap_put(uint64_t a, uint64_t t) {
  if (!g_wkey) return;
  uint32_t i = wmap_h(a);
  for (uint32_t n = 0; n < WMAP_CAP; n++) {
    uint32_t s = (i + n) & (WMAP_CAP - 1);
    if (g_wkey[s] == 0 || g_wkey[s] == a) {
      __atomic_store_n(&g_wval[s], t, __ATOMIC_RELEASE);
      __atomic_store_n(&g_wkey[s], a, __ATOMIC_RELEASE);
      return;
    }
  }
}
static uint64_t wmap_get(uint64_t a) {
  if (!g_wkey) return 0;
  uint32_t i = wmap_h(a);
  for (uint32_t n = 0; n < WMAP_CAP; n++) {
    uint32_t s = (i + n) & (WMAP_CAP - 1);
    uint64_t k = __atomic_load_n(&g_wkey[s], __ATOMIC_ACQUIRE);
    if (k == 0) return 0;
    if (k == a) return __atomic_load_n(&g_wval[s], __ATOMIC_ACQUIRE);
  }
  return 0;
}

// ------------------------------------------------- the SIGTRAP redirect -----
static struct sigaction g_old_trap;
static void trap_handler(int sig, siginfo_t *si, void *uc) {
  ucontext_t *u = (ucontext_t *)uc;
  uint64_t rip = (uint64_t)u->uc_mcontext.gregs[REG_RIP];
  // an `int3` that we planted: rip is one past the breakpoint byte
  uint64_t t = wmap_get(rip - 1);
  if (t) {
    __atomic_fetch_add(&g_c.int3_traps, 1, __ATOMIC_RELAXED);
    u->uc_mcontext.gregs[REG_RIP] = (greg_t)t;   // straight into the finished trampoline
    return;
  }
  __atomic_fetch_add(&g_c.foreign_traps, 1, __ATOMIC_RELAXED);
  if (g_old_trap.sa_flags & SA_SIGINFO) { g_old_trap.sa_sigaction(sig, si, uc); return; }
  if (g_old_trap.sa_handler != SIG_DFL && g_old_trap.sa_handler != SIG_IGN) {
    g_old_trap.sa_handler(sig); return;
  }
  signal(SIGTRAP, SIG_DFL);
  raise(SIGTRAP);
}
static void trap_install(void) {
  struct sigaction sa; memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = trap_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTRAP, &sa, &g_old_trap);
}

// ------------------------------------------- the two-phase atomic install ---
// ptj_patch_object calls `hs_install` once per window; it performs phase 1 (the int3) and
// queues the finished bytes.  `hs_commit` then runs phases 2-6 for the whole code object.
// D-J12b the design notes: a window is usually SEVERAL instructions, and the
// SDM's int3 protocol only covers one.  A thread whose rip sits on an INTERIOR instruction
// boundary of the window when phase 3 overwrites those bytes (descheduled there, parked in a
// page fault or a ptrace stop, ...) resumes into the rel32 -- seen as `NewVectorIterator.
// advanceA' dying at site+4 and `AccessHistory.recordRead' at site+3.  Neither phase 2 nor any
// number of serialising IPIs can find such a thread: only the thread itself can report its
// rip.  So between phases 2 and 3 every other thread of the process is sent `g_qsig' and
// its handler checks the rip against the pending windows' interior boundaries; a thread that
// is on one is moved to the RELOCATED COPY of that instruction inside the finished
// trampoline (exact: the copy is what the detour would have executed there), everyone acks,
// and phase 3 runs only once every thread has answered.  After a thread's handler ran it can
// only enter the window through the int3 at its first byte, so the invariant holds.
struct PendWin { uint8_t *at; uint8_t bytes[32]; uint32_t n; uint64_t tramp;
                 uint32_t nint; uint32_t ioff[16]; uint64_t itramp[16]; };
static PendWin g_pend[4096];
static uint32_t g_npend = 0;
static uint8_t g_last_want[32]; static uint32_t g_last_wlen = 0;
static int g_quiesce = 1;                 // agent option `quiesce=0' disables the phase (A/B)
static int g_qsig = 0;                    // `qsig=N'; default SIGRTMAX-3
static int g_selftest = 1;                // `selftest=0': do not abort on a swallowed int3
static volatile uint64_t g_pend_lo = 0, g_pend_hi = 0;   // [lo,hi) hull of the pending windows
static volatile uint32_t g_q_gen = 0;     // generation carried in si_value
static uint32_t g_q_acks = 0;

static const PtjReloc *relocs_tail(uint32_t *n);   // defined after g_cx
static int hs_install(uint8_t *at, const uint8_t *bytes, uint32_t n, uint64_t tramp) {
  if (g_npend >= 4096) return 0;
  PendWin *w = &g_pend[g_npend];
  w->at = at; w->n = n; memcpy(w->bytes, bytes, n); w->tramp = tramp; w->nint = 0;
  // The interior boundaries -> their relocated copies.  ptj_patch_object appended one
  // PtjReloc per displaced instruction of THIS window just before calling us, in address
  // order, so they are the tail of cx->relocs.
  if (g_quiesce) {
    uint32_t nrel = 0; const PtjReloc *rel = relocs_tail(&nrel);
    if (!rel) return 0;                            // cannot make the window safe: refuse it
    for (uint32_t r = nrel; r > 0; r--) {
      const PtjReloc *R = &rel[r - 1];
      if (R->orig_addr < (uint64_t)at) break;
      if (R->orig_addr >= (uint64_t)at + n) continue;
      if (R->orig_addr == (uint64_t)at) continue;  // the head: the int3 / jmp itself
      if (w->nint >= 16) return 0;
      w->ioff[w->nint] = (uint32_t)(R->orig_addr - (uint64_t)at);
      w->itramp[w->nint] = R->tramp_addr; w->nint++;
    }
  }
  g_npend++;
  // The redirect must be visible before the breakpoint is.
  wmap_put((uint64_t)at, tramp);
  memcpy(g_last_want, bytes, n); g_last_wlen = n;
  __atomic_store_n(at, (uint8_t)0xcc, __ATOMIC_SEQ_CST);   // phase 1
  g_c.installs++;
  return 1;
}

// The quiescence handler: async-signal-safe, reads only the pending table (which the
// committing thread does not touch while it waits) and the ucontext.
static void quiesce_handler(int sig, siginfo_t *si, void *ucv) {
  (void)sig;
  if (!si || si->si_code != SI_QUEUE || (uint32_t)si->si_value.sival_int != g_q_gen) return;
  ucontext_t *u = (ucontext_t *)ucv;
  uint64_t rip = (uint64_t)u->uc_mcontext.gregs[REG_RIP];
  if (rip > g_pend_lo && rip < g_pend_hi) {
    for (uint32_t i = 0; i < g_npend; i++) {
      const PendWin *w = &g_pend[i];
      uint64_t at = (uint64_t)(uintptr_t)w->at;
      if (rip <= at || rip >= at + w->n) continue;
      uint32_t k = 0;
      for (; k < w->nint; k++)
        if (at + w->ioff[k] == rip) {
          u->uc_mcontext.gregs[REG_RIP] = (greg_t)w->itramp[k];
          __atomic_fetch_add(&g_c.quiesce_redirects, 1, __ATOMIC_RELAXED);
          break;
        }
      if (k == w->nint) __atomic_fetch_add(&g_c.quiesce_unknown_rip, 1, __ATOMIC_RELAXED);
      break;
    }
  }
  __atomic_fetch_add(&g_q_acks, 1, __ATOMIC_RELEASE);
}
static void quiesce_install(void) {
  if (!g_qsig) g_qsig = SIGRTMAX - 3;
  struct sigaction sa; memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = quiesce_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sigaction(g_qsig, &sa, nullptr);
}
// Signal every other thread and wait for every ack.  Returns the number of threads that did
// not answer within `timeout_ms'.
static uint32_t quiesce_all(int timeout_ms) {
  uint64_t t0 = ptj_ns();
  uint32_t gen = __atomic_add_fetch(&g_q_gen, 1, __ATOMIC_SEQ_CST);
  __atomic_store_n(&g_q_acks, 0, __ATOMIC_SEQ_CST);
  pid_t self = (pid_t)syscall(SYS_gettid), pid = getpid();
  static uint64_t tids[65536]; uint32_t ntid = 0;
  DIR *dp = opendir("/proc/self/task");
  if (!dp) return 0;
  struct dirent *de;
  while ((de = readdir(dp)) && ntid < 65536) {
    if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
    uint64_t tid = strtoull(de->d_name, nullptr, 10);
    if ((pid_t)tid == self) continue;
    siginfo_t si; memset(&si, 0, sizeof si);
    si.si_signo = g_qsig; si.si_code = SI_QUEUE; si.si_pid = pid; si.si_uid = getuid();
    si.si_value.sival_int = (int)gen;
    if (syscall(SYS_rt_tgsigqueueinfo, pid, (pid_t)tid, g_qsig, &si) == 0) tids[ntid++] = tid;
  }
  closedir(dp);
  g_c.quiesce_rounds++; g_c.quiesce_signals += ntid;
  uint64_t deadline = t0 + (uint64_t)timeout_ms * 1000000ull;
  uint32_t missing = 0;
  for (;;) {
    if (__atomic_load_n(&g_q_acks, __ATOMIC_ACQUIRE) >= ntid) break;
    if (ptj_ns() > deadline) {
      // Threads that exited meanwhile never ack; drop those, and remember the rest.
      uint32_t alive = 0;
      for (uint32_t i = 0; i < ntid; i++) {
        char pth[64]; snprintf(pth, sizeof pth, "/proc/self/task/%llu", (unsigned long long)tids[i]);
        struct stat st; if (stat(pth, &st) == 0) alive++;
      }
      if (__atomic_load_n(&g_q_acks, __ATOMIC_ACQUIRE) >= alive) break;
      missing = alive - __atomic_load_n(&g_q_acks, __ATOMIC_ACQUIRE);
      g_c.quiesce_timeouts++;
      // A timeout means a live thread is not running signal handlers (a debugger stop, a
      // thread that blocks the signal); phase 3 proceeds at the pre-D-J12b risk for this
      // commit and the run reports it (`quiesce_timeouts').
      if (g_verbose) fprintf(stderr, "PTJAVA quiesce timeout: %u of %u threads silent after %d ms\n",
                             missing, ntid, timeout_ms);
      break;
    }
    struct timespec ts = {0, 20000}; nanosleep(&ts, nullptr);
  }
  g_c.quiesce_ns += ptj_ns() - t0;
  return missing;
}

static void hs_commit(void) {
  if (!g_npend) return;
  sync_cores();                                            // phase 2
  if (g_quiesce) {                                         // phase 2b: interior quiescence
    uint64_t lo = ~0ull, hi = 0;
    for (uint32_t i = 0; i < g_npend; i++) {
      uint64_t a = (uint64_t)(uintptr_t)g_pend[i].at;
      if (a < lo) lo = a;
      if (a + g_pend[i].n > hi) hi = a + g_pend[i].n;
    }
    g_pend_lo = lo; g_pend_hi = hi;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    quiesce_all(2000);
  }
  for (uint32_t i = 0; i < g_npend; i++)                   // phase 3
    memcpy(g_pend[i].at + 1, g_pend[i].bytes + 1, g_pend[i].n - 1);
  sync_cores();                                            // phase 4
  for (uint32_t i = 0; i < g_npend; i++)                   // phase 5
    __atomic_store_n(g_pend[i].at, g_pend[i].bytes[0], __ATOMIC_SEQ_CST);
  sync_cores();                                            // phase 6
  g_pend_lo = g_pend_hi = 0;
  g_npend = 0;
}

// D-J12a: is a planted `int3' actually delivered to trap_handler?  A ptrace tracer sees the
// SIGTRAP first and may suppress it (pt_capture2 --sideband did, until 2026-09-17): the thread
// then resumes one byte into the window.  Nothing inside the process can repair that, so the
// install protocol is unsafe under such a tracer and the run must not proceed silently.
static int int3_selftest(void) {
  uint8_t *pg = (uint8_t *)mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pg == MAP_FAILED) return -1;
  pg[0] = 0xcc; pg[1] = 0xc3;                              // int3 ; ret
  wmap_put((uint64_t)(uintptr_t)pg, (uint64_t)(uintptr_t)pg + 1);
  uint64_t before = g_c.int3_traps;
  ((void (*)(void))pg)();
  int ok = g_c.int3_traps == before + 1;
  g_c.int3_traps = before;                                 // the probe is not a real trap
  munmap(pg, 4096);
  return ok;
}

// --------------------------------------------------- analyzer + patch ctx ---
static ZydisDecoder g_dec;
static PtjClient g_cli;
static PtjPatchCtx g_cx;
static const PtjReloc *relocs_tail(uint32_t *n) { *n = g_cx.nrelocs; return g_cx.relocs; }

// ------------------------------------------------- D-J13: implicit exceptions ----
// HotSpot does not test for null before most field loads, does not test the safepoint poll
// word, and does not test a divisor for zero: it lets the access FAULT and turns the signal
// into the right event by looking the faulting pc up in the nmethod's implicit-exception
// table (`nmethod::continuation_for_implicit_exception').  That lookup is keyed by pc, and
// a pc only resolves if it is inside the code cache.
//
// Our full-site detour moves the faulting instruction OUT of the nmethod: the relocated copy
// lives in the trampoline slab, an anonymous RWX mapping the VM knows nothing about.  When
// such an instruction finally sees its null (the four `scala-kmeans -r1400' exhibits are
// `java.lang.ThreadLocal::get' loading `map.table' at +0x14 on a fresh shutdown-hook thread
// whose `threadLocals' is still NULL), the VM's handler finds no blob for the pc and reports
// a fatal SIGSEGV instead of throwing a NullPointerException / taking the uncommon trap.
//
// The fix is a translation: our handler runs in FRONT of the VM's, and when the faulting pc
// is the address of a relocated instruction it rewrites the context's rip to that
// instruction's ORIGINAL address before chaining.  The register and stack state at a
// relocated instruction is by construction identical to the state at the original (the
// site's push/pop are complete before it and after it), so the continuation the VM picks --
// a deoptimisation, an NPE throw, a safepoint -- unwinds exactly the frame it would have.
#define PTJ_RELMAP_BITS 19
#define PTJ_RELMAP_N    (1u << PTJ_RELMAP_BITS)
static uint64_t g_relmap_k[PTJ_RELMAP_N];      // relocated copy's address (0 = empty)
static uint64_t g_relmap_v[PTJ_RELMAP_N];      // the original instruction's address
static int g_xlat = 1;                         // agent option `xlat=0' disables it (A/B)
static struct sigaction g_old_sa[3];           // SEGV, BUS, FPE -- the VM's own handlers
static inline uint32_t relmap_h(uint64_t a) {
  a *= 0x9E3779B97F4A7C15ull;
  return (uint32_t)(a >> (64 - PTJ_RELMAP_BITS));
}
// Single writer (always under g_patchlock); lock-free readers, one of them a signal handler.
static void relmap_put(uint64_t k, uint64_t v) {
  uint32_t m = PTJ_RELMAP_N - 1, i = relmap_h(k) & m;
  for (uint32_t n = 0; n <= m; n++, i = (i + 1) & m) {
    uint64_t cur = __atomic_load_n(&g_relmap_k[i], __ATOMIC_RELAXED);
    if (cur == k) { __atomic_store_n(&g_relmap_v[i], v, __ATOMIC_RELAXED); return; }
    if (cur == 0) {
      __atomic_store_n(&g_relmap_v[i], v, __ATOMIC_RELAXED);
      __atomic_store_n(&g_relmap_k[i], k, __ATOMIC_RELEASE);
      g_c.xlat_relmap++;
      return;
    }
  }
}
static uint64_t relmap_get(uint64_t k) {
  uint32_t m = PTJ_RELMAP_N - 1, i = relmap_h(k) & m;
  for (uint32_t n = 0; n <= m; n++, i = (i + 1) & m) {
    uint64_t cur = __atomic_load_n(&g_relmap_k[i], __ATOMIC_ACQUIRE);
    if (cur == k) return __atomic_load_n(&g_relmap_v[i], __ATOMIC_RELAXED);
    if (cur == 0) return 0;
  }
  return 0;
}
static int xlat_slot(int sig) { return sig == SIGSEGV ? 0 : sig == SIGBUS ? 1 : 2; }
static void xlat_handler(int sig, siginfo_t *si, void *ucv) {
  ucontext_t *u = (ucontext_t *)ucv;
  uint64_t pc = (uint64_t)u->uc_mcontext.gregs[REG_RIP];
  __atomic_fetch_add(&g_c.xlat_sigs, 1, __ATOMIC_RELAXED);
  if (g_tslab && pc >= (uint64_t)(uintptr_t)g_tslab &&
      pc < (uint64_t)(uintptr_t)g_tslab + g_tslab_cap) {
    uint64_t orig = relmap_get(pc);
    if (orig) {
      u->uc_mcontext.gregs[REG_RIP] = (greg_t)orig;   // hand the VM the pc it can resolve
      __atomic_fetch_add(&g_c.xlat_hits, 1, __ATOMIC_RELAXED);
    } else {
      // Not a relocated instruction: a site's own code faulted.  The buffer sink's
      // ring-full guard page is exactly this and its handler is next in the chain.
      __atomic_fetch_add(&g_c.xlat_other, 1, __ATOMIC_RELAXED);
    }
  }
  const struct sigaction *old = &g_old_sa[xlat_slot(sig)];
  if (old->sa_flags & SA_SIGINFO) { if (old->sa_sigaction) old->sa_sigaction(sig, si, ucv); return; }
  if (old->sa_handler == SIG_IGN) return;
  if (old->sa_handler && old->sa_handler != SIG_DFL) { old->sa_handler(sig); return; }
  signal(sig, SIG_DFL);            // no chain target: let the default action happen on return
}
// Installed at VM_INIT, i.e. AFTER the VM's own SIGSEGV/SIGBUS/SIGFPE handlers (implicit
// null checks, stack banging, the polling page) and after the buffer sink re-armed its
// guard-page handler, so ours is outermost and both of those stay in the chain.
static void xlat_install(void) {
  static const int sigs[3] = {SIGSEGV, SIGBUS, SIGFPE};
  struct sigaction sa; memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = xlat_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK | SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  for (int i = 0; i < 3; i++) sigaction(sigs[i], &sa, &g_old_sa[i]);
}

// ctr=1 (measurement only).  The counters live at the head of the trampoline slab so the
// emitted `incq <slot>(%rip)' reaches them from any trampoline; tslab_commit() 0xcc-fills
// the committed pages, so they have to be zeroed here.
static void ctr_init_slab(void) {
  if (!g_ctr_on || !g_tslab || g_cx.ctr) return;
  size_t cb = (size_t)g_ctr_max * 8;
  if (cb >= g_tslab_cap) return;
  memset(g_tslab, 0, cb);
  g_cx.ctr_orig = (uint64_t *)calloc(g_ctr_max, 8);
  if (!g_cx.ctr_orig) return;
  g_cx.ctr = (uint64_t *)g_tslab;
  g_cx.ctr_max = g_ctr_max;
  g_tslab_used = cb;
}

// The keyframe countdown cells and the ground-truth ring cursor, carved out of the slab just
// after the (optional) `ctr' array for the same reason: the slab is the only mapping certain
// to be within +-2GB of every trampoline, and `dec CNT(%rip)' / `lock xadd %rcx,CUR(%rip)'
// have to reach them.  tslab_commit() 0xcc-fills the pages, so zero them here.
static void kf_init_slab(void) {
  if (!g_tslab || g_cx.gt_cur) return;
  uint32_t nctr = g_keyframe ? g_kfctr_max : 0;
  size_t db = ptj_slab_data_bytes(nctr);
  if (g_tslab_used + db + TSLAB_CHUNK >= g_tslab_cap) return;
  tslab_commit(g_tslab_used + db + TSLAB_CHUNK);
  ptj_slab_data_init(&g_cx, g_tslab + g_tslab_used, nctr);
  g_tslab_used += db;
  if (g_cx.gt) ptj_gt_arm(&g_gt, g_cx.gt_cur);
}
static int g_have_cli = 0;
static PtjObjRec *g_objs = nullptr;
static const uint32_t OBJCAP = 1u << 20;
#define PTJ_MAXSITES 8192
static PtjSite g_sitebuf[PTJ_MAXSITES];

// in-memory content-hash cache (D7), same shape as jithook.cc's
#define PTJ_KEYMAX 80          // 64 hex content hash + ".a" + 8 hex avoid hash + NUL
#define PTJ_MAXROOTS 1024      // D-J6: analyzer restart roots kept per code object
struct CacheEnt { char key[PTJ_KEYMAX]; PtjSite *sites; int n;
                  uint32_t *rroots; int nrroots; };
static CacheEnt *g_mem = nullptr;
static const uint32_t MEMCAP = 1u << 16;
static uint32_t key_hash(const char *k) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < PTJ_KEYMAX - 1 && k[i]; i++) { h ^= (uint8_t)k[i]; h *= 16777619u; }
  return h;
}
static CacheEnt *mem_find(const char *k, int insert) {
  if (!g_mem) return nullptr;
  uint32_t i = key_hash(k) & (MEMCAP - 1);
  for (uint32_t n = 0; n < MEMCAP; n++) {
    CacheEnt *c = &g_mem[(i + n) & (MEMCAP - 1)];
    if (!c->key[0]) {
      if (!insert) return nullptr;
      snprintf(c->key, PTJ_KEYMAX, "%s", k);
      return c;
    }
    if (!strcmp(c->key, k)) return c;
  }
  return nullptr;
}

// The cache key masks every field the VM may relocate: >=32-bit immediates (embedded oops,
// metadata, external references) and >=32-bit displacements.  Two compilations of the same
// method then hash the same however the heap moved (D7).
// D-J14.2: 0 = the old "give up at the first undecodable byte"; N > 0 = resynchronise and
// mask only after N consecutive successful decodes (see masked_hash).
static int g_mask_resync = 4;
static int g_mask_verify = 0;       // re-ask the analyzer on every cache hit and compare
static uint64_t g_mv_checked = 0, g_mv_same = 0, g_mv_diff = 0;
static uint64_t g_mv_self_same = 0, g_mv_self_diff = 0;
static uint64_t g_mv_same_values = 0, g_mv_diff_values = 0;
static thread_local uint8_t *g_maskbuf = nullptr;
static thread_local size_t g_maskcap = 0;
// D-J6: `data_from` (the object's inline-data boundary) and `roots` (the jvmtiAddrLocationMap)
// are part of the ANSWER the analyzer gives, so they are part of the key.  `data_from` in
// particular is derived from a rip-relative disp32, which the mask below zeroes out.
static void masked_hash(const uint8_t *code, size_t len, char out[65],
                        uint32_t data_from = 0, const uint32_t *roots = nullptr,
                        uint32_t nroots = 0) {
  if (len > g_maskcap) { g_maskbuf = (uint8_t *)realloc(g_maskbuf, len * 2); g_maskcap = len * 2; }
  if (!g_maskbuf) { out[0] = 0; return; }
  memcpy(g_maskbuf, code, len);
  // DEFECT D-J14.2.  This loop used to `break' at the first byte Zydis could not decode, and
  // a generated code object may START with data: HotSpot's template interpreter blob opens
  // with an 8-byte pointer, so the decode died at offset 0xd and the remaining 97 743 bytes
  // were hashed RAW -- every embedded `call rel32' and `movabs imm64' included.  Those fields
  // move with the code cache, so the interpreter's content hash was different on every run:
  // 13 byte-identical answers sat in the cache under 13 keys and the blob was re-analysed
  // from scratch every time, for 15.6 s of the four reserved analyzer cores.  Resynchronise
  // instead: on a failure advance ONE byte and keep going.
  //
  // Why this cannot mask more than it should: a byte is only zeroed when it lies in the
  // >=32-bit immediate or displacement field of an instruction Zydis decoded, which is the
  // same rule as before -- it is now simply applied to the whole object instead of to its
  // first decodable prefix.  Masking more bytes makes the key COARSER, so the risk it adds
  // is a false cache HIT (two different objects sharing a site list), not a false miss.
  // `maskresync=0' is the A/B control and `maskverify=1' re-asks the analyzer on every hit
  // and compares, which is how the coarsening was checked the design notes.
  //
  // The resync is CONFIDENCE-GATED.  Resuming one byte after a failure can land mid-data, and
  // masking there zeroes bytes that are not immediates at all -- which makes the key coarser
  // than intended and two different small methods can then share a site list.  x86 re-syncs
  // within a few instructions, so nothing is masked until `g_mask_resync' consecutive decodes
  // have succeeded since the last failure.  Measured effect of the gate: the fraction of cache
  // hits whose site list differs from a fresh analyzer answer (`maskverify') goes back to the
  // level the old key already had, while the interpreter stays a single cache entry.
  size_t off = 0;
  int conf = g_mask_resync ? g_mask_resync : 1;   // confident from offset 0
  while (off < len) {
    ZydisDecodedInstruction ins;
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&g_dec, code + off, len - off, &ins, ops,
                                             ZYDIS_MAX_OPERAND_COUNT, 0))) {
      if (!g_mask_resync) break;
      conf = 0; off++; continue;
    }
    if (conf < g_mask_resync) { conf++; off += ins.length; continue; }
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
  ptj_sha_update(&sh, &data_from, 4);
  if (roots && nroots) ptj_sha_update(&sh, roots, (size_t)nroots * 4);
  ptj_sha_hex(&sh, out);
}

// -------------------------------------------------- live-object bookkeeping -
// HotSpot never *moves* an nmethod (there is no compacting code cache, hence no
// CompiledMethodMove event), so the V8 CODE_MOVED repair has no analogue here.  What it
// does do is *free* one (CompiledMethodUnload, or a sweep we are not told about) and reuse
// the address, so the table is time keyed exactly as in V8: an overlapping new object, or
// an unload, retires the old records.
struct LiveObj { uint64_t addr; uint32_t len; uint64_t first_prec; uint32_t n_prec; uint8_t dead; };
static LiveObj *g_live = nullptr;
static uint32_t g_nlive = 0;
static const uint32_t LIVECAP = 1u << 20;

static void live_retire(uint64_t lo, uint64_t hi, uint64_t *counter) {
  if (!g_live) return;
  for (uint32_t i = 0; i < g_nlive; i++) {
    LiveObj *L = &g_live[i];
    if (L->dead) continue;
    if (lo < L->addr + L->len && hi > L->addr) {
      L->dead = 1;
      for (uint64_t k = L->first_prec; k < L->first_prec + L->n_prec && g_prec; k++)
        g_prec[k].dead = 1;
      (*counter)++;
    }
  }
}
static void live_note(uint64_t addr, uint32_t len, uint64_t first) {
  if (!g_live || g_nlive >= LIVECAP || g_nprec == first) return;
  LiveObj *L = &g_live[g_nlive++];
  L->addr = addr; L->len = len; L->first_prec = first;
  L->n_prec = (uint32_t)(g_nprec - first); L->dead = 0;
}

static void obj_note(uint64_t a, uint32_t l, uint64_t tsc, uint32_t ns, uint32_t np) {
  if (g_objs && g_cx.obj < OBJCAP) {
    PtjObjRec *o = &g_objs[g_cx.obj];
    o->addr = a; o->len = l; o->tsc = tsc; o->nsites = ns; o->npatched = np;
  }
}

static void note_tramp(PtjPatchCtx *cx, uint8_t *code, size_t clen, uint8_t *t, size_t tlen) {
  if (g_prec && g_nprec < PREC_MAX) {
    PatchRec *r = &g_prec[g_nprec++];
    r->addr = (uint64_t)code; r->tramp = (uint64_t)t; r->tsc = cx->tsc;
    r->obj_addr = cx->last_obj_addr; r->obj_len = cx->last_obj_len;
    r->obj_idx = cx->obj; r->off = cx->last_off; r->dead = 0;
    r->wlen = (uint8_t)(clen > 32 ? 32 : clen);
    memcpy(r->orig, cx->last_orig, r->wlen);
    // `code` currently holds the int3 of phase 1, so record the *intended* final bytes.
    memcpy(r->want, g_last_wlen ? g_last_want : code, r->wlen);
  }
  if (g_mode >= MODE_DUMP) {
    // evt 3 must show the window as it will be, not mid-install
    uint8_t saved[32];
    memcpy(saved, code, clen > 32 ? 32 : clen);
    ptj_arena_record(&g_dump, 3, 1, code, nullptr, g_last_want, clen, nullptr, 0, 1);
    (void)saved;
    ptj_arena_record(&g_dump, 4, 1, t, (void *)code, t, tlen, nullptr, 0, 1);
  }
}

// Read our own memory without risking a fault: an nmethod's page can be handed back.
static int safe_read(const void *src, void *dst, size_t n) {
  struct iovec l = {dst, n}, r = {(void *)src, n};
  return process_vm_readv(getpid(), &l, 1, &r, 1, 0) == (ssize_t)n;
}

// ------------------------------------------------ HotSpot code-shape rules ---
// `code_addr`/`code_size` from JVMTI span the nmethod's insts section *and* its stub
// section (verified by disassembly: the tail is the exception handler, then
// `movabs r10,<pc>; push r10; jmp <unpack>` -- the deopt handler -- padded with `hlt`).
//
// Two offsets matter:
//   * the **verified entry**: HotSpot writes a 5-byte `jmp` there itself when it makes the
//     nmethod not-entrant (`NativeJump::patch_verified_entry`).  Nothing of ours may
//     overlap it.  It is 32-byte aligned (CodeEntryAlignment) and is marked by the stack
//     bang `mov dword ptr [rsp-N], eax` = `89 84 24 <neg disp32>`.  Measured on Renaissance
//     scrabble: 1598/1745 nmethods have an aligned bang, at offset 0 (static: the
//     verified entry *is* code_begin) or 0x20 (instance: after the inline-cache check).
//   * everything before it is the inline-cache check, which carries the narrow-oop base as
//     a `movabs` imm64 (a metadata relocation the GC rewrites) and a `jne` to the
//     `ic_miss` stub outside the object -- both already refused by jitpatch.h.
//
// The rule is therefore `lo = max(last aligned bang in the first 0x80 bytes, 0x20) + 5`:
// conservative (a false positive can only raise `lo`, i.e. cost coverage) and it protects
// the verified entry whether it is at 0 or at 0x20.
static uint32_t hs_patch_lo(const uint8_t *code, size_t len) {
  uint32_t ve = 0; int found = 0;
  size_t lim = len < 0x80 ? len : 0x80;
  for (size_t o = 0; o + 7 <= lim; o += 32) {
    if (code[o] == 0x89 && code[o + 1] == 0x84 && code[o + 2] == 0x24) {
      int32_t d; memcpy(&d, code + o + 3, 4);
      if (d < 0) { ve = (uint32_t)o; found = 1; }
    }
  }
  if (!found) g_c.ve_none++;
  else if (ve > 0x20) g_c.ve_gt_20++;
  if (ve < 0x20) ve = 0x20;
  return ve + 5;
}

// The entry-only site: the first *displaceable* instruction boundary at or after `lo`.
// `lo` itself is almost never a boundary (the stack bang is 7 bytes and starts 5 before it),
// and the instruction right after the prologue is very often HotSpot's `movabs rax,
// <MethodCounters*>` -- a metadata relocation, i.e. an imm64 jitpatch.h must refuse -- so
// picking the site up front instead of retrying the whole patch pass is worth doing.
// Returns `len` if there is none.
static uint32_t hs_entry_off(const uint8_t *code, size_t len, uint32_t lo) {
  uint64_t base = (uint64_t)(uintptr_t)code;
  size_t scan_end = lo + 160 < len ? (size_t)lo + 160 : len;
  uint32_t off = 0;
  while (off < scan_end) {
    ZydisDecodedInstruction ins;
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&g_dec, code + off, len - off, &ins, ops,
                                             ZYDIS_MAX_OPERAND_COUNT, 0))) return (uint32_t)len;
    if (off >= lo) {
      // does a >=5-byte window of displaceable instructions start here?
      uint32_t o = off, end = off; int ok = 1;
      while (end - off < 5) {
        ZydisDecodedInstruction i2;
        ZydisDecodedOperand p2[ZYDIS_MAX_OPERAND_COUNT];
        if (o >= len ||
            !ZYAN_SUCCESS(ZydisDecoderDecodeFull(&g_dec, code + o, len - o, &i2, p2,
                                                 ZYDIS_MAX_OPERAND_COUNT, 0))) { ok = 0; break; }
        if (ptj_insn_unsafe(&i2, p2, base, base + len, base + o)) { ok = 0; break; }
        end = o + i2.length; o = end;
      }
      if (ok) return off;
    }
    off += ins.length;
  }
  return (uint32_t)len;
}

// The offsets JVMTI hands us in the jvmtiAddrLocationMap are HotSpot's PcDesc table: every
// safepoint and call return in the method, i.e. every address the VM itself can resume
// execution at.  They are exactly the indirect entries jitpatch.h cannot see in the code,
// so they go into `cx->forbid`: no displaced window may contain one strictly inside.
static uint32_t g_forbid[65536];
static uint32_t g_nforbid = 0;
static void build_forbid(const void *code_addr, jint map_length, const jvmtiAddrLocationMap *map) {
  g_nforbid = 0;
  if (!map || map_length <= 0) return;
  uint64_t base = (uint64_t)code_addr;
  for (jint i = 0; i < map_length && g_nforbid < 65536; i++) {
    uint64_t a = (uint64_t)map[i].start_address;
    if (a >= base) g_forbid[g_nforbid++] = (uint32_t)(a - base);
  }
  g_c.locmap_entries += (uint64_t)g_nforbid;
  if (g_nforbid) g_c.locmap_objs++;
}

// -------------------------------------- the interpreter template blob --------
// `DynamicCodeGenerated("Interpreter")` is one ~95 KB blob generated once during VM init
// and never moved or freed -- attractive, because it is where all non-JIT time goes.  It
// is also unreachable by the machinery every other code object uses: a recursive-descent
// sweep from its first byte reaches **1 byte of 97,760** (measured on Renaissance
// scrabble), because every bytecode template is entered only through a dispatch table.
//
// The templates' addresses are not exposed by JVMTI, but HotSpot's own dispatch tables are
// in libjvm.so's symbol table (`nm libjvm.so | grep _active_table`).  With `interp=1` the
// agent reads every `TemplateInterpreter::*` / `AbstractInterpreter::*` data symbol out of
// the .symtab, treats each 8-byte word in it that points inside the blob as an entry point,
// and hands the set to jitpatch.h as BOTH decode roots and forbidden window interiors.
// That is the only sound way to displace bytes there: a window that straddles a template
// entry would put a thread dispatched to it in the middle of our `jmp`.
//
// This deliberately binds the agent to HotSpot-internal symbol names, which is what D9 set
// out to avoid, so it is off by default.  See the design notes for the verdict.
#include <elf.h>

static uint64_t g_interp_addr = 0; static uint32_t g_interp_len = 0;
static uint32_t *g_interp_roots = nullptr; static uint32_t g_n_interp_roots = 0;
static int g_interp_done = 0;
static uint64_t g_interp_full_sites = 0, g_interp_full_sites_at_exit = 0, g_interp_install_ns = 0;
// Explicit experiment option: hold VM_INIT until the full interpreter is installed.
// Its elapsed cost is reported, never subtracted from process wall time.
static uint32_t g_bootstrap_ms = 0;
static uint64_t g_bootstrap_ns = 0;

static void note_full_interpreter(const char *name, int placed) {
  if (placed <= 0 || strcmp(name, "Interpreter")) return;
  __atomic_store_n(&g_interp_install_ns, ptj_ns(), __ATOMIC_RELEASE);
  __atomic_store_n(&g_interp_full_sites, (uint64_t)placed, __ATOMIC_RELEASE);
}

static int libjvm_base(char *path, size_t pathsz, uint64_t *base) {
  FILE *f = fopen("/proc/self/maps", "r");
  if (!f) return 0;
  char line[512]; int found = 0;
  while (fgets(line, sizeof line, f)) {
    uint64_t lo, hi, off;
    char perms[8], dev[16], p[400];
    long ino;
    int k = sscanf(line, "%lx-%lx %7s %lx %15s %ld %399s", &lo, &hi, perms, &off, dev, &ino, p);
    if (k < 7) continue;
    size_t n = strlen(p);
    if (n < 10 || strcmp(p + n - 10, "libjvm.so")) {
      if (n < 9 || strcmp(p + n - 9, "libjvm.so")) continue;
    }
    if (off != 0) continue;
    snprintf(path, pathsz, "%s", p);
    *base = lo; found = 1; break;
  }
  fclose(f);
  return found;
}

// Collect every word inside [lo,hi) held by a data symbol whose (mangled) name starts with
// one of the interpreter prefixes.
static void harvest_interp_entries(uint64_t lo, uint64_t hi) {
  char path[512]; uint64_t base;
  if (!libjvm_base(path, sizeof path, &base)) return;
  int fd = open(path, O_RDONLY);
  if (fd < 0) return;
  struct stat stb;
  if (fstat(fd, &stb) != 0) { close(fd); return; }
  uint8_t *m = (uint8_t *)mmap(nullptr, (size_t)stb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (m == MAP_FAILED) return;
  Elf64_Ehdr *eh = (Elf64_Ehdr *)m;
  if (memcmp(eh->e_ident, ELFMAG, 4)) { munmap(m, stb.st_size); return; }
  Elf64_Shdr *sh = (Elf64_Shdr *)(m + eh->e_shoff);
  Elf64_Sym *sym = nullptr; const char *str = nullptr; size_t nsym = 0;
  for (int i = 0; i < eh->e_shnum; i++)
    if (sh[i].sh_type == SHT_SYMTAB) {
      sym = (Elf64_Sym *)(m + sh[i].sh_offset);
      nsym = sh[i].sh_size / sizeof(Elf64_Sym);
      str = (const char *)(m + sh[sh[i].sh_link].sh_offset);
    }
  if (!sym) { munmap(m, stb.st_size); return; }
  static const char *PFX[] = {"_ZN19TemplateInterpreter", "_ZN18AbstractInterpreter", nullptr};
  uint32_t cap = 65536;
  g_interp_roots = (uint32_t *)malloc(cap * 4);
  if (!g_interp_roots) { munmap(m, stb.st_size); return; }
  uint64_t tables = 0;
  for (size_t i = 0; i < nsym; i++) {
    if (ELF64_ST_TYPE(sym[i].st_info) != STT_OBJECT || !sym[i].st_size) continue;
    const char *nm = str + sym[i].st_name;
    int hit = 0;
    for (int k = 0; PFX[k]; k++) if (!strncmp(nm, PFX[k], strlen(PFX[k]))) hit = 1;
    if (!hit) continue;
    uint64_t a = base + sym[i].st_value, n = sym[i].st_size / 8;
    tables++;
    for (uint64_t w = 0; w < n; w++) {
      uint64_t v;
      if (!safe_read((const void *)(a + w * 8), &v, 8)) break;
      if (v >= lo && v < hi && g_n_interp_roots < cap) {
        uint32_t off = (uint32_t)(v - lo);
        int dup = 0;
        for (uint32_t q = 0; q < g_n_interp_roots; q++) if (g_interp_roots[q] == off) { dup = 1; break; }
        if (!dup) g_interp_roots[g_n_interp_roots++] = off;
      }
    }
  }
  munmap(m, stb.st_size);
  if (g_verbose)
    fprintf(stderr, "PTJAVA interpreter: %llu tables, %u distinct entry points in [%#lx,%#lx)\n",
            (unsigned long long)tables, g_n_interp_roots, lo, hi);
}

// ---------------------------------------------------------- the patch path --
// Split in two because HotSpot's event delivery is asynchronous.  V8 calls the handler on
// the JS thread before the code runs, so "pause and analyze" literally pauses the program
// and the whole pipeline can be one function.  HotSpot posts CompiledMethodLoad on the
// Service Thread *after* the nmethod is live, so blocking there does NOT stall the mutator
// -- it stalls the event queue, and every event still queued when the VM exits is silently
// dropped.  Measured (scrabble, r=3): a callback that analyses inline sees 132 of 1810
// CompiledMethodLoad events; one that returns immediately sees 1810.
//
// So: `resolve_sites` (hash -> cache -> analyzer, the slow half) runs on a pool of worker
// threads with one analyzer service each, and `apply_patch` (decode, window selection,
// trampoline emission, install) runs under one lock because jitpatch.h's decoder state is
// a single global.  Patching from a thread that is not the event thread is only safe
// *because* the install is the int3-then-jmp protocol -- which it has to be anyway.
static pthread_mutex_t g_patchlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cachelock = PTHREAD_MUTEX_INITIALIZER;

// `install` stub for a PLANNING pass: run the whole window-selection and emission path so
// that the drop set is exactly the one the real pass would produce, but never write a byte
// of live code and never queue a pending window.
static int plan_noop_install(uint8_t *at, const uint8_t *bytes, uint32_t n, uint64_t tramp) {
  (void)at; (void)bytes; (void)n; (void)tramp;
  return 1;
}

// apply_patch(..., plan_only, unp, nunp, maxunp)
//
// With plan_only the object is decoded, windows are chosen and trampolines are emitted into
// the slab exactly as usual -- so `unp` is the true set of sites this object cannot host --
// but nothing is installed and every counter the real pass would move is restored, so the
// pass is invisible.  The slab is used as scratch and handed back.
//
// Planning has to run against the LIVE code address, not against the snapshot: the
// trampoline's `jmp rel32` reach is computed from the slab to the code object, and a
// snapshot on the heap is nowhere near the code cache, which would make every window look
// out-of-range and poison the avoid set with the whole site list.
static int apply_patch(uint8_t *code, size_t len, PtjSite *sites, int n, uint8_t code_type,
                       const uint32_t *forbid, uint32_t nforbid,
                       const uint32_t *roots = nullptr, uint32_t nroots = 0,
                       int plan_only = 0, uint32_t *unp = nullptr,
                       uint32_t *nunp = nullptr, uint32_t maxunp = 0) {
  if (g_pinbridge) {
    // The worker holds g_lock: unloading cannot retire and then reinstall an old job.
    // Pin owns executable-code instrumentation; no prologue exclusions or native detours.
    if (nunp) *nunp = 0;
    if (plan_only) return n;
    uint64_t pb0 = ns_now();
    pin_publish(PT_PIN_ADD, (uint64_t)code, len, sites, (uint64_t)n);
    g_sgtl.pub += ns_now() - pb0;
    g_pin_objects++; g_pin_sites += (uint64_t)n;
    uint64_t now = ns_now();
    if (!g_c.first_install_ns) g_c.first_install_ns = now;
    g_c.last_install_ns = now;
    if (g_instlog_buf && g_instlog_n < PTJ_INSTLOG_MAX) {
      uint64_t *row = g_instlog_buf[g_instlog_n++];
      row[0] = now; row[1] = g_pin_objects; row[2] = g_pin_sites;
    }
    return n;
  }
  pthread_mutex_lock(&g_patchlock);
  if (!g_tslab) tslab_init_near((uint64_t)code, g_slab_mb << 20);
  if (!g_tslab) { if (!plan_only) g_c.skipped_cap++; pthread_mutex_unlock(&g_patchlock); return 0; }
  ensure_writable((uint64_t)code, len);
  g_cx.slab = g_tslab; g_cx.slab_cap = g_tslab_cap; g_cx.slab_used = &g_tslab_used;
  g_cx.forbid = nforbid ? forbid : nullptr; g_cx.nforbid = nforbid;
  g_cx.roots = nroots ? roots : nullptr; g_cx.nroots = nroots;
  g_cx.hi = 0;
  g_cx.lo = (code_type == 1) ? hs_patch_lo(code, len) : 0;
  if (g_cx.lo + 8 >= len) { if (!plan_only) g_c.skipped_nolo++; pthread_mutex_unlock(&g_patchlock); return 0; }
  if (!plan_only) {
    live_retire((uint64_t)code, (uint64_t)code + len, &g_c.superseded);
    g_c.objs_analyzed++;
    g_c.sites_req += (uint64_t)n;
  }
  PtjObjStat ost; memset(&ost, 0, sizeof ost);
  g_cx.tsc = ptj_rdtscp();
  uint64_t p0 = ptj_ns(), first = g_nprec;
  tslab_commit(g_tslab_used + (1u << 20));
  g_npend = 0;
  g_cx.unpatched = unp; g_cx.nunpatched = 0; g_cx.maxunpatched = unp ? maxunp : 0;
  if (plan_only) {
    // save everything ptj_patch_object appends to, then put it all back
    uint32_t s_ent = g_cx.nentries, s_rel = g_cx.nrelocs, s_tr = g_cx.ntramps;
    uint64_t s_val = g_cx.n_values, s_drop[PTJ_DROP_N];
    size_t s_slab = g_tslab_used;
    memcpy(s_drop, g_cx.drops, sizeof s_drop);
    int (*s_inst)(uint8_t *, const uint8_t *, uint32_t, uint64_t) = g_cx.install;
    g_cx.install = plan_noop_install;
    ptj_patch_object(&g_cx, code, len, sites, n, &ost, nullptr, nullptr, nullptr);
    g_cx.install = s_inst;
    g_cx.nentries = s_ent; g_cx.nrelocs = s_rel; g_cx.ntramps = s_tr;
    g_cx.n_values = s_val; g_tslab_used = s_slab;
    memcpy(g_cx.drops, s_drop, sizeof s_drop);
    g_npend = 0;                                 // nothing was queued; make sure of it
    if (nunp) *nunp = g_cx.nunpatched;
    g_cx.unpatched = nullptr; g_cx.maxunpatched = 0;
    g_c.plan_ns += ptj_ns() - p0;
    g_c.plan_passes++;
    pthread_mutex_unlock(&g_patchlock);
    return (int)ost.patched;
  }
  uint32_t rel0 = g_cx.nrelocs;
  ptj_patch_object(&g_cx, code, len, sites, n, &ost, nullptr, nullptr, note_tramp);
  hs_commit();                                   // finish the cross-modifying sequence
  // D-J13: publish every relocated instruction's copy -> original mapping BEFORE the
  // window can execute... it already can (hs_commit returned), but a fault needs the
  // instruction to be REACHED, which needs the detour, which needs phase 5 above; the map
  // is filled within microseconds of that and a miss is counted, not silently ignored.
  for (uint32_t r = rel0; r < g_cx.nrelocs; r++)
    relmap_put(g_cx.relocs[r].tramp_addr, g_cx.relocs[r].orig_addr);
  if (nunp) *nunp = g_cx.nunpatched;
  g_cx.unpatched = nullptr; g_cx.maxunpatched = 0;
  g_c.patch_ns += ptj_ns() - p0;
  g_c.sites_patched += ost.patched;
  if (ost.patched) {
    g_c.objs_patched++;
    uint64_t now = ptj_ns();
    if (!g_c.first_install_ns) g_c.first_install_ns = now;
    g_c.last_install_ns = now;                  // convergence: see jit_java_perf.md
    if (g_instlog_buf && g_instlog_n < PTJ_INSTLOG_MAX) {
      g_instlog_buf[g_instlog_n][0] = now;
      g_instlog_buf[g_instlog_n][1] = g_nprec;          // cumulative windows installed
      g_instlog_buf[g_instlog_n][2] = g_cx.n_values;    // cumulative values logged
      g_instlog_n++;
    }
  }
  obj_note((uint64_t)code, (uint32_t)len, g_cx.tsc, (uint32_t)n, ost.patched);
  live_note((uint64_t)code, (uint32_t)len, first);
  g_cx.obj++;
  pthread_mutex_unlock(&g_patchlock);
  return (int)ost.patched;
}

// The analysis half.  `cli` is the caller's own analyzer connection; `snap` are the code
// bytes as they were when the event fired (the live code may have moved on -- HotSpot
// rewrites inline caches -- but the sites are offsets, and a site that no longer lands on
// an instruction boundary is dropped by the patcher, so drift degrades coverage, not
// correctness).
// The cache key must cover the avoid set: the same code bytes with a different set of
// forbidden addresses are a different question and get a different answer.  This mirrors
// analyze.py's own `avoid_key` -- a short hash of the sorted address list appended to the
// content hash -- so the two caches partition the space the same way.
// D-J14.2 validation.  A coarser cache key can only fail one way: by handing an object the
// site list of a DIFFERENT object.  Nothing downstream would notice -- the Pintool would
// instrument the wrong offsets and log the wrong values -- so the coarsening is checked
// head-on: with `maskverify=1' every cache hit is re-asked of the analyzer and the two site
// lists are compared field by field.  Ruinously slow; a validation run, never a timing row.
static void mask_verify(PtjClient *cli, const uint8_t *snap, size_t len, uint64_t base,
                        const char *name, const PtjSite *got, int n,
                        const uint64_t *avoid, int navoid,
                        const uint32_t *roots, uint32_t nroots, uint32_t data_from) {
  if (!g_mask_verify || cli->fd < 0) return;
  // `maskverify=N' checks one hit in N, so the run still finishes; the workers verify in
  // parallel (their own analyzer connections, their own reply buffer).
  static uint64_t seq = 0;
  if (__atomic_fetch_add(&seq, 1, __ATOMIC_RELAXED) % (uint64_t)g_mask_verify) return;
  static thread_local PtjSite *ref = nullptr;
  if (!ref) ref = (PtjSite *)malloc(sizeof(PtjSite) * PTJ_MAXSITES);
  if (!ref) return;
  int m = ptj_analyze(cli, snap, len, base, name, ref, PTJ_MAXSITES, avoid, navoid,
                      roots, (int)nroots, data_from, nullptr, 0, nullptr);
  if (m < 0) return;
  // Control for the control: ask the SAME question again, same bytes, same base.  If the two
  // analyzer answers already disagree, "cached answer differs from analyzer answer" says
  // nothing about the cache key and everything about the analyzer.
  {
    static thread_local PtjSite *ref2 = nullptr;
    if (!ref2) ref2 = (PtjSite *)malloc(sizeof(PtjSite) * PTJ_MAXSITES);
    if (ref2) {
      int m2 = ptj_analyze(cli, snap, len, base, name, ref2, PTJ_MAXSITES, avoid, navoid,
                           roots, (int)nroots, data_from, nullptr, 0, nullptr);
      int s2 = (m2 == m);
      for (int i = 0; s2 && i < m; i++)
        s2 = (ref2[i].off == ref[i].off && ref2[i].id == ref[i].id &&
              ref2[i].when == ref[i].when && ref2[i].kind == ref[i].kind &&
              ref2[i].size == ref[i].size && ref2[i].nregs == ref[i].nregs &&
              !memcmp(ref2[i].regs, ref[i].regs, ref2[i].nregs));
      __atomic_fetch_add(s2 ? &g_mv_self_same : &g_mv_self_diff, 1, __ATOMIC_RELAXED);
    }
  }
  {
    __atomic_fetch_add(&g_mv_checked, 1, __ATOMIC_RELAXED);
    int same = (m == n);
    for (int i = 0; same && i < n; i++)
      same = (ref[i].off == got[i].off && ref[i].id == got[i].id && ref[i].when == got[i].when &&
              ref[i].kind == got[i].kind && ref[i].size == got[i].size &&
              ref[i].nregs == got[i].nregs && !memcmp(ref[i].regs, got[i].regs, ref[i].nregs));
    if (same) __atomic_fetch_add(&g_mv_same, 1, __ATOMIC_RELAXED);
    else {
      __atomic_fetch_add(&g_mv_diff, 1, __ATOMIC_RELAXED);
      // Does the difference change WHAT is logged, or only WHERE?  The analyzer returns a
      // covering set for a set of critical values; ties can be broken differently for the
      // same bytes at a different address.  If the two answers name the same value ids the
      // same number of times, the same values are logged and only the placement moved.
      int vals_same = (m == n);
      if (vals_same) {
        static const int IDMAX = 4096;
        static thread_local uint16_t *ha = nullptr, *hb = nullptr;
        if (!ha) { ha = (uint16_t *)calloc(IDMAX, 2); hb = (uint16_t *)calloc(IDMAX, 2); }
        if (ha && hb) {
          memset(ha, 0, IDMAX * 2); memset(hb, 0, IDMAX * 2);
          for (int i = 0; i < n; i++) {
            if (got[i].id < IDMAX) ha[got[i].id]++;
            if (ref[i].id < IDMAX) hb[ref[i].id]++;
          }
          vals_same = !memcmp(ha, hb, IDMAX * 2);
        } else vals_same = 0;
      }
      __atomic_fetch_add(vals_same ? &g_mv_same_values : &g_mv_diff_values, 1, __ATOMIC_RELAXED);
      fprintf(stderr, "PTJAVA maskverify MISMATCH len=%zu cached=%d analyzer=%d "
                      "same_value_set=%d name=%s\n", len, n, m, vals_same, name ? name : "");
    }
  }
}

// `cache_only' (D-J14): answer from the content-hash cache or return -3 without calling the
// analyzer.  The caller then hands the job to the analyzer lane instead of holding a worker
// for the ~0.5 s a round trip costs.  Nothing about the ANSWER changes -- same key, same
// cache, same analyzer, same site list; only which thread waits for it.
static int resolve_sites(PtjClient *cli, const uint8_t *snap, size_t len, uint64_t base,
                         const char *name, PtjSite *out, int maxs,
                         const uint64_t *avoid = nullptr, int navoid = 0,
                         const uint32_t *roots = nullptr, uint32_t nroots = 0,
                         uint32_t *rroots = nullptr, int maxrroots = 0, int *nrroots = nullptr,
                         int cache_only = 0) {
  char key[PTJ_KEYMAX];
  uint64_t h0 = ptj_ns();
  // D-J6: the analyzer must see the same inline-data boundary and the same extra entry points
  // the patcher's own sweep uses, or its sites land on boundaries the patcher never decodes.
  uint32_t data_from = ptj_scan_data_from(&g_dec, snap, len);
  masked_hash(snap, len, key, data_from, roots, nroots);
  uint64_t h1 = ptj_ns();
  __atomic_fetch_add(&g_c.hash_ns, h1 - h0, __ATOMIC_RELAXED);
  g_sgtl.hash += h1 - h0;
  if (nrroots) *nrroots = 0;
  if (key[0] && navoid > 0) {                 // <content-hash>.a<avoid-hash>
    char ak[9]; ptj_hs_avoid_key(avoid, navoid, base, g_relative_avoid, ak);
    size_t kl = strlen(key);
    // Different prefix prevents aliasing the old absolute-address namespace.
    key[kl] = '.'; key[kl + 1] = g_relative_avoid ? 'o' : 'a'; memcpy(key + kl + 2, ak, 9);
  }
  if (key[0]) {
    pthread_mutex_lock(&g_cachelock);
    CacheEnt *ce = mem_find(key, 0);
    int n = -1;
    if (ce) { memcpy(out, ce->sites, sizeof(PtjSite) * ce->n); n = ce->n; cli->mem_hits++;
              if (rroots && nrroots && ce->rroots) { int k = ce->nrroots > maxrroots ? maxrroots : ce->nrroots;
                                       memcpy(rroots, ce->rroots, sizeof(uint32_t) * (size_t)k); *nrroots = k; } }
    pthread_mutex_unlock(&g_cachelock);
    if (n >= 0) {
      g_sgtl.cache += ptj_ns() - h1;
      mask_verify(cli, snap, len, base, name, out, n, avoid, navoid, roots, nroots, data_from);
      return n;
    }
  }
  int n = key[0] ? ptj_cache_load(cli, key, out, maxs, rroots, maxrroots, nrroots) : -1;
  g_sgtl.cache += ptj_ns() - h1;
  if (n >= 0)
    mask_verify(cli, snap, len, base, name, out, n, avoid, navoid, roots, nroots, data_from);
  if (n < 0 && cache_only) return -3;            // hand it to the analyzer lane
  // NOT `&& cli->fd >= 0'.  That guard made the re-dial inside ptj_analyze unreachable: a
  // worker whose `analyze.py --serve' had died stopped analysing anything for the rest of
  // the run, `analysis_calls' and `analysis_unserved' both stayed at 0, and every object
  // whose avoid-round key missed the cache was dropped without a trace.  Measured: a run
  // that started while the services were restarting patched 0 of 2370 code objects, exited
  // 0 and validated.  ptj_analyze returns -1 by itself when it cannot connect.
  if (n < 0) {
    uint64_t a0 = ptj_ns();
    n = ptj_analyze(cli, snap, len, base, name, out, maxs, avoid, navoid,
                    roots, (int)nroots, data_from, rroots, maxrroots, nrroots);
    g_sgtl.anal += ptj_ns() - a0;
    if (n >= 0 && key[0]) {
      pthread_mutex_lock(&g_cachelock);
      ptj_cache_store(cli, key, out, n, rroots, nrroots ? *nrroots : 0);
      pthread_mutex_unlock(&g_cachelock);
    }
  }
  if (n >= 0 && key[0]) {
    pthread_mutex_lock(&g_cachelock);
    CacheEnt *c2 = mem_find(key, 1);
    if (c2 && !c2->sites) {
      c2->sites = (PtjSite *)malloc(sizeof(PtjSite) * (n ? n : 1));
      if (c2->sites) { memcpy(c2->sites, out, sizeof(PtjSite) * n); c2->n = n; }
      int k = (rroots && nrroots) ? *nrroots : 0;
      if (k > PTJ_MAXROOTS) k = PTJ_MAXROOTS;
      c2->rroots = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(k ? k : 1));
      if (c2->rroots && k) memcpy(c2->rroots, rroots, sizeof(uint32_t) * (size_t)k);
      c2->nrroots = c2->rroots ? k : 0;
    }
    pthread_mutex_unlock(&g_cachelock);
  }
  return n;
}

// --- the job queue -----------------------------------------------------------
struct Job {
  uint64_t enq_ns;                   // when the CompiledMethodLoad event queued it (D-J3.2)
  uint64_t ev_ns;                    // D-J14: when the event handler itself started
  uint64_t q0_ns;                    // D-J14: FIRST enqueue -- keeps patch_latency_ms's
                                     //        definition (enqueue -> published) unchanged
  uint64_t qwait;                    // D-J14: time on queues, summed over both lanes
  uint64_t s_hash, s_cache;          // D-J14: stage time already spent in the cache lane
  uint64_t pin_generation;
  uint64_t addr; uint32_t len; uint8_t code_type;
  uint8_t prio;                      // D-J14: 1 = the runtime has compiled this before
  uint8_t *snap;                     // the code bytes at event time (owned)
  uint32_t nforbid; uint32_t *forbid;
  // D-J6: the analyzer's `restart_roots' for this object, plus the union with `forbid' that
  // the patcher's recursive descent is seeded with.
  uint32_t nrroots; uint32_t *rroots;      // owned by the worker that runs the job
  char name[128];
};
static Job *g_q = nullptr;
static uint32_t g_qcap = 1u << 16, g_qhead = 0, g_qtail = 0;
static pthread_mutex_t g_qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_qcond = PTHREAD_COND_INITIALIZER;
static int g_qstop = 0, g_nworkers = 0, g_workers_busy = 0;
static uint64_t g_q_enq = 0, g_q_done = 0, g_q_dropped = 0, g_q_stale = 0;
static PtjClient *g_wcli = nullptr;

// --- D-J14: the analyzer lane -------------------------------------------------------------
// Measured cause of the 0.5-3.1 s publication latency: NOT steady-state capacity (analyzer
// time over worker-seconds is 21-74 % across cells) and NOT the analyzer's own speed.  It is
// HEAD-OF-LINE BLOCKING in a burst.  About one job in twenty misses the content-hash cache
// and costs a ~0.5 s analyzer round trip; the other nineteen are answered from the cache in
// ~1 ms.  With one FIFO and four workers, four concurrent misses stop the queue dead, and at
// ~57 events/s about 30 cheap jobs pile up behind each one.
//
// So the cheap jobs get their own lane.  A cache-lane worker hashes the object and probes the
// cache; if the cache answers it publishes immediately, and if it does not the job is handed
// to the analyzer lane, whose workers own the analyzer connections.  Neither the site list nor
// the cache nor the analyzer changes -- only which thread does the waiting.  This is a property
// of the analysis service, not of HotSpot: the same split is what V8 would need if its handler
// were ever made asynchronous.  `lanes=0' restores the single FIFO (the A/B control).
static Job *g_aq = nullptr, *g_aqhi = nullptr;
static uint32_t g_aqcap = 0, g_aqhead = 0, g_aqtail = 0;
static uint32_t g_aqhihead = 0, g_aqhitail = 0;
static pthread_cond_t g_aqcond = PTHREAD_COND_INITIALIZER;   // guarded by g_qlock
static int g_lanes = 1, g_nfast = 0, g_fast_busy = 0, g_prio = 1;
static uint64_t g_aq_enq = 0, g_aq_done = 0, g_aq_dropped = 0, g_aq_hi = 0;
// D-J14, second fix: the analyzer lane is FIFO, and its jobs cost ~0.6 s each, so a burst of
// them takes ~20 s to drain on four workers.  Attribution (-jitmiss 3) showed the residual
// unplanned executions are not spread evenly over that burst: they are a handful of HOT
// methods (`String.split', the Scrabble lambdas) whose C2 code is profile-dependent and
// therefore always a cache miss.  A method the runtime has ALREADY compiled once and is
// compiling again is a method the runtime itself has just decided is hot -- a tier-up.  That
// one bit, available for free at the event, orders the analyzer lane.  It is not a HotSpot
// concept: V8 has exactly the same signal when it re-optimises a function.
static PtjClient *g_fcli = nullptr;                          // cache-lane clients (no socket)

// An nmethod whose memory was handed back can be reused by a later compilation; a job that
// was queued before that must not write into it.  Cheap guard: remember every unloaded
// address, and re-check a prefix/suffix of the snapshot against the live bytes.
static uint64_t g_unloaded[4096]; static uint32_t g_nunloaded = 0;
static int was_unloaded(uint64_t a) {
  for (uint32_t i = 0; i < g_nunloaded && i < 4096; i++) if (g_unloaded[i] == a) return 1;
  return 0;
}
static int snapshot_still_valid(const Job *j) {
  size_t k = j->len < 64 ? j->len : 64;
  uint8_t cur[64];
  if (!safe_read((const void *)j->addr, cur, k) || memcmp(cur, j->snap, k)) return 0;
  if (j->len > 128) {
    if (!safe_read((const void *)(j->addr + j->len - 64), cur, 64)) return 0;
    if (memcmp(cur, j->snap + j->len - 64, 64)) return 0;
  }
  return 1;
}

// ---------------------------------------------------- the `avoid` re-solve loop ----
// Analyzer v2.18 accepts `"avoid": [addresses]`: sites at those addresses are removed from
// the hitting-set solver's alphabet and it returns an ALTERNATIVE covering set for the same
// values.  So a site the patcher cannot host -- an instruction carrying a 32-bit immediate
// the GC rewrites, one inside the verified-entry prologue, one whose 5-byte window would
// swallow a PcDesc -- no longer costs us the value; it costs us one more analyzer round.
//
// The loop must PLAN before it installs.  Patching, then re-asking, then patching again
// would be wrong: the second pass would decode code that already contains our `jmp rel32`
// detours and could choose a window overlapping one, which is memory corruption in live
// code.  So each round only plans (no install, all counters restored) and just the final
// site set is installed, exactly once.
static const uint32_t AVOID_MAX = 512;
static int g_avoid_rounds = 2;
static int g_noroots = 0;      // agent option `noroots=1': the D-J6 A/B control (see Agent_OnLoad)

// returns sites placed, -1 analyzer failure, -2 the snapshot went stale,
// -3 (cache_only) the content-hash cache did not answer: give it to the analyzer lane
static int resolve_and_patch(PtjClient *cli, Job *j, PtjSite *buf, uint32_t *rootbuf,
                             int cache_only = 0) {
  uint64_t avoid[AVOID_MAX];
  uint32_t unp[AVOID_MAX], nunp = 0;
  int navoid = 0, n, plan0 = -1, base_unpl = 0, rounds = 0;
  int nrr = 0;
  // D-J6: the descent roots BOTH sides start from.  The jvmtiAddrLocationMap (HotSpot's PcDesc
  // table: every safepoint and call return) goes to the analyzer as `"roots"`, and the analyzer's
  // `restart_roots` come back and join it here, so the patcher's recursive-descent sweep decodes
  // exactly the boundaries the analyzer placed sites on.  Both are also forbidden window
  // interiors (`forbid`), which is what they already were.
  uint32_t nroot = 0;
  if (!g_noroots)
    for (uint32_t q = 0; q < j->nforbid && nroot < (uint32_t)PTJ_MAXROOTS; q++) rootbuf[nroot++] = j->forbid[q];
  for (int round = 0; ; round++) {
    n = resolve_sites(cli, j->snap, j->len, j->addr, j->name, buf, PTJ_MAXSITES,
                      navoid ? avoid : nullptr, navoid,
                      // noroots>=2: do not even SEND the jvmtiAddrLocationMap as `"roots"',
                      // i.e. ask the analyzer the pre-v2.21 question (bar `data_from').
                      g_noroots >= 2 ? nullptr : j->forbid, g_noroots >= 2 ? 0u : j->nforbid,
                      j->rroots, PTJ_MAXROOTS, &nrr, cache_only);
    if (n == -3) return -3;
    if (n < 0) return -1;
    if (round == 0) {
      for (int q = 0; !g_noroots && q < nrr && nroot < (uint32_t)PTJ_MAXROOTS; q++) {
        int dup = 0;
        for (uint32_t y = 0; y < j->nforbid; y++) if (j->forbid[y] == j->rroots[q]) { dup = 1; break; }
        if (!dup) rootbuf[nroot++] = j->rroots[q];
      }
      j->nrroots = (uint32_t)nrr;
      if (nrr) { __atomic_fetch_add(&g_c.restart_objs, 1, __ATOMIC_RELAXED);
                 __atomic_fetch_add(&g_c.restart_roots, (uint64_t)nrr, __ATOMIC_RELAXED); }
    }
    if ((!g_pinbridge && was_unloaded(j->addr)) || !snapshot_still_valid(j)) return -2;
    if (g_avoid_rounds <= 0) break;                    // loop disabled: old behaviour
    nunp = 0;
    int pl = apply_patch((uint8_t *)j->addr, j->len, buf, n, j->code_type,
                         j->forbid, j->nforbid, rootbuf, nroot, 1, unp, &nunp, AVOID_MAX);
    if (round == 0) { plan0 = pl; base_unpl = (int)nunp; }
    if (nunp == 0) { if (round) __atomic_fetch_add(&g_c.avoid_converged, 1, __ATOMIC_RELAXED); break; }
    if (round >= g_avoid_rounds) break;                // out of rounds; install what we have
    int added = 0;
    for (uint32_t q = 0; q < nunp && navoid < (int)AVOID_MAX; q++) {
      uint64_t a = j->addr + unp[q];
      int seen = 0;
      for (int y = 0; y < navoid; y++) if (avoid[y] == a) { seen = 1; break; }
      if (!seen) { avoid[navoid++] = a; added++; }
    }
    if (!added) break;               // the solver handed back the same impossible sites
    rounds++;
  }
  if (g_pinbridge) {
    pthread_mutex_lock(&g_lock);
    auto current = g_pin_live.find(j->addr);
    if (current == g_pin_live.end() || current->second != j->pin_generation ||
        !snapshot_still_valid(j)) {
      pthread_mutex_unlock(&g_lock);
      return -2;
    }
    int placed = apply_patch((uint8_t *)j->addr, j->len, buf, n, j->code_type,
                             j->forbid, j->nforbid, rootbuf, nroot);
    note_full_interpreter(j->name, placed);
    pthread_mutex_unlock(&g_lock);
    return placed;
  }
  if (was_unloaded(j->addr) || !snapshot_still_valid(j)) return -2;
  if (g_verbose >= 2) {
    // D-J11 diagnosis the design notes: the object exactly as the VM emitted it
    // (the analysed snapshot), the VM roots (jvmtiAddrLocationMap) and the analyzer's restart
    // roots, all as OFFSETS, so a crash can be replayed with runtime/jit/diag/dj11_phase.py.
    static pthread_mutex_t vl = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&vl);
    fprintf(stderr, "PTJAVAOBJ addr=%#llx len=%u name=%s code=", (unsigned long long)j->addr, j->len, j->name);
    for (uint32_t q = 0; q < j->len; q++) fprintf(stderr, "%02x", j->snap[q]);
    fprintf(stderr, "\nPTJAVAROOTS addr=%#llx noroots=%d nvm=%u nrr=%d vm=", (unsigned long long)j->addr,
            g_noroots, j->nforbid, nrr);
    for (uint32_t q = 0; q < j->nforbid; q++) fprintf(stderr, "%s0x%x", q ? "," : "", j->forbid[q]);
    fprintf(stderr, " rr=");
    for (int q = 0; q < nrr; q++) fprintf(stderr, "%s0x%x", q ? "," : "", j->rroots[q]);
    fprintf(stderr, "\n");
    pthread_mutex_unlock(&vl);
  }
  int placed = apply_patch((uint8_t *)j->addr, j->len, buf, n, j->code_type,
                           j->forbid, j->nforbid, rootbuf, nroot);
  note_full_interpreter(j->name, placed);
  if (rounds) {
    __atomic_fetch_add(&g_c.avoid_objs, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_c.avoid_rounds_used, (uint64_t)rounds, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_c.avoid_addrs, (uint64_t)navoid, __ATOMIC_RELAXED);
    if (plan0 >= 0 && placed > plan0)
      __atomic_fetch_add(&g_c.avoid_recovered, (uint64_t)(placed - plan0), __ATOMIC_RELAXED);
  }
  __atomic_fetch_add(&g_c.avoid_unplaceable, (uint64_t)nunp, __ATOMIC_RELAXED);
  __atomic_fetch_add(&g_c.avoid_baseline_unpl, (uint64_t)(base_unpl < 0 ? 0 : base_unpl),
                     __ATOMIC_RELAXED);
  return placed;
}

// Push a job the cache could not answer onto the analyzer lane.  Ownership of `snap'/`forbid'
// moves with it, so the caller must NOT free them once this returns 1.
static int analyzer_lane_push(const Job *j) {
  const int hi = (g_prio && j->prio);
  pthread_mutex_lock(&g_qlock);
  uint32_t *head = hi ? &g_aqhihead : &g_aqhead, *tail = hi ? &g_aqhitail : &g_aqtail;
  Job *ring = hi ? g_aqhi : g_aq;
  if (!ring || *tail - *head >= g_aqcap) {
    g_aq_dropped++;
    pthread_mutex_unlock(&g_qlock);
    return 0;
  }
  Job *d = &ring[*tail % g_aqcap];
  *d = *j;
  d->s_hash = g_sgtl.hash; d->s_cache = g_sgtl.cache;
  d->enq_ns = ns_now();              // second queue entry; `qwait' keeps the running total
  d->rroots = nullptr; d->nrroots = 0;
  (*tail)++; g_aq_enq++;
  if (hi) g_aq_hi++;
  pthread_cond_signal(&g_aqcond);
  pthread_mutex_unlock(&g_qlock);
  return 1;
}

// lane 0 = the analyzer lane (owns g_wcli[w]); lane 1 = the cache lane (g_fcli[w], no socket).
// With `lanes=0' there is no cache lane and lane 0 reads the single FIFO, i.e. the behaviour
// measured before D-J14.
struct LaneArg { int lane; int w; };
static void *worker_main(void *arg) {
  LaneArg la = *(LaneArg *)arg; free(arg);
  const int cache_lane = la.lane;
  PtjSite *buf = (PtjSite *)malloc(sizeof(PtjSite) * PTJ_MAXSITES);
  // D-J6: per-worker scratch for the analyzer's restart roots and for the union of them with
  // the jvmtiAddrLocationMap that the patcher's sweep is seeded with.
  uint32_t *rrbuf = (uint32_t *)malloc(sizeof(uint32_t) * PTJ_MAXROOTS);
  uint32_t *rootbuf = (uint32_t *)malloc(sizeof(uint32_t) * PTJ_MAXROOTS);
  PtjClient *cli = cache_lane ? &g_fcli[la.w] : &g_wcli[la.w];
  for (;;) {
    pthread_mutex_lock(&g_qlock);
    if (cache_lane) {
      while (g_qhead == g_qtail && !g_qstop) pthread_cond_wait(&g_qcond, &g_qlock);
      if (g_qhead == g_qtail && g_qstop) { pthread_mutex_unlock(&g_qlock); break; }
    } else if (g_lanes) {
      // The analyzer lane outlives the cache lane: a cache worker can still forward a job
      // after g_qstop, so this lane may only exit once the cache lane is empty AND idle.
      // Otherwise a forwarded job is stranded and pin mode refuses the run's statistics.
      while (g_aqhead == g_aqtail && g_aqhihead == g_aqhitail &&
             !(g_qstop && g_fast_busy == 0 && g_qhead == g_qtail))
        pthread_cond_wait(&g_aqcond, &g_qlock);
      if (g_aqhead == g_aqtail && g_aqhihead == g_aqhitail) {
        pthread_mutex_unlock(&g_qlock); break;
      }
    } else {
      while (g_qhead == g_qtail && !g_qstop) pthread_cond_wait(&g_qcond, &g_qlock);
      if (g_qhead == g_qtail && g_qstop) { pthread_mutex_unlock(&g_qlock); break; }
    }
    Job j;
    if (cache_lane || !g_lanes)               { j = g_q[g_qhead % g_qcap]; g_qhead++; }
    else if (g_aqhihead != g_aqhitail)        { j = g_aqhi[g_aqhihead % g_aqcap]; g_aqhihead++; }
    else                                      { j = g_aq[g_aqhead % g_aqcap]; g_aqhead++; }
    if (cache_lane) g_fast_busy++; else g_workers_busy++;
    pthread_mutex_unlock(&g_qlock);
    uint64_t deq_ns = ns_now();
    j.qwait += (deq_ns > j.enq_ns) ? deq_ns - j.enq_ns : 0;
    int forwarded = 0;
    if (buf && rrbuf && rootbuf) {
      stage_reset();
      if (!cache_lane) { g_sgtl.hash = j.s_hash; g_sgtl.cache = j.s_cache; }
      j.rroots = rrbuf; j.nrroots = 0;
      int r = resolve_and_patch(cli, &j, buf, rootbuf, cache_lane && g_lanes);
      if (r == -3) {
        // Ownership of snap/forbid moves with the job.  If the lane is full the push fails,
        // the job is freed below and the object stays unplanned -- counted, never silent.
        forwarded = analyzer_lane_push(&j);
      } else if (r == -2) {
        __atomic_fetch_add(&g_q_stale, 1, __ATOMIC_RELAXED);
      } else if (r >= 0) {
        note_latency(j.q0_ns, j.qwait);                     // D-J3.2
        // q0_ns, not enq_ns: a forwarded job is enqueued twice, and `evt' means the event
        // handler, not the second enqueue.  (Using enq_ns charged the cache-lane pass to
        // `evt' and made the analyzer row read 226 ms of "dump".)
        stage_note(j.ev_ns, j.q0_ns, j.qwait);              // D-J14
        obj_log(j.addr, j.len, ns_now() - j.q0_ns, j.qwait, j.name);
      }
    }
    if (!forwarded) { free(j.snap); free(j.forbid); }
    pthread_mutex_lock(&g_qlock);
    if (cache_lane)   g_fast_busy--;
    else            { g_workers_busy--; if (g_lanes) g_aq_done++; }
    if (!forwarded) g_q_done++;                  // a forwarded job is done by the other lane
    pthread_cond_broadcast(&g_qcond);
    pthread_cond_broadcast(&g_aqcond);
    pthread_mutex_unlock(&g_qlock);
  }
  free(buf); free(rrbuf); free(rootbuf);
  return nullptr;
}

static thread_local uint64_t g_ev_ns = 0;      // D-J14: when this thread's callback started
static thread_local uint8_t g_ev_prio = 0;     // D-J14: ... and whether it is a recompilation
static void enqueue_job(const uint8_t *code, size_t len, const char *name, size_t name_len,
                        uint8_t code_type, const uint32_t *forbid, uint32_t nforbid) {
  uint64_t ev_ns = g_ev_ns;
  uint8_t prio = g_ev_prio;
  // Retire an earlier plan immediately, even if the replacement cannot be queued.
  // Analysis is asynchronous: old sites must not remain active until it finishes.
  uint64_t generation = 0;
  if (g_pinbridge) {
    generation = ++g_pin_generation;
    g_pin_live[(uint64_t)code] = generation;
    pin_publish(PT_PIN_REMOVE, (uint64_t)code, len);
  }
  pthread_mutex_lock(&g_qlock);
  if (g_qtail - g_qhead >= g_qcap) { g_q_dropped++; pthread_mutex_unlock(&g_qlock); return; }
  Job *j = &g_q[g_qtail % g_qcap];
  j->enq_ns = ns_now();
  j->q0_ns = j->enq_ns;
  j->s_hash = 0; j->s_cache = 0;
  j->ev_ns = ev_ns ? ev_ns : j->enq_ns;
  j->qwait = 0;
  j->addr = (uint64_t)code; j->len = (uint32_t)len; j->code_type = code_type;
  j->prio = prio;
  // enqueue_job is called under the callback lock. A fresh generation permits
  // legitimate address reuse but never publication of the previous occupant's plan.
  j->pin_generation = generation;
  j->snap = (uint8_t *)malloc(len);
  if (!j->snap) { g_q_dropped++; pthread_mutex_unlock(&g_qlock); return; }
  memcpy(j->snap, code, len);
  j->rroots = nullptr; j->nrroots = 0;
  j->nforbid = nforbid;
  j->forbid = nforbid ? (uint32_t *)malloc(nforbid * 4) : nullptr;
  if (j->forbid) memcpy(j->forbid, forbid, nforbid * 4);
  else j->nforbid = 0;
  size_t nl = name_len > 120 ? 120 : name_len;
  if (name && nl) memcpy(j->name, name, nl);
  j->name[nl] = 0;
  g_qtail++; g_q_enq++;
  pthread_cond_signal(&g_qcond);
  pthread_mutex_unlock(&g_qlock);
}

// Wait for the queue to drain (VM exit).  Bounded so a huge backlog cannot hang the JVM.
static void drain_queue(int max_ms) {
  struct timespec ts;
  for (int i = 0; i < max_ms / 10; i++) {
    pthread_mutex_lock(&g_qlock);
    int idle = (g_qhead == g_qtail && g_aqhead == g_aqtail && g_aqhihead == g_aqhitail &&
                g_workers_busy == 0 && g_fast_busy == 0);
    pthread_mutex_unlock(&g_qlock);
    if (idle) return;
    ts.tv_sec = 0; ts.tv_nsec = 10000000;
    nanosleep(&ts, nullptr);
  }
}

// The synchronous path: entry-only mode (no analyzer call at all) and the DynamicCode blobs.
static void patch_object(uint8_t *code, size_t len, const char *name, size_t name_len,
                         uint8_t code_type, int entry_only) {
  (void)name; (void)name_len;
  if (!len || (!g_pinbridge && len < 16)) { g_c.skipped_small++; return; }
  uint64_t att = g_attempt++;
  if (att < g_min_patch || att >= g_max_patch) { g_c.skipped_cap++; return; }
  if (!g_pinbridge && !g_tslab) tslab_init_near((uint64_t)code, g_slab_mb << 20);
  if (!g_pinbridge && !g_tslab) {
    g_c.skipped_cap++;
    if (g_verbose) fprintf(stderr, "PTJAVA no slab near %p cap=%zu\n", (void *)code,
                           g_slab_mb << 20);
    return;
  }
  if (!entry_only) {                     // full sites: hand it to the pool
    if (g_maxlen && len > g_maxlen) { g_c.skipped_small++; return; }
    enqueue_job(code, len, name, name_len, code_type, g_forbid, g_nforbid);
    return;
  }
  uint32_t lo = (code_type == 1) ? hs_patch_lo(code, len) : 0;
  if (lo + 8 >= len) { g_c.skipped_nolo++; return; }
  // The Java analogue of V8's "log %rdi at the code object's entry": log j_rarg0 (%rsi on
  // x86-64 SysV HotSpot -- the receiver for an instance method, argument 0 for a static
  // one) at the first patchable boundary after the verified-entry prologue.
  PtjSite one; memset(&one, 0, sizeof one);
  one.id = 0; one.when = PTJ_WHEN_BEFORE; one.kind = PTJ_KIND_REG; one.size = 8;
  one.nregs = 1; one.regs[0] = 6;                          // %rsi
  one.off = hs_entry_off(code, len, lo);
  if (one.off >= len) { g_c.skipped_nolo++; return; }
  apply_patch(code, len, &one, 1, code_type, g_forbid, g_nforbid);
}

// ------------------------------------------------------------- callbacks ----
static void note_event_thread(void) {
  if (g_event_threads >= 8) return;
  char p[64], nm[32] = {0};
  snprintf(p, sizeof p, "/proc/self/task/%ld/comm", (long)syscall(SYS_gettid));
  FILE *f = fopen(p, "r");
  if (f) { if (fgets(nm, sizeof nm, f)) { char *e = strchr(nm, '\n'); if (e) *e = 0; } fclose(f); }
  for (int i = 0; i < g_event_threads; i++) if (!strcmp(g_event_thread_names[i], nm)) return;
  snprintf(g_event_thread_names[g_event_threads++], 32, "%s", nm);
}

static void JNICALL cb_compiled_load(jvmtiEnv *jvmti, jmethodID method, jint code_size,
                                     const void *code_addr, jint map_length,
                                     const jvmtiAddrLocationMap *map, const void *compile_info) {
  (void)compile_info;
  uint64_t t0 = ns_now();
  g_ev_ns = t0;
  pthread_mutex_lock(&g_lock);
  // D-J14: has the VM compiled this method before?  If so it is tiering it up, i.e. the VM's
  // own profiler has just called it hot, and its analysis goes to the head of the analyzer
  // lane.  One bit, no extra JVMTI call, no tier-specific knowledge.
  g_ev_prio = g_seen_methods.insert((uint64_t)(uintptr_t)method).second ? 0 : 1;
  if (g_ev_prio) g_c.recompiles++;
  g_c.load++; g_c.nmethod_bytes += (uint64_t)code_size;
  note_event_thread();
  char nbuf[256] = {0}; size_t nl = 0;
  if (g_mode >= MODE_DUMP) {
    char *mname = nullptr, *msig = nullptr, *csig = nullptr; jclass cls;
    if (jvmti->GetMethodName(method, &mname, &msig, nullptr) == JVMTI_ERROR_NONE) {
      if (jvmti->GetMethodDeclaringClass(method, &cls) == JVMTI_ERROR_NONE)
        jvmti->GetClassSignature(cls, &csig, nullptr);
      int k = snprintf(nbuf, sizeof nbuf, "%s%s%s", csig ? csig : "", mname, msig ? msig : "");
      nl = (k < 0) ? 0 : ((size_t)k >= sizeof nbuf ? sizeof nbuf - 1 : (size_t)k);
      jvmti->Deallocate((unsigned char *)mname);
      if (msig) jvmti->Deallocate((unsigned char *)msig);
      if (csig) jvmti->Deallocate((unsigned char *)csig);
    }
    pthread_mutex_lock(&g_patchlock);
    ptj_arena_record(&g_dump, 0, 1, code_addr, nullptr, code_addr, (size_t)code_size,
                     nbuf, nl, 1);
    pthread_mutex_unlock(&g_patchlock);
    if (g_hashdump) {
      char key[65];
      masked_hash((const uint8_t *)code_addr, (size_t)code_size, key);
      fprintf(g_hashdump, "%s %llu %d %.*s\n", key,
              (unsigned long long)(uintptr_t)code_addr, (int)code_size, (int)nl, nbuf);
    }
  }
  if (g_mode >= MODE_ENTRY && g_patch_interp && !g_interp_done && g_interp_addr) {
    // Now, not at the DynamicCodeGenerated event: the dispatch tables are only filled once
    // TemplateInterpreter::initialize() has finished, and the first compilation is well
    // after that.
    g_interp_done = 1;
    harvest_interp_entries(g_interp_addr, g_interp_addr + g_interp_len);
    if ((g_pinbridge || g_patch_interp == 2) && g_n_interp_roots) {
      // Analyze real critical values, not the legacy synthetic bytecode-pointer probes.
      g_c.interp_entries = g_n_interp_roots;
      // The template interpreter is the single hottest generated-code object in a JVM and it
      // is known to be so before it runs a byte.  It goes to the head of the lane too.
      uint8_t save = g_ev_prio; g_ev_prio = 1;
      if (g_verbose) {                     // D-J14: why is the interpreter never a cache hit?
        char raw[65], msk[65], rt[65];
        PtjSha sh; ptj_sha_init(&sh);
        ptj_sha_update(&sh, (const void *)g_interp_addr, g_interp_len);
        ptj_sha_hex(&sh, raw);
        ptj_sha_init(&sh);
        ptj_sha_update(&sh, g_interp_roots, (size_t)g_n_interp_roots * 4);
        ptj_sha_hex(&sh, rt);
        uint32_t df = ptj_scan_data_from(&g_dec, (const uint8_t *)g_interp_addr, g_interp_len);
        masked_hash((const uint8_t *)g_interp_addr, g_interp_len, msk, df,
                    g_interp_roots, g_n_interp_roots);
        fprintf(stderr, "PTJAVA interpkey len=%u data_from=%u nroots=%u raw=%.16s "
                        "roots=%.16s masked=%.16s\n",
                g_interp_len, df, g_n_interp_roots, raw, rt, msk);
        const char *dp = getenv("PTJAVA_INTERPDUMP");
        if (dp) { FILE *df2 = fopen(dp, "wb");
                  if (df2) { fwrite((const void *)g_interp_addr, 1, g_interp_len, df2);
                             fprintf(df2, "%s", "");
                             fclose(df2); } }
      }
      enqueue_job((uint8_t *)g_interp_addr, g_interp_len, "Interpreter", 11, 3,
                  g_interp_roots, g_n_interp_roots);
      g_ev_prio = save;
    } else if (g_n_interp_roots) {
      // one `ptwrite %r13` (the template interpreter's bytecode pointer) per template entry
      PtjSite *ss = (PtjSite *)calloc(g_n_interp_roots, sizeof(PtjSite));
      int ns = 0;
      for (uint32_t i = 0; i < g_n_interp_roots; i++) {
        ss[ns].off = g_interp_roots[i]; ss[ns].id = (uint16_t)i;
        ss[ns].when = PTJ_WHEN_BEFORE; ss[ns].kind = PTJ_KIND_REG; ss[ns].size = 8;
        ss[ns].nregs = 1; ss[ns].regs[0] = 13;                 // %r13
        ns++;
      }
      if (!g_tslab) tslab_init_near(g_interp_addr, g_slab_mb << 20);
      int np = apply_patch((uint8_t *)g_interp_addr, g_interp_len, ss, ns, 3,
                           g_interp_roots, g_n_interp_roots,
                           g_interp_roots, g_n_interp_roots);
      g_c.interp_entries = g_n_interp_roots;
      g_c.interp_patched = (uint64_t)np;
      free(ss);
    }
  }
  if (g_mode >= MODE_ENTRY) {
    build_forbid(code_addr, map_length, map);
    patch_object((uint8_t *)code_addr, (size_t)code_size, nbuf, nl, 1, g_mode == MODE_ENTRY);
  }
  pthread_mutex_unlock(&g_lock);
  g_c.hook_ns += ns_now() - t0;
}

static void JNICALL cb_compiled_unload(jvmtiEnv *jvmti, jmethodID method, const void *code_addr) {
  (void)jvmti; (void)method;
  uint64_t t0 = ns_now();
  pthread_mutex_lock(&g_lock);
  g_c.unload++;
  if (g_pinbridge) {
    g_pin_live.erase((uint64_t)code_addr);
    pin_publish(PT_PIN_REMOVE, (uint64_t)code_addr, 1);
    g_pin_retired++;
  }
  if (g_mode >= MODE_DUMP) {
    pthread_mutex_lock(&g_patchlock);
    ptj_arena_record(&g_dump, 2, 1, code_addr, nullptr, nullptr, 0, nullptr, 0, 0);
    pthread_mutex_unlock(&g_patchlock);
  }
  if (g_nunloaded < 4096) g_unloaded[g_nunloaded++] = (uint64_t)code_addr;
  // Retire the trampolines of the nmethod that lived here: its address can be handed to a
  // new compilation at any time, and a stale record would make `verify()` and the offline
  // time-keyed lookup both wrong.
  if (g_mode >= MODE_ENTRY) {
    pthread_mutex_lock(&g_patchlock);
    live_retire((uint64_t)code_addr, (uint64_t)code_addr + 1, &g_c.unload_retired);
    pthread_mutex_unlock(&g_patchlock);
  }
  pthread_mutex_unlock(&g_lock);
  g_c.hook_ns += ns_now() - t0;
}

static void JNICALL cb_dynamic_code(jvmtiEnv *jvmti, const char *name,
                                    const void *address, jint length) {
  (void)jvmti;
  uint64_t t0 = ns_now();
  g_ev_ns = t0;
  pthread_mutex_lock(&g_lock);
  g_c.dyn++; g_c.dyn_bytes += (uint64_t)length;
  note_event_thread();
  // Reserve the trampoline slab now: these blobs live in the same code cache as the
  // nmethods, they arrive during VM init, and the address space around the code cache is
  // much emptier before the Java heap is committed.
  if (g_mode >= MODE_ENTRY && !g_pinbridge && !g_tslab)
    tslab_init_near((uint64_t)address, g_slab_mb << 20);
  size_t nl = name ? strlen(name) : 0;
  if (g_mode >= MODE_DUMP) {
    pthread_mutex_lock(&g_patchlock);
    ptj_arena_record(&g_dump, 0, 3, address, nullptr, address, (size_t)length, name, nl, 1);
    pthread_mutex_unlock(&g_patchlock);
  }
  int is_interp = (name && !strcmp(name, "Interpreter"));
  if (is_interp) { g_interp_addr = (uint64_t)address; g_interp_len = (uint32_t)length; }
  if (g_mode >= MODE_ENTRY && !is_interp && g_patch_stubs) {
    g_nforbid = 0;
    // Other arraycopy blobs branch directly to +4, bypassing the frame prologue.
    // Without that cross-object entry a 5-byte detour at +0 can overwrite it.
    // Feed the verified boundary to BOTH analysis and the window planner; the
    // existing root-aware cache identity keeps old plans separate.
    uint32_t secondary = !g_pinbridge ? ptj_hs_arraycopy_entry((const uint8_t *)address, (size_t)length, name) : 0;
    if (secondary) { g_forbid[g_nforbid++] = secondary; g_stub_secondary_entries++; }
    patch_object((uint8_t *)address, (size_t)length, name, nl, 3, g_mode == MODE_ENTRY);
  } else if (g_mode >= MODE_ENTRY) {
    g_c.skipped_type++;
  }
  pthread_mutex_unlock(&g_lock);
  g_c.hook_ns += ns_now() - t0;
}

// ------------------------------------------------------------- reporting ----
// The clobber check of the Node runtime: how many of our windows still hold the bytes we
// wrote?  A JIT that rewrites its own code under us shows up here and nowhere else.
static void verify_pass(uint64_t *intact, uint64_t *clob, uint64_t *retired, uint64_t *gone,
                        uint64_t *restored, uint64_t *tail_only, uint64_t *other) {
  for (uint64_t i = 0; i < g_nprec && g_prec; i++) {
    PatchRec *r = &g_prec[i];
    if (r->dead || !r->wlen) { (*retired)++; continue; }
    uint8_t cur[32];
    if (!safe_read((const void *)r->addr, cur, r->wlen)) { (*gone)++; continue; }
    if (!memcmp(cur, r->want, r->wlen)) { (*intact)++; continue; }
    (*clob)++;
    if (!memcmp(cur, r->orig, r->wlen)) (*restored)++;
    else if (cur[0] == 0xe9 && !memcmp(cur + 1, r->want + 1, 4)) (*tail_only)++;
    else (*other)++;
#ifdef PTJ_CLOBDUMP
    // Diagnostic build (`./build.sh --clobdump`, path in $PTJ_CLOBDUMP): dump enough to
    // disassemble the window and name whatever overwrote it.  See jit_java.md section 5b,
    // "One open issue the loop exposed".
    { FILE *cd = fopen(getenv("PTJ_CLOBDUMP") ? getenv("PTJ_CLOBDUMP") : "/tmp/ptj_clob.txt", "a");
      if (cd) {
        fprintf(cd, "addr=%#llx obj=%#llx+%u off=%llu wlen=%u kind=%s\n",
                (unsigned long long)r->addr, (unsigned long long)r->obj_addr, r->obj_len,
                (unsigned long long)(r->addr - r->obj_addr), r->wlen,
                !memcmp(cur, r->orig, r->wlen) ? "restored"
                  : (cur[0] == 0xe9 ? "tail_only" : "other"));
        fprintf(cd, "  orig:"); for (uint32_t z = 0; z < r->wlen; z++) fprintf(cd, " %02x", r->orig[z]);
        fprintf(cd, "\n  want:"); for (uint32_t z = 0; z < r->wlen; z++) fprintf(cd, " %02x", r->want[z]);
        fprintf(cd, "\n  cur :"); for (uint32_t z = 0; z < r->wlen; z++) fprintf(cd, " %02x", cur[z]);
        fprintf(cd, "\n"); fclose(cd); } }
#endif
  }
}

static void dump_maps(const char *path) {
  FILE *in = fopen("/proc/self/maps", "r"), *out = fopen(path, "w");
  if (in && out) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, in)) > 0) fwrite(b, 1, n, out); }
  if (in) fclose(in);
  if (out) fclose(out);
}

// ctr=1: dump `idx tramp_addr window_addr count' for every trampoline that got a counter.
static void write_ctrdump(void) {
  if (!g_ctrdump || !g_cx.ctr) return;
  FILE *f = fopen(g_ctrdump, "w");
  if (!f) return;
  uint32_t n = g_cx.ntramps < g_cx.ctr_max ? g_cx.ntramps : g_cx.ctr_max;
  fprintf(f, "# idx tramp_addr window_addr count   (trampolines=%u counted=%u)\n",
          g_cx.ntramps, n);
  for (uint32_t i = 0; i < n; i++)
    fprintf(f, "%u %llu %llu %llu\n", i, (unsigned long long)g_cx.tramps[i],
            (unsigned long long)g_cx.ctr_orig[i], (unsigned long long)g_cx.ctr[i]);
  fclose(f);
}

static void write_instlog(void) {
  if (!g_instlog || !g_instlog_buf) return;
  FILE *f = fopen(g_instlog, "w");
  if (!f) return;
  fprintf(f, "# ns %s   (agent_load_ns=%llu entries=%u)\n",
          g_pinbridge ? "pin_objects pin_sites" : "windows values",
          (unsigned long long)g_c.agent_load_ns, g_instlog_n);
  for (uint32_t i = 0; i < g_instlog_n; i++)
    fprintf(f, "%llu %llu %llu\n", (unsigned long long)g_instlog_buf[i][0],
            (unsigned long long)g_instlog_buf[i][1], (unsigned long long)g_instlog_buf[i][2]);
  fclose(f);
}

static void write_stats(void) {
  write_ctrdump();
  write_instlog();
  write_objlog();
  FILE *f = g_statsfile ? fopen(g_statsfile, "w") : stderr;
  if (!f) f = stderr;
  for (int w = 0; w < g_nworkers && g_wcli; w++) {
    g_cli.calls += g_wcli[w].calls; g_cli.call_ns += g_wcli[w].call_ns;
    g_cli.mem_hits += g_wcli[w].mem_hits; g_cli.disk_hits += g_wcli[w].disk_hits;
    g_cli.disk_writes += g_wcli[w].disk_writes; g_cli.errors += g_wcli[w].errors;
    g_cli.reconnects += g_wcli[w].reconnects; g_cli.unserved += g_wcli[w].unserved;
  }
  for (int w = 0; w < g_nfast && g_fcli; w++) {          // D-J14: the cache lane
    g_cli.calls += g_fcli[w].calls; g_cli.call_ns += g_fcli[w].call_ns;
    g_cli.mem_hits += g_fcli[w].mem_hits; g_cli.disk_hits += g_fcli[w].disk_hits;
    g_cli.disk_writes += g_fcli[w].disk_writes; g_cli.errors += g_fcli[w].errors;
    g_cli.reconnects += g_fcli[w].reconnects; g_cli.unserved += g_fcli[w].unserved;
  }
  uint64_t intact = 0, clob = 0, retired = 0, gone = 0, rest = 0, tail = 0, oth = 0;
  if (g_mode >= MODE_ENTRY) verify_pass(&intact, &clob, &retired, &gone, &rest, &tail, &oth);
  fprintf(f, "PTJAVA {");
  fprintf(f, "\"mode\":%d,\"ptw\":%d,", g_mode, g_use_ptwrite);
  fprintf(f, "\"analysis_fast\":%d,\"noroots\":%d,\"interp_mode\":%d,\"patch_stubs\":%d,",
          g_cli.fast, g_noroots, g_patch_interp, g_patch_stubs);
  fprintf(f, "\"stub_secondary_entries\":%llu,", (unsigned long long)g_stub_secondary_entries);
  fprintf(f, "\"bootstrap_ms\":%u,\"bootstrap_ns\":%llu,", g_bootstrap_ms,
          (unsigned long long)g_bootstrap_ns);
  fprintf(f, "\"relative_avoid\":%d,", g_relative_avoid);
  fprintf(f, "\"pin_bridge\":%d,\"pin_objects\":%llu,\"pin_sites\":%llu,\"pin_retired\":%llu,"
             "\"pin_objects_at_exit\":%llu,\"pin_sites_at_exit\":%llu,",
          g_pinbridge, (unsigned long long)g_pin_objects, (unsigned long long)g_pin_sites,
          (unsigned long long)g_pin_retired, (unsigned long long)g_pin_objects_at_exit,
          (unsigned long long)g_pin_sites_at_exit);
  fprintf(f, "\"compiled_load\":%llu,\"compiled_unload\":%llu,\"dynamic_code\":%llu,\"recompiles\":%llu,",
          (unsigned long long)g_c.load, (unsigned long long)g_c.unload,
          (unsigned long long)g_c.dyn, (unsigned long long)g_c.recompiles);
  fprintf(f, "\"nmethod_bytes\":%llu,\"dyn_bytes\":%llu,",
          (unsigned long long)g_c.nmethod_bytes, (unsigned long long)g_c.dyn_bytes);
  fprintf(f, "\"hook_ns\":%llu,\"patch_ns\":%llu,\"hash_ns\":%llu,\"analysis_ns\":%llu,",
          (unsigned long long)g_c.hook_ns, (unsigned long long)g_c.patch_ns,
          (unsigned long long)g_c.hash_ns, (unsigned long long)g_cli.call_ns);
  fprintf(f, "\"analysis_calls\":%llu,\"analysis_errors\":%llu,",
          (unsigned long long)g_cli.calls, (unsigned long long)g_cli.errors);
  // unserved > 0 means objects went UNANALYSED because no `--serve` process answered:
  // the run is then silently less instrumented than it looks.  See jit_java_perf.md.
  fprintf(f, "\"analysis_unserved\":%llu,\"analysis_reconnects\":%llu,",
          (unsigned long long)g_cli.unserved, (unsigned long long)g_cli.reconnects);
  fprintf(f, "\"cache_mem_hits\":%llu,\"cache_disk_hits\":%llu,\"cache_disk_writes\":%llu,",
          (unsigned long long)g_cli.mem_hits, (unsigned long long)g_cli.disk_hits,
          (unsigned long long)g_cli.disk_writes);
  fprintf(f, "\"objs_analyzed\":%llu,\"objs_patched\":%llu,",
          (unsigned long long)g_c.objs_analyzed, (unsigned long long)g_c.objs_patched);
  fprintf(f, "\"avoid\":{\"rounds_max\":%d,\"objs\":%llu,\"rounds_used\":%llu,\"addrs\":%llu,\"recovered\":%llu,\"unplaceable\":%llu,\"baseline_unplaceable\":%llu,\"converged\":%llu,\"plan_passes\":%llu,\"plan_ms\":%llu},",
          g_avoid_rounds, (unsigned long long)g_c.avoid_objs,
          (unsigned long long)g_c.avoid_rounds_used, (unsigned long long)g_c.avoid_addrs,
          (unsigned long long)g_c.avoid_recovered,
          (unsigned long long)g_c.avoid_unplaceable,
          (unsigned long long)g_c.avoid_baseline_unpl,
          (unsigned long long)g_c.avoid_converged,
          (unsigned long long)g_c.plan_passes,
          (unsigned long long)(g_c.plan_ns / 1000000));
  fprintf(f, "\"sites_requested\":%llu,\"sites_patched\":%llu,\"values\":%llu,",
          (unsigned long long)g_c.sites_req, (unsigned long long)g_c.sites_patched,
          (unsigned long long)g_cx.n_values);
  fprintf(f, "\"windows\":%llu,\"trampolines\":%u,\"map_entries\":%u,\"map_relocated\":%u,",
          (unsigned long long)g_nprec, g_cx.ntramps, g_cx.nentries, g_cx.nrelocs);
  fprintf(f, "\"tslab_base\":%llu,\"tslab_used\":%llu,\"tslab_live\":%llu,",
          (unsigned long long)(uintptr_t)g_tslab, (unsigned long long)g_tslab_used,
          (unsigned long long)g_tslab_live);
  fprintf(f, "\"installs\":%llu,\"int3_traps\":%llu,\"foreign_traps\":%llu,\"sync_calls\":%llu,"
             "\"have_sync_core\":%d,",
          (unsigned long long)g_c.installs, (unsigned long long)g_c.int3_traps,
          (unsigned long long)g_c.foreign_traps, (unsigned long long)g_c.sync_calls,
          g_have_sync_core);
  fprintf(f, "\"quiesce\":%d,\"qsig\":%d,\"quiesce_rounds\":%llu,\"quiesce_signals\":%llu,"
             "\"quiesce_redirects\":%llu,\"quiesce_timeouts\":%llu,\"quiesce_unknown_rip\":%llu,"
             "\"quiesce_ns\":%llu,\"selftest_int3\":%llu,\"selftest_quiesce\":%llu,",
          g_quiesce, g_qsig, (unsigned long long)g_c.quiesce_rounds, (unsigned long long)g_c.quiesce_signals,
          (unsigned long long)g_c.quiesce_redirects, (unsigned long long)g_c.quiesce_timeouts,
          (unsigned long long)g_c.quiesce_unknown_rip, (unsigned long long)g_c.quiesce_ns,
          (unsigned long long)g_c.selftest_int3,
          (unsigned long long)g_c.selftest_quiesce);
  // D-J13: implicit exceptions translated out of the trampoline slab.
  fprintf(f, "\"xlat\":%d,\"xlat_hits\":%llu,\"xlat_other\":%llu,\"xlat_sigs\":%llu,"
             "\"xlat_relmap\":%llu,",
          g_xlat, (unsigned long long)g_c.xlat_hits, (unsigned long long)g_c.xlat_other,
          (unsigned long long)g_c.xlat_sigs, (unsigned long long)g_c.xlat_relmap);
  fprintf(f, "\"restart_objs\":%llu,\"restart_roots\":%llu,",
          (unsigned long long)g_c.restart_objs, (unsigned long long)g_c.restart_roots);
  fprintf(f, "\"ve_none\":%llu,\"ve_gt_0x20\":%llu,\"locmap_objs\":%llu,\"locmap_entries\":%llu,",
          (unsigned long long)g_c.ve_none, (unsigned long long)g_c.ve_gt_20,
          (unsigned long long)g_c.locmap_objs, (unsigned long long)g_c.locmap_entries);
  fprintf(f, "\"skipped_small\":%llu,\"skipped_cap\":%llu,\"skipped_type\":%llu,"
             "\"skipped_no_entry\":%llu,\"entry_probe_tries\":%llu,",
          (unsigned long long)g_c.skipped_small, (unsigned long long)g_c.skipped_cap,
          (unsigned long long)g_c.skipped_type, (unsigned long long)g_c.skipped_nolo,
          (unsigned long long)g_c.entry_probe_tries);
  fprintf(f, "\"agent_load_ns\":%llu,\"first_install_ns\":%llu,\"last_install_ns\":%llu,",
          (unsigned long long)g_c.agent_load_ns, (unsigned long long)g_c.first_install_ns,
          (unsigned long long)g_c.last_install_ns);
  fprintf(f, "\"superseded\":%llu,\"unload_retired\":%llu,\"mprotect_calls\":%llu,"
             "\"mprotect_fail\":%llu,",
          (unsigned long long)g_c.superseded, (unsigned long long)g_c.unload_retired,
          (unsigned long long)g_c.mprotect_calls, (unsigned long long)g_c.mprotect_fail);
  fprintf(f, "\"verify\":{\"intact\":%llu,\"clobbered\":%llu,\"retired\":%llu,\"unmapped\":%llu,"
             "\"restored\":%llu,\"tail_only\":%llu,\"other\":%llu},",
          (unsigned long long)intact, (unsigned long long)clob, (unsigned long long)retired,
          (unsigned long long)gone, (unsigned long long)rest, (unsigned long long)tail,
          (unsigned long long)oth);
  fprintf(f, "\"dropped\":{");
  int first = 1;
  for (int i = 0; i < PTJ_DROP_N; i++)
    if (g_cx.drops[i]) {
      fprintf(f, "%s\"%s\":%llu", first ? "" : ",", PTJ_DROP_NAME[i],
              (unsigned long long)g_cx.drops[i]);
      first = 0;
    }
  fprintf(f, "},");
  fprintf(f, "\"at_exit\":{\"windows\":%llu,\"values\":%llu,\"objects\":%llu},",
          (unsigned long long)g_c.win_at_exit, (unsigned long long)g_c.val_at_exit,
          (unsigned long long)g_c.obj_at_exit);
  fprintf(f, "\"sites_at_exit\":%llu,", (unsigned long long)g_c.sites_at_exit);
  fprintf(f, "\"interp\":{\"addr\":%llu,\"entries\":%llu,\"patched\":%llu,\"len\":%u},",
          (unsigned long long)g_interp_addr,
          (unsigned long long)g_c.interp_entries, (unsigned long long)g_c.interp_patched,
          g_interp_len);
  fprintf(f, "\"interp_full_sites\":%llu,\"interp_full_sites_at_exit\":%llu,\"interp_install_ns\":%llu,",
          (unsigned long long)__atomic_load_n(&g_interp_full_sites, __ATOMIC_ACQUIRE),
          (unsigned long long)g_interp_full_sites_at_exit,
          (unsigned long long)__atomic_load_n(&g_interp_install_ns, __ATOMIC_ACQUIRE));
  // DEFECT D-J3.2: CODE_ADDED -> patched, in milliseconds.  A workload shorter than p90
  // here ran on a JVM whose hot nmethods were never instrumented.
  fprintf(f, "\"patch_latency_ms\":{\"n\":%llu,\"mean\":%.1f,\"p50\":%.1f,\"p90\":%.1f,"
             "\"p99\":%.1f,\"max\":%.1f,\"queue_mean\":%.1f,\"queue_max\":%.1f},",
          (unsigned long long)g_c.lat_n,
          g_c.lat_n ? (double)g_c.lat_sum / g_c.lat_n / 1e6 : 0.0,
          lat_pct(50) / 1e6, lat_pct(90) / 1e6, lat_pct(99) / 1e6, g_c.lat_max / 1e6,
          g_c.lat_n ? (double)g_c.qlat_sum / g_c.lat_n / 1e6 : 0.0, g_c.qlat_max / 1e6);
  fprintf(f, "\"keyframe\":{\"period\":%u,\"requested\":%llu,\"sites\":%llu,\"values\":%llu,"
             "\"counters\":%u},",
          g_keyframe, (unsigned long long)g_cx.kf_requested, (unsigned long long)g_cx.kf_sites,
          (unsigned long long)g_cx.kf_values, g_cx.kfctr_used);
  fprintf(f, "\"gt\":{\"sites\":%u,\"refused\":%llu,\"records\":%llu},",
          g_cx.ngtents, (unsigned long long)g_cx.gt_refused,
          (unsigned long long)(g_gt.base ? (ptj_gt_used(&g_gt) - 32) / 16 : 0));
  fprintf(f, "\"queue\":{\"enqueued\":%llu,\"done\":%llu,\"dropped\":%llu,\"stale\":%llu,"
             "\"workers\":%d,\"lanes\":%d,\"fastworkers\":%d,\"analyzer_lane\":%llu,"
             "\"analyzer_lane_done\":%llu,\"analyzer_lane_dropped\":%llu,\"analyzer_lane_hi\":%llu,\"prio\":%d},\"maskverify\":{\"checked\":%llu,\"same\":%llu,\"different\":%llu,\"analyzer_self_same\":%llu,\"analyzer_self_diff\":%llu,\"diff_same_value_set\":%llu,\"diff_other_value_set\":%llu,\"resync\":%d},",
          (unsigned long long)g_q_enq, (unsigned long long)g_q_done,
          (unsigned long long)g_q_dropped, (unsigned long long)g_q_stale, g_nworkers,
          g_lanes, g_nfast, (unsigned long long)g_aq_enq,
          (unsigned long long)g_aq_done, (unsigned long long)g_aq_dropped,
          (unsigned long long)g_aq_hi, g_prio,
          (unsigned long long)g_mv_checked, (unsigned long long)g_mv_same,
          (unsigned long long)g_mv_diff, (unsigned long long)g_mv_self_same,
          (unsigned long long)g_mv_self_diff, (unsigned long long)g_mv_same_values,
          (unsigned long long)g_mv_diff_values, g_mask_resync);
  // D-J14: the same publication latency, split by stage and by who answered.  Milliseconds,
  // MEANS over the population named by "n".  evt+queue+hash+cache+anal+pub should account for
  // `total' up to scheduling slop.
  {
    const char *nm[2] = {"cache", "analyzer"};
    StageSum *ss[2] = {&g_sg_hit, &g_sg_miss};
    fprintf(f, "\"stage_ms\":{");
    for (int i = 0; i < 2; i++) {
      StageSum *x = ss[i];
      double d = x->n ? (double)x->n * 1e6 : 1.0;
      fprintf(f, "%s\"%s\":{\"n\":%llu,\"evt\":%.2f,\"queue\":%.2f,\"hash\":%.2f,"
                 "\"cache\":%.2f,\"anal\":%.2f,\"pub\":%.2f,\"total\":%.2f}",
              i ? "," : "", nm[i], (unsigned long long)x->n,
              x->evt / d, x->queue / d, x->hash / d, x->cache / d,
              x->anal / d, x->pub / d, x->total / d);
    }
    fprintf(f, "},");
  }
  fprintf(f, "\"event_threads\":\"");
  for (int i = 0; i < g_event_threads; i++) fprintf(f, "%s%s", i ? "|" : "", g_event_thread_names[i]);
  fprintf(f, "\",\"arena_used\":%zu,\"dump_truncated\":%llu}\n",
          g_dump.used, (unsigned long long)g_dump.truncated);
  fflush(f);
  if (f != stderr) fclose(f);
}

// BUFFER SINK: `ptlog_jit_arm' from runtime/rt/ptlogrt.so, resolved at Agent_OnLoad.
static void (*g_rt_arm)(void) = nullptr;

// HotSpot installs its own SIGSEGV handler (implicit null checks, the safepoint polling
// page, stack banging) AFTER Agent_OnLoad and does not chain to what it displaced unless
// libjsig is preloaded.  VM_INIT is the first point at which the VM's handlers are all in
// place, so this is where the buffer sink's guard-page handler goes back in FRONT of them,
// keeping HotSpot's as its own chain target the design notes.
static void JNICALL cb_vm_init(jvmtiEnv *jvmti, JNIEnv *env, jthread thr) {
  (void)jvmti; (void)env; (void)thr;
  if (g_pinbridge) return;   // no native detours, signal handlers or quiescence protocol
  if (g_cx.sink_buffer && g_rt_arm) g_rt_arm();
  if (g_mode >= MODE_ENTRY) {
    // D-J13: our fault translator goes in front of the VM's handlers (and of the buffer
    // sink's, armed just above), so that an implicit exception in a displaced instruction
    // still reaches the VM as one.
    if (g_xlat) xlat_install();
    // D-J12a/b self-tests, after the VM's own handlers are installed (VM_INIT is the first
    // point where they all are): (1) an int3 we plant must reach trap_handler; (2) every
    // thread must answer the quiescence signal.
    int r = int3_selftest();
    g_c.selftest_int3 = (r == 1);
    if (r != 1) {
      fprintf(stderr, "PTJAVA FATAL: a planted int3 did not reach the SIGTRAP redirect handler "
                      "(r=%d): a ptrace tracer is swallowing SIGTRAP, or the handler was displaced. "
                      "The int3->jmp install would resume racing threads at site+1 (D-J12a). "
                      "%s\n", r, g_selftest ? "Aborting (selftest=0 overrides)." : "Continuing (selftest=0).");
      if (g_selftest) _exit(72);
    }
    if (g_quiesce) {
      g_pend_lo = g_pend_hi = 0;
      uint32_t miss = quiesce_all(2000);
      g_c.selftest_quiesce = (miss == 0);
      g_c.quiesce_rounds = 0; g_c.quiesce_signals = 0; g_c.quiesce_ns = 0;   // the probe is not a commit
      if (miss) fprintf(stderr, "PTJAVA WARNING: %u threads did not answer the quiescence signal %d at VM_INIT\n", miss, g_qsig);
    }
  }
  if (g_bootstrap_ms) {
    uint64_t begin = ns_now();
    pthread_mutex_lock(&g_lock);
    // At VM_INIT the interpreter dispatch tables are initialized. A prior
    // CompiledMethodLoad may already have queued the same object.
    if (!g_interp_done && g_interp_addr) {
      g_interp_done = 1;
      harvest_interp_entries(g_interp_addr, g_interp_addr + g_interp_len);
      g_c.interp_entries = g_n_interp_roots;
      if (g_n_interp_roots)
        enqueue_job((uint8_t *)g_interp_addr, g_interp_len, "Interpreter", 11, 3,
                    g_interp_roots, g_n_interp_roots);
    }
    int have_roots = g_n_interp_roots != 0;
    pthread_mutex_unlock(&g_lock);
    uint64_t deadline = begin + (uint64_t)g_bootstrap_ms * 1000000ull;
    while (have_roots && !__atomic_load_n(&g_interp_full_sites, __ATOMIC_ACQUIRE) && ns_now() < deadline) {
      struct timespec delay = {0, 1000000}; nanosleep(&delay, nullptr);
    }
    g_bootstrap_ns = ns_now() - begin;
    if (!have_roots || !__atomic_load_n(&g_interp_full_sites, __ATOMIC_ACQUIRE)) {
      fprintf(stderr, "PTJAVA: full interpreter bootstrap failed within %u ms (roots=%u)\n",
              g_bootstrap_ms, g_n_interp_roots);
      _exit(70);
    }
  }
}

static void JNICALL cb_vm_death(jvmtiEnv *jvmti, JNIEnv *env) {
  (void)jvmti; (void)env;
  g_interp_full_sites_at_exit = __atomic_load_n(&g_interp_full_sites, __ATOMIC_ACQUIRE);
  pthread_mutex_lock(&g_lock);
  g_pin_objects_at_exit = g_pin_objects;
  g_pin_sites_at_exit = g_pin_sites;
  pthread_mutex_unlock(&g_lock);
  // Finish the analysis backlog before reporting: what is still queued has not been
  // patched, and the site map must describe what is actually in the code.
  // What was actually instrumented *while the benchmark ran*: the drain below installs the
  // backlog, which is correct for the site map (every record is tsc-stamped) but must not
  // be counted as coverage during the run.
  g_c.win_at_exit = g_nprec; g_c.val_at_exit = g_cx.n_values; g_c.obj_at_exit = g_cx.obj;
  g_c.sites_at_exit = g_c.sites_patched;   // in-run site coverage, before the drain
  if (g_nworkers) {
    drain_queue((int)g_drain_ms);
    pthread_mutex_lock(&g_qlock); g_qstop = 1;
    pthread_cond_broadcast(&g_qcond);
    pthread_cond_broadcast(&g_aqcond);
    // Setting g_qstop only stops workers from taking a NEW job.  A worker already inside
    // apply_patch still has a trampoline to install and a site-map record to add, and
    // flushing the dump underneath it yields a site map naming a trampoline whose code
    // object never reached the dump -- check_sitemap's `tramp_not_in_dump` /
    // `reloc_no_tramp`, seen exactly once when a run did not fully drain.  So wait for the
    // in-flight workers (bounded: VMDeath must not hang the JVM).
    {
      struct timespec dl;
      clock_gettime(CLOCK_REALTIME, &dl);
      dl.tv_sec += 10;
      while (g_workers_busy > 0 || g_fast_busy > 0)
        if (pthread_cond_timedwait(&g_qcond, &g_qlock, &dl) == ETIMEDOUT) break;
    }
    if (g_pinbridge && (g_workers_busy || g_fast_busy || g_qhead != g_qtail ||
                        g_aqhead != g_aqtail || g_aqhihead != g_aqhitail)) {
      fprintf(stderr, "PTJAVA pin=1: analysis did not drain; refusing partial final statistics\n");
      _exit(70);
    }
    pthread_mutex_unlock(&g_qlock);
  }
  pthread_mutex_lock(&g_lock);
  if (g_mode >= MODE_DUMP) {
    if (g_tslab && g_tslab_used)
      ptj_arena_record(&g_dump, 5, 1, g_tslab, nullptr, g_tslab, g_tslab_used, nullptr, 0, 1);
    ptj_arena_flush(&g_dump, g_dumpfile);
  }
  ptj_gt_close(&g_gt);            // record the byte count; runtime/jit/gt_finish.py truncates
  if (g_mode >= MODE_ENTRY && g_sitemapfile)
    ptj_write_sitemap(g_sitemapfile, "hotspot", (int)getpid(), &g_cx, g_objs, g_cx.obj,
                      g_tslab, g_tslab_cap, g_tslab_used, g_use_ptwrite, g_space, g_keyframe);
  if (g_hashdump) { fflush(g_hashdump); }
  // Reap the analyzer services we spawned.  They are per-process, and one left behind both
  // wastes ~200 MB and holds this process's inherited stderr open.
  for (int w = 0; w < g_nworkers && g_wcli; w++)
    if (g_wcli[w].svc_pid > 0) kill(g_wcli[w].svc_pid, SIGTERM);
  if (g_cli.svc_pid > 0) kill(g_cli.svc_pid, SIGTERM);
  if (g_mapsfile) dump_maps(g_mapsfile);
  write_stats();
  pthread_mutex_unlock(&g_lock);
}

// ----------------------------------------------------------------- setup ----
static const char *opt_str(char *opts, const char *k, const char *dflt) {
  size_t kl = strlen(k);
  for (char *p = opts; p && *p;) {
    char *e = strchr(p, ',');
    if (!strncmp(p, k, kl) && p[kl] == '=') {
      size_t vl = e ? (size_t)(e - p - kl - 1) : strlen(p + kl + 1);
      char *v = (char *)malloc(vl + 1);
      memcpy(v, p + kl + 1, vl); v[vl] = 0;
      return v;
    }
    if (!e) break;
    p = e + 1;
  }
  return dflt;
}
static long opt_num(char *opts, const char *k, long dflt) {
  const char *v = opt_str(opts, k, nullptr);
  return v ? atol(v) : dflt;
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
  g_c.agent_load_ns = ptj_ns();
  (void)reserved;
  jvmtiEnv *jvmti = nullptr;
  if (vm->GetEnv((void **)&jvmti, JVMTI_VERSION_1_2) != JNI_OK) return JNI_ERR;

  char *o = options ? strdup(options) : strdup("");
  g_mode = (int)opt_num(o, "mode", 1);
  const char *pin = opt_str(o, "pin", "0");
  if (strcmp(pin, "0") && strcmp(pin, "1")) {
    fprintf(stderr, "PTJAVA: pin must be 0 or 1\n"); _exit(71);
  }
  g_pinbridge = !strcmp(pin, "1");
  if (g_pinbridge && (g_mode != MODE_FULL || opt_num(o, "keyframe", 1024) != 0 ||
                     opt_str(o, "sink", nullptr) || opt_num(o, "gt", 0) || opt_num(o, "ctr", 0))) {
    fprintf(stderr, "PTJAVA pin=1 requires mode=4,keyframe=0 and no sink/gt/ctr override\n");
    _exit(71);
  }
  if (g_pinbridge) pin_publish(PT_PIN_REMOVE, (uint64_t)&ptj_pin_publish, 1);
  g_verbose = (int)opt_num(o, "verbose", 0);
  const char *relative_avoid = opt_str(o, "relativeavoid", "0");
  if (strcmp(relative_avoid, "0") && strcmp(relative_avoid, "1")) {
    fprintf(stderr, "PTJAVA: relativeavoid must be 0 or 1\n"); _exit(71);
  }
  g_relative_avoid = !strcmp(relative_avoid, "1");
  g_diag = (int)opt_num(o, "diag", 0);
  g_use_ptwrite = (int)opt_num(o, "ptw", 1);
  if (g_pinbridge) g_use_ptwrite = 0;
  g_space = (uint32_t)opt_num(o, "space", 3);
  const char *interp = opt_str(o, "interp", "0");
  if (strcmp(interp, "0") && strcmp(interp, "1") && strcmp(interp, "2")) {
    fprintf(stderr, "PTJAVA: interp must be 0, 1 (legacy native probes), or 2 (full analysis)\n");
    _exit(71);
  }
  g_patch_interp = atoi(interp);
  if (g_patch_interp == 2 && g_mode != MODE_FULL) {
    fprintf(stderr, "PTJAVA: interp=2 requires mode=4\n"); _exit(71);
  }
  g_patch_stubs = (int)opt_num(o, "stubs", 0);
  const char *bootstrap = opt_str(o, "bootstrapms", "0");
  char *bootstrap_end = nullptr;
  long bootstrap_value = strtol(bootstrap, &bootstrap_end, 10);
  if (!*bootstrap || !bootstrap_end || *bootstrap_end || bootstrap_value < 0 || bootstrap_value > 300000 ||
      (bootstrap_value && (g_pinbridge || g_mode != MODE_FULL || g_patch_interp != 2))) {
    fprintf(stderr, "PTJAVA: bootstrapms must be 0..300000; positive requires native mode=4,interp=2\n");
    _exit(71);
  }
  g_bootstrap_ms = (uint32_t)bootstrap_value;
  g_arena_mb = (size_t)opt_num(o, "arenamb", 1024);
  g_slab_mb = (size_t)opt_num(o, "slabmb", 64);
  g_min_patch = (uint64_t)opt_num(o, "minpatch", 0);
  { long mp = opt_num(o, "maxpatch", -1); if (mp >= 0) g_max_patch = (uint64_t)mp; }
  g_dumpfile = opt_str(o, "dump", nullptr);
  g_sitemapfile = opt_str(o, "sitemap", nullptr);
  g_statsfile = opt_str(o, "stats", nullptr);
  g_mapsfile = opt_str(o, "maps", nullptr);

  g_maxlen = (uint32_t)opt_num(o, "maxlen", 0);
  if (g_mode >= MODE_DUMP) {
    ptj_arena_init(&g_dump, g_arena_mb << 20);
    ZydisDecoderInit(&g_dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    const char *hd = opt_str(o, "hashdump", nullptr);
    if (hd) g_hashdump = fopen(hd, "w");
  }
  if (g_mode >= MODE_ENTRY) {
    if (!g_pinbridge) { sync_core_init(); trap_install(); }
    g_quiesce = (int)opt_num(o, "quiesce", 1);
    g_xlat = (int)opt_num(o, "xlat", 1);   // D-J13 fault translation (xlat=0 = the A/B control)
    if (g_pinbridge) { g_quiesce = 0; g_xlat = 0; }
    g_qsig = (int)opt_num(o, "qsig", 0);
    g_selftest = (int)opt_num(o, "selftest", 1);
    if (!g_pinbridge) quiesce_install();
    memset(&g_cx, 0, sizeof g_cx);
    g_cx.dec = &g_dec;
    g_cx.use_ptwrite = g_use_ptwrite;
    g_cx.space = (int)g_space;
    g_cx.sparkplug = 0;                       // a V8 rule; HotSpot has its own (hs_patch_lo)
    g_cx.single = (int)opt_num(o, "single", 0);
    // ---- BUFFER SINK the design notes ---------------------------------
    // `sink=buffer': a logged value is STORED into the thread's %gs ring instead of being
    // PTWRITEd.  The ring, its guard-page handler, the cv files and the sync markers are
    // runtime/rt/ptlogrt.c -- the same runtime the E9Patch buffer sink uses -- so `ptrecon'
    // consumes the run unchanged.  Both it and the pthread shim must be preloaded:
    //   LD_PRELOAD=runtime/rt/ptlogmt.so:runtime/rt/ptlogrt.so
    // The shim gives every Java thread its OWN region .  Unlike a whole-program ELF
    // build it is SUFFICIENT here (no spare-TCB handoff needed,  the earliest
    // instrumented instruction a new thread can execute is JIT code, which is far past the
    // pthread start-routine wrapper, so no thread ever logs through its creator's cursor.
    { const char *sk = opt_str(o, "sink", nullptr);
      if (sk && !strcmp(sk, "buffer")) {
        unsigned long (*abi)(void) = (unsigned long (*)(void))dlsym(RTLD_DEFAULT, "ptlog_jit_abi");
        g_rt_arm = (void (*)(void))dlsym(RTLD_DEFAULT, "ptlog_jit_arm");
        if (!abi || !g_rt_arm || abi() == 0) {
          fprintf(stderr, "PTJAVA sink=buffer: runtime/rt/ptlogrt.so is not loaded "
                          "(LD_PRELOAD it) -- refusing to run\n");
          _exit(71);
        }
        g_cx.sink_buffer = 1;
        const char *sy = getenv("PTLOG_SYNC");     // MUST equal the runtime's own period
        g_cx.sync = sy ? (uint32_t)strtoul(sy, nullptr, 0) : 4096u;
        g_cx.maxsyncs = 1u << 21;
        g_cx.syncs = (PtjSyncMark *)mmap(nullptr, (size_t)g_cx.maxsyncs * sizeof(PtjSyncMark),
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (g_cx.syncs == MAP_FAILED) { g_cx.syncs = nullptr; g_cx.maxsyncs = 0; }
      } else if (sk && strcmp(sk, "ptwrite")) {
        fprintf(stderr, "PTJAVA: unknown sink=%s (want `ptwrite' or `buffer')\n", sk);
        _exit(71);
      } }
    g_ctr_on = (int)opt_num(o, "ctr", 0);
    // ---- keyframes (defect D-J1, the design notes -------------------------
    // HotSpot enters a freshly compiled nmethod by OSR at a loop header and (D-J3) patches
    // it seconds after CODE_ADDED, so the object's entry anchor may never execute.
    // `keyframe=K' asks the analyzer for `resync' sites at loop back-edge headers, logged
    // every K-th execution; the countdown cell is per SITE and shared by every thread in
    // the nmethod -- see ptj_kf_open() for why a racy `dec' is sound here.
    g_keyframe = (uint32_t)opt_num(o, "keyframe", 1024);
    { long kc = opt_num(o, "kfctr", -1); if (kc > 0) g_kfctr_max = (uint32_t)kc; }
    g_cx.kf_flags_live = (int)opt_num(o, "kfflagslive", 0);
    // ---- same-run ground truth (defect D-J2) ----------------------------------------
    if ((int)opt_num(o, "gt", 0)) {
      g_gtdir = opt_str(o, "gtdir", ".");
      g_gt_mb = (size_t)opt_num(o, "gtmb", 4096);
      if (ptj_gt_open(&g_gt, g_gtdir, (int)getpid(), g_gt_mb << 20)) {
        g_cx.gt = 1;
        g_cx.maxgtents = 1u << 22;
        g_cx.gtents = (PtjGtEnt *)mmap(nullptr, (size_t)g_cx.maxgtents * sizeof(PtjGtEnt),
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (g_cx.gtents == MAP_FAILED) { g_cx.gtents = nullptr; g_cx.maxgtents = 0; }
        fprintf(stderr, "PTJAVA gt: %s (%zu MB window)\n", g_gt.path, g_gt_mb);
      } else {
        fprintf(stderr, "PTJAVA gt: FAILED to open the ground-truth ring -- refusing to run\n");
        _exit(70);
      }
    }
    g_ctrdump = opt_str(o, "ctrdump", nullptr);
    g_instlog = opt_str(o, "instlog", nullptr);
    if (g_instlog) g_instlog_buf = (uint64_t (*)[3])calloc(PTJ_INSTLOG_MAX, 24);
    { long cm = opt_num(o, "ctrmax", -1); if (cm > 0) g_ctr_max = (uint32_t)cm; }
    g_cx.install = hs_install;                // the int3-then-jmp protocol
    g_cx.maxentries = 1u << 21; g_cx.maxrelocs = 1u << 21; g_cx.maxtramps = 1u << 21;
#define MAPZ(n) mmap(nullptr, (size_t)(n), PROT_READ | PROT_WRITE, \
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0)
    g_cx.entries = (PtjMapEntry *)MAPZ((size_t)g_cx.maxentries * sizeof(PtjMapEntry));
    g_cx.relocs = (PtjReloc *)MAPZ((size_t)g_cx.maxrelocs * sizeof(PtjReloc));
    g_cx.tramps = (uint64_t *)MAPZ((size_t)g_cx.maxtramps * 8);
    g_objs = (PtjObjRec *)MAPZ((size_t)OBJCAP * sizeof(PtjObjRec));
    g_live = (LiveObj *)MAPZ((size_t)LIVECAP * sizeof(LiveObj));
    g_prec = (PatchRec *)MAPZ(PREC_MAX * sizeof(PatchRec));
    g_wkey = (uint64_t *)MAPZ((size_t)WMAP_CAP * 8);
    g_wval = (uint64_t *)MAPZ((size_t)WMAP_CAP * 8);
#undef MAPZ
    if (g_cx.entries == MAP_FAILED) { g_cx.entries = nullptr; g_cx.maxentries = 0; }
    if (g_cx.relocs == MAP_FAILED) { g_cx.relocs = nullptr; g_cx.maxrelocs = 0; }
    if (g_cx.tramps == MAP_FAILED) { g_cx.tramps = nullptr; g_cx.maxtramps = 0; }
    if (g_objs == MAP_FAILED) g_objs = nullptr;
    if (g_live == MAP_FAILED) g_live = nullptr;
    if (g_prec == MAP_FAILED) g_prec = nullptr;
    if (g_wkey == MAP_FAILED) g_wkey = nullptr;
    if (g_wval == MAP_FAILED) g_wval = nullptr;
  }
  if (g_mode >= MODE_FULL) {
    memset(&g_cli, 0, sizeof g_cli);
    g_cli.fd = -1;
    const char *sk = opt_str(o, "sock", nullptr);
    if (sk) snprintf(g_cli.sock, sizeof g_cli.sock, "%s", sk);
    else snprintf(g_cli.sock, sizeof g_cli.sock, "/tmp/ptjava.%d.sock", (int)getpid());
    snprintf(g_cli.cache, sizeof g_cli.cache, "%s",
             opt_str(o, "cache", "cache"));
    mkdir(g_cli.cache, 0755);
    snprintf(g_cli.py, sizeof g_cli.py, "%s",
             opt_str(o, "py", "python3"));
    snprintf(g_cli.script, sizeof g_cli.script, "%s",
             opt_str(o, "analyze", "analyze.py"));
    snprintf(g_cli.ver, sizeof g_cli.ver, "%s", opt_str(o, "cachever", "v2.24"));
    // K is part of the on-disk cache key (jitsites.h): a k0 site list and a k1024 one
    // describe the same bytes, and serving one for the other silently undoes the build.
    g_cli.kf = g_keyframe;
    // fast=1: ask the analyzer for its coalesced ("fast") site set.  The mode is part of the
    // cache identity -- a fast answer must never be served for a hifi question -- so it is
    // appended to the version tag that keys the on-disk cache.
    g_cli.fast = (int)opt_num(o, "fast", 0);
    if (g_cli.fast) {
      size_t vl = strlen(g_cli.ver);
      if (vl + 2 < sizeof g_cli.ver) { g_cli.ver[vl] = 'f'; g_cli.ver[vl + 1] = 0; }
    }
    g_mem = (CacheEnt *)mmap(nullptr, (size_t)MEMCAP * sizeof(CacheEnt), PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (g_mem == MAP_FAILED) g_mem = nullptr;
    // The analysis pool: N worker threads, one analyzer service each.  One service per
    // worker because analyze.py serves a connection synchronously.
    g_nworkers = (int)opt_num(o, "workers", 4);
    g_avoid_rounds = (int)opt_num(o, "avoidrounds", 2);
    if (g_pinbridge) g_avoid_rounds = 0;
  // D-J6 A/B control (mirrors the V8 front end's PTJIT_NOROOTS): still ASK the analyzer with
  // `roots'/`data_from', but do not seed the patcher's own recursive descent with the reply's
  // `restart_roots' (nor with the jvmtiAddrLocationMap).  Every site beyond the first restart
  // then lands on an offset the sweep never decoded and is dropped as `no_insn_boundary',
  // which is the pre-v2.21 behaviour.
  // noroots=1: ask as usual, do not seed the patcher's sweep (isolates the patcher half).
  // noroots=2: also stop sending the jvmtiAddrLocationMap as `"roots"' (isolates the analyzer half).
  // Was 1 for part of 2026-09-17 (D-J11, the design notes: under analyzer v2.22/23 a
  // restart root could be one byte inside a real instruction and the seeded sweep displaced a
  // non-boundary -- philosophers died 19/20 in G1's code-cache unloading).  DEFAULT 0 again
  // since analyzer v2.24 (phase-anchored restarts, jit_accuracy.md 9: 20/20 clean with the roots
  // seeded, cold and warm cache).  `noroots=1' is the A/B control.
  g_noroots = (int)opt_num(o, "noroots", 0);
    g_ptj_no_disp32 = (int)opt_num(o, "nodisp32", 0);
    if (g_nworkers < 1) g_nworkers = 1;
    if (g_nworkers > 32) g_nworkers = 32;
    g_drain_ms = opt_num(o, "drainms", 60000);
    g_qcap = (uint32_t)opt_num(o, "qcap", 1 << 14);
    g_q = (Job *)calloc(g_qcap, sizeof(Job));
    g_wcli = (PtjClient *)calloc((size_t)g_nworkers, sizeof(PtjClient));
    // D-J14: the cache lane.  `lanes=0' is the A/B control (one FIFO, the old behaviour);
    // `fastworkers' sizes the lane.  These threads hold no analyzer connection -- they hash,
    // probe the content-hash cache, and either publish or hand the job to the analyzer lane.
    g_objlogfile = opt_str(o, "objlog", nullptr);
    if (g_objlogfile) {
      g_objlog_max = 1u << 14;
      g_objlog = (ObjRec *)calloc(g_objlog_max, sizeof(ObjRec));
      if (!g_objlog) g_objlog_max = 0;
    }
    g_lanes = (int)opt_num(o, "lanes", 1);
    g_prio = (int)opt_num(o, "prio", 1);
    g_mask_resync = (int)opt_num(o, "maskresync", 4);
    g_mask_verify = (int)opt_num(o, "maskverify", 0);
    g_nfast = (int)opt_num(o, "fastworkers", 4);
    if (g_nfast < 1) g_nfast = 1;
    if (g_nfast > 32) g_nfast = 32;
    if (!g_lanes) g_nfast = 0;
    if (g_lanes) {
      g_aqcap = g_qcap;
      g_aq = (Job *)calloc(g_aqcap, sizeof(Job));
      g_aqhi = (Job *)calloc(g_aqcap, sizeof(Job));
      g_fcli = (PtjClient *)calloc((size_t)g_nfast, sizeof(PtjClient));
      if (!g_aq || !g_aqhi || !g_fcli) { g_lanes = 0; g_nfast = 0; }
    }
    int nospawn = (int)opt_num(o, "nospawn", 0);
    for (int w = 0; w < g_nworkers; w++) {
      PtjClient *c = &g_wcli[w];
      c->fd = -1;
      if (sk) snprintf(c->sock, sizeof c->sock, "%s.%d", sk, w);
      else snprintf(c->sock, sizeof c->sock, "/tmp/ptjava.%d.%d.sock", (int)getpid(), w);
      snprintf(c->cache, sizeof c->cache, "%s", g_cli.cache);
      snprintf(c->py, sizeof c->py, "%s", g_cli.py);
      snprintf(c->script, sizeof c->script, "%s", g_cli.script);
      snprintf(c->ver, sizeof c->ver, "%s", g_cli.ver);
      c->kf = g_cli.kf;
      c->fast = g_cli.fast;
      if (nospawn) c->fd = ptj_connect(c);
      else ptj_start_service(c, (int)opt_num(o, "spawnms", 30000));
      if (c->fd >= 0) g_have_cli = 1;
      else fprintf(stderr, "PTJAVA: worker %d could not %s analyzer service %s: %s\n",
                   w, nospawn ? "connect to" : "start", c->sock, strerror(errno));
    }
    // DEFECT D-J3.3 the design notes: with `nospawn=1' and nothing listening,
    // every code object used to be "analysed" with an EMPTY site list and the agent wrote a
    // syntactically valid site map with zero entries -- a silently unpatched JVM that looks
    // like a successful run.  Mode 3/4 has no meaning without a service, so refuse to run.
    // `allowempty=1' is the escape hatch for a deliberate no-analyzer measurement.
    if (!g_have_cli && !(int)opt_num(o, "allowempty", 0)) {
      fprintf(stderr,
              "PTJAVA: FATAL -- mode %d needs an analyzer service and not one of the %d workers\n"
              "        could reach `%s.<w>'.  Start them with\n"
              "          for w in $(seq 0 %d); do %s %s --serve %s.$w & done\n"
              "        or drop `nospawn=1' to let the agent spawn them (but see D-J3: with\n"
              "        `pt_capture2 --cpu N' those children are pinned to the traced core).\n"
              "        `allowempty=1' overrides this check.\n",
              g_mode, g_nworkers, g_cli.sock, g_nworkers - 1, g_cli.py, g_cli.script, g_cli.sock);
      _exit(70);
    }
    if (g_q && g_wcli) {
      pthread_t th;
      for (int w = 0; w < g_nfast; w++) {          // the cache lane: no analyzer connection
        PtjClient *c = &g_fcli[w];
        c->fd = -1;
        snprintf(c->cache, sizeof c->cache, "%s", g_cli.cache);
        snprintf(c->ver, sizeof c->ver, "%s", g_cli.ver);
        c->kf = g_cli.kf; c->fast = g_cli.fast;
        LaneArg *la = (LaneArg *)malloc(sizeof(LaneArg));
        if (!la) continue;
        la->lane = 1; la->w = w;
        pthread_create(&th, nullptr, worker_main, la), pthread_detach(th);
      }
      for (int w = 0; w < g_nworkers; w++) {
        LaneArg *la = (LaneArg *)malloc(sizeof(LaneArg));
        if (!la) continue;
        la->lane = 0; la->w = w;
        pthread_create(&th, nullptr, worker_main, la), pthread_detach(th);
      }
    } else { g_nworkers = 0; g_nfast = 0; g_lanes = 0; }
  }

  jvmtiCapabilities caps; memset(&caps, 0, sizeof caps);
  caps.can_generate_compiled_method_load_events = 1;
  if (jvmti->AddCapabilities(&caps) != JVMTI_ERROR_NONE)
    fprintf(stderr, "PTJAVA: AddCapabilities failed\n");
  jvmtiEventCallbacks cb; memset(&cb, 0, sizeof cb);
  cb.VMInit = cb_vm_init;
  cb.CompiledMethodLoad = cb_compiled_load;
  cb.CompiledMethodUnload = cb_compiled_unload;
  cb.DynamicCodeGenerated = cb_dynamic_code;
  cb.VMDeath = cb_vm_death;
  jvmti->SetEventCallbacks(&cb, sizeof cb);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_COMPILED_METHOD_LOAD, nullptr);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_COMPILED_METHOD_UNLOAD, nullptr);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_DYNAMIC_CODE_GENERATED, nullptr);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, nullptr);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, nullptr);
  return JNI_OK;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) { (void)vm; }
