// main.cpp -- ptrecon command line. See recon.h.
#include "recon.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
static void usage() {
    fprintf(stderr,
        "usage: ptrecon --aux AUX --sideband SB.json [--spec SPEC.json --sitemap MAP.json [--orig-image ELF]]...\n"
        "               [--jitdump FILE]... [--cv CVFILE] [-o OUT.mtrace] [--summary OUT.json]\n"
        "               [--site-stats OUT.csv]\n"
        "               [--output-stride N] (output-only sample; full interpretation/statistics)\n"
        "               [--jobs N] [--warm-mb MB] [--no-delta-scan] [--text] [--max-insn N] [--max-rep N] [--fs-base A] [-v]\n"
        "\n"
        "  --spec / --sitemap / --orig-image are REPEATABLE: give one group per rewritten image\n"
        "  (python3.12, libc, libm, every extension .so).  They are matched to the process's\n"
        "  mappings by the site map's own \"image\" field, and --orig-image applies to the\n"
        "  --spec/--sitemap given just before it (it also defaults to the map's \"orig_image\").\n"
        "  --jitdump FILE is REPEATABLE: the JIT code dump\n"
        "  of one traced process.  It builds a TIME-KEYED code table -- an address in a JIT code\n"
        "  space is only valid for an interval -- which supplies the decoder's instruction bytes\n"
        "  and the original bytes behind a `jit:<pid>' site map.\n"
        "  --site-stats writes image,site,orig_addr,tramp_addr,kind,arg,payload_bits,count.\n"
        "  --jobs N reconstructs chunks of the AUX stream in parallel.  A chunk boundary is placed\n"
        "               only where the stream re-anchors -- a keyframe has re-defined the registers and\n"
        "               (buffer sink) a sync marker has re-aligned the value cursor -- and the chunk\n"
        "               starts decoding at a PSB before it.  A capture that cannot re-anchor (no\n"
        "               keyframe sites, or a buffer sink with --sync 0) is REFUSED with the reason on\n"
        "               stderr and reconstructed serially instead.\n"
        "  --delta-scan / --no-delta-scan (default ON for --jobs N): with log-on-change logging, a\n"
        "               chunk cannot know the runtime's cache slots at its own start.  The scan is one\n"
        "               extra, cheap parallel decode pass that reports the last value logged at every\n"
        "               site in each chunk; the parent folds them into the EXACT table at each chunk\n"
        "               boundary, which is what makes --jobs N bit-identical to serial.\n"
        "  --exact-warm makes every chunk replay the trace from byte 0 before its own range: bit-\n"
        "               identical to serial by construction, and it costs most of the speed-up.  The\n"
        "               default warm-up (--warm-mb) cannot reach state established during process\n"
        "               startup, which is where a parallel run can still differ from a serial one --\n"
        "               always as known vs unknown, never as a different address.\n"
        "  --warm-kb KB (default 0): each chunk warms up from the PSB its boundary probe started at\n"
        "               (which precedes the keyframe + sync marker the boundary sits on) plus KB more.\n"
        "  --chunks-per-job C: split into C*N chunks, at most N in flight (default: auto).\n"
        "  --rec-hash: put a composable hash of the whole record stream in the summary (rec_hash);\n"
        "               equal across --jobs values iff the record streams are (up to 2^-61 collisions).\n"
        "  --warm-mb MB (legacy rule) is how much of the PREVIOUS chunk each chunk decodes first, with output\n"
        "  suppressed, so its registers/delta table/shadow are warm when its own range begins\n"
        "  (default 32; 0 disables).  --fs-base overrides the sideband's TLS base, which a chunk\n"
        "  child seeds because the analyzer anchors `fs_base' only once, at `main'.\n"
        "  --mt: MULTI-THREADED reconstruction of a per-CPU capture (pt_capture2 --cpu A --cpu B ...,\n"
        "               sideband version 2).  No --aux: the sideband names one AUX file and one switch-record\n"
        "               file per core.  Every core's stream is cut at TIP.PGE/overflow/resync boundaries,\n"
        "               each region is attributed to the thread the core's context-switch records say was\n"
        "               running, and every thread is reconstructed on its own (in parallel, --mt-jobs N) and\n"
        "               merged by time into -o with a real tid per record.  --cv is repeatable / --cv-dir DIR\n"
        "               globs cv.*.bin (per-thread v2 files or the v3 mux file); --gt-dir DIR globs the\n"
        "               per-thread gt.<pid>.<tid>.bin of a --gt-all run.\n"
        "  --gt-in FILE is the gt.<pid>.<tid>.bin a `rewrite.py --gt-all' build wrote ITSELF:\n"
        "  one { effective address, original ip } record per dynamic access, in program order.\n"
        "  It is the SAME execution, so the summary's \"gt\" block compares it to the\n"
        "  reconstruction record by record (identical / unknown / wrong / excluded), with no\n"
        "  alignment.  --gt-out writes it as an mtrace index-aligned with -o (eval/e2e/gtsame.py).\n"
        "  --gt-continue reconstructs through PT end after oracle EOF, counting the unverified\n"
        "  --gt-attach-anchor N: the trace and the gt ring need NOT begin at the same\n"
        "               instruction.  `pt_capture2 --trace-after' enables PT inside a process that\n"
        "               has been logging ground truth since exec, so the walk's implicit start at\n"
        "               gt record 0 is wrong by however much ran first (79 %% of the ring, on DSB).\n"
        "               N buffers the first N reconstructed records and locates them in the ring by\n"
        "               a CONTROL-FLOW (ip-only) rigid-shift vote confirmed over 2048 records; the\n"
        "               addresses are never used to choose the alignment, because they are what is\n"
        "               being measured.  0 = off (a co-started whole-program run).\n"
        "  --gt-confirm C (default 32): a streak re-alignment must also agree with the C preceding\n"
        "               reconstructed gt-site ips.  A single (ip, addr) pair is not a dynamic\n"
        "               instance in a server workload.  0 = off.\n"
        "  suffix as excluded/recon-only. Without it, comparison stops at the last GT record.\n");
}
int main(int argc, char** argv) {
    ReconOptions o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i]; auto next = [&]() { if (i + 1 >= argc) { usage(); exit(2); } return std::string(argv[++i]); };
        if (a == "--aux") o.aux = next();
        else if (a == "--sideband") o.sideband = next();
        else if (a == "--spec") { o.specs.push_back(next()); while (o.orig_images.size() + 1 < o.specs.size()) o.orig_images.push_back(""); }
        else if (a == "--sitemap") { o.sitemaps.push_back(next()); while (o.orig_images.size() + 1 < o.sitemaps.size()) o.orig_images.push_back(""); }
        else if (a == "--orig-image") { size_t want = o.sitemaps.size() ? o.sitemaps.size() : o.specs.size();
                                        if (!want) want = 1;
                                        while (o.orig_images.size() < want) o.orig_images.push_back("");
                                        o.orig_images[want - 1] = next(); }
        else if (a == "--jitdump") o.jitdumps.push_back(next());
        else if (a == "--cv") { std::string f = next(); if (o.cvfile.empty()) o.cvfile = f; o.cvfiles.push_back(f); }
        // MULTI-THREADED reconstruction
        else if (a == "--mt") o.mt = true;
        else if (a == "--tid") o.tid = (uint32_t)strtoul(next().c_str(), nullptr, 0);
        else if (a == "--cv-dir") o.cv_dir = next();
        else if (a == "--gt-dir") o.gt_dir = next();
        else if (a == "--sw-slack-tsc") o.sw_slack_tsc = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--keep-thread-files") o.keep_thread_files = true;
        else if (a == "--mt-jobs") o.mt_jobs = atoi(next().c_str());
        else if (a == "--mt-split") o.mt_split = atoi(next().c_str());
        else if (a == "--mt-tid") o.mt_tids.push_back((uint32_t)strtoul(next().c_str(), nullptr, 0));
        else if (a == "--mt-dump") o.sw_dump = next();
        else if (a == "-o") o.out = next();
        else if (a == "--summary") o.summary = next();
        else if (a == "--site-stats") o.site_stats = next();
        else if (a == "--jobs" || a == "-j") o.jobs = atoi(next().c_str());
        else if (a == "--skip-bytes") o.skip_bytes = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--end-bytes") o.end_bytes = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--text") o.text = true;
        else if (a == "--decode-only") o.decode_only = true;
        else if (a == "--rec-hash-cuts" && i + 1 < argc) { o.rec_hash_cuts = argv[++i]; o.rec_hash = true; }
        else if (a == "--no-time") o.no_time = true;
        else if (a == "--max-insn") o.max_insn = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--output-stride") {
            std::string value = next(); char *end = nullptr; errno = 0;
            o.output_stride = strtoull(value.c_str(), &end, 10);
            if (value.empty() || value[0] == '-' || *end || errno == ERANGE || !o.output_stride) {
                fprintf(stderr, "--output-stride requires a positive integer\n"); return 2;
            }
        }
        else if (a == "--max-rep") o.max_rep = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--fs-base") { o.fs_base = strtoull(next().c_str(), nullptr, 0); }
        else if (a == "--warm-mb") { o.warm_bytes = strtoull(next().c_str(), nullptr, 0) << 20; o.warm_legacy = true; }
        else if (a == "--warm-kb") o.warm_min = strtoull(next().c_str(), nullptr, 0) << 10;
        else if (a == "--chunks-per-job") o.chunks_per_job = atoi(next().c_str());
        else if (a == "--rec-hash") o.rec_hash = true;
        else if (a == "--exact-warm") o.exact_warm = true;
        else if (a == "--delta-scan") o.delta_scan = true;
        else if (a == "--no-delta-scan") o.delta_scan = false;
        else if (a == "--emit-from") { o.emit_from = strtoull(next().c_str(), nullptr, 0); o.seed_fs = true; }
        else if (a == "--seed-fs") o.seed_fs = true;
        else if (a == "--no-seed-fs") { o.no_seed_fs = true; o.seed_fs = false; }
        else if (a == "--resync-log") o.resync_log = next();
        else if (a == "--cv-audit") o.cv_audit = next();   // one line per sync marker
        // SAME-PROCESS GROUND TRUTH
        else if (a == "--gt-in") { o.gt_in = next(); o.gt_compare = true; }
        else if (a == "--gt-out") o.gt_out = next();
        else if (a == "--gt-compare") o.gt_compare = true;
        else if (a == "--gt-continue") o.gt_continue = true;
        else if (a == "--gt-lookahead") o.gt_lookahead = atoi(next().c_str());
        else if (a == "--gt-resync-window") o.gt_resync_window = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--gt-attach-anchor") o.gt_attach_anchor = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--gt-confirm") o.gt_confirm = atoi(next().c_str());
        else if (a == "-v") o.verbose++;
        else { usage(); return 2; }
    }
    if (o.output_stride > 1 && (o.jobs > 1 || o.gt_compare || !o.gt_in.empty() ||
                              !o.gt_out.empty() || !o.gt_dir.empty())) {
        fprintf(stderr, "--output-stride is an explicitly sampled output: incompatible with GT comparison "
                        "or --jobs chunking (use --mt-jobs for independent threads)\n"); return 2;
    }
    // `ptrecon --jitdump FILE' with no trace: load the table and print what it contains.  This is
    // the offline half of runtime/jit/check_sitemap.py and needs no capture.
    if (o.aux.empty() && o.sideband.empty() && !o.jitdumps.empty()) {
        JitTable t; int rc = 0;
        for (auto& p : o.jitdumps) { std::string err;
            if (!t.load(p, &err)) { fprintf(stderr, "ptrecon: --jitdump %s: %s\n", p.c_str(), err.c_str()); rc = 1; }
            else if (!err.empty()) fprintf(stderr, "warning: --jitdump %s: %s\n", p.c_str(), err.c_str()); }
        t.finalize();
        printf("%s\n", t.stats_json().c_str());
        return rc;
    }
    if (o.mt) {
        if (o.sideband.empty()) { usage(); return 2; }
        while (o.orig_images.size() < std::max(o.specs.size(), o.sitemaps.size())) o.orig_images.push_back("");
        try { return Recon::run_threads(o); }
        catch (std::exception& e) { fprintf(stderr, "ptrecon --mt: %s\n", e.what()); return 1; }
    }
    if (o.aux.empty() || o.sideband.empty()) { usage(); return 2; }
    while (o.orig_images.size() < std::max(o.specs.size(), o.sitemaps.size())) o.orig_images.push_back("");
    try {
        if (o.jobs > 1 && !o.gt_in.empty()) { fprintf(stderr, "ptrecon: --gt-in needs serial reconstruction (--jobs 1): the gt stream is one program-order sequence\n"); return 2; }
        if (o.jobs > 1) return Recon::run_parallel(o);
        Recon r(o); return r.run();
    } catch (std::exception& e) { fprintf(stderr, "ptrecon: %s\n", e.what()); return 1; }
}
