// recon_par.cpp -- ptrecon --jobs N: chunked, parallel reconstruction.
//
// WHERE A CHUNK MAY START.  Three things have to hold at a chunk boundary, and
// they are three different mechanisms:
//   1. the hardware decoder can only be entered at a PSB packet;
//   2. the machine state is all-unknown at a fresh chunk, so a KEYFRAME (a guarded site that logs
//      its registers every K-th execution) has to re-define the registers;
//   3. with the buffer sink the value stream is POSITIONAL, so a SYNC MARKER (`ptwrite <running
//      count>') has to re-align the cursor.
// A capture with neither 2 nor 3 must not be split at all (its chunks would reconstruct an
// all-unknown machine).  A probe per boundary finds the first point at or after each ideal split
// where 2 and 3 have both happened with no state loss after them (Recon::run_anchor_scan), the
// chunk decodes from the PSB before that point, and a capture that cannot supply such points is
// reconstructed SERIALLY with the reason printed (Recon::can_split).  Chunk 0 starts at byte 0,
// not at the first PSB: `skip_bytes != 0' turns off the pre-init image view (e9phase.h), which
// the loader window of every rewritten image needs.
//
// Each chunk is reconstructed by a forked child with its own PtDecoder and its own VexInterp,
// which decodes [warm, bound[k+1]) but emits only from bound[k]: the warm-up prefix rebuilds the
// registers, the shadow memory and the cursor that a serial run would have had there.  The chunk
// `.mtrace' files are concatenated in chunk order, which reproduces program order because the
// chunks partition the stream.
//
// Why processes and not threads: Recon holds a VexLifter (libVEX has global state and is not
// thread safe) and large per-chunk caches; fork() also gives copy-on-write sharing of everything
// the parent had already read.  The mmap()ed AUX file is shared physically between all children.
#include "recon.h"
#include <map>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

static std::string chunk_path(const std::string& base, int i, const char* suffix) {
    char buf[64]; snprintf(buf, sizeof buf, ".chunk%03d%s", i, suffix);
    return base + buf;
}

int Recon::run_parallel(const ReconOptions& o) {
    const uint64_t total = PtDecoder::aux_size(o.aux);
    std::vector<uint64_t> psb = PtDecoder::psb_offsets(o.aux);
    int jobs = o.jobs; if (jobs > (int)psb.size()) jobs = (int)psb.size();
    if (jobs < 1) jobs = 1;
    const std::string base = o.out.empty() ? (o.summary.empty() ? std::string("ptrecon.par") : o.summary) : o.out;

    // ---- ONE setup for all the workers ---------------------------------------------------
    // Parsing the site-map JSONs, sorting the interval map over every executable mapping and
    // reading the cv value stream describe the PROCESS, not the chunk, so all of it is built once
    // here and inherited through fork(), physically shared copy-on-write.  The parent never runs
    // it, so each child forks from a pristine Recon and only has to say which chunk it is
    // (retarget()) -- and the boundary probes below use the same object.
    std::unique_ptr<Recon> shared;
    if (o.gt_in.empty()) {
        ReconOptions so = o;
        so.jobs = 1; so.out.clear(); so.summary.clear(); so.text = false;
        so.skip_bytes = 0; so.end_bytes = 0; so.emit_from = 0; so.delta_seed_in.clear();
        try { shared.reset(new Recon(so)); shared->preload_values(); }
        catch (std::exception& e) { fprintf(stderr, "ptrecon: shared setup failed (%s)\n", e.what()); shared.reset(); }
    }

    // ---- refusing, loudly, instead of producing garbage ----------------------------------
    // On a build with no keyframes and no sync markers there is NOTHING at a mid-trace PSB to
    // re-anchor a chunk on, and the chunks would reconstruct an all-unknown machine.  A capture
    // that cannot be split is reconstructed serially, with the reason on stderr, which is the
    // answer the caller actually wanted.
    auto run_serial = [&](const std::string& why) -> int {
        fprintf(stderr, "ptrecon --jobs %d: NOT splitting this capture -- %s.\n"
                        "ptrecon: falling back to SERIAL reconstruction (the output is the serial output).\n",
                o.jobs, why.c_str());
        ReconOptions so = o;
        so.jobs = 1; so.skip_bytes = 0; so.end_bytes = 0; so.emit_from = 0;
        so.seed_fs = false; so.delta_seed_in.clear(); so.anchor_scan_out.clear(); so.delta_scan_out.clear();
        if (shared) { shared->retarget(so); return shared->run(); }
        try { Recon r(so); return r.run(); }
        catch (std::exception& e) { fprintf(stderr, "ptrecon: %s\n", e.what()); return 1; }
    };
    if (psb.empty()) return run_serial("there is no PSB packet in " + o.aux + ", so the stream can "
                                       "only be entered at its beginning");
    if (!shared)     return run_serial("the shared setup failed");
    if (jobs < 2)    return run_serial("fewer than two chunks are available");
    // `--decode-only' interprets nothing and emits no records, so it has no replay state to
    // re-anchor: any exact partition of the stream at PSB packets is sound for what it counts.
    // Everything else needs the anchors below.
    const bool need_anchor = !o.decode_only;
    if (need_anchor) { std::string why; if (!shared->can_split(why)) return run_serial(why); }

    // ---- round 0: SOUND chunk boundaries -------------------------------------------------
    // A chunk can only be replayed independently if, where it starts emitting, the decoder has
    // synchronised (a PSB), the registers have been re-defined (a KEYFRAME fired and its site
    // logged) and -- with the buffer sink -- the positional value cursor has been re-aligned (a
    // SYNC MARKER).  None of those is at a fixed distance from an even division of the file, so
    // one cheap probe per boundary decodes forward from the PSB at or before the ideal split
    // point and reports the first offset at which all of it holds.  The probes are independent of
    // the machine state (both events are instruction-stream facts), run concurrently, and only
    // decode as far as the next anchor.
    const uint64_t ideal = total / (uint64_t)jobs;
    const uint64_t search = ideal ? ideal / 2 : 1;   // an anchor further than half a chunk past the
                                                    // ideal split point does not pay for itself
    auto psb_ge = [&](uint64_t x) -> uint64_t {      // the first PSB at or after x (0 if none)
        size_t lo = 0, hi = psb.size();
        while (lo < hi) { size_t mid = (lo + hi) / 2; if (psb[mid] < x) lo = mid + 1; else hi = mid; }
        return lo < psb.size() ? psb[lo] : 0;
    };
    auto psb_le = [&](uint64_t x) -> uint64_t {      // the last PSB at or before x
        size_t lo = 0, hi = psb.size();
        while (lo < hi) { size_t mid = (lo + hi) / 2; if (psb[mid] <= x) lo = mid + 1; else hi = mid; }
        return lo ? psb[lo - 1] : psb.front();
    };
    std::vector<uint64_t> tgt;
    for (int k = 1; k < jobs; k++) tgt.push_back(total * (uint64_t)k / (uint64_t)jobs);
    if (need_anchor) {
        std::vector<pid_t> probes;
        for (size_t k = 0; k < tgt.size(); k++) {
            pid_t p = fork();
            if (p < 0) { perror("fork"); return 2; }
            if (p == 0) {
                ReconOptions co = o; co.jobs = 1; co.out.clear(); co.summary.clear();
                co.skip_bytes = psb_le(tgt[k]); co.emit_from = 0; co.delta_seed_in.clear();
                co.end_bytes = tgt[k] + search < total ? tgt[k] + search : total;
                co.anchor_scan_out = chunk_path(base, (int)k, ".anchor");
                int rc = 1;
                try { shared->retarget(co); rc = shared->run_anchor_scan(); }
                catch (std::exception& e) { fprintf(stderr, "ptrecon anchor probe %zu: %s\n", k, e.what()); rc = 1; }
                _exit(rc);
            }
            probes.push_back(p);
        }
        int pbad = 0;
        for (size_t k = 0; k < probes.size(); k++) { int st = 0; waitpid(probes[k], &st, 0); if (!WIFEXITED(st) || WEXITSTATUS(st)) pbad++; }
        if (pbad) fprintf(stderr, "ptrecon: %d of %zu boundary probes failed\n", pbad, probes.size());
    }
    // Chunk 0 starts at byte 0, NOT at the first PSB: `skip_bytes != 0' turns off the pre-init
    // view of the E9Patch images (e9phase.h) and empties the log-on-change table, so a chunk 0
    // that began at the first PSB would reconstruct the loader window against the PATCHED bytes.
    // libipt synchronises forward to that PSB by itself.
    std::vector<uint64_t> bound; bound.push_back(0);
    long long lag_sum = 0; int lag_n = 0, missed = 0, too_close = 0;
    for (size_t k = 0; k < tgt.size(); k++) {
        int ok = 0; unsigned long anchor = 0, from = 0, kfo = 0, syo = 0, nkf = 0, nin = 0;
        if (!need_anchor) { anchor = (unsigned long)psb_ge(tgt[k]); ok = anchor != 0; }
        else {
            const std::string ap = chunk_path(base, (int)k, ".anchor");
            FILE* f = fopen(ap.c_str(), "r");
            if (f) { if (fscanf(f, "%d %lu %lu %lu %lu %lu %lu", &ok, &anchor, &from, &kfo, &syo, &nkf, &nin) != 7) ok = 0;
                     fclose(f); }
            remove(ap.c_str());
        }
        if (!ok) { missed++; continue; }
        // Two boundaries closer than a quarter of an ideal chunk are one boundary: keep the first.
        if ((uint64_t)anchor <= bound.back() + ideal / 4) { too_close++; continue; }
        lag_sum += (long long)anchor - (long long)tgt[k]; lag_n++;
        bound.push_back((uint64_t)anchor);
    }
    bound.push_back(total);
    jobs = (int)bound.size() - 1;
    if (jobs < 2)
        return run_serial("no sound chunk boundary was found: the capture re-anchors (keyframe + "
                          "sync marker) too rarely to place even one chunk boundary");
    if (o.exact_warm)
        fprintf(stderr, "ptrecon --jobs: --exact-warm: every chunk replays the trace from byte 0 before its own\n"
                        "ptrecon:   range, which is bit-identical to serial by construction and costs most of the\n"
                        "ptrecon:   parallel speed-up\n");
    fprintf(stderr, "ptrecon --jobs: %zu PSB packets in %.1f MB; %d of %d chunk boundaries %s"
                    " (mean %+.1f KB from the ideal split; %d without an anchor, %d merged) -> %d chunks\n",
            psb.size(), total / 1048576.0, lag_n, (int)tgt.size(),
            need_anchor ? "anchored" : "at a PSB (--decode-only: no replay state to anchor)",
            lag_n ? (double)lag_sum / lag_n / 1024.0 : 0.0, missed, too_close, jobs);

    // Where each chunk warms up from (the same rule the round-2 children use below).  The warm
    // start must be a PSB -- it is where the decoder is entered -- and must lie at or before the
    // boundary, so that the keyframe and the sync marker the boundary was chosen for are inside
    // the warm window.  A warm start that would reach the first PSB is pulled back to byte 0
    // instead, so that such a chunk sees the same pre-init image the serial run does.
    auto warm_start = [&](int k) -> uint64_t {
        if (k <= 0) return 0;
        if (o.exact_warm) return 0;       // --exact-warm: replay the whole prefix
        uint64_t want = bound[k] > o.warm_bytes ? bound[k] - o.warm_bytes : 0;
        if (want < bound[k - 1]) want = bound[k - 1];
        if (want <= psb.front()) return 0;
        size_t lo = 0, hi = psb.size();
        while (lo < hi) { size_t mid = (lo + hi) / 2; if (psb[mid] < want) lo = mid + 1; else hi = mid; }
        if (lo < psb.size() && psb[lo] < bound[k]) return psb[lo];
        return psb_le(bound[k] - 1);
    };

    // ---- round 1: the log-on-change scan -------------------------------------------------
    // Only for a build that actually has log-on-change values, and only when asked: it costs one
    // extra PT decode of the file (spread over the same N processes).  Chunk k scans its OWN range
    // and reports the last value logged at every site, plus a snapshot at chunk k+1's warm start,
    // which is the point chunk k+1 has to start its table from.
    bool scan = o.delta_scan && shared->has_delta();
    if (o.delta_scan && !scan)
        fprintf(stderr, "ptrecon --delta-scan: no log-on-change values in these site maps -- skipped\n");
    if (scan) {
        std::vector<pid_t> sk;
        for (int k = 0; k < jobs; k++) {
            pid_t p = fork();
            if (p < 0) { perror("fork"); return 2; }
            if (p == 0) {
                ReconOptions co = o; co.jobs = 1; co.out.clear(); co.summary.clear();
                co.skip_bytes = bound[k]; co.end_bytes = bound[k + 1]; co.emit_from = 0;
                co.scan_snap_at = (k + 1 < jobs) ? warm_start(k + 1) : 0;
                co.delta_scan_out = chunk_path(base, k, ".dscan");
                int rc = 1;
                try { Recon r(co); rc = r.run_delta_scan(); } catch (std::exception& e) { fprintf(stderr, "ptrecon dscan %d: %s\n", k, e.what()); rc = 1; }
                if (rc) fprintf(stderr, "ptrecon dscan %d: rc=%d\n", k, rc);
                _exit(rc);
            }
            sk.push_back(p);
        }
        int sbad = 0;
        for (size_t k = 0; k < sk.size(); k++) { int st = 0; waitpid(sk[k], &st, 0); if (!WIFEXITED(st) || WEXITSTATUS(st)) sbad++; }
        if (sbad) { fprintf(stderr, "ptrecon: %d of %d delta-scan chunks failed\n", sbad, jobs); return 1; }
        // Fold the per-chunk reports, in chunk order, into the exact table at each warm start.
        // A part's rule per entry: logged after this part's last state loss -> that value; else, if
        // the part had a state loss at all -> unknown; else -> whatever came in.
        size_t N = 0;
        {   std::string p0 = chunk_path(base, 0, ".dscan"); FILE* f = fopen(p0.c_str(), "rb");
            if (!f) { fprintf(stderr, "ptrecon: missing %s\n", p0.c_str()); return 1; }
            uint64_t hdr[4]; if (fread(hdr, 8, 4, f) != 4) { fclose(f); return 1; } N = (size_t)hdr[1]; fclose(f); }
        std::vector<uint64_t> run_val(N, 0); std::vector<uint8_t> run_ok(N, 1);   // the table at bound[0]
        auto load = [&](int k, std::vector<uint64_t>& sv, std::vector<uint64_t>& ss, uint64_t& sl,
                              std::vector<uint64_t>& fv, std::vector<uint64_t>& fs, uint64_t& fl) -> bool {
            std::string p = chunk_path(base, k, ".dscan"); FILE* f = fopen(p.c_str(), "rb"); if (!f) return false;
            uint64_t hdr[4]; if (fread(hdr, 8, 4, f) != 4 || hdr[1] != N) { fclose(f); return false; }
            sl = hdr[2]; fl = hdr[3];
            sv.resize(N); ss.resize(N); fv.resize(N); fs.resize(N);
            bool ok = fread(sv.data(), 8, N, f) == N && fread(ss.data(), 8, N, f) == N
                   && fread(fv.data(), 8, N, f) == N && fread(fs.data(), 8, N, f) == N;
            fclose(f); return ok;
        };
        auto apply = [&](std::vector<uint64_t>& tv, std::vector<uint8_t>& tok,
                         const std::vector<uint64_t>& v, const std::vector<uint64_t>& sq, uint64_t ls) {
            for (size_t i = 0; i < N; i++) {
                if (sq[i] && sq[i] > ls) { tv[i] = v[i]; tok[i] = 1; }
                else if (ls) tok[i] = 0;
            }
        };
        for (int k = 1; k < jobs; k++) {
            std::vector<uint64_t> sv, ss, fv, fs; uint64_t sl = 0, fl = 0;
            if (!load(k - 1, sv, ss, sl, fv, fs, fl)) { fprintf(stderr, "ptrecon: bad delta scan for chunk %d\n", k - 1); return 1; }
            std::vector<uint64_t> seed_val = run_val; std::vector<uint8_t> seed_ok = run_ok;
            apply(seed_val, seed_ok, sv, ss, sl);                 // ... up to chunk k's warm start
            std::string sp = chunk_path(base, k, ".dseed");
            FILE* f = fopen(sp.c_str(), "wb"); if (!f) { perror("dseed"); return 1; }
            uint64_t n64 = N; fwrite(&n64, 8, 1, f);
            fwrite(seed_val.data(), 8, N, f); fwrite(seed_ok.data(), 1, N, f); fclose(f);
            apply(run_val, run_ok, fv, fs, fl);                   // ... to the end of chunk k-1
        }
        for (int k = 0; k < jobs; k++) remove(chunk_path(base, k, ".dscan").c_str());
        size_t known = 0; for (size_t i = 0; i < N; i++) known += run_ok[i] ? 1 : 0;
        fprintf(stderr, "ptrecon --delta-scan: %zu logged values, %zu known at the last boundary\n", N, known);
    }

    std::vector<pid_t> kids;
    for (int k = 0; k < jobs; k++) {
        pid_t p = fork();
        if (p < 0) { perror("fork"); return 2; }
        if (p == 0) {
            ReconOptions co = o;
            co.jobs = 1;
            // ---- chunk warm-up ---------------------------------------------------------------
            // Chunk k decodes [warm, bound[k+1]) but only EMITS from bound[k]: the prefix
            // [warm, bound[k]) is the tail of chunk k-1, decoded with every record and every
            // counter suppressed, purely so that the registers, the shadow memory, the
            // log-on-change table and the buffer-sink cursor are warm when chunk k's own range
            // begins.  The warm start is snapped to a PSB (nothing else can be synchronised on)
            // and never crosses the previous chunk's own start, so the extra work is bounded by
            // one chunk.  Chunk 0 has nothing to warm up from.
            uint64_t warm = warm_start(k);
            if (scan && k > 0) co.delta_seed_in = chunk_path(base, k, ".dseed");
            co.skip_bytes = warm; co.end_bytes = bound[k + 1];
            co.emit_from = warm < bound[k] ? bound[k] : 0;
            // Process invariants the chunk cannot re-derive: the image load bases already come
            // from the sideband, the TLS base does not (the analyzer anchors `fs_base' once, at
            // `main', so no mid-stream chunk ever sees it logged).  It is a process constant for a
            // single-threaded target, so seed it -- but only in a chunk that does not start at the
            // beginning of the trace, where %fs is still 0 until the loader sets it.
            co.seed_fs = warm > 0 && !o.no_seed_fs;
            co.out = o.out.empty() ? std::string() : chunk_path(base, k, ".mtrace");
            co.summary = chunk_path(base, k, ".json");
            co.text = false;
            int rc = 1;
            try {
                if (shared) { shared->retarget(co); rc = shared->run(); }
                else { Recon r(co); rc = r.run(); }
            } catch (std::exception& e) { fprintf(stderr, "ptrecon chunk %d: %s\n", k, e.what()); rc = 1; }
            _exit(rc);
        }
        kids.push_back(p);
    }
    int bad = 0;
    for (size_t k = 0; k < kids.size(); k++) { int st = 0; waitpid(kids[k], &st, 0); if (!WIFEXITED(st) || WEXITSTATUS(st)) bad++; }
    if (scan) for (int k = 1; k < jobs; k++) remove(chunk_path(base, k, ".dseed").c_str());
    if (bad) { fprintf(stderr, "ptrecon: %d of %d chunks failed\n", bad, jobs); return 1; }

    // ---- concatenate the chunk traces, in order -------------------------------------
    uint64_t out_records = 0;
    if (!o.out.empty()) {
        FILE* out = fopen(o.out.c_str(), "wb"); if (!out) { perror("out"); return 1; }
        mtrace_hdr h{}; h.magic = MTRACE_MAGIC; h.version = MTRACE_VERSION; h.tid_count = 1;
        fwrite(&h, sizeof h, 1, out);
        std::vector<char> buf(1 << 20);
        for (int k = 0; k < jobs; k++) {
            std::string cp = chunk_path(base, k, ".mtrace");
            FILE* in = fopen(cp.c_str(), "rb"); if (!in) { fprintf(stderr, "ptrecon: missing %s\n", cp.c_str()); fclose(out); return 1; }
            fseek(in, sizeof(mtrace_hdr), SEEK_SET);
            size_t n;
            while ((n = fread(buf.data(), 1, buf.size(), in)) > 0) { fwrite(buf.data(), 1, n, out); out_records += n / sizeof(mtrace_rec); }
            fclose(in); remove(cp.c_str());
        }
        fclose(out);
    }

    // ---- merge the per-chunk summaries ----------------------------------------------
    static const char* SUM[] = {"records", "unknown_addr", "instructions", "pt_overflows", "resyncs", "sync_failures", "psb_repaired",
                                "ptw_packets", "ptw_used", "ptw_unconsumed", "lift_failures",
                                "lift_fail_state_kept", "rep_unknown_count", "rmw_records",
                                "memory_omission_events", "lift_failed_memory_instructions", "rep_collapsed_unknown_count", "lifted_memory_shortfall",
                                "instr_skipped", "cv_used", "cv_missing", "sync_markers",
                                "sync_realigned", "unknown_ops", "ccall_known", "ccall_unknown",
                                "tramp_mismatch", "tramp_unknown_origin", "fb_logsite", "unanchored_records",
                                "delta_skipped", "delta_unknown", "delta_reanchored", "patch_jumps",
                                "keyframes_taken", "keyframe_reanchors_after_resync",
                                "keyframe_site_defs",
                                "warm_instructions", nullptr};
    std::vector<uint64_t> tot; for (int i = 0; SUM[i]; i++) tot.push_back(0);
    uint64_t delta_values = 0;                 // the same in every chunk, not a sum
    uint64_t kf_values = 0, kf_resync_values = 0;      // ditto
    // Per-image records / unknown addresses, merged by path (the serial run's `images').
    struct ImgAgg { uint64_t lo = ~0ull, hi = 0, records = 0, unknown = 0; std::string code; };
    std::map<std::string, ImgAgg> imgs; std::vector<std::string> img_order;
    double wall_max = 0; std::string chunkjs = "[";
    for (int k = 0; k < jobs; k++) {
        std::string sp = chunk_path(base, k, ".json");
        Json j;
        try { j = json_load(sp); } catch (std::exception&) { fprintf(stderr, "ptrecon: cannot read %s\n", sp.c_str()); continue; }
        for (int i = 0; SUM[i]; i++) tot[i] += j[SUM[i]].u64();
        if (j["delta_values"].u64() > delta_values) delta_values = j["delta_values"].u64();
        if (j["keyframe_values"].u64() > kf_values) kf_values = j["keyframe_values"].u64();
        if (j["keyframe_resync_values"].u64() > kf_resync_values) kf_resync_values = j["keyframe_resync_values"].u64();
        for (auto& im : j["images"].arr) {
            const std::string path = im["path"].str();
            auto it = imgs.find(path);
            if (it == imgs.end()) { imgs[path] = ImgAgg(); img_order.push_back(path); it = imgs.find(path); }
            ImgAgg& a = it->second;
            uint64_t lo = im["start"].u64(), hi = im["end"].u64();
            if (lo < a.lo) a.lo = lo; if (hi > a.hi) a.hi = hi;
            a.records += im["records"].u64(); a.unknown += im["unknown_addr"].u64();
            if (a.code.empty()) a.code = im["code"].str();
        }
        double w = j["wall_s"].dbl(); if (w > wall_max) wall_max = w;
        chunkjs += std::string(k ? "," : "") + "{\"chunk\":" + std::to_string(k) +
                   ",\"aux_from\":" + std::to_string(bound[k]) + ",\"aux_to\":" + std::to_string(bound[k + 1]) +
                   ",\"records\":" + std::to_string(j["records"].u64()) +
                   ",\"unknown_addr\":" + std::to_string(j["unknown_addr"].u64()) +
                   ",\"instructions\":" + std::to_string(j["instructions"].u64()) +
                   ",\"unanchored_records\":" + std::to_string(j["unanchored_records"].u64()) +
                   ",\"aux_warm_from\":" + std::to_string(j["aux_skip_bytes"].u64()) +
                   ",\"warm_instructions\":" + std::to_string(j["warm_instructions"].u64()) +
                   ",\"decode_window\":" + json_dump(j["decode_window"]) +
                   ",\"memory_omission_examples\":" + json_dump(j["memory_omission_examples"]) +
                   ",\"wall_s\":" + std::to_string(w) + "}";
        remove(sp.c_str());
    }
    chunkjs += "]";

    std::string js = "{";
    for (int i = 0; SUM[i]; i++) js += std::string(i ? "," : "") + "\"" + SUM[i] + "\":" + std::to_string(tot[i]);
    {   // per-image breakdown, biggest first
        std::vector<std::pair<std::string, ImgAgg>> v(imgs.begin(), imgs.end());
        std::sort(v.begin(), v.end(), [](const std::pair<std::string, ImgAgg>& a,
                                         const std::pair<std::string, ImgAgg>& b) {
            return a.second.records > b.second.records; });
        js += ",\"images\":["; bool first = true;
        for (auto& kv : v) {
            if (!kv.second.records) continue;
            js += std::string(first ? "" : ",") + "{\"path\":" + json_quote(kv.first) +
                  ",\"start\":" + std::to_string(kv.second.lo) + ",\"end\":" + std::to_string(kv.second.hi) +
                  ",\"code\":" + json_quote(kv.second.code) +
                  ",\"records\":" + std::to_string(kv.second.records) +
                  ",\"unknown_addr\":" + std::to_string(kv.second.unknown) + "}";
            first = false;
        }
        js += "]";
    }
    js += ",\"jit\":null";
    js += ",\"delta_values\":" + std::to_string(delta_values);
    js += ",\"keyframe_values\":" + std::to_string(kf_values) + ",\"keyframe_resync_values\":" + std::to_string(kf_resync_values);
    js += ",\"warm_bytes\":" + std::to_string(o.warm_bytes);
    js += ",\"jobs\":" + std::to_string(jobs) + ",\"wall_s\":" + std::to_string(wall_max) +
          ",\"insn_per_s\":" + std::to_string(wall_max > 0 ? tot[2] / wall_max : 0) +
          ",\"chunks\":" + chunkjs + "}\n";
    if (!o.summary.empty()) { FILE* f = fopen(o.summary.c_str(), "w"); if (f) { fputs(js.c_str(), f); fclose(f); } }
    fprintf(stderr, "%s", js.c_str());
    if (!o.out.empty()) fprintf(stderr, "ptrecon --jobs %d: %lu records concatenated -> %s\n", jobs, (unsigned long)out_records, o.out.c_str());
    // Same refusal as the serial run (Recon::run): a buffer-sink build given --cv from which no
    // chunk consumed a single value.
    uint64_t cv_used_total = 0; for (int i = 0; SUM[i]; i++) if (!strcmp(SUM[i], "cv_used")) cv_used_total = tot[i];
    if (!o.cvfile.empty() && cv_used_total == 0 && shared->has_buffer_values()) {
        fprintf(stderr, "ptrecon: REFUSING the result: --cv %s was given for a buffer-sink build and cv_used == 0 "
                        "over all %d chunks\n", o.cvfile.c_str(), jobs);
        return 3;
    }
    return 0;
}
