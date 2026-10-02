// recon.h -- PTracer Stage 3 driver: walks the PT instruction stream, consumes the critical value
// log (PTW packets / cv buffer), interprets the VEX skeleton per instruction, writes *.mtrace.
#pragma once
#include "ptdecode.h"
#include "vexinterp.h"
#include "trace_format.h"
#include "gtstream.h"
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>

struct Site { int id = -1; uint64_t addr = 0; std::string when, kind, reg; std::vector<std::string> regs; int size = 8; };
struct Spec { std::string image; uint64_t base = 0; std::vector<Site> sites; static Spec load(const std::string& path); };
// Site map.  Addresses are link-time vaddrs; Recon adds the
// runtime load base of the rewritten image before using them.
struct SiteMapEnt { uint64_t tramp = 0; int site = -1; uint64_t orig = 0; std::string kind, reg, half; int size = 0, payload_bits = 64;
                    // JIT profile: the code object version
                    // this record belongs to.  A JIT address is only valid for a time interval, so
                    // the ORIGINAL instruction behind a `memop' site is resolved through the
                    // jitdump with (obj -> addr, tsc) at load time and cached here by index --
                    // never by address, which the VM reuses.
                    int obj = -1; uint64_t jtsc = 0; int jit_ii = -1;
                    int map = 0;          // index of the site map this entry came from (which image)
                    uint64_t link_orig = 0;  // orig_addr as written in the map (link-time, for --site-stats)
                    bool buffer = false;     // this value is logged into cv.*.bin, not PTWRITE
                    // MIXED SINK: a site map may name a sink PER ENTRY
                    // ("sink": "ptwrite" | "buffer"), because one rewritten image can log the
                    // sparse sites through `ptwrite' and the hot ones into the buffer.  Empty =
                    // the map's own top-level `sink' answers for this entry.
                    std::string sink;
                    // Log-on-change ("delta") guard: the value is
                    // logged only when it differs from the last one logged here, so the logging
                    // instruction at `tramp' runs only when the `je' at `je_addr' falls through.
                    // Seeing `join_addr' as the instruction after the branch means it was SKIPPED
                    // and the value is this reconstructor's own copy of the runtime's cache slot.
                    bool delta = false; uint64_t slot = 0, cmp_addr = 0, je_addr = 0, join_addr = 0;
                    // KEYFRAME guard: this value is logged only every
                    // `kf_period'-th execution of its site.  The countdown is a real branch, so PT
                    // records the decision: seeing `kf_join' as the instruction after `kf_branch'
                    // means this execution was NOT a keyframe and nothing was logged.  `resync' marks
                    // a spec `resync' site (a loop back-edge re-anchor) as opposed to a
                    // log-on-change value that also keyframes.
                    bool resync = false; long kf_period = 0; uint64_t counter = 0, kf_branch = 0, kf_join = 0;
                    bool counter_gs = false; };
struct RelocEnt { uint64_t tramp = 0, orig = 0; int len = 0, tramp_len = 0;
                  // JIT profile: the original bytes this copy was made from, resolved through the
                  // jitdump at load time (see SiteMapEnt above).  `jit_id' indexes the per-entry
                  // lift/decode caches, because two versions of a JIT object share addresses.
                  int obj = -1; int jit_id = -1; const uint8_t* obytes = nullptr; int olen = 0; };
// One row of the JIT site map's `objects' table: which code object a record belongs to, and when
// it was installed -- the key into the jitdump's time-keyed table.
struct JitObjRow { uint64_t addr = 0, tsc = 0; uint32_t len = 0; };
// --sync-carrier tnt: one sync marker's count, LSB first, from the outcomes of its `jc' (role b,
// the bit is 1 unless the `nop', role z, follows).  `s' (the marker's first instruction) starts it,
// `e' (the marker's END) completes it; a loss in between resets it, so a partial count is dropped.
struct TntAcc {
    bool on = false; int k = 0; uint64_t v = 0;
    void reset() { on = false; }
    bool feed(char role) {
        switch (role) {
            case 's': on = true; k = 0; v = 0; return false;
            case 'b': if (on) { if (k < 64) v |= 1ull << k; k++; } return false;
            case 'z': if (on && k > 0 && k <= 64) v &= ~(1ull << (k - 1)); return false;
            case 'e': { const bool ok = on && k > 0 && k <= 64; on = false; return ok; }
        }
        return false;
    }
};

struct SiteMap {
    std::string sink = "ptwrite", image, orig_image;
    uint64_t sync = 0; bool present = false, applied = false, mapped = false;
    // JIT profile: `image' is "jit:<pid>" rather than a path, addresses are absolute (no load
    // base), and every record carries `obj'/`tsc'.
    bool jit = false; uint64_t slab_base = 0, slab_size = 0;
    std::vector<JitObjRow> objects;
    std::vector<SiteMapEnt> entries;
    std::vector<RelocEnt> relocated;
    std::vector<uint64_t> sync_markers;
    // --sync-carrier tnt: the TNT loop roles s/b/z of every marker; the
    // marker's END is its `sync_markers' entry.  Empty for the default PTWRITE carrier.
    std::vector<std::pair<uint64_t, char>> sync_tnt; bool sync_tnt_carrier = false;
    std::vector<uint64_t> trampolines;   // entry address of every trampoline the map describes
    long gt_sites = 0;                   // `role: "gt"' entries, skipped on load
    // --gt-all build: link-time address of EVERY instruction that records its
    // effective address in the gt ring, including the ones whose trampoline the
    // rewriter could not describe.
    std::vector<uint64_t> gt_site_addrs;
    // JIT ONLY: the rdtscp at which each gt_site_addrs[i] became a
    // gt site.  A JIT address is instrumented only from the moment its code object was patched,
    // and both VMs execute a code object BEFORE that (V8 enumerates code that is already
    // running; HotSpot patches an nmethod seconds after CODE_ADDED) -- so without this the same
    // ip is a gt site for part of the run and not for the rest, and the lockstep walk desynchronises.
    std::vector<uint64_t> gt_site_tsc;
    // JIT ONLY: the LOCKSTEP KEY each gt record is written with -- the address of
    // the relocated copy of gt_site_addrs[i] inside its trampoline.  An ip is not a dynamic
    // instance for JIT code (a code address is reused by a later object version, and inside a
    // loop the next gt record with the same ip is another iteration), a trampoline address is:
    // the slab is bump-allocated and never reused.  Empty / all-zero
    // (every ELF `--gt-all' map) => the walk keys on the ip.
    std::vector<uint64_t> gt_site_tramps;
    bool delta = false;                  // some of its values are logged on change
    bool keyframe = false;               // some of its values are logged every K-th execution
    static SiteMap load(const std::string& path);
};

struct ReconOptions {
    // --spec / --sitemap / --orig-image are REPEATABLE, one (spec, sitemap, orig-image) group per
    // rewritten image (whole-program CPython is ten rewritten images, and with a
    // single site map the other nine are either unmapped instrumentation or, worse, interpreted as
    // program code).  They are matched to the process's mappings by the site map's own `image'
    // field, not by position; --orig-image applies to the --spec/--sitemap given just before it.
    std::vector<std::string> specs, sitemaps, orig_images;   // orig_images[i] may be empty
    // --jitdump FILE (repeatable): the time-keyed code table of jitcode.h.  Needed for any
    // `--sitemap' group whose image is `jit:<pid>', and for decoding JIT code at all.
    std::vector<std::string> jitdumps;
    std::string aux, sideband, cvfile, out, summary, site_stats;
    bool text = false; bool decode_only = false; bool no_time = false;           // print "L/S addr size ip" lines to stdout
    bool use_skeleton = false;   // skip non-skeleton instructions
    bool gt_continue = false;  // reconstruct PT suffix after oracle EOF; never infer missing GT
    uint64_t max_insn = 0;       // stop after N instructions (0 = all)
    uint64_t output_stride = 1; // materialize every Nth record; interpretation/statistics remain complete
    uint64_t max_rep = 1u << 20; // refuse to expand a `rep' longer than this (0 = no limit)
    uint64_t skip_bytes = 0;     // parallel mode: start decoding at this AUX byte offset
    uint64_t end_bytes = 0;      // parallel mode: stop at this AUX byte offset (0 = end of file)
    // ---- chunk warm-up -------------------------------------------------------------------
    // A chunk that simply starts decoding at its own PSB starts with an ALL-UNKNOWN machine state
    // and has to re-anchor from its own logged values, which never happens for a register whose
    // only logging site is a rare one (`%rbx'/`%rbp' inside a long-running interpreter frame).
    // `emit_from' makes the chunk decode a TAIL OF THE PREVIOUS CHUNK first, with every record and
    // every counter suppressed, so registers, the delta table and the shadow are warm when its own
    // range begins: decoding starts at `skip_bytes' and output starts at `emit_from'.
    uint64_t emit_from = 0;      // AUX offset at which this chunk starts emitting (0 = from skip_bytes)
    uint64_t warm_bytes = 32ull << 20;   // --jobs: how much of the previous chunk to decode as warm-up
    // --exact-warm: every chunk warms up from BYTE 0, not from the previous chunk.  The
    // residual difference between a parallel and a serial reconstruction is state established
    // during process startup and never logged again: no bounded
    // warm-up recovers it, so this is the only setting that is bit-identical to serial by
    // construction -- and it costs the whole prefix per chunk, i.e. most of the parallel speed-up.
    bool exact_warm = false;
    // ---- warm window -------------------------------------------------------------------------
    // Reaching back `warm_bytes' (32 MB) clamped to the PREVIOUS chunk's start means a whole
    // chunk for any AUX file under 32 MB per chunk, which doubles the total work and caps the
    // speed-up at jobs/2.  The default is therefore the ANCHOR window: warm from the PSB the boundary probe started at, which by construction
    // precedes the keyframe and the sync marker the boundary was placed on, plus `warm_min'
    // bytes of margin.  `--warm-mb' selects the previous-chunk rule (warm_legacy).
    bool warm_legacy = false;    // --warm-mb given: "warm_bytes clamped to previous chunk" rule
    uint64_t warm_min = 0;       // --warm-kb: extra warm-up margin before the anchor window
    int chunks_per_job = 0;      // --chunks-per-job C: C*jobs chunks, at most `jobs' in flight (0 = auto)
    std::string rec_hash_cuts;   // --rec-hash-cuts FILE: ascending record counts; at each, log "count prefix_hash unknown_so_far" to FILE.out
    bool rec_hash = false;       // --rec-hash: composable 61-bit hash of the record stream in the summary
    // Process invariants a chunk child inherits from the parent instead of re-deriving them.
    // The image load bases already come from the sideband; the TLS base does not,
    // so pt_capture2 reads it with ptrace and stores it in the sideband ("fs_base"), and it is
    // seeded ONLY in a chunk that does not start at the beginning of the trace (before the loader
    // has run, %fs is still 0 and seeding the final value would be wrong).
    uint64_t fs_base = 0;        // --fs-base: override / supply the TLS base
    bool seed_fs = false;        // seed guest_FS_CONST at startup (set for chunk children)
    bool no_seed_fs = false;     // --no-seed-fs: ablation, do not inherit the TLS base
    int jobs = 1;                // ptrecon --jobs N: chunked parallel reconstruction
    // ---- exact log-on-change state across a chunk boundary --------------------------------
    // A chunk starts in the middle of the execution, where the runtime's log-on-change cache slots
    // hold values the chunk has not seen.  The warm-up and the delta keyframes recover most of
    // them, but a site that is neither logged nor keyframed inside the chunk's decode range stays
    // unknown, so `--jobs N' could not be bit-identical to serial on a delta build.
    // `--delta-scan' makes it exact with ONE extra, cheap pass: every chunk first scans its OWN
    // range for logging events only (no VEX interpretation, no records, no warm-up) and reports,
    // per logged value, the LAST value logged in that range and when -- which is independent of
    // any starting state, because the logging events are in the trace.  The parent folds those
    // reports in chunk order into the exact table at each chunk's warm start and hands it back.
    std::string delta_scan_out;  // round 1: write this chunk's scan here
    std::string delta_seed_in;   // round 2: start the log-on-change table from this file
    uint64_t scan_snap_at = 0;   // round 1: also snapshot the table at this AUX offset
    bool delta_scan = true;      // --no-delta-scan turns it off; a no-delta build skips it anyway
    // ---- sound chunk boundaries -------------------------------------------------------------
    // A chunk may only START where the replay can be re-anchored: the decoder synchronises at a
    // PSB, a KEYFRAME re-defines the registers and (buffer sink) a SYNC MARKER re-aligns the
    // positional value cursor.  `run_anchor_scan()' decodes [skip_bytes, end_bytes) looking for
    // the first offset at which all three hold with no state loss after them, and writes it here.
    std::string anchor_scan_out; // --jobs probe: write "found anchor kf sync insns" to this file
    int verbose = 0;
    std::string resync_log;   // --resync-log FILE: one line per PT overflow / decoder resync
    // --cv-audit FILE: one line per buffer-sink sync marker --
    //   marker_index  aux_offset  marker_ip  payload  cv_pos  payload-cv_pos
    // A non-zero last column means the runtime wrote a different number of values between these
    // two markers than the reconstructor consumed; the first such line localises the drift.
    std::string cv_audit;
    // ---- SAME-PROCESS GROUND TRUTH -----------------------------------------------------------
    // `--gt-in' is the gt.<pid>.<tid>.bin the traced process itself wrote: one 16-byte record
    // { effective address, original ip } per ordinary access, with PTGT v2 compact completed
    // REP STOS descriptors expanded by GtStream. It comes from the SAME execution as the PT stream;
    // no alignment and no edit distance is needed -- the two are compared record by record.
    std::string gt_in;        // --gt-in FILE
    std::string gt_out;       // --gt-out FILE: the gt stream as an mtrace, index-aligned with -o
    bool gt_compare = false;  // --gt-compare: count identical/unknown/wrong/excluded per record
    int gt_lookahead = 64;    // bounded local resync when the two streams disagree
    uint64_t gt_resync_window = 1u << 22;   // --gt-resync-window: (ip, addr) re-alignment after a PT state loss; 0 = off
    // --gt-attach-anchor N: the ground truth and the
    // reconstruction need NOT begin at the same instruction.  When PT is attached to a process
    // that is already running (`pt_capture2 --trace-after'), the thread's gt ring already holds
    // every access it made before the trace was enabled, and the walk's implicit "both streams
    // start at record 0" assumption is false by seconds of execution.  N > 0 buffers the first N
    // gt-site reconstructed records, locates them in the gt stream by a CONTROL-FLOW (ip-only)
    // RIGID SHIFT VOTE confirmed over a 2048-record positional window, and starts the walk
    // there.  The addresses are deliberately not consulted: they are the quantity under test,
    // and an alignment chosen with them is an alignment chosen to make the tracer look right.
    // 0 = off (the co-started case: every whole-program cell, anchor 0 by construction).
    uint64_t gt_attach_anchor = 0;
    // --gt-confirm C: a streak re-alignment must ALSO agree with the C preceding reconstructed
    // gt-site ips.  A single (ip, addr) pair is not a dynamic instance in a server workload:
    // 96.7 % of the DSB gt records share their (ip, addr) with another record of the same thread
    // (mean multiplicity 15, max 111 405), so the unconfirmed nearest-pair rule re-aligned on
    // coincidences and thrashed.  0 = off (nearest-pair rule, unconfirmed).
    int gt_confirm = 32;
    // ---- MULTI-THREADED reconstruction (mtdemux.cpp) ------------------------------------------
    // `--mt': the sideband is a per-CPU capture (version 2, `cpus'/`threads'); every core's AUX
    // stream is cut into thread regions by its context-switch records and each thread is
    // reconstructed by its own Recon on a ThreadPlan (ptdecode.h), in parallel, then merged by
    // time into one mtrace with a real tid per record.
    bool mt = false;
    uint32_t tid = 0;                  // this Recon reconstructs thread `tid' (0 = the sideband's pid)
    const ThreadPlan* plan = nullptr;  // plan mode: decode exactly this thread's regions
    std::vector<std::string> cvfiles;  // --cv (repeatable) / --cv-dir: per-thread v2 files or a v3 mux file
    std::string cv_dir, gt_dir;        // directories to glob cv.*.bin / gt.<pid>.<tid>.bin from
    uint64_t sw_slack_tsc = 0;         // attribution slack: a switch-in this close AFTER a region start still counts (0: none)
    bool keep_thread_files = false;    // keep the per-thread .tid<T>.mtrace/.json after the merge
    int mt_split = 0;   // --mt-split N -- split each thread's stream into --jobs-style chunks, N workers in all
    int mt_jobs = 0;                   // threads reconstructed concurrently (0 = min(#threads, 8))
    // --mt-tid N (repeatable): reconstruct ONLY these threads.  A 548-thread DSB window costs
    // ~9 s of process-invariant construction per thread, so a
    // per-ip post-mortem of ONE thread (with --out / --site-stats, which write a file per thread)
    // must not re-run all of them.  Empty = every thread with regions.
    std::vector<uint32_t> mt_tids;
    std::string sw_dump;               // --mt-dump FILE: write the attributed region table (debug)
};

class Recon {
public:
    explicit Recon(const ReconOptions& o);
    int run();
    // --delta-scan round 1: decode this chunk's range for LOGGING EVENTS ONLY and write the per-entry
    // "last value logged here, and when" table.  No interpretation, no records.
    int run_delta_scan();
    // --jobs boundary probe: decode this range looking for the first point at which a
    // chunk could start and be anchored (keyframe + sync marker, no state loss after).  No
    // interpretation, no records; costs about a `--decode-only' pass over the scanned bytes.
    int run_anchor_scan();
    // Can this capture be split at all?  False (with a reason) when nothing in the stream can
    // re-anchor a mid-trace chunk: no keyframe sites, or a buffer sink with no sync markers.
    bool can_split(std::string& why) const;
    size_t n_logged_values() const { return flat_ent_.size(); }
    bool has_delta() const { return n_delta_values_ != 0; }
    // --jobs: re-point this (never-run) Recon at one chunk of the same trace, and read the value
    // stream before forking so that every chunk child shares it.  See recon.cpp.
    void retarget(const ReconOptions& co);
    // --mt: re-point this (never-run) Recon at ONE THREAD of the same process -- the multi-thread
    // analogue of retarget().  Everything the constructor built is a property of the PROCESS; what
    // belongs to the thread is its plan, its %fs base, its own value stream and its own ground
    // truth ring.  See mtdemux.cpp step 4.
    void retarget_thread(const ReconOptions& co);
    void preload_values();
    // Parallel driver (ptrecon --jobs N): split the AUX file at PSB packets and reconstruct the
    // chunks concurrently.  Defined in recon_par.cpp.
    static int run_parallel(const ReconOptions& o);
    void prebuild_decoder() { if (!have_jit_ && o_.plan == nullptr) PtDecoder::prebuild_shared(o_.aux, sb_); }
    // --rec-hash: H(r_1..r_n) = sum x(r_i) P^(n-i) mod 2^61-1, so H(A||B) = H(A) P^|B| + H(B) --
    // the parallel parent composes the chunks' hashes without ever materialising the records.
    static constexpr uint64_t RH_M = (1ull << 61) - 1, RH_P = 0x1f3a5c7e9b2d4861ull % ((1ull << 61) - 1);
    static uint64_t rh_mul(uint64_t a, uint64_t b) {
        __uint128_t z = (__uint128_t)a * b; uint64_t lo = (uint64_t)(z & RH_M), hi = (uint64_t)(z >> 61);
        uint64_t r = lo + hi; return r >= RH_M ? r - RH_M : r; }
    static uint64_t rh_add(uint64_t a, uint64_t b) { uint64_t r = a + b; return r >= RH_M ? r - RH_M : r; }
    static uint64_t rh_pow(uint64_t n) { uint64_t r = 1, b = RH_P; while (n) { if (n & 1) r = rh_mul(r, b); b = rh_mul(b, b); n >>= 1; } return r; }
    static uint64_t rh_mix(uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33; return x; }
    // Multi-threaded driver (ptrecon --mt): segment every per-CPU stream, attribute the regions to
    // threads, reconstruct each thread on its plan, merge.  Defined in mtdemux.cpp.
    static int run_threads(const ReconOptions& o);
    // Segmentation pass for one per-CPU AUX file (decode-only; writes the boundary table).
    int run_segment(const std::string& aux, const std::string& out);
private:
    friend struct ReconVersionTest; // deterministic cache regression, no perf/JVM required
    ReconOptions o_;
    Sideband sb_;
    // --- JIT code ----------------------------------------------------------------------------
    JitTable jit_; bool have_jit_ = false;
    void load_jitdumps();
    // Per-entry decode/lift caches for JIT site-map records.  A JIT object's addresses are
    // reused across versions, so nothing here may be keyed by address: the key is the site-map
    // record, which is unique (trampolines are bump-allocated and never reused).

    std::vector<Spec> specs_; std::vector<SiteMap> smaps_;
    std::unordered_map<std::string, std::string> orig_image_of_;   // mapped path -> pre-rewrite ELF
    void load_specs();
    VexLifter lifter_; VexInterp vi_;
    std::unordered_map<uint64_t, std::unique_ptr<IRBlockC>> blocks_;
    // Relocated ORIGINAL instructions are lifted from the ORIGINAL bytes at their ORIGINAL address:
    // a separate cache, because the same address in the rewritten image can hold E9Patch's patch jump.
    std::unordered_map<uint64_t, std::unique_ptr<IRBlockC>> orig_blocks_;
    std::unordered_map<uint64_t, std::vector<uint8_t>> orig_raw_;
    const std::vector<uint8_t>* orig_bytes_at(uint64_t addr);
    // One memory operand of an instruction, as Zydis decoded it -- INCLUDING the operands Zydis
    // marks HIDDEN, which is how a `rep stos'/`rep movs' string operand is reported (without
    // them `ii.memsize' would be 0 for every string instruction and the `rep' expansion guard
    // could never see an unknown %rdi/%rsi).
    struct MemDesc { int base = -1, index = -1, scale = 1; int64_t disp = 0; int size = 0;
                     bool riprel = false; int seg = 0;      // seg: 0 = none/DS/SS/ES/CS, 1 = FS, 2 = GS
                     bool wr = false;                       // the instruction writes this operand
                     bool vsib = false; };                  // vector index (gather/scatter): no single address
    struct InsnInfo { bool ptwrite = false, rep = false, ok = false, glue = false, branch = false, state_save = false, orig = false; int ptw_reg_off = -1, ptw_reg_size = 0; bool ptw_mem = false;
        int base = -1, index = -1, scale = 1; int64_t disp = 0; bool riprel = false; int memsize = 0; int len = 0; uint64_t hash = 0;
        int seg = 0;                 // segment override of the FIRST VISIBLE memory operand
        uint32_t gpwrite = 0;        // bitmask of 64-bit GP registers the instruction writes (Zydis)
        uint32_t vecwrite = 0;       // bitmask of vector registers 0-15 (xmm/ymm/zmm) it writes (Zydis)
        bool vsib = false;           // its first visible memory operand has a vector index
        bool mem_write = false;      // it writes memory
        bool patch_jump = false;     // E9Patch's punned `48 e9 <rel32>' patch jump (see run())
        MemDesc mem_ops[2]; int n_mem_ops = 0;   // every memory operand, hidden ones included
        bool mem_rmw = false;        // its single memory operand is read AND written (-> one MT_RMW record)
        uint8_t encoding[16]{};      // exact input bytes; an address alone is not a JIT version
        // ---- per-ip DISPATCH CACHE -----------------------------------------------------------
        // Asking the site-map tables what an ip is on EVERY execution of it costs up to
        // nine std::unordered_map lookups per instruction (sm_kf_, sm_je_, sm_ent_, sm_sync_,
        // sm_rel_, sm_tramp_, tramp_entry_, orig_raw_, orig_insns_), which the whole-program
        // CPython profile showed costing ~40 % of the run.  Every one of those tables is built
        // once, before run(), and never changed afterwards, so an ip's ROLE is a function of the
        // ip alone and is cached here -- in the InsnInfo, which is exactly the object that gets
        // thrown away when self-modifying/JIT code reuses an address.  No decision changes; the
        // tramp_unmapped() counters are bumped by sm_unmapped().
        mutable uint16_t sm_flags = 0;                  // SMF_*; SMF_DONE = the rest is valid
        mutable uint32_t sm_ent_idx = 0, sm_je_idx = 0, sm_kf_idx = 0;
        mutable char sm_tnt_role = 0;                   // SMF_TNT: s/b/z/e (--sync-carrier tnt)
        mutable const RelocEnt* sm_relp = nullptr;      // SMF_REL: the sm_rel_ entry for this ip
        mutable const std::vector<uint8_t>* sm_ob = nullptr;   // SMF_OB: orig_bytes_at(sm_relp->orig)
        mutable const InsnInfo* sm_oi = nullptr;        // SMF_OI: orig_info(sm_relp->orig)
        mutable const IRBlockC* sm_blk_orig = nullptr;  // SMF_BO: orig_blocks_[sm_relp->orig]
        mutable const IRBlockC* sm_blk_self = nullptr;  // SMF_BS: blocks_[ip]
    };
    enum { SMF_DONE = 1u << 0, SMF_ENT = 1u << 1, SMF_SYNC = 1u << 2, SMF_JE = 1u << 3,
           SMF_KF = 1u << 4, SMF_REL = 1u << 5, SMF_TRAMPENT = 1u << 6, SMF_TRAMPSET = 1u << 7,
           SMF_OB = 1u << 8, SMF_OI = 1u << 9, SMF_BO = 1u << 10, SMF_BS = 1u << 11, SMF_TNT = 1u << 12 };
    void sm_fill(const InsnInfo& ii, uint64_t ip);
    inline bool sm_unmapped(const InsnInfo& ii) {       // == tramp_unmapped(ip), counter included
        if (sm_tramp_.empty()) return false;
        if (ii.sm_flags & (SMF_TRAMPSET | SMF_REL | SMF_ENT | SMF_SYNC | SMF_TNT)) return false;
        if (!(ii.sm_flags & SMF_TRAMPENT)) return false;
        n_tramp_unknown_++;
        return true;
    }
    std::unordered_map<uint64_t, InsnInfo> insns_;
    // Direct-mapped front cache for `insns_'.  The hash table itself stays node-based (run()
    // holds an InsnInfo reference across calls that insert into it, which only a node-based
    // container survives); this only skips the bucket walk on the common hit.
    struct ICacheEnt { uint64_t ip = 0; InsnInfo* p = nullptr; };
    // 2^16 slots: whole-program CPython's working set of instruction addresses overflows
    // 8 192 slots, and the fallback unordered_map walk is then ~10 % of its replay.
    static const size_t kICacheBits = 16;
    std::vector<ICacheEnt> icache_ = std::vector<ICacheEnt>(1u << kICacheBits);
    static inline size_t icache_slot(uint64_t ip) { return (size_t)((ip * 0x9E3779B97F4A7C15ull) >> (64 - kICacheBits)); }
    uint64_t insn_gen_ = 1;   // bumped on every insns_ erase: validates the decoder's per-instruction memo slots
    inline void icache_drop(uint64_t ip) { insn_gen_++; ICacheEnt& c = icache_[icache_slot(ip)]; if (c.ip == ip) { c.ip = 0; c.p = nullptr; } }
    // Erase of a TEMPORARY key (addr ^ 1<<63, orig_info/decode_info/fallback cursor).  No
    // decoder memo slot can point at such a node (slots are only ever set for real ips, which never
    // have bit 63 set) and erasing one node of the node-based map leaves every other node in place,
    // so the slots stay valid: only the direct-mapped front entry is cleared, insn_gen_ is not bumped.
    inline void icache_drop_tmp(uint64_t key) { ICacheEnt& c = icache_[icache_slot(key)]; if (c.ip == key) { c.ip = 0; c.p = nullptr; } }
    const InsnInfo& info(uint64_t ip, const uint8_t* raw, int len);
    // Same, but for the ORIGINAL instruction at an original address, in its own cache: the
    // rewritten image holds E9Patch's patch jump at exactly that address, so `insns_' (keyed by
    // the runtime ip) would otherwise return the jump's decoding for it.
    const InsnInfo* orig_info(uint64_t addr);
    // original-code / instrumentation regions (E9Patch trampolines, handlers, loader)
    struct Range { uint64_t lo, hi; };
    std::vector<Range> orig_code_;                      // executable mappings of original ELF PT_LOADs (plus libraries)
    bool in_orig_code(uint64_t ip) const;
    void parse_dbg_ips();                          // PTRECON_DBGIP (diagnostic, see run())
    std::unordered_set<uint64_t> dbg_ips_;
    // E9Patch trampolines: entry address -> patched original address (from the jmp planted at the site)
    std::unordered_map<uint64_t, uint64_t> tramp_entry_;
    std::unordered_map<std::string, uint64_t> bias_;          // file -> load bias
    // EVERY load base a file has in the sideband.  A sideband that spans an
    // execve is a union over two address spaces and an image mapped in both has two.
    std::unordered_map<std::string, std::vector<uint64_t>> biases_;
    std::unordered_map<std::string, std::vector<uint8_t>> file_cache_;
    struct Seg { uint64_t vaddr, off, filesz; };
    std::unordered_map<std::string, std::vector<Seg>> segs_;   // file -> PT_LOADs
    const std::vector<uint8_t>* file_bytes(const std::string& path);
    // ---- interval index over the sideband's executable mappings ------------------------
    // A LINEAR SCAN of all mappings for every byte read is too slow: PolyBench has ~20 mappings,
    // whole-program CPython has ~17 000 (E9Patch maps every trampoline page).
    // The mappings are painted into a disjoint, sorted interval list -- later mappings overwrite
    // earlier ones, which reproduces the "last mapping wins" rule (E9Patch's loader re-maps a
    // text page from the patched copy with MAP_FIXED) -- and looked up by binary search, with a
    // one-entry "last interval" cache and a per-page byte-pointer cache in front of it.
    struct IMap { uint64_t lo, hi; const MapEnt* m; };
    std::vector<IMap> imap_;                       // disjoint, sorted by lo
    mutable size_t imap_last_ = 0;                 // last hit (locality: consecutive reads)
    void build_imap();
    void init_delta_table();                 // the one table that depends on WHICH chunk this is
    bool cv_loaded_ = false;                 // preload_values() has read o_.cvfile
    const MapEnt* map_at(uint64_t addr) const;
    struct PageRef { const uint8_t* p = nullptr; size_t avail = 0; };
    std::unordered_map<uint64_t, PageRef> page_map_, page_orig_;   // page vaddr -> bytes in the cached file
    const PageRef* page_ref(uint64_t addr, bool orig);
    std::vector<Range> orig_sorted_;               // orig_code_, coalesced + sorted (in_orig_code)
    void build_orig_index();
    bool read_mapped_bytes(uint64_t addr, uint8_t* out, int n);   // bytes as mapped at runtime (patched code)
    bool read_orig_bytes(uint64_t addr, uint8_t* out, int n);     // bytes of the ORIGINAL program (pre-rewrite)
    std::unordered_map<uint64_t, InsnInfo> orig_insns_;
    // JIT per-record caches (see load_jitdumps / build_site_map)
    std::vector<InsnInfo> jit_ent_insn_; std::vector<char> jit_ent_ok_;
    std::vector<InsnInfo> jit_rel_insn_; std::vector<char> jit_rel_ok_;
    std::vector<std::unique_ptr<IRBlockC>> jit_rel_blk_;
    InsnInfo decode_info(uint64_t addr, const uint8_t* raw, int len, bool* ok);
    const InsnInfo* site_orig_info(const SiteMapEnt& e);
    // JIT per-image statistics + coverage: records whose ip is JIT code, and the executed
    // instructions whose bytes the table could not supply.
    uint64_t n_jit_rec_ = 0, n_jit_unknown_ = 0, n_jit_tramp_undescribed_ = 0;
    // Executable mappings reclassified as ORIGINAL because they are a second mapping of a
    // file's own PT_LOAD (V8's embedded-builtin blob next to the code range).
    uint64_t n_second_map_orig_ = 0;
    uint64_t n_jit_site_undecodable_ = 0, n_jit_reloc_undecodable_ = 0, n_jit_reloc_nobytes_ = 0;
    uint64_t n_jit_insn_ = 0, n_jit_lift_fail_ = 0;
    uint64_t cursor_ = 0; bool cursor_valid_ = false, tramp_fallback_ = false; uint64_t n_tramp_mismatch_ = 0, n_tramp_unknown_ = 0;
    long dbg_tramp_ = 0;   // PTRECON_DEBUG_TRAMP=N: dump the first N trampoline-fallback events
    bool tramp_unmapped(uint64_t ip);
    void build_tramp_map();
    // --- site map, rebased to runtime addresses -------------------------------------------
    bool smap_ok_ = false; uint64_t smap_base_ = 0; int n_smap_ok_ = 0, n_smap_bad_ = 0;
    std::vector<uint64_t> smap_bases_;                  // per site map
    std::vector<SiteMapEnt> flat_ent_;                  // every logged value of every site map
    std::vector<uint64_t> ent_hits_;                    // --site-stats: executions per logged value
    // --site-stats, profile-guided delta: per logged value, how often the value was the SAME as the
    // one logged there before (`repeats'), and how often a log-on-change guard skipped the logging
    // instruction (`skipped').  `repeats + skipped' over `hits + skipped' is the repeat ratio
    // runtime/rewrite.py --delta-profile thresholds on.  Only allocated with --site-stats.
    std::vector<uint64_t> ent_repeats_, ent_skipped_;
    std::vector<uint64_t> ent_last_; std::vector<char> ent_haslast_;
    void note_value(long idx, uint64_t v);
    std::unordered_map<uint64_t, uint32_t> sm_ent_;     // logging instruction -> index into flat_ent_
    std::unordered_map<uint64_t, RelocEnt> sm_rel_;     // relocated copy -> original instruction
    std::unordered_map<uint64_t, uint32_t> sm_sync_;    // buffer-sink sync markers -> the sync period of their image's rewrite
    std::unordered_map<uint64_t, char> sm_tnt_;         // --sync-carrier tnt: marker ip -> role s/b/z/e
    TntAcc tnt_; uint64_t n_tnt_sync_ = 0, n_tnt_partial_ = 0;
    // Sync-period bookkeeping (--sync N): the runtime starts TOTAL at 1 - PTLOG_SYNC and every
    // marker adds (ITS image's period - COUNT); both are exact only if every image and the runtime use one period.
    int64_t sync_rt_ = 0, sync_arm_ = 0, sync_err_ = 0; uint32_t sync_mark_period_ = 0;
    uint64_t n_sync_corrected_ = 0; bool sync_mixed_ = false;
    std::unordered_set<uint64_t> sm_tramp_;             // trampoline entries the map describes
    // --- log-on-change ("delta") logging ---------------------------------------------------
    // The reconstructor keeps the same per-value "last logged value" table the trampolines keep
    // in their cache slots.  Both start at zero (the slot area is mapped zero-filled), so they
    // agree from the first execution.  A PT overflow or resync may hide executions that updated
    // a slot, so every entry is invalidated there and the values taken from an invalid entry are
    // reported as unknown addresses and counted separately (`delta_unknown').
    std::unordered_map<uint64_t, uint32_t> sm_je_;      // the guard's `je' -> index into flat_ent_
    std::vector<uint64_t> delta_val_;                   // last logged value, per flat_ent_ entry
    std::vector<char> delta_valid_;
    long pending_delta_ = -1;      // a guard's `je' was the previous instruction
    long ptw_pend_ent_ = -1;       // the pending PTW belongs to this entry (update its table slot)
    uint64_t n_delta_skipped_ = 0, n_delta_unknown_ = 0, n_delta_values_ = 0;
    // Skipped guards whose cache slot was re-derived from the machine state instead of
    // from a logged value (a taken `je' proves the slot equals the value the site would log).
    uint64_t n_delta_reanchor_ = 0;
    void delta_take_from_table(long idx);
    void delta_invalidate();
    // --- keyframe logging ----------------------------------------------------------------
    // A keyframe site logs only every K-th execution; the countdown's `jnz' is a real branch, so
    // the PT stream says which it was.  Nothing has to be replayed when it is skipped -- the
    // logging instructions simply do not appear -- so this is pure accounting, plus the answer to
    // "did a keyframe put a register back after a state loss".
    std::unordered_map<uint64_t, uint32_t> sm_kf_;      // the counter's `jnz' -> index into flat_ent_
    long pending_kf_ = -1;
    // The guard whose countdown JUST reached zero, so that a re-anchor is counted for a value the
    // KEYFRAME logged and not for one a log-on-change guard would have logged anyway (a keyframed
    // delta value also logs whenever its value changes).  All of a guard's logging instructions run
    // immediately after its branch, so one address is enough to scope it.
    uint64_t kf_fired_branch_ = 0;
    uint64_t n_kf_taken_ = 0, n_kf_reanchor_ = 0, n_kf_site_defs_ = 0, n_kf_values_ = 0, n_kf_resync_values_ = 0;
    void kf_note(uint32_t idx);    // count a keyframe that re-defines an UNKNOWN register
    void build_site_map();
    void write_site_stats();
    // critical value log
    enum PtwKind { PTW_NONE, PTW_REG, PTW_MEM, PTW_SYNC };
    std::deque<std::pair<uint64_t, int>> ptw_q_;      // (payload, size)
    bool ptw_pending_ = false; PtwKind ptw_kind_ = PTW_NONE;
    // The ip of the instruction whose PTW payload is pending.  libipt binds an
    // ip-less PTW packet to the PTWRITE instruction that produced it and reports that ip, so a payload
    // is given only to ITS site; a payload whose instruction was not seen as a pending site (e.g. decoded
    // around a PT overflow / resync) is dropped (`ptw_orphans') instead of being handed, positionally,
    // to the next site, which would shift every later PTWRITE value and sync payload by one.
    uint64_t ptw_pend_ip_ = 0, n_ptw_orphan_ = 0, n_ptw_missing_ = 0, n_ptw_ip_ = 0;
    int ptw_pend_off_ = -1, ptw_pend_size_ = 0, ptw_pend_memsize_ = 0; uint64_t ptw_pend_ea_ = 0;
    bool ptw_pend_mem_legacy_ = false;   // no site map: the `ptwrite mem' form describes itself
    // The buffer sink's value stream.  A flat (PTCV/legacy) file is MAPPED read-only instead of
    // read (a multi-GB stream would otherwise be a multi-second copy per process tree); a
    // PTCW (per-thread blocks) file is still gathered into `own'.  Same values either way.
public:
    struct CvStore {
        std::vector<uint64_t> own; const uint64_t* p = nullptr; size_t n = 0; void* map = nullptr; size_t maplen = 0;
        size_t size() const { return n; } bool empty() const { return n == 0; }
        uint64_t operator[](size_t i) const { return p[i]; }
        void adopt() { p = own.data(); n = own.size(); }
        void clear();
        ~CvStore() { clear(); }
    };
private:
    CvStore cv_; size_t cv_pos_ = 0;
    size_t cv_base_ = 0;   // gated capture: absolute slot of cv_[0] (hole-punched prefix)
    bool cv_have(size_t p) const { return p >= cv_base_ && p - cv_base_ < cv_.size(); }
    uint64_t cv_at(size_t p) const { return cv_[p - cv_base_]; }
    // A site's sync marker is emitted AFTER the
    // site's value stores, so the marker that realigns the positional cv cursor arrives one value
    // too late for the site it fires at.  That costs exactly one value per run: the runtime makes the
    // thread's FIRST logged value fire a marker, and before that marker the decoder has consumed
    // the cv slots of trampolines whose patched pages were not yet installed when the code really
    // ran (libc's IFUNC resolvers / __libc_early_init).  These two remember the values consumed at
    // the CURRENT trampoline so a realigning marker can re-read them at the corrected positions;
    // the bookkeeping stops at the first marker, so the steady state costs one predictable branch.
    bool pre_first_sync_ = true;
    // A --jobs chunk starts decoding in the middle of the value stream
    // with the positional cursor at 0, i.e. POINTING AT OTHER SITES' VALUES, until its first sync
    // marker.  Defining those values as KNOWN gives known WRONG addresses (e.g. `%rdx' := 0 at
    // polybench_flush_cache, 1 024 records per affected chunk on gemm_small).  Until the first
    // marker a chunk therefore defines
    // nothing from the cv stream (the destination becomes unknown), and the marker re-reads the
    // current trampoline's values at the corrected slots (the realigning-marker mechanism above, generalised).
    bool cv_unaligned_ = false; uint64_t n_cv_unaligned_ = 0;
    void undefine_value(const SiteMapEnt& e);
    std::vector<std::pair<uint32_t, size_t>> tramp_cv_;   // (site-map entry index, cv slot)
    uint64_t n_cv_redefined_ = 0;                         // values re-read after a realignment
    FILE* cv_audit_ = nullptr; uint64_t n_sync_seen_ = 0, sync_ip_ = 0, sync_off_ = 0;   // --cv-audit
    uint64_t sync_nres_ = 0, sync_ninsn_ = 0;      // --cv-audit: decoder state at the marker
    uint64_t n_fb_logsite_ = 0;   // logging instructions seen inside a trampoline the map does not describe
    bool arm_value_site(const SiteMapEnt& e);         // set the pending PTW target from the site map
    void define_value(const SiteMapEnt& e, uint64_t v);// buffer sink: consume one logged value
    // output
    std::vector<uint64_t> rh_cuts_; size_t rh_cut_i_ = 0; FILE* rh_cut_out_ = nullptr;
    FILE* out_ = nullptr; uint64_t n_arch_prctl_fs_ = 0; uint64_t n_output_rec_ = 0; uint64_t rh_ = 0; uint64_t n_rec_ = 0, n_unknown_ = 0, n_ovf_ = 0, n_rep_unknown_ = 0, n_ptw_used_ = 0, n_lift_fail_ = 0, n_instr_skipped_ = 0;
    uint64_t n_rep_overlong_ = 0;
    // An omitted instruction/REP range is NOT one unknown memory access. Keep a
    // bounded diagnostic ledger so a small observed-record error cannot hide it.
    uint64_t n_lift_fail_memory_ = 0, n_rep_collapse_unknown_count_ = 0;
    uint64_t n_memory_omission_events_ = 0;
    uint64_t n_lifted_memory_shortfall_ = 0;
    uint64_t recent_original_ips_[32] = {};
    std::vector<std::unique_ptr<VexInterp::FProg>> fprogs_; std::vector<uint32_t> marks_; uint64_t n_fused_insn_ = 0;
    static uint64_t plan_total(const ThreadPlan& p);
    VexInterp::FProg* build_fused_at(const PtDecoder::BInsn* bp, uint32_t avail);
    void flush_range(const InsnInfo& ii, uint64_t ip, uint64_t ts, const MemAccess* ab, const MemAccess* ae);
    uint64_t n_batched_ = 0, n_batch_stop_[5] = {};   // batch path: instructions batched; stops by reason
    uint64_t recent_original_count_ = 0;
    std::vector<std::string> memory_omission_examples_;
    void note_memory_omission(const char* kind, uint64_t ip, const InsnInfo& ii,
                              const uint8_t* raw, int len, const Val& count, bool addr_known);
    uint64_t n_patch_jump_ = 0;      // E9Patch `48 e9' patch jumps executed (NOT lift failures)
    uint64_t warm_insn_ = 0;         // instructions decoded as chunk warm-up, before `emit_from'
    bool warming_ = false;           // suppress every record and every counter until `emit_from'
    void build_imap_quadratic();
    void reset_counters();           // called when warm-up ends: the warm-up must not be reported
    uint64_t n_lift_fail_kept_ = 0, n_cv_used_ = 0, n_cv_missing_ = 0, n_sync_used_ = 0, n_sync_resync_ = 0, n_rmw_ = 0;
    // Parallel reconstruction: a chunk starts with an all-unknown machine state and re-anchors as
    // logged values arrive, so the records it emits before the first logged value is consumed are
    // the ones a single-threaded run would have had anchored.  Counted and reported.
    bool anchored_ = false; uint64_t n_unanchored_rec_ = 0;
    uint32_t tid_ = 0; bool in_ovf_ = false;
    // Multi-thread: a region of this thread began at a LOSSY boundary (PT overflow / resync hid
    // the switch-in).  The state is dropped exactly as for an overflow; in addition the unknown
    // records emitted until the thread's next logged value are counted separately, so the
    // switch-induced share of `unknown_addr' is reported on its own.
    uint64_t n_switch_loss_ = 0, n_unknown_switch_ = 0, n_rec_switch_ = 0, switch_loss_vals_ = 0, switch_loss_sync_ = 0, g_wrong_switch_ = 0; bool switch_loss_pending_ = false;
    uint64_t n_lf_forget_ = 0, n_lf_clear_ = 0, n_lf_clear_unkea_ = 0, n_lf_clear_chunks_ = 0, n_lf_allunk_ = 0; int lf_diag_ = 0; bool lf_clear_ = false;
    std::unordered_map<uint64_t, uint64_t> lf_clear_ips_;   // PTRECON_LFDIAG: shadow_clear count per record ip
    std::vector<MemAccess> acc_;                       // accesses of the instruction being interpreted
    void flush_acc(const InsnInfo& ii, uint64_t ip, uint64_t ts);
    // per-image statistics (by the record's ip, using the sideband maps)
    struct ImgStat { std::string path; uint64_t lo = 0, hi = 0; bool orig = false; uint64_t records = 0, unknown = 0; };
    std::vector<ImgStat> imgs_; ImgStat other_{"(unmapped)", 0, 0, false, 0, 0}; size_t img_last_ = 0;
    // JIT code is not a file, so it gets its own row: `jit:<pid>'.  It takes precedence over the anonymous-dump mapping
    // the same addresses fall in.
    ImgStat jit_stat_{"jit", 0, 0, true, 0, 0};
    void count_image(uint64_t ip, bool unknown);
    void emit(uint64_t addr, bool known, int size, int op, uint64_t ip, uint64_t ts);
    // ---- same-process ground truth ----
    GtStream gt_; size_t gt_n_ = 0, gt_pos_ = 0; int gt_fd_ = -1; void* gt_mm_ = nullptr; size_t gt_mm_len_ = 0;
    std::unordered_set<uint64_t> gt_sites_;
    // JIT: trampoline addresses (of relocated copies) that a gt record is keyed by, and the
    // one the reconstruction is executing right now (0 = not in a described JIT trampoline).
    std::unordered_set<uint64_t> gt_tramps_;
    uint64_t cur_tramp_ = 0;
    uint64_t g_gt_key_tramp_ = 0, g_gt_key_ip_ = 0;   // how each compared record was matched
    FILE* gt_trace_ = nullptr; bool gt_trace_tried_ = false;   // PTRECON_GT_TRACE diagnostic
    // ---- attach anchoring + confirmed re-alignment ------------------------------------
    struct GtPend { uint64_t ip, addr, ts, vals, syncs; int32_t size, op; uint8_t fl; };
    std::vector<GtPend> gt_pend_;          // the buffered prefix (flags: 1 known, 2 in_ovf,
    bool gt_anchor_done_ = false;          // 4 unanchored, 8 switch_loss, 16 warming, 32 arm_resync)
    size_t gt_anchor_pos_ = 0;             // gt index the compared interval starts at
    size_t gt_hw_ = 0;                     // high-water mark of gt_pos_ (gt_only must not double count)
    uint64_t g_anchor_score_ = 0, g_anchor_of_ = 0;   // diagnostics
    bool gt_anchor_failed_ = false;
    std::vector<uint64_t> gt_ipv_;         // flat ip vector of the gt stream (anchoring only)
    uint64_t gt_hist_[128] = {0}; size_t gt_hist_n_ = 0;   // last reconstructed gt-site ips
    bool gt_arm_pending_ = false;          // a state loss happened while the prefix was buffered
    uint64_t g_gt_confirm_rejected_ = 0;   // re-alignments the confirmation window refused
    uint64_t g_gt_skipped_raw_ = 0, g_gt_before_ = 0;
    void gt_anchor_now();
    void gt_replay(const GtPend& p);
    bool gt_confirm_at(size_t k2) const;
    void gt_classify(uint64_t addr, bool known, uint64_t ip, int size, int op, uint64_t ts,
                     bool f_ovf, bool f_unanch, bool f_switch, bool f_warm,
                     uint64_t vals, uint64_t syncs, bool arm);
    FILE* gt_out_ = nullptr; bool gt_on_ = false;
    // The ground-truth log is BOUNDED (runtime `PTLOG_GT_MAX'): the traced process flushes and
    // exits when its gt file is full, so the two streams end at the same instruction.  They do
    // NOT necessarily BEGIN at the same one -- see `gt_attach_anchor'.  If the PT
    // stream nonetheless outlives the gt one (the process was killed, say), decoding stops here
    // rather than reporting every later access as `recon_only'.
    bool gt_exhausted_ = false; uint64_t gt_stopped_at_ = 0;
    uint64_t g_after_end_ = 0;
    uint64_t g_identical_ = 0, g_unknown_ = 0, g_wrong_ = 0, g_excluded_ = 0;
    uint64_t g_excluded_unpatched_ = 0;   // a JIT gt site, executed before its patch
    std::unordered_map<uint64_t, uint64_t> gt_site_from_;   // gt site -> first instrumented tsc
    uint64_t g_gt_only_ = 0, g_recon_only_ = 0, g_gt_tail_ = 0;
    // Re-align the gt walk on (ip, address) after a state loss; `gt_resync_armed_' counts
    // down the known records it may try.
    uint64_t gt_resync_armed_ = 0, g_gt_resync_loss_ = 0, g_gt_skipped_loss_ = 0, g_gt_rewound_loss_ = 0, g_gt_resync_fail_ = 0;
    bool gt_resync_pending_ = false; uint64_t gt_vals_at_loss_ = 0, gt_sync_at_loss_ = 0, gt_vals_at_sync_ = 0;
    uint64_t gt_streak_ = 0, g_gt_resync_streak_ = 0, g_gt_skipped_streak_ = 0;   // recon_only streak re-alignment
    void gt_arm_resync();
    uint64_t g_wrong_ovf_ = 0, g_wrong_samepage_ = 0, g_wrong_unanchored_ = 0;
    uint64_t g_unknown_ovf_ = 0, g_unknown_unanchored_ = 0;
    std::unordered_map<uint64_t, uint64_t> g_wrong_ip_, g_unknown_ip_;
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> g_wrong_ex_;
    void gt_open();
    void gt_step(uint64_t addr, bool known, uint64_t ip, int size, int op, uint64_t ts);
    void gt_finish();
    std::string gt_json() const;
    void define_from_ptw(uint64_t payload, int size);
    uint64_t ea(const InsnInfo& ii, uint64_t ip, bool* known);
    uint64_t ea_desc(const MemDesc& d, uint64_t ip, int len, bool* known);
};
