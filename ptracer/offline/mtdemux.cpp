// mtdemux.cpp -- ptrecon --mt: multi-threaded reconstruction of a per-CPU Intel PT capture.
//
// Design.  A `pt_capture2 --cpu A --cpu B ...' capture gives one AUX
// stream per core plus, per core, the kernel's context-switch records (PERF_RECORD_SWITCH_CPU_WIDE
// with pid/tid/time).  Because the events exclude ring 0, a core's stream is a sequence of USER
// REGIONS [TIP.PGE, TIP.PGD], each executed by exactly one thread, and a switch always happens in
// the gap between two regions.  So:
//
//   1. SEGMENT   every core's stream once, decode-only, recording a boundary at every TIP.PGE, at
//                every PT overflow and after every decoder resync, with the decoder's offset and
//                its TSC there (Recon::run_segment, one forked child per core);
//   2. ATTRIBUTE each region to the thread of the LAST switch-in on that core whose time is <= the
//                region's start time (the record time is perf's clock, converted back to TSC by
//                pt_capture2 with the mmap page's own time_mult/shift/zero); regions of other
//                processes that shared the core are dropped;
//   3. PLAN      per thread: its regions from all cores, in time order (a migrating thread simply
//                continues on another core's file); a region that begins at an overflow/resync
//                is LOSSY -- the switch-in itself may be inside the gap, so the thread's state is
//                dropped there exactly as after an overflow, and the unknown records that follow
//                until its next logged value are counted apart (`unknown_after_switch');
//   4. RECONSTRUCT each thread with an ordinary Recon on a plan-mode PtDecoder (ptdecode.h):
//                per-thread machine state, per-thread cv file (or the v3 mux file demuxed by tid),
//                per-thread ground truth, per-thread fs_base from the sideband's `threads'; one
//                forked child per thread, --mt-jobs at a time;
//   5. MERGE     the per-thread mtraces by TSC into one file whose records carry the real tid.
//
// The single-thread reconstructor is not changed: a plan-mode decoder hands it exactly the
// instructions and PTW events of its thread, in that thread's program order, and nothing else.
#include "recon.h"
#include <memory>
#include <ctime>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>
#include <glob.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct BndRec { uint64_t off, tsc, ip; uint32_t kind, pad; };     // on-disk boundary record (32 B)
struct Region {
    int cpu = 0; uint64_t begin = 0, end = 0, tsc = 0, ip = 0; char kind = 'E';
    uint32_t pid = 0, tid = 0; bool lossy = false, slack_used = false, inherited = false, conflict = false;
    bool realigned = false;   // start time fell in a switch-out..switch-in gap; moved forward
    int64_t gap = 0;      // region start TSC - switch-in TSC (attribution margin)
};

std::string with_suffix(const std::string& base, const std::string& suf) { return base + suf; }

// All switch records of a core, sorted by time.  `ins' gets the switch-ins of REAL tasks: a
// switch-in of the idle task (pid 0) never owns a user region, so it is kept only in `all' --
// where it marks, like a switch-out, that the previous task left the core.
void load_switches(const std::string& path, std::vector<SwRec>& ins, std::vector<SwRec>& all) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return;
    SwRec r;
    while (fread(&r, sizeof r, 1, f) == 1) if (r.tsc) { all.push_back(r); if ((r.flags & SW_IN) && r.pid != 0) ins.push_back(r); }
    fclose(f);
    auto by_tsc = [](const SwRec& a, const SwRec& b) { return a.tsc < b.tsc; };
    std::stable_sort(ins.begin(), ins.end(), by_tsc); std::stable_sort(all.begin(), all.end(), by_tsc);
}
std::vector<BndRec> load_bnd(const std::string& path) {
    std::vector<BndRec> v;
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return v;
    BndRec r;
    while (fread(&r, sizeof r, 1, f) == 1) v.push_back(r);
    fclose(f);
    return v;
}
uint64_t file_size(const std::string& p) { struct stat st; return stat(p.c_str(), &st) ? 0 : (uint64_t)st.st_size; }

std::vector<std::string> glob_files(const std::string& pattern) {
    std::vector<std::string> out; glob_t g; memset(&g, 0, sizeof g);
    if (glob(pattern.c_str(), 0, nullptr, &g) == 0) for (size_t i = 0; i < g.gl_pathc; i++) out.push_back(g.gl_pathv[i]);
    globfree(&g);
    return out;
}
// cv file header: "PTCV" v2 (tid in the header), "PTCW" v3 (multiplexed), else legacy.
struct CvInfo { std::string path; uint32_t magic = 0, tid = 0; };
CvInfo cv_info(const std::string& path) {
    CvInfo c; c.path = path; unsigned char h[16];
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return c;
    if (fread(h, 1, 16, f) == 16) { c.magic = h[0] | (h[1] << 8) | (h[2] << 16) | ((uint32_t)h[3] << 24); c.tid = h[8] | (h[9] << 8) | (h[10] << 16) | ((uint32_t)h[11] << 24); }
    fclose(f); return c;
}

}  // namespace

// ---- 1. the segmentation pass (runs in a forked child, one per core) -------------------------
int Recon::run_segment(const std::string& aux, const std::string& out) {
    if (PtDecoder::aux_size(aux) == 0) {      // a core the process never ran on: no regions
        FILE* f = fopen(out.c_str(), "wb"); if (!f) { perror("segment out"); return 1; } fclose(f);
        fprintf(stderr, "segment %s: empty AUX file (the process never ran on this core)\n", aux.c_str());
        return 0;
    }
    // One core's stream carries the start-up window (the core the process was exec'd on)
    // and the others do not, and which one it is is not known before decoding.  So the pass is
    // tried in the PRE-INIT phase (unpatched images, e9phase.h) and re-run WITHOUT it if the
    // decoder reports that the phase explained nothing -- it lost sync before any image's loader
    // ran, i.e. this core's stream begins after the images were already patched.  The re-run
    // costs the few thousand instructions decoded before that first sync loss.
    for (int preinit = 1; preinit >= 0; preinit--) {
    PtDecoder dec(aux, sb_, 0, 0, have_jit_ ? &jit_ : nullptr, nullptr, preinit);
    dec.no_time = false;
    FILE* f = fopen(out.c_str(), "wb"); if (!f) { perror("segment out"); return 1; }
    setvbuf(f, nullptr, _IOFBF, 1 << 20);
    uint64_t n = 0;
    PtEvents ev;
    ev.on_boundary = [&](char k, uint64_t off, uint64_t tsc, uint64_t ip) { BndRec r{off, tsc, ip, (uint32_t)(unsigned char)k, 0}; fwrite(&r, sizeof r, 1, f); n++; };
    ev.on_ptwrite = [](uint64_t, int, uint64_t) {};
    ev.on_overflow = [](uint64_t) {};
    ev.on_resync = [](int, uint64_t) {};
    ev.on_enable = [](uint64_t, bool) {};
    struct pt_insn insn; uint64_t tsc = 0;
    bool bail = false;
    while (dec.next(insn, &tsc, ev)) {
        if (preinit && dec.e9_preinit_useless()) { bail = true; break; }
    }
    fclose(f);
    if (bail || (preinit && dec.e9_preinit_useless())) {
        fprintf(stderr, "segment %s: this core's stream does not cover the start-up window "
                        "(sync lost at %lu with nothing patched) -- decoding it again without the "
                        "pre-init phase\n", aux.c_str(), (unsigned long)dec.e9_abort_off);
        continue;
    }
    const uint64_t fsz = PtDecoder::aux_size(aux), stopped = dec.offset();
    fprintf(stderr, "segment %s: %lu instructions, %lu boundaries, %lu overflows, %lu resyncs, %lu sync failures, %lu psb+ headers repaired, %lu e9 swaps, ended at %lu of %lu bytes%s%s\n",
            aux.c_str(), (unsigned long)dec.n_insn, (unsigned long)n, (unsigned long)dec.n_ovf,
            (unsigned long)dec.n_resync, (unsigned long)dec.n_sync_fail, (unsigned long)dec.n_psb_repaired, (unsigned long)dec.n_e9_swap,
            (unsigned long)stopped, (unsigned long)fsz,
            dec.eos_why[0] ? " -- " : "", dec.eos_why);
    return 0;
    }
    return 0;
}

// ---- the driver ------------------------------------------------------------------------------
int Recon::run_threads(const ReconOptions& o) {
    Sideband sb = Sideband::load(o.sideband);
    if (!sb.per_cpu()) {
        fprintf(stderr, "ptrecon --mt: %s is not a per-CPU capture (sideband version %d, no `cpus' list) -- "
                        "capture with pt_capture2 --cpu A [--cpu B ...]\n", o.sideband.c_str(), sb.sb_version);
        return 2;
    }
    const uint32_t pid = (uint32_t)sb.pid;
    const std::string base = o.out.empty() ? (o.summary.empty() ? std::string("ptrecon.mt") : o.summary) : o.out;
    const int ncpu = (int)sb.cpus.size();
    std::vector<std::string> aux_files; for (auto& c : sb.cpus) aux_files.push_back(c.aux);
    for (auto& c : sb.cpus) if (c.data_lost) fprintf(stderr, "warning: cpu %d lost %lu perf data records -- some switch records are missing\n", c.cpu, (unsigned long)c.data_lost);

    // ---- 1. segment every core's stream (parallel children) ---------------------------------
    std::vector<std::string> bnd_files(ncpu);
    { std::vector<pid_t> kids;
      for (int i = 0; i < ncpu; i++) {
          bnd_files[i] = with_suffix(base, ".cpu" + std::to_string(sb.cpus[i].cpu) + ".bnd");
          pid_t p = fork(); if (p < 0) { perror("fork"); return 2; }
          if (p == 0) {
              ReconOptions co = o; co.mt = false; co.plan = nullptr; co.aux = sb.cpus[i].aux; co.out.clear(); co.summary.clear(); co.site_stats.clear(); co.gt_in.clear(); co.gt_compare = false;
              int rc = 1;
              try { Recon r(co); rc = r.run_segment(sb.cpus[i].aux, bnd_files[i]); } catch (std::exception& e) { fprintf(stderr, "ptrecon --mt segment cpu %d: %s\n", sb.cpus[i].cpu, e.what()); }
              _exit(rc);
          }
          kids.push_back(p);
      }
      int bad = 0; for (pid_t p : kids) { int st = 0; waitpid(p, &st, 0); if (!WIFEXITED(st) || WEXITSTATUS(st)) bad++; }
      if (bad) { fprintf(stderr, "ptrecon --mt: %d of %d segmentation passes failed\n", bad, ncpu); return 1; } }

    // ---- 2. attribute the regions -------------------------------------------------------------
    std::vector<Region> regions;
    struct CpuStat { uint64_t regions = 0, target = 0, other = 0, unattributed = 0, inherited = 0, slack = 0, lossy = 0, conflict = 0, realigned = 0, bytes_target = 0, bytes_other = 0, bytes_unattr = 0, sw = 0; int64_t min_gap = INT64_MAX; };
    std::vector<CpuStat> cs(ncpu);
    std::set<uint32_t> other_pids;
    for (int i = 0; i < ncpu; i++) {
        std::vector<SwRec> sw, swall; load_switches(sb.cpus[i].switch_file, sw, swall);
        std::vector<BndRec> bn = load_bnd(bnd_files[i]);
        const uint64_t fsz = file_size(sb.cpus[i].aux);
        cs[i].sw = sw.size();
        if (sw.empty()) fprintf(stderr, "warning: cpu %d has no switch-in records (%s) -- its regions cannot be attributed\n", sb.cpus[i].cpu, sb.cpus[i].switch_file.c_str());
        std::vector<Region> rs;
        for (size_t k = 0; k < bn.size(); k++) {
            Region r; r.cpu = i; r.begin = bn[k].off; r.end = k + 1 < bn.size() ? bn[k + 1].off : fsz; r.tsc = bn[k].tsc; r.ip = bn[k].ip; r.kind = (char)bn[k].kind;
            r.lossy = (r.kind == 'O' || r.kind == 'R');
            if (r.end < r.begin) r.end = r.begin;
            if (r.tsc == 0) {                                 // no time yet: inherit the previous region's thread
                if (!rs.empty()) { r.pid = rs.back().pid; r.tid = rs.back().tid; r.tsc = rs.back().tsc; r.inherited = true; }
            } else if (!sw.empty()) {
                // last switch-in with tsc <= T + slack
                const uint64_t T = r.tsc + o.sw_slack_tsc;
                size_t lo = 0, hi = sw.size();
                while (lo < hi) { size_t mid = (lo + hi) / 2; if (sw[mid].tsc <= T) lo = mid + 1; else hi = mid; }
                // Consistency: by the records, was that task still on the core at T?  The last
                // record at or before T must be its switch-in, not a later switch-out / idle
                // switch-in.
                bool left = false;
                if (lo) { size_t alo = 0, ahi = swall.size();
                          while (alo < ahi) { size_t mid = (alo + ahi) / 2; if (swall[mid].tsc <= T) alo = mid + 1; else ahi = mid; }
                          if (alo) { const SwRec& last = swall[alo - 1]; const SwRec& s = sw[lo - 1];
                                     left = !((last.flags & SW_IN) && last.pid == s.pid && last.tid == s.tid) && last.tsc > s.tsc; } }
                // `left' means the region's start time falls in the GAP between a
                // switch-OUT and the next switch-in: by the records no task of the process was on
                // the core then, yet the region is user code of the process, so the time and the
                // records disagree.  Which of the two is in doubt?  Only the time: libipt's TSC at
                // a TIP.PGE is a LOWER BOUND (it is the last TSC/MTC/CYC the decoder has seen, and
                // the wall clock has moved on since), so the region's true start is at or after
                // `r.tsc' -- never before it.  The thread that switched OUT before `r.tsc' is
                // therefore EXCLUDED, and the only candidate left is the NEXT switch-in.  Taking
                // the last switch-in instead handed a departed thread a stretch of the thread that
                // replaced it: on memcached mc3 exactly ONE region did this (cpu2 [2007167,2007405),
                // tsc ...894608, 1267 ticks after worker 2086503's switch-out and 44 ticks before
                // 2086498's switch-in), and it cost 2086503 54 345 unknown + 16 007 wrong + 61 323
                // gt-only -- the foreign stretch also consumed 90 cv values the thread never logged,
                // so its positional value cursor stayed 90 ahead until the next sync marker.
                // The correction is bounded by construction (the next switch-in is 19-134 tsc ahead
                // on that capture, i.e. clock noise) and it can only ever move a region FORWARD.
                if (left && lo < sw.size()) {
                    const SwRec& s = sw[lo];
                    r.pid = s.pid; r.tid = s.tid; r.gap = (int64_t)r.tsc - (int64_t)s.tsc; r.realigned = true; r.slack_used = true;
                } else if (lo) {
                    const SwRec& s = sw[lo - 1]; r.pid = s.pid; r.tid = s.tid; r.gap = (int64_t)r.tsc - (int64_t)s.tsc; r.slack_used = s.tsc > r.tsc;
                    if (r.gap < cs[i].min_gap) cs[i].min_gap = r.gap;
                    if (left) r.conflict = true;   // no switch-in after it: the best guess stands
                }
            }
            // A "conflict" is a region whose start time disagrees with the switch records.  When
            // the region continues the SAME thread as the region immediately before it (and is
            // contiguous with it in the file) the attribution is not in doubt -- so it is
            // not counted as an attribution conflict.
            if (r.conflict && !rs.empty() && rs.back().pid == r.pid && rs.back().tid == r.tid && rs.back().end == r.begin)
                r.conflict = false;
            rs.push_back(r);
        }
        // Merging adjacent regions of one thread HERE, per CPU, would be wrong on a per-CPU
        // capture: two regions of thread T that are adjacent in cpu K's AUX
        // FILE are adjacent in K's trace, not in T's EXECUTION -- between them T may have run for
        // a long time on another core, whose regions live in another file.  The merged region
        // keeps the FIRST one's timestamp, so the plan (sorted by tsc) would replay a later
        // stretch of the thread at an earlier position: on memcached -t 4 worker 2070279 executed
        // markers with payloads 159757, 163853, 167950, 172046 ... but the plan decoded them
        // 159757, 172046, 176143, 163853 -- a cpu2 block that had been merged across a 0.7 ms gap
        // during which the thread ran on cpu3.  Out-of-order replay makes the positional cv cursor
        // run ahead (+6304) and then behind (-9792) and the registers it anchors stale: 39 966 of
        // that thread's 1 554 356 compared records were wrong, with 77 157 gt-only and 3 756
        // recon-only.  The merge therefore happens in step 3, on the thread's OWN time-sorted region
        // list, where "adjacent" means adjacent IN THE THREAD'S EXECUTION.
        std::vector<Region>& merged = rs;
        if (false) for (auto& r : rs) {
            // NEVER merge a LOSSY boundary ('O' overflow / 'R' decoder resync) into the
            // region before it, even when the thread is the same.  Merging hid the loss: the
            // plan then held one non-lossy segment, `next_plan' fired no switch-loss/overflow
            // hook at the gap, and the reconstruction carried its registers and its POSITIONAL
            // cv cursor straight across instructions it never saw -- so the values logged inside
            // the gap were never consumed (the cursor ran behind until the next sync marker
            // pulled it straight) and every address derived from a register that the gap had
            // changed was reported as a confident WRONG address instead of an unknown one.
            // Measured on memcached -t 4 (cpu2, 9 decoder resyncs): worker 4060747 lost the
            // seven-instruction prologue of `event_base_loop' -- its ONE anchor of the
            // `event_base *' -- to a resync inside its own region, and reported 51 813 wrong
            // addresses (3.5 % of the thread) from that stale pointer.
            (void)r;
        }
        for (auto& r : merged) {
            cs[i].regions++;
            const uint64_t bytes = r.end - r.begin;
            if (r.tid == 0) { cs[i].unattributed++; cs[i].bytes_unattr += bytes; }
            else if (r.pid != pid) { cs[i].other++; cs[i].bytes_other += bytes; other_pids.insert(r.pid); }
            else { cs[i].target++; cs[i].bytes_target += bytes; if (r.lossy) cs[i].lossy++; if (r.slack_used) cs[i].slack++; if (r.inherited) cs[i].inherited++; if (r.conflict) cs[i].conflict++; if (r.realigned) cs[i].realigned++; }
        }
        regions.insert(regions.end(), merged.begin(), merged.end());
        if (!o.keep_thread_files) remove(bnd_files[i].c_str());
    }
    if (!o.sw_dump.empty()) {
        FILE* f = fopen(o.sw_dump.c_str(), "w");
        if (f) { fprintf(f, "# cpu begin end kind tsc pid tid lossy slack_used inherited conflict realigned gap_tsc ip\n");
                 for (auto& r : regions) fprintf(f, "%d %lu %lu %c %lu %u %u %d %d %d %d %d %ld %#lx\n", sb.cpus[r.cpu].cpu, (unsigned long)r.begin, (unsigned long)r.end, r.kind, (unsigned long)r.tsc, r.pid, r.tid, (int)r.lossy, (int)r.slack_used, (int)r.inherited, (int)r.conflict, (int)r.realigned, (long)r.gap, (unsigned long)r.ip);
                 fclose(f); }
    }
    for (int i = 0; i < ncpu; i++)
        fprintf(stderr, "cpu %d: %lu switch-ins, %lu regions: %lu of pid %u (%.1f MB, %lu lossy, %lu needed slack, %lu inherited, %lu conflicts, %lu gap-realigned), %lu of other pids (%.1f MB), %lu unattributed (%.1f MB); min start-after-switch gap %ld tsc\n",
                sb.cpus[i].cpu, (unsigned long)cs[i].sw, (unsigned long)cs[i].regions, (unsigned long)cs[i].target, pid, cs[i].bytes_target / 1e6,
                (unsigned long)cs[i].lossy, (unsigned long)cs[i].slack, (unsigned long)cs[i].inherited, (unsigned long)cs[i].conflict, (unsigned long)cs[i].realigned,
                (unsigned long)cs[i].other, cs[i].bytes_other / 1e6, (unsigned long)cs[i].unattributed, cs[i].bytes_unattr / 1e6,
                cs[i].min_gap == INT64_MAX ? 0L : (long)cs[i].min_gap);

    // ---- 3. per-thread plans -----------------------------------------------------------------
    std::map<uint32_t, ThreadPlan> plans;
    for (auto& r : regions) {
        if (r.pid != pid || r.tid == 0 || r.end <= r.begin) continue;
        ThreadPlan& p = plans[r.tid]; p.tid = r.tid; p.files = aux_files;
        AuxSeg s; s.file = r.cpu; s.begin = r.begin; s.end = r.end; s.tsc = r.tsc; s.lossy = r.lossy;
        p.segs.push_back(s);
    }
    for (auto& kv : plans) {
        std::vector<AuxSeg>& v = kv.second.segs;
        std::stable_sort(v.begin(), v.end(), [](const AuxSeg& a, const AuxSeg& b) { return a.tsc < b.tsc; });
        // Merge only what is adjacent in THIS THREAD's execution -- consecutive in its own
        // time-sorted plan, in the same file, contiguous in that file -- and never across a lossy
        // ('O'/'R') boundary.  That is the syscall PGD/PGE pair and the preemption that
        // came straight back, and it cannot reorder the thread's own execution.
        std::vector<AuxSeg> m;
        for (auto& s2 : v) {
            if (!m.empty() && !s2.lossy && m.back().file == s2.file && m.back().end == s2.begin) { m.back().end = s2.end; continue; }
            m.push_back(s2);
        }
        v.swap(m);
    }
    std::map<uint32_t, uint64_t> fs_of; for (auto& t : sb.threads) fs_of[t.tid] = t.fs_base;
    if (o.tid) { auto it = plans.find(o.tid); if (it == plans.end()) { fprintf(stderr, "ptrecon --mt: no regions for --tid %u\n", o.tid); return 1; }
                 ThreadPlan one = it->second; plans.clear(); plans[o.tid] = one; }
    fprintf(stderr, "ptrecon --mt: pid %u, %zu threads with regions (%zu in the sideband's thread list)%s\n", pid, plans.size(), sb.threads.size(),
            other_pids.empty() ? "" : (", " + std::to_string(other_pids.size()) + " other pids shared the cores").c_str());
    for (auto& kv : plans) {
        uint64_t bytes = 0, lossy = 0; std::set<int> cpus; for (auto& s : kv.second.segs) { bytes += s.end - s.begin; lossy += s.lossy; cpus.insert(s.file); }
        fprintf(stderr, "  tid %u: %zu segments (%lu lossy) on %zu core(s), %.1f MB, fs_base %#lx%s\n", kv.first, kv.second.segs.size(), (unsigned long)lossy, cpus.size(), bytes / 1e6,
                (unsigned long)(fs_of.count(kv.first) ? fs_of[kv.first] : 0), fs_of.count(kv.first) ? "" : "  [not in the sideband's thread list]");
    }
    if (plans.empty()) { fprintf(stderr, "ptrecon --mt: nothing to reconstruct\n"); return 1; }

    // ---- per-thread inputs: cv files, ground truth ---------------------------------------------
    std::vector<CvInfo> cvs;
    { std::vector<std::string> paths = o.cvfiles;
      if (!o.cv_dir.empty()) for (auto& p : glob_files(o.cv_dir + "/cv.*.bin")) paths.push_back(p);
      std::sort(paths.begin(), paths.end()); paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
      for (auto& p : paths) cvs.push_back(cv_info(p)); }
    auto cv_for = [&](uint32_t tid) -> std::string {
        for (auto& c : cvs) if (c.magic == 0x56435450u /* PTCV */ && c.tid == tid) return c.path;
        for (auto& c : cvs) if (c.magic == 0x57435450u /* PTCW */) return c.path;
        return "";
    };
    std::string gt_dir = o.gt_dir;
    if (gt_dir.empty() && !o.gt_in.empty()) { struct stat st; if (!stat(o.gt_in.c_str(), &st) && S_ISDIR(st.st_mode)) gt_dir = o.gt_in; }
    auto gt_for = [&](uint32_t tid) -> std::string {
        if (gt_dir.empty()) return "";
        std::string p = gt_dir + "/gt." + std::to_string(pid) + "." + std::to_string(tid) + ".bin";
        struct stat st; return stat(p.c_str(), &st) ? std::string() : p;
    };

    // ---- 4. reconstruct every thread (forked children, mt_jobs at a time) --------------------
    std::vector<uint32_t> tids; for (auto& kv : plans) tids.push_back(kv.first);
    if (!o.mt_tids.empty()) {                       // --mt-tid: reconstruct only the named threads
        std::vector<uint32_t> keep;
        for (uint32_t t : o.mt_tids) if (plans.count(t)) keep.push_back(t); else fprintf(stderr, "ptrecon --mt: --mt-tid %u has no regions in this capture\n", t);
        tids.swap(keep);
        fprintf(stderr, "ptrecon --mt: --mt-tid selected %zu of %zu thread(s)\n", tids.size(), plans.size());
        if (tids.empty()) return 1;
    }
    int jobs = o.mt_jobs > 0 ? o.mt_jobs : (o.jobs > 1 ? o.jobs : std::min<int>((int)tids.size(), 8));
    // --mt-split N -- every thread runs at once and a thread's stream is split into chunks
    // (Recon::run_parallel on its plan), N workers shared in proportion to the thread's bytes.
    std::map<uint32_t, int> split_of;
    if (o.mt_split > 1) {
        uint64_t sum = 0; std::map<uint32_t, uint64_t> by;
        for (uint32_t t : tids) { uint64_t b = Recon::plan_total(plans[t]); by[t] = b; sum += b; plans[t].index(); }
        for (uint32_t t : tids) { int j = sum ? (int)((double)o.mt_split * (double)by[t] / (double)sum + 0.5) : 1; split_of[t] = j < 1 ? 1 : j; }
        jobs = (int)tids.size();
        fprintf(stderr, "ptrecon --mt-split %d:", o.mt_split);
        for (uint32_t t : tids) if (split_of[t] > 1) fprintf(stderr, " tid %u x%d", t, split_of[t]);
        fprintf(stderr, "\n");
    }
    if (jobs < 1) jobs = 1;
    auto tpath = [&](uint32_t tid, const char* suf) { return base + ".tid" + std::to_string(tid) + suf; };
    std::map<pid_t, uint32_t> running; size_t next = 0; int bad = 0;
    std::vector<uint32_t> failed_tids;
    // ---- ONE Recon for the whole process ---------------------------------------------------
    // Everything Recon's constructor builds -- both site-map JSONs, the rebased site tables, the
    // interval map over every mapping, the trampoline map, the original-code index -- is a
    // property of the PROCESS, not of the thread, so a thread child need not rebuild it.
    // Build it ONCE here, with no thread named (no plan, no cv file, no gt ring), and let each
    // child `retarget_thread()' it; fork shares the tables physically, copy-on-write.
    // PTRECON_MT_NOSHARE=1 is the ablation: construct per child.
    const bool noshare = getenv("PTRECON_MT_NOSHARE") != nullptr;
    std::unique_ptr<Recon> shared;
    double t_shared = 0;
    if (!noshare) {
        ReconOptions po = o; po.mt = false; po.jobs = 1; po.tid = 0; po.plan = nullptr;
        po.aux.clear(); po.skip_bytes = po.end_bytes = po.emit_from = 0;
        po.cvfile.clear(); po.cvfiles.clear();
        po.gt_in.clear(); po.gt_compare = false; po.gt_out.clear();
        po.out.clear(); po.summary.clear(); po.site_stats.clear(); po.cv_audit.clear();
        po.resync_log.clear(); po.text = false;
        struct timespec a{}, b{}; clock_gettime(CLOCK_MONOTONIC, &a);
        try { shared.reset(new Recon(po)); }
        catch (std::exception& e) { fprintf(stderr, "ptrecon --mt: shared setup failed (%s); "
                                            "falling back to per-thread construction\n", e.what()); shared.reset(); }
        clock_gettime(CLOCK_MONOTONIC, &b);
        t_shared = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
        if (shared) fprintf(stderr, "ptrecon --mt: shared per-process setup built once in %.2f s "
                                    "(was rebuilt in every thread child)\n", t_shared);
    }
    while (next < tids.size() || !running.empty()) {
        while (next < tids.size() && (int)running.size() < jobs) {
            uint32_t tid = tids[next++];
            pid_t p = fork(); if (p < 0) { perror("fork"); return 2; }
            if (p == 0) {
                ReconOptions co = o; co.mt = false; co.jobs = 1; co.tid = tid; co.plan = &plans[tid]; co.aux.clear();
                co.skip_bytes = co.end_bytes = co.emit_from = 0;
                // TLS base: the kernel's value for this thread (sideband `threads'); a worker thread
                // has it from its first instruction (CLONE_SETTLS), so it is seeded from the start;
                // the main thread keeps the single-thread rule (0 until the loader's arch_prctl).
                auto fit = fs_of.find(tid);
                if (fit != fs_of.end() && fit->second) co.fs_base = fit->second;
                co.seed_fs = (tid != pid) && !o.no_seed_fs;
                co.cvfile = cv_for(tid); co.cvfiles.clear();
                co.gt_in = gt_for(tid); co.gt_compare = !co.gt_in.empty();
                co.gt_out = (co.gt_compare && !o.gt_out.empty()) ? tpath(tid, ".gt.mtrace") : std::string();
                co.out = o.out.empty() ? std::string() : tpath(tid, ".mtrace");
                co.summary = tpath(tid, ".json");
                co.site_stats = o.site_stats.empty() ? std::string() : tpath(tid, ".sites.csv");
                co.cv_audit = o.cv_audit.empty() ? std::string() : tpath(tid, ".cvaudit");
                co.resync_log = o.resync_log.empty() ? std::string() : tpath(tid, ".resync");
                co.text = false;
                int rc = 1;
                struct timespec a{}, b{}, c{}; clock_gettime(CLOCK_MONOTONIC, &a);
                try {
                    if (split_of.count(tid) && split_of[tid] > 1 && co.gt_in.empty()) {
                        co.jobs = split_of[tid]; clock_gettime(CLOCK_MONOTONIC, &b); rc = Recon::run_parallel(co); }
                    else if (shared) { shared->retarget_thread(co); clock_gettime(CLOCK_MONOTONIC, &b); rc = shared->run(); }
                    else { Recon r(co); clock_gettime(CLOCK_MONOTONIC, &b); rc = r.run(); }
                } catch (std::exception& e) { fprintf(stderr, "ptrecon --mt tid %u: %s\n", tid, e.what()); rc = 1; }
                clock_gettime(CLOCK_MONOTONIC, &c);
                fprintf(stderr, "ptrecon --mt tid %u: setup %.2f s, run %.2f s\n", tid,
                        (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9,
                        (c.tv_sec - b.tv_sec) + (c.tv_nsec - b.tv_nsec) / 1e9);
                _exit(rc);
            }
            running[p] = tid;
        }
        int st = 0; pid_t p = waitpid(-1, &st, 0);
        if (p < 0) break;
        auto it = running.find(p); if (it == running.end()) continue;
        if (!WIFEXITED(st) || WEXITSTATUS(st)) { bad++; failed_tids.push_back(it->second); fprintf(stderr, "ptrecon --mt: thread %u failed (status %d)\n", it->second, st); }
        running.erase(it);
    }
    if (bad) {
        // A failed thread must NOT throw away the other N-1: the run is a COMPLETENESS
        // measurement, and a missing aggregate is worse than an aggregate that says how
        // many threads it is missing.  The failures are dropped from the merge, recorded
        // in the summary as `threads_failed'/`threads_failed_tids', printed here, and the
        // exit status is still non-zero -- but only after the summary has been written.
        fprintf(stderr, "ptrecon --mt: %d of %zu threads failed; they are EXCLUDED from the "
                "totals below and listed as threads_failed_tids in the summary\n", bad, tids.size());
        std::vector<uint32_t> keep;
        for (uint32_t t : tids)
            if (std::find(failed_tids.begin(), failed_tids.end(), t) == failed_tids.end()) keep.push_back(t);
        tids.swap(keep);
    }

    // ---- 5. merge the per-thread traces by time --------------------------------------------
    uint64_t out_records = 0, no_ts = 0;
    if (!o.out.empty()) {
        struct Rd { FILE* f; mtrace_rec cur; bool has; uint32_t tid; std::vector<mtrace_rec> buf; size_t pos; };
        std::vector<Rd> rds;
        for (uint32_t tid : tids) {
            std::string cp = tpath(tid, ".mtrace"); FILE* f = fopen(cp.c_str(), "rb");
            if (!f) { fprintf(stderr, "ptrecon --mt: missing %s\n", cp.c_str()); continue; }
            fseek(f, sizeof(mtrace_hdr), SEEK_SET);
            Rd r; r.f = f; r.has = false; r.tid = tid; r.pos = 0; r.buf.resize(1 << 14); r.buf.clear();
            rds.push_back(std::move(r));
        }
        auto refill = [&](Rd& r) -> bool {
            if (r.pos < r.buf.size()) { r.cur = r.buf[r.pos++]; r.has = true; return true; }
            r.buf.resize(1 << 14); size_t n = fread(r.buf.data(), sizeof(mtrace_rec), r.buf.size(), r.f); r.buf.resize(n); r.pos = 0;
            if (!n) { r.has = false; return false; }
            r.cur = r.buf[r.pos++]; r.has = true; return true;
        };
        for (auto& r : rds) refill(r);
        FILE* out = fopen(o.out.c_str(), "wb"); if (!out) { perror("out"); return 1; }
        setvbuf(out, nullptr, _IOFBF, 1 << 20);
        mtrace_hdr h{}; h.magic = MTRACE_MAGIC; h.version = MTRACE_VERSION; h.tid_count = (uint32_t)tids.size(); h.flags = MTH_TIME_MERGED | (o.output_stride > 1 ? MTH_SAMPLED_OUTPUT : 0);
        fwrite(&h, sizeof h, 1, out);
        // k-way merge on (ts, tid); a record with ts == 0 (no timing) is emitted in file order
        // relative to its own thread and ahead of any timed record, so per-thread order is kept.
        using Key = std::pair<uint64_t, size_t>;
        std::priority_queue<Key, std::vector<Key>, std::greater<Key>> pq;
        for (size_t i = 0; i < rds.size(); i++) if (rds[i].has) pq.push({rds[i].cur.ts, i});
        std::set<uint32_t> tids_seen;
        while (!pq.empty()) {
            Key k = pq.top(); pq.pop(); Rd& r = rds[k.second];
            if (r.cur.ts == 0) no_ts++;
            tids_seen.insert(r.cur.tid);
            fwrite(&r.cur, sizeof r.cur, 1, out); out_records++;
            if (refill(r)) pq.push({r.cur.ts, k.second});
        }
        // tid_count = the distinct tids IN THE FILE (a thread that ran no instrumented code --
        // the buffer sink's own drain thread -- has a plan but no record).
        h.tid_count = (uint32_t)tids_seen.size();
        fseek(out, 0, SEEK_SET); fwrite(&h, sizeof h, 1, out);
        fclose(out);
        for (auto& r : rds) { fclose(r.f); if (!o.keep_thread_files) remove(tpath(r.tid, ".mtrace").c_str()); }
    }

    // ---- summary: totals + per thread + per cpu ------------------------------------------------
    static const char* SUM[] = {"records", "unknown_addr", "instructions", "pt_overflows", "resyncs", "sync_failures", "psb_repaired", "output_records",
                                "ptw_packets", "ptw_used", "ptw_unconsumed", "lift_failures", "lift_fail_state_kept",
                                "rep_unknown_count", "rmw_records", "instr_skipped", "cv_values", "cv_used", "cv_missing",
                                "memory_omission_events", "lift_failed_memory_instructions", "rep_collapsed_unknown_count", "lifted_memory_shortfall",
                                "sync_markers", "sync_realigned", "unanchored_records", "delta_skipped", "delta_unknown",
                                "keyframes_taken", "keyframe_reanchors_after_resync", "segments", "segments_lossy",
                                "segments_unsyncable", "skipped_insn", "reseeks", "switch_losses", "unknown_after_switch", "records_after_switch",
                                "tramp_mismatch", "tramp_unknown_origin", "fb_logsite", "patch_jumps",
                                "e9_swaps", "e9_swaps_triggered", "e9_preinit_insn", nullptr};
    static const char* GT[] = {"records_compared", "identical", "unknown", "wrong", "excluded", "gt_only", "recon_only", "gt_tail", "gt_records", "gt_physical_records", "gt_rep_ranges",
                               "records_after_gt_end", "wrong_in_overflow", "wrong_same_page", "wrong_unanchored", "unknown_in_overflow", "unknown_unanchored",
                               "wrong_after_switch", "resync_after_streak", "gt_skipped_after_streak", "resync_after_loss", "gt_skipped_after_loss", "gt_rewound_after_loss", "resync_after_loss_failed", nullptr};
    std::vector<uint64_t> tot; for (int i = 0; SUM[i]; i++) tot.push_back(0);
    std::vector<uint64_t> gtot; for (int i = 0; GT[i]; i++) gtot.push_back(0);
    bool any_gt = false; double wall_max = 0; std::string thrjs = "[";
    for (uint32_t tid : tids) {
        std::string sp = tpath(tid, ".json"); Json j;
        try { j = json_load(sp); } catch (std::exception&) { fprintf(stderr, "ptrecon --mt: cannot read %s\n", sp.c_str()); continue; }
        for (int i = 0; SUM[i]; i++) tot[i] += j[SUM[i]].u64();
        if (j["gt"].t == Json::OBJ) { any_gt = true; for (int i = 0; GT[i]; i++) gtot[i] += j["gt"][GT[i]].u64(); }
        double w = j["wall_s"].dbl(); if (w > wall_max) wall_max = w;
        uint64_t nseg = 0, nlossy = 0, bytes = 0; for (auto& s : plans[tid].segs) { nseg++; nlossy += s.lossy; bytes += s.end - s.begin; }
        thrjs += std::string(thrjs.size() > 1 ? "," : "") + "{\"tid\":" + std::to_string(tid) + ",\"main\":" + (tid == pid ? "true" : "false") +
                 ",\"fs_base\":" + std::to_string(fs_of.count(tid) ? fs_of[tid] : 0) +
                 ",\"plan_segments\":" + std::to_string(nseg) + ",\"plan_segments_lossy\":" + std::to_string(nlossy) + ",\"aux_bytes\":" + std::to_string(bytes) +
                 ",\"cv\":" + json_quote(cv_for(tid)) + ",\"gt_in\":" + json_quote(gt_for(tid));
        for (int i = 0; SUM[i]; i++) thrjs += std::string(",\"") + SUM[i] + "\":" + std::to_string(j[SUM[i]].u64());
        thrjs += ",\"decode_window\":" + json_dump(j["decode_window"]) +
                 ",\"memory_omission_examples\":" + json_dump(j["memory_omission_examples"]) +
                 ",\"gt\":" + json_dump(j["gt"]) + ",\"images\":" + json_dump(j["images"]) + ",\"wall_s\":" + std::to_string(w) + "}";
        if (!o.keep_thread_files) remove(sp.c_str());
    }
    thrjs += "]";
    std::string failjs = "[";
    for (size_t i = 0; i < failed_tids.size(); i++) failjs += std::string(i ? "," : "") + std::to_string(failed_tids[i]);
    failjs += "]";
    std::string cpujs = "[";
    for (int i = 0; i < ncpu; i++)
        cpujs += std::string(i ? "," : "") + "{\"cpu\":" + std::to_string(sb.cpus[i].cpu) + ",\"aux\":" + json_quote(sb.cpus[i].aux) + ",\"aux_bytes\":" + std::to_string(sb.cpus[i].aux_bytes) +
                 ",\"switch_ins\":" + std::to_string(cs[i].sw) + ",\"regions\":" + std::to_string(cs[i].regions) + ",\"regions_target\":" + std::to_string(cs[i].target) +
                 ",\"regions_other_pid\":" + std::to_string(cs[i].other) + ",\"regions_unattributed\":" + std::to_string(cs[i].unattributed) +
                 ",\"regions_lossy\":" + std::to_string(cs[i].lossy) + ",\"regions_slack_used\":" + std::to_string(cs[i].slack) + ",\"regions_inherited\":" + std::to_string(cs[i].inherited) + ",\"regions_conflict\":" + std::to_string(cs[i].conflict) + ",\"regions_gap_realigned\":" + std::to_string(cs[i].realigned) +
                 ",\"bytes_target\":" + std::to_string(cs[i].bytes_target) + ",\"bytes_other_pid\":" + std::to_string(cs[i].bytes_other) + ",\"bytes_unattributed\":" + std::to_string(cs[i].bytes_unattr) +
                 ",\"min_gap_tsc\":" + std::to_string(cs[i].min_gap == INT64_MAX ? 0 : cs[i].min_gap) + ",\"aux_lost_bytes\":" + std::to_string(sb.cpus[i].lost) + ",\"data_lost\":" + std::to_string(sb.cpus[i].data_lost) + "}";
    cpujs += "]";
    std::string js = "{";
    for (int i = 0; SUM[i]; i++) js += std::string(i ? "," : "") + "\"" + SUM[i] + "\":" + std::to_string(tot[i]);
    js += ",\"output_stride\":" + std::to_string(o.output_stride);
    js += ",\"mt\":true,\"pid\":" + std::to_string(pid) + ",\"threads_reconstructed\":" + std::to_string(tids.size()) + ",\"threads_in_sideband\":" + std::to_string(sb.threads.size()) +
          ",\"other_pids\":" + std::to_string(other_pids.size()) + ",\"sw_slack_tsc\":" + std::to_string(o.sw_slack_tsc) + ",\"mt_jobs\":" + std::to_string(jobs) +
          ",\"merged_records\":" + std::to_string(out_records) + ",\"merged_no_ts\":" + std::to_string(no_ts) +
          ",\"threads_failed\":" + std::to_string(bad) + ",\"threads_failed_tids\":" + failjs;
    if (any_gt) { js += ",\"gt\":{"; for (int i = 0; GT[i]; i++) js += std::string(i ? "," : "") + "\"" + GT[i] + "\":" + std::to_string(gtot[i]);
                  const uint64_t cmp = gtot[1] + gtot[2] + gtot[3]; char b[64];
                  snprintf(b, sizeof b, "%.6f", cmp ? 100.0 * (double)gtot[3] / (double)cmp : 0.0); js += std::string(",\"wrong_pct\":") + b;
                  snprintf(b, sizeof b, "%.6f", cmp ? 100.0 * (double)gtot[1] / (double)cmp : 0.0); js += std::string(",\"identical_pct\":") + b;
                  snprintf(b, sizeof b, "%.6f", cmp ? 100.0 * (double)gtot[2] / (double)cmp : 0.0); js += std::string(",\"unknown_pct\":") + b + "}"; }
    else js += ",\"gt\":null";
    js += ",\"threads\":" + thrjs + ",\"cpus\":" + cpujs + ",\"wall_s\":" + std::to_string(wall_max) + "}\n";
    if (!o.summary.empty()) { FILE* f = fopen(o.summary.c_str(), "w"); if (f) { fputs(js.c_str(), f); fclose(f); } }
    fprintf(stderr, "%s", js.c_str());
    if (!o.out.empty()) fprintf(stderr, "ptrecon --mt: %lu records from %zu threads merged by time -> %s\n", (unsigned long)out_records, tids.size(), o.out.c_str());
    return bad ? 1 : 0;
}
