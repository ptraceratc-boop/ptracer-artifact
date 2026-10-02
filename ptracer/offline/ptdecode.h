// ptdecode.h -- Intel PT instruction-flow decoding (libipt) for PTracer Stage 3.
// The "control flow log" is the raw AUX buffer saved by pt_capture2 --aux-out; the sideband JSON
// (--sideband) supplies the process memory map from which the libipt image is built: EVERY
// executable file-backed mapping is added as-is (E9Patch's loader maps the rewritten code pages
// and trampolines from the rewritten file at runtime, so /proc/PID/maps is the ground truth, not
// the ELF program headers).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <intel-pt.h>
#include "json.h"
#include "jitcode.h"
#include "e9phase.h"
#include "decode_progress.h"

struct MapEnt { uint64_t start = 0, end = 0, off = 0; std::string perms, path; bool exec() const { return perms.size() > 2 && perms[2] == 'x'; } };
struct Sideband {
    int pid = 0, exit_code = 0; uint64_t aux_bytes = 0; bool wrapped = false; int mtc_period = 3;
    int family = 6, model = 0, stepping = 0; uint32_t cpuid15_eax = 0, cpuid15_ebx = 0;
    uint32_t time_mult = 0, time_shift = 0; uint64_t time_zero = 0;
    // TLS base of the traced (single) thread, read with ptrace by pt_capture2.  It is a process
    // constant, and it is the one piece of machine state a parallel chunk cannot re-derive from
    // its own logged values (the analyzer anchors `fs_base' once, at `main').
    uint64_t fs_base = 0;
    std::vector<MapEnt> maps;
    // ---- MULTI-THREADED capture (sideband version 2) ----------------------------------------
    // `cpus': one entry per `pt_capture2 --cpu N' event -- that core's AUX file and the binary
    // context-switch record stream drained from its perf data ring (`struct SwRec').  Empty for
    // a per-task capture.  `threads': every thread ptrace saw in the traced process, with the
    // TLS base the kernel gave it at creation (CLONE_SETTLS); the main thread has tid == pid.
    int sb_version = 1;
    struct Cpu { int cpu = -1; std::string aux, switch_file; uint64_t aux_bytes = 0, lost = 0, trunc = 0, n_switch = 0, data_lost = 0; };
    struct Thread { uint32_t tid = 0; uint64_t fs_base = 0; };
    std::vector<Cpu> cpus; std::vector<Thread> threads;
    bool per_cpu() const { return !cpus.empty(); }
    static Sideband load(const std::string& path);
};
// One context-switch record of `<sideband>.cpu<N>.sw' (written by pt_capture2, 32 bytes).
struct SwRec { uint64_t time, tsc; uint32_t pid, tid, flags, cpu; };
enum { SW_IN = 1, SW_OUT = 2, SW_PREEMPT = 4, SW_ITRACE = 8, SW_LOST = 16 };

// ---- thread plan: which byte ranges of which AUX files belong to ONE thread ----------------
// A per-CPU AUX stream is a sequence of user regions [TIP.PGE, TIP.PGD], each run by one thread;
// the demultiplexer (mtdemux.cpp) cuts every core's stream at those boundaries, attributes each
// region by its start time against the core's switch records, and hands each thread the list of
// its regions in time order -- possibly spanning several files when the thread migrated.  A
// PtDecoder built on a plan decodes exactly those regions, in that order, and returns nothing
// else: an instruction belongs to a region iff the decoder's offset AFTER decoding it lies in
// [begin, end), the same predicate the segmenter used (and the one the --jobs chunking uses).
// `lossy' marks a region that begins at a PT overflow or a decoder resync rather than at a clean
// TIP.PGE: the thread may have run inside the gap, so its state is not to be trusted there.
struct AuxSeg { int file = 0; uint64_t begin = 0, end = 0; uint64_t tsc = 0; bool lossy = false; };
struct ThreadPlan { uint32_t tid = 0; std::vector<std::string> files; std::vector<AuxSeg> segs;
    // The thread's VIRTUAL stream = its segments concatenated; vbase[i] = virtual offset of
    // segs[i].begin.  --mt chunking (--jobs N with --mt) addresses a thread by these offsets.
    std::vector<uint64_t> vbase; uint64_t vtotal = 0;
    void index() { vbase.clear(); vtotal = 0; for (auto& s : segs) { vbase.push_back(vtotal); vtotal += s.end > s.begin ? s.end - s.begin : 0; } }
    // the virtual range [from, to) of this plan (segments clipped; global virtual offsets kept)
    ThreadPlan slice(uint64_t from, uint64_t to) const {
        ThreadPlan r; r.tid = tid; r.files = files; r.vtotal = vtotal;
        for (size_t i = 0; i < segs.size(); i++) {
            const uint64_t vb = vbase[i], len = segs[i].end > segs[i].begin ? segs[i].end - segs[i].begin : 0;
            if (vb + len <= from || (to && vb >= to)) continue;
            AuxSeg sg = segs[i]; uint64_t nv = vb;
            if (from > vb) { sg.begin += from - vb; nv = from; sg.lossy = false; }
            if (to && to < vb + len) sg.end = segs[i].begin + (to - vb);
            r.segs.push_back(sg); r.vbase.push_back(nv);
        }
        return r; }
};

struct PtEvents {
    std::function<void(uint64_t payload, int size, uint64_t ip)> on_ptwrite;
    std::function<void(uint64_t tsc)> on_overflow;      // PT lost packets: state must be considered unknown
    std::function<void(uint64_t ip, bool enabled)> on_enable; // tracing enabled/disabled (e.g. kernel entry/exit)
    std::function<void(int err, uint64_t off)> on_resync; // decoder lost sync and re-synchronized at a PSB
    // Segmentation hook (mtdemux.cpp): a point at which a per-CPU stream may change thread.
    // kind: 'E' tracing enabled (TIP.PGE), 'O' overflow, 'R' first instruction after a resync,
    // 'S' first instruction of the trace.  `off' is the decoder's offset, `tsc' its time there.
    std::function<void(char kind, uint64_t off, uint64_t tsc, uint64_t ip)> on_boundary;
    // Plan mode: the thread's next region begins at a LOSSY boundary (see AuxSeg).  When unset,
    // on_overflow is called instead, which has the same effect on the reconstructor's state.
    std::function<void(uint64_t tsc)> on_switch_loss;
};

// Parallel reconstruction (implemented; ptrecon --jobs N, see recon_par.cpp): a chunk =
// [PSB_i, PSB_j) of the AUX buffer, given here as [skip, end); each chunk gets its own PtDecoder
// (pt_insn_sync_forward finds the first PSB at or after `skip' via pt_config.begin/end) and its own
// VexInterp starting from an all-unknown state, and the chunk outputs are concatenated in order.
// Nothing in PtDecoder is shared between instances.  The AUX file is mmap()ed, so N chunk decoders
// of the same file share one physical copy.
class PtDecoder {
public:
    // `jit' (ptrecon --jitdump) is the time-keyed code table of jitcode.h.  When it is given,
    // every executable mapping it describes is LEFT OUT of the libipt image -- pt_capture2 dumps
    // an anonymous code range once, when it first sees it, which for a JIT code space and for the
    // trampoline slab is a stale snapshot -- and served through pt_image_set_callback() instead,
    // keyed by the decoder's current TSC estimate.  The dump is kept as the table's last-resort
    // "background" layer, so nothing that decoded before decodes worse.
    // `preinit' (e9phase.h) selects the PRE-INIT view of every E9Patch-rewritten image:
    // the original bytes at the original addresses, swapped for the patched overlay when that
    // image's loader segment executes.  -1 = decide automatically: a decoder that starts at the
    // process's first instruction (the main thread's plan, or an unchunked decode from offset 0)
    // sees the pre-init window and must start in it; every other decoder starts after all
    // DT_INITs and must not.  0/1 force it off/on (the per-core segmentation pass, mtdemux.cpp).
    PtDecoder(const std::string& auxfile, const Sideband& sb, uint64_t skip = 0, uint64_t end = 0,
              JitTable* jit = nullptr, const ThreadPlan* plan = nullptr, int preinit = -1);
    // Offsets (in the AUX file) of every PSB packet, for splitting a trace into chunks.
    static std::vector<uint64_t> psb_offsets(const std::string& auxfile);
    // Build the post-init decoder image once in a --jobs parent; later decoders share it (see ctor).
    static void prebuild_shared(const std::string& auxfile, const Sideband& sb);
    static uint64_t aux_size(const std::string& auxfile);
    ~PtDecoder();
    // Next executed instruction. Returns false at end of trace. Events fire through `ev`.
    bool next(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev);
    uint64_t n_insn = 0, n_ovf = 0, n_resync = 0, n_ptw = 0; bool no_time = false;
    uint64_t n_sync_fail = 0;               // pt_insn_sync_forward() errors we stepped past
    uint64_t n_psb_repaired = 0;            // truncated PSB+ headers given a PSBEND
    static constexpr int kMaxResync = 64;   // consecutive sync failures before we call it the end
    int sync_fail_run_ = 0;
    const std::vector<MapEnt>& maps() const { return maps_; }
    // ---- time-aware images ------------------------------------------------------------
    bool preinit() const { return preinit_; }
    uint64_t n_e9_swap = 0;            // images swapped from original to patched bytes
    uint64_t n_e9_swap_trig = 0;       // ... of them because their own loader segment executed
    uint64_t n_e9_preinit_insn = 0; uint64_t n_walk_fail = 0;    // instructions decoded while any image was still original
    bool e9_aborted = false;           // a resync ended the pre-init phase early (safety net)
    uint64_t e9_abort_off = 0;
    // The pre-init phase was abandoned before it explained anything: this decoder did not start
    // at the process's first instruction after all.  mtdemux re-runs the file without it.
    bool e9_preinit_useless() const { return e9_aborted && n_e9_swap_trig == 0; }
    std::string e9_json() const;
    // The decoder's current time estimate (TSC/MTC), refreshed before every pt_insn_next() and
    // handed to the JIT code table as the time key.  ~0 = "no timing information yet: latest".
    uint64_t cur_tsc = ~0ull;
    JitTable* jit = nullptr;
    // Absolute offset in the AUX FILE of the decoder's current position.  Used by the chunk
    // warm-up (recon.cpp) to tell "still decoding the previous chunk's tail" from
    // "inside my own range"; libipt reports it relative to cfg_.begin, hence + skip_.
    // Block mode (blk_): an instruction that is NOT the last of its libipt block was decoded
    // without a trace query, so the per-instruction decoder would report the offset and time from
    // BEFORE the block for it; the last one sees the live position.  See step_blk().
    uint64_t offset() const { if (blk_ && bi_ < bn_) return skip_ + boff0_; return skip_ + live_offset(); }
    // Which libipt decoder feeds next(): "blk" (pt_blk_next + a per-(isid,ip) instruction walk
    // cache) or "insn" (pt_insn_next).  PTRECON_DECODER=insn|blk forces one; blk is used only
    // where it is exact by construction (no JIT callback, no plan, every section cached).
    const char* decoder_kind() const { return blk_ ? "blk" : "insn"; }
    uint64_t n_blocks = 0, n_walk_miss = 0;
    // PTRECON_DECHASH=1: a hash of the delivered instruction stream (ip, bytes, per-instruction
    // offset and time) and of every event in order; the insn/blk identity check.
    uint64_t dec_hash = 0; bool dec_hash_on = false;
    // A per-instruction slot the CALLER may use to memoise what it derives from (ip, bytes) of the
    // instruction just returned: block mode keys it on (section, walk position), whose ip and bytes
    // never change.  nullptr in insn mode or for a truncated instruction.  The caller must validate
    // its own memo (Recon stores a generation number next to the pointer).
    // Block mode, --decode-only: count (without delivering) up to `max' of the remaining
    // instructions of the current block EXCEPT its last one.  They carry the offset and time of
    // the instruction just delivered, so every per-instruction test a decode-only caller makes
    // (chunk cut, warm-up end, --max-insn) has the same outcome for them; the E9 pre-init swap
    // check still runs per instruction.  Returns how many were skipped (added to n_insn).  0 when
    // not applicable (insn mode, PTRECON_DECHASH, a progress limit).
    uint64_t skip_nonlast(uint64_t max);
    struct BInsn { uint64_t ip = 0, next = 0; uint8_t size = 0, kind = 0; uint8_t raw[15]; mutable void* uptr = nullptr; mutable uint64_t ugen = 0; mutable void* fptr = nullptr; mutable uint64_t fgen = 0; };
    // Batch delivery: the NON-LAST instructions of the current block that follow
    // the one just delivered.  Every per-instruction test the consumer makes on them has the same
    // outcome as for the instruction just delivered (same offset() and time: see skip_nonlast), so
    // a consumer may process them from this array and then batch_advance(n) -- exactly what n
    // next() calls would have delivered.  0 when not applicable (insn mode, PTRECON_DECHASH, a
    // progress limit, the E9 pre-init window whose swap check runs per instruction).
    uint32_t batch_avail(const BInsn** p, uint32_t* nonlast, PtEvents& ev) {
        if (!blk_ || dec_hash_on || progress_.limit) return 0;
        if (plan_) {   // only the rest of a block next_plan() is delivering (same offset: same segment test)
            if (!positioned_ || seg_first_ || !bi_ || bi_ >= bn_) return 0;
            const uint32_t nl0 = bn_ - 1 - bi_; uint32_t k0 = nl0;
            if (preinit_ && e9_unswapped_)
                for (uint32_t j = 0; j < k0; j++) { int ii = e9_.loader_image(bcur_[bi_ + j].ip);
                    if (ii >= 0 && !e9_[(size_t)ii].swapped) { k0 = j; break; } }
            *nonlast = k0; *p = bcur_ + bi_; return k0; }
        if (bi_ >= bn_ && !fetch_blk(ev)) return 0;
        if (pending_boundary_ || !seen_insn_) return 0;      // on_boundary is the general path's
        uint32_t k = bn_ - bi_; if (blk_cur_.truncated) k--;   // a truncated last insn's bytes come from the block
        if (preinit_ && e9_unswapped_)       // stop BEFORE an instruction step_blk's swap check would act on
            for (uint32_t j = 0; j < k; j++) { int ii = e9_.loader_image(bcur_[bi_ + j].ip);
                if (ii >= 0 && !e9_[(size_t)ii].swapped) { k = j; break; } }
        const uint32_t nl = bn_ - 1 - bi_; *nonlast = k < nl ? k : nl;
        *p = bcur_ + bi_; return k; }
    // offset()/time the per-instruction path reports for a NON-LAST instruction of the current block
    // (the position before the block) and for its LAST one (the live position after pt_blk_next)
    uint64_t batch_off0() const { if (plan_) return vpos(skip_ + boff0_); return skip_ + boff0_; }
    // The offset chunk tests use -- the file offset, or in plan mode the VIRTUAL offset
    uint64_t coff() const { return plan_ ? vpos(offset()) : offset(); }
    uint64_t vpos(uint64_t off) const {
        if (seg_i_ >= plan_->segs.size()) return plan_->vtotal;
        const AuxSeg& s = plan_->segs[seg_i_]; const uint64_t vb = plan_->vbase[seg_i_];
        if (!positioned_ || off < s.begin) return vb;
        if (off >= s.end) return vb + (s.end - s.begin);
        return vb + (off - s.begin); }
    uint64_t batch_off_last() const { return skip_ + live_offset(); }
    uint64_t batch_ts0() const { return no_time || btsc0_ok_ < 0 ? 0 : btsc0_; }
    uint64_t batch_ts_last() const { if (no_time) return 0; uint64_t t = 0; return live_time(&t) < 0 ? 0 : t; }
    void batch_advance(uint32_t k) { if (!k) return; if (preinit_ && e9_unswapped_) n_e9_preinit_insn += k; bi_ += k; n_insn += k;
                                     pending_boundary_ = false; pending_kind_ = 'R'; seen_insn_ = true; }
    bool user_slot(void*** p, uint64_t** gen) const {
        if (!blk_ || !bi_ || (bi_ == bn_ && blk_cur_.truncated)) return false;
        const BInsn& b = bcur_[bi_ - 1]; *p = &b.uptr; *gen = &b.ugen; return true; }
    // Plan mode accounting (0 otherwise).
    uint64_t n_segs = 0, n_segs_lossy = 0, n_segs_unsync = 0, n_skipped_insn = 0, n_reseeks = 0;
    int cur_file() const { return cur_file_; }
    // Why the decode ended, for the "ended before the end of the file" diagnostic.
    const char* eos_why = "";      // "" = still running / normal end of buffer
    uint64_t eos_off = 0; int eos_status = 0;
    bool plan_mode() const { return plan_ != nullptr; }
private:
    DecodeProgress progress_; // opt-in diagnostic limit, never skips/repairs code
    // First PSB strictly after `off' (both relative to cfg_.begin), ~0ull if there is none.
    uint64_t next_psb_after(uint64_t off) const;
    FILE* skipdbg_ = nullptr; bool skipdbg_tried_ = false;
    // ---- plan mode ----
    const ThreadPlan* plan_ = nullptr; ThreadPlan own_plan_;
    struct PlanFile { void* map = nullptr; size_t len = 0; std::vector<uint64_t> psb; };
    std::vector<PlanFile> pfiles_;
    size_t seg_i_ = 0; int cur_file_ = -1; bool positioned_ = false, seg_first_ = false;
    bool pending_boundary_ = false, seen_insn_ = false; char pending_kind_ = 'R';
    // Plan mode: is the decoder's current position inside the current segment?
    bool ev_ok() const { if (!plan_) return true; if (!positioned_ || seg_i_ >= plan_->segs.size()) return false;
                         uint64_t off = offset(); const AuxSeg& s = plan_->segs[seg_i_]; return off >= s.begin && off < s.end; }
    bool open_file(int f);
    bool seek_seg(const AuxSeg& s);
    bool next_plan(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev);
    bool step(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev, bool deliver);   // one decode step
    bool step_blk(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev);               // block-mode step
    bool fetch_blk(PtEvents& ev);                                                   // refill (no delivery)
    // ---- block mode --------------------------------------------------------------------------
    bool blk_ = false;
    struct pt_block_decoder* bdec_ = nullptr;
    std::unordered_map<uint64_t, std::vector<BInsn>> bwalk_;   // key: ip | isid << 48
    struct BFront { uint64_t key = ~0ull; std::vector<BInsn>* v = nullptr; };
    std::vector<BFront> bfront_;
    const BInsn* bcur_ = nullptr; uint32_t bn_ = 0, bi_ = 0;
    struct pt_block blk_cur_;
    uint64_t boff0_ = 0, btsc0_ = 0; int btsc0_ok_ = 0; int bpend_err_ = 0;
    const BInsn* blk_walk(const struct pt_block& b);
    uint64_t live_offset() const;
    int live_time(uint64_t* t) const;     // pt_{insn,blk}_time of the live decoder
    int insn_time(uint64_t* t) const;     // the time the per-instruction decoder would report now
    int d_sync_forward(); int d_sync_set(uint64_t off); int d_event(struct pt_event* e);
    void hash_mix(uint64_t v) { dec_hash = (dec_hash ^ v) * 0x100000001b3ull + (dec_hash >> 29); }
    static bool s_uncached_;
    void alloc_decoder();
    uint64_t skip_ = 0;
    const uint8_t* aux_ = nullptr; size_t aux_len_ = 0; void* aux_map_ = nullptr; size_t aux_map_len_ = 0;
    struct pt_config cfg_;
    struct pt_image* image_ = nullptr;
    // ---- time-aware images (e9phase.h) ----
    E9Phases e9_; bool preinit_ = false; bool uncached_ = false; size_t n_uncached_adds_ = 0;
    size_t e9_unswapped_ = 0;
    std::vector<std::vector<E9Sec>> e9_pending_;   // withheld patched sections, per image
    int add_section(const char* path, uint64_t off, uint64_t len, uint64_t vaddr);
    size_t merge_trampolines(std::vector<char>& merged, size_t n_exec);   // trampoline pages -> few sections
    void e9_swap(size_t i, uint64_t ip, bool forced = false);
    void e9_abort(uint64_t off);                   // a resync while still pre-init: swap everything
    bool shared_img_ = false;
    static struct pt_image* s_img_; static struct pt_image_section_cache* s_isc_;
    static const Sideband* s_sb_; static bool s_publish_;
    struct pt_image_section_cache* iscache_ = nullptr;   // keeps file sections mapped (pt_image_add_file alone remaps per read: ~1M insn/s)
    struct pt_insn_decoder* dec_ = nullptr;
    std::vector<MapEnt> maps_;
    std::vector<std::vector<uint8_t>> bgfiles_;   // (plan_ is declared above)   // anon dumps of the mappings the JIT table serves
    int status_ = 0; bool synced_ = false, eos_ = false;
    bool drain_events(PtEvents& ev);
};
