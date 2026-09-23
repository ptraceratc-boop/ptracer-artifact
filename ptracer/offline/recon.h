// recon.h -- PTracer offline driver: walks the PT instruction stream, consumes the critical value
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
// Site map (SPEC_FORMAT section 2).  Addresses are link-time vaddrs; Recon adds the runtime load
// base of the rewritten image before using them.
struct SiteMapEnt { uint64_t tramp = 0; int site = -1; uint64_t orig = 0; std::string kind, reg, half; int size = 0, payload_bits = 64;
                    int map = 0;          // index of the site map this entry came from (which image)
                    bool buffer = false;     // this value is logged into cv.*.bin, not PTWRITE
                    // MIXED SINK: a site map may name a sink PER ENTRY ("sink": "ptwrite" |
                    // "buffer"), because one rewritten image can log the sparse sites through
                    // `ptwrite' and the hot ones into the buffer.  Empty = the map's own top-level
                    // `sink' answers for this entry.
                    std::string sink;
                    // Log-on-change ("delta") guard: the value is logged only when it differs from
                    // the last one logged here, so the logging instruction at `tramp' runs only when
                    // the `je' at `je_addr' falls through.  Seeing `join_addr' as the instruction
                    // after the branch means it was SKIPPED and the value is this reconstructor's
                    // own copy of the runtime's cache slot.
                    bool delta = false; uint64_t slot = 0, cmp_addr = 0, je_addr = 0, join_addr = 0;
                    // KEYFRAME guard: this value is logged only every `kf_period'-th execution of
                    // its site.  The countdown is a real branch, so PT records the decision: seeing
                    // `kf_join' as the instruction after `kf_branch' means this execution was NOT a
                    // keyframe and nothing was logged.  `resync' marks a spec `resync' site (a loop
                    // back-edge re-anchor) as opposed to a log-on-change value that also keyframes.
                    bool resync = false; long kf_period = 0; uint64_t counter = 0, kf_branch = 0, kf_join = 0;
                    bool counter_gs = false; };
struct RelocEnt { uint64_t tramp = 0, orig = 0; int len = 0, tramp_len = 0; };
struct SiteMap {
    std::string sink = "ptwrite", image, orig_image;
    uint64_t sync = 0; bool present = false, applied = false, mapped = false;
    std::vector<SiteMapEnt> entries;
    std::vector<RelocEnt> relocated;
    std::vector<uint64_t> sync_markers;
    std::vector<uint64_t> trampolines;   // entry address of every trampoline the map describes
    long gt_sites = 0;                   // `role: "gt"' entries, skipped on load
    // --gt-all build: link-time address of EVERY instruction that records its effective address
    // in the gt ring, including the ones whose trampoline the rewriter could not describe.
    std::vector<uint64_t> gt_site_addrs;
    bool delta = false;                  // some of its values are logged on change
    bool keyframe = false;               // some of its values are logged every K-th execution
    static SiteMap load(const std::string& path);
};

struct ReconOptions {
    // --spec / --sitemap / --orig-image are REPEATABLE, one (spec, sitemap, orig-image) group per
    // rewritten image (whole-program CPython is ten rewritten images, and with a single site map
    // the other nine are either unmapped instrumentation or, worse, interpreted as program code).
    // They are matched to the process's mappings by the site map's own `image' field, not by
    // position; --orig-image applies to the --spec/--sitemap given just before it.
    std::vector<std::string> specs, sitemaps, orig_images;   // orig_images[i] may be empty
    std::string aux, sideband, cvfile, out, summary;
    bool text = false; bool decode_only = false; bool no_time = false;           // print "L/S addr size ip" lines to stdout
    bool gt_continue = false;  // reconstruct PT suffix after oracle EOF; never infer missing GT
    uint64_t max_insn = 0;       // stop after N instructions (0 = all)
    uint64_t max_rep = 1u << 20; // refuse to expand a `rep' longer than this (0 = no limit)
    uint64_t skip_bytes = 0;     // parallel mode: start decoding at this AUX byte offset
    uint64_t end_bytes = 0;      // parallel mode: stop at this AUX byte offset (0 = end of file)
    // ---- chunk warm-up -----------------------------------------------------------------------
    // A chunk that simply starts decoding at its own PSB starts with an ALL-UNKNOWN machine state
    // and has to re-anchor from its own logged values, which never happens for a register whose
    // only logging site is a rare one (`%rbx'/`%rbp' inside a long-running interpreter frame).
    // `emit_from' makes the chunk decode a TAIL OF THE PREVIOUS CHUNK first, with every record and
    // every counter suppressed, so registers, the delta table and the shadow are warm when its own
    // range begins: decoding starts at `skip_bytes' and output starts at `emit_from'.
    uint64_t emit_from = 0;      // AUX offset at which this chunk starts emitting (0 = from skip_bytes)
    uint64_t warm_bytes = 32ull << 20;   // --jobs: how much of the previous chunk to decode as warm-up
    // --exact-warm: every chunk warms up from BYTE 0, not from the previous chunk.  The residual
    // difference between a parallel and a serial reconstruction is state established during
    // process startup and never logged again: no bounded warm-up recovers it, so this is the only
    // setting that is bit-identical to serial by construction -- and it costs the whole prefix per
    // chunk, i.e. most of the parallel speed-up.
    bool exact_warm = false;
    // Process invariants a chunk child inherits from the parent instead of re-deriving them.  The
    // image load bases already come from the sideband; the TLS base does not, so pt_capture2 reads
    // it with ptrace and stores it in the sideband ("fs_base"), and it is seeded ONLY in a chunk
    // that does not start at the beginning of the trace (before the loader has run, %fs is still 0
    // and seeding the final value would be wrong).
    uint64_t fs_base = 0;        // --fs-base: override / supply the TLS base
    bool seed_fs = false;        // seed guest_FS_CONST at startup (set for chunk children)
    bool no_seed_fs = false;     // --no-seed-fs: ablation, do not inherit the TLS base
    int jobs = 1;                // ptrecon --jobs N: chunked parallel reconstruction
    // ---- exact log-on-change state across a chunk boundary -----------------------------------
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
    // ---- sound chunk boundaries --------------------------------------------------------------
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
};

class Recon {
public:
    explicit Recon(const ReconOptions& o);
    int run();
    // --delta-scan round 1: decode this chunk's range for LOGGING EVENTS ONLY and write the
    // per-entry "last value logged here, and when" table.  No interpretation, no records.
    int run_delta_scan();
    // --jobs boundary probe: decode this range looking for the first point at which a chunk could
    // start and be anchored (keyframe + sync marker, no state loss after).  No interpretation,
    // no records; costs about a `--decode-only' pass over the scanned bytes.
    int run_anchor_scan();
    // Can this capture be split at all?  False (with a reason) when nothing in the stream can
    // re-anchor a mid-trace chunk: no keyframe sites, or a buffer sink with no sync markers.
    bool can_split(std::string& why) const;
    size_t n_logged_values() const { return flat_ent_.size(); }
    // Does any applied site map log values into the buffer sink (cv.*.bin)?  When it does, a
    // reconstruction given --cv that consumed no value at all is wrong, not merely incomplete.
    bool has_buffer_values() const { for (const auto& e : flat_ent_) if (e.sink.empty() ? e.buffer : (e.sink == "buffer")) return true; return false; }
    int n_sitemaps_applied() const { return n_smap_ok_; }
    bool has_delta() const { return n_delta_values_ != 0; }
    // --jobs: re-point this (never-run) Recon at one chunk of the same trace, and read the value
    // stream before forking so that every chunk child shares it.  See recon.cpp.
    void retarget(const ReconOptions& co);
    void preload_values();
    // Parallel driver (ptrecon --jobs N): split the AUX file at PSB packets and reconstruct the
    // chunks concurrently.  Defined in recon_par.cpp.
    static int run_parallel(const ReconOptions& o);
private:
    ReconOptions o_;
    Sideband sb_;
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
    // marks HIDDEN, which is how a `rep stos'/`rep movs' string operand is reported.
    struct MemDesc { int base = -1, index = -1, scale = 1; int64_t disp = 0; int size = 0;
                     bool riprel = false; int seg = 0; };   // seg: 0 = none/DS/SS/ES/CS, 1 = FS, 2 = GS
    struct InsnInfo { bool ptwrite = false, rep = false, ok = false, glue = false, branch = false, state_save = false, orig = false; int ptw_reg_off = -1, ptw_reg_size = 0; bool ptw_mem = false;
        int base = -1, index = -1, scale = 1; int64_t disp = 0; bool riprel = false; int memsize = 0; int len = 0; uint64_t hash = 0;
        int seg = 0;                 // segment override of the FIRST VISIBLE memory operand
        uint32_t gpwrite = 0;        // bitmask of 64-bit GP registers the instruction writes (Zydis)
        bool mem_write = false;      // it writes memory
        bool patch_jump = false;     // E9Patch's punned `48 e9 <rel32>' patch jump (see run())
        MemDesc mem_ops[2]; int n_mem_ops = 0;   // every memory operand, hidden ones included
        bool mem_rmw = false;        // its single memory operand is read AND written (-> one MT_RMW record)
        uint8_t encoding[16]{};      // exact input bytes; an address alone does not identify mutable code
        // ---- per-ip DISPATCH CACHE ----------------------------------------------------------
        // run() would otherwise ask the site-map tables what an ip is on EVERY execution of it:
        // up to nine std::unordered_map lookups per instruction (sm_kf_, sm_je_, sm_ent_,
        // sm_sync_, sm_rel_, sm_tramp_, tramp_entry_, orig_raw_, orig_insns_), measured at ~40 %
        // of a whole-program CPython run.  Every one of those tables is built once, before run(),
        // and never changed afterwards, so an ip's ROLE is a function of the ip alone and is
        // cached here -- in the InsnInfo, which is exactly the object that gets thrown away when
        // self-modifying code reuses an address.  No decision changes; the counters that
        // tramp_unmapped() bumps are bumped by sm_unmapped().
        mutable uint16_t sm_flags = 0;                  // SMF_*; SMF_DONE = the rest is valid
        mutable uint32_t sm_ent_idx = 0, sm_je_idx = 0, sm_kf_idx = 0;
        mutable const RelocEnt* sm_relp = nullptr;      // SMF_REL: the sm_rel_ entry for this ip
        mutable const std::vector<uint8_t>* sm_ob = nullptr;   // SMF_OB: orig_bytes_at(sm_relp->orig)
        mutable const InsnInfo* sm_oi = nullptr;        // SMF_OI: orig_info(sm_relp->orig)
        mutable const IRBlockC* sm_blk_orig = nullptr;  // SMF_BO: orig_blocks_[sm_relp->orig]
        mutable const IRBlockC* sm_blk_self = nullptr;  // SMF_BS: blocks_[ip]
    };
    enum { SMF_DONE = 1u << 0, SMF_ENT = 1u << 1, SMF_SYNC = 1u << 2, SMF_JE = 1u << 3,
           SMF_KF = 1u << 4, SMF_REL = 1u << 5, SMF_TRAMPENT = 1u << 6, SMF_TRAMPSET = 1u << 7,
           SMF_OB = 1u << 8, SMF_OI = 1u << 9, SMF_BO = 1u << 10, SMF_BS = 1u << 11 };
    void sm_fill(const InsnInfo& ii, uint64_t ip);
    inline bool sm_unmapped(const InsnInfo& ii) {       // == tramp_unmapped(ip), counter included
        if (sm_tramp_.empty()) return false;
        if (ii.sm_flags & (SMF_TRAMPSET | SMF_REL | SMF_ENT | SMF_SYNC)) return false;
        if (!(ii.sm_flags & SMF_TRAMPENT)) return false;
        n_tramp_unknown_++;
        return true;
    }
    std::unordered_map<uint64_t, InsnInfo> insns_;
    // Direct-mapped front cache for `insns_'.  The hash table itself stays node-based (run()
    // holds an InsnInfo reference across calls that insert into it, which only a node-based
    // container survives); this only skips the bucket walk on the common hit.
    struct ICacheEnt { uint64_t ip = 0; InsnInfo* p = nullptr; };
    static const size_t kICacheBits = 13;
    ICacheEnt icache_[1u << kICacheBits];
    static inline size_t icache_slot(uint64_t ip) { return (size_t)((ip * 0x9E3779B97F4A7C15ull) >> (64 - kICacheBits)); }
    inline void icache_drop(uint64_t ip) { ICacheEnt& c = icache_[icache_slot(ip)]; if (c.ip == ip) { c.ip = 0; c.p = nullptr; } }
    const InsnInfo& info(uint64_t ip, const uint8_t* raw, int len);
    // Same, but for the ORIGINAL instruction at an original address, in its own cache: the
    // rewritten image holds E9Patch's patch jump at exactly that address, so `insns_' (keyed by
    // the runtime ip) would otherwise return the jump's decoding for it.
    const InsnInfo* orig_info(uint64_t addr);
    // original-code / instrumentation regions (E9Patch trampolines, handlers, loader)
    struct Range { uint64_t lo, hi; };
    std::vector<Range> orig_code_;                      // executable mappings of original ELF PT_LOADs (plus libraries)
    bool in_orig_code(uint64_t ip) const;
    // E9Patch trampolines: entry address -> patched original address (from the jmp planted at the site)
    std::unordered_map<uint64_t, uint64_t> tramp_entry_;
    std::unordered_map<std::string, uint64_t> bias_;          // file -> load bias
    // EVERY load base a file has in the sideband.  A sideband that spans an execve is a union
    // over two address spaces and an image mapped in both has two.
    std::unordered_map<std::string, std::vector<uint64_t>> biases_;
    std::unordered_map<std::string, std::vector<uint8_t>> file_cache_;
    struct Seg { uint64_t vaddr, off, filesz; };
    std::unordered_map<std::string, std::vector<Seg>> segs_;   // file -> PT_LOADs
    const std::vector<uint8_t>* file_bytes(const std::string& path);
    // ---- interval index over the sideband's executable mappings ---------------------------
    // The mappings are painted into a disjoint, sorted interval list -- later mappings overwrite
    // earlier ones, which is the process's own "last mapping wins" rule (E9Patch's loader re-maps
    // a text page from the patched copy with MAP_FIXED) -- and looked up by binary search, with a
    // one-entry "last interval" cache and a per-page byte-pointer cache in front of it.  A linear
    // scan is out of the question: whole-program CPython has ~17 000 mappings.
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
    uint64_t cursor_ = 0; bool cursor_valid_ = false, tramp_fallback_ = false; uint64_t n_tramp_mismatch_ = 0, n_tramp_unknown_ = 0;
    bool tramp_unmapped(uint64_t ip);
    void build_tramp_map();
    // --- site map, rebased to runtime addresses -------------------------------------------
    bool smap_ok_ = false; uint64_t smap_base_ = 0; int n_smap_ok_ = 0, n_smap_bad_ = 0;
    std::vector<uint64_t> smap_bases_;                  // per site map
    std::vector<SiteMapEnt> flat_ent_;                  // every logged value of every site map
    std::unordered_map<uint64_t, uint32_t> sm_ent_;     // logging instruction -> index into flat_ent_
    std::unordered_map<uint64_t, RelocEnt> sm_rel_;     // relocated copy -> original instruction
    std::unordered_set<uint64_t> sm_sync_;              // buffer-sink sync markers
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
    // Skipped guards whose cache slot was re-derived from the machine state instead of from a
    // logged value (a taken `je' proves the slot equals the value the site would log).
    uint64_t n_delta_reanchor_ = 0;
    void delta_take_from_table(long idx);
    void delta_invalidate();
    // --- keyframe logging ------------------------------------------------------------------
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
    // critical value log
    enum PtwKind { PTW_NONE, PTW_REG, PTW_MEM, PTW_SYNC };
    std::deque<std::pair<uint64_t, int>> ptw_q_;      // (payload, size)
    bool ptw_pending_ = false; PtwKind ptw_kind_ = PTW_NONE;
    int ptw_pend_off_ = -1, ptw_pend_size_ = 0, ptw_pend_memsize_ = 0; uint64_t ptw_pend_ea_ = 0;
    bool ptw_pend_mem_legacy_ = false;   // no site map: the `ptwrite mem' form describes itself
    std::vector<uint64_t> cv_; size_t cv_pos_ = 0;
    // A site's sync marker is emitted AFTER the site's value stores, so the marker that realigns
    // the positional cv cursor arrives one value too late for the site it fires at.  That costs
    // exactly one value per run: the thread's FIRST logged value fires a marker, and before that
    // marker the decoder has consumed the cv slots of trampolines whose patched pages were not
    // yet installed when the code really ran (libc's IFUNC resolvers / __libc_early_init).  These
    // two remember the values consumed at the CURRENT trampoline so a realigning marker can
    // re-read them at the corrected positions; the bookkeeping stops at the first marker, so the
    // steady state costs one predictable branch.
    bool pre_first_sync_ = true;
    std::vector<std::pair<uint32_t, size_t>> tramp_cv_;   // (site-map entry index, cv slot)
    uint64_t n_cv_redefined_ = 0;                         // values re-read after a realignment
    FILE* cv_audit_ = nullptr; uint64_t n_sync_seen_ = 0, sync_ip_ = 0, sync_off_ = 0;   // --cv-audit
    uint64_t sync_nres_ = 0, sync_ninsn_ = 0;      // --cv-audit: decoder state at the marker
    uint64_t n_fb_logsite_ = 0;   // logging instructions seen inside a trampoline the map does not describe
    bool arm_value_site(const SiteMapEnt& e);         // set the pending PTW target from the site map
    void define_value(const SiteMapEnt& e, uint64_t v);// buffer sink: consume one logged value
    // output
    FILE* out_ = nullptr; uint64_t n_arch_prctl_fs_ = 0; uint64_t n_output_rec_ = 0; uint64_t n_rec_ = 0, n_unknown_ = 0, n_ovf_ = 0, n_rep_unknown_ = 0, n_ptw_used_ = 0, n_lift_fail_ = 0, n_instr_skipped_ = 0;
    uint64_t n_rep_overlong_ = 0;
    // An omitted instruction/REP range is NOT one unknown memory access. Keep a
    // bounded diagnostic ledger so a small observed-record error cannot hide it.
    uint64_t n_lift_fail_memory_ = 0, n_rep_collapse_unknown_count_ = 0;
    uint64_t n_memory_omission_events_ = 0;
    uint64_t n_lifted_memory_shortfall_ = 0;
    uint64_t recent_original_ips_[32] = {};
    uint64_t recent_original_count_ = 0;
    std::vector<std::string> memory_omission_examples_;
    void note_memory_omission(const char* kind, uint64_t ip, const InsnInfo& ii,
                              const uint8_t* raw, int len, const Val& count, bool addr_known);
    uint64_t n_patch_jump_ = 0;      // E9Patch `48 e9' patch jumps executed (NOT lift failures)
    uint64_t warm_insn_ = 0;         // instructions decoded as chunk warm-up, before `emit_from'
    bool warming_ = false;           // suppress every record and every counter until `emit_from'
    void reset_counters();           // called when warm-up ends: the warm-up must not be reported
    uint64_t n_lift_fail_kept_ = 0, n_cv_used_ = 0, n_cv_missing_ = 0, n_sync_used_ = 0, n_sync_resync_ = 0, n_rmw_ = 0;
    // Parallel reconstruction: a chunk starts with an all-unknown machine state and re-anchors as
    // logged values arrive, so the records it emits before the first logged value is consumed are
    // the ones a single-threaded run would have had anchored.  Counted and reported.
    bool anchored_ = false; uint64_t n_unanchored_rec_ = 0;
    uint32_t tid_ = 0; bool in_ovf_ = false;
    std::vector<MemAccess> acc_;                       // accesses of the instruction being interpreted
    void flush_acc(const InsnInfo& ii, uint64_t ip, uint64_t ts);
    // per-image statistics (by the record's ip, using the sideband maps)
    struct ImgStat { std::string path; uint64_t lo = 0, hi = 0; bool orig = false; uint64_t records = 0, unknown = 0; };
    std::vector<ImgStat> imgs_; ImgStat other_{"(unmapped)", 0, 0, false, 0, 0}; size_t img_last_ = 0;
    void count_image(uint64_t ip, bool unknown);
    void emit(uint64_t addr, bool known, int size, int op, uint64_t ip, uint64_t ts);
    // ---- same-process ground truth ----
    GtStream gt_; size_t gt_n_ = 0, gt_pos_ = 0; int gt_fd_ = -1; void* gt_mm_ = nullptr; size_t gt_mm_len_ = 0;
    std::unordered_set<uint64_t> gt_sites_;
    size_t gt_hw_ = 0;                     // high-water mark of gt_pos_ (gt_only must not double count)
    // A streak re-alignment must also agree with the CONTROL FLOW that led to it: the ips of the
    // last reconstructed gt-site records are compared with the gt ips before the candidate.
    uint64_t gt_hist_[128] = {0}; size_t gt_hist_n_ = 0;   // last reconstructed gt-site ips
    uint64_t g_gt_confirm_rejected_ = 0;   // re-alignments the confirmation window refused
    uint64_t g_gt_skipped_raw_ = 0;
    bool gt_confirm_at(size_t k2) const;
    FILE* gt_out_ = nullptr; bool gt_on_ = false;
    // The ground-truth log is BOUNDED (runtime `PTLOG_GT_MAX'): the traced process flushes and
    // exits when its gt file is full, so the two streams end at the same instruction.  If the PT
    // stream nonetheless outlives the gt one (the process was killed, say), decoding stops here
    // rather than reporting every later access as `recon_only'.
    bool gt_exhausted_ = false; uint64_t gt_stopped_at_ = 0;
    uint64_t g_after_end_ = 0;
    uint64_t g_identical_ = 0, g_unknown_ = 0, g_wrong_ = 0, g_excluded_ = 0;
    uint64_t g_gt_only_ = 0, g_recon_only_ = 0, g_gt_tail_ = 0;
    // Re-align the gt walk on (ip, address) after a state loss; `gt_resync_armed_' counts down
    // the known records it may try.
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
