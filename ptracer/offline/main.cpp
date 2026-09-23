// main.cpp -- ptrecon command line.  See recon.h for the options' semantics.
#include "recon.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
static void usage() {
    fprintf(stderr,
        "usage: ptrecon --aux AUX --sideband SB.json [--spec SPEC.json --sitemap MAP.json [--orig-image ELF]]...\n"
        "               [--cv CVFILE] [-o OUT.mtrace] [--summary OUT.json]\n"
        "               [--jobs N] [--warm-mb MB] [--exact-warm] [--no-delta-scan]\n"
        "               [--gt-in GT.bin [--gt-out GT.mtrace] [--gt-continue] [--gt-lookahead N] [--gt-resync-window N]]\n"
        "               [--text] [--decode-only] [--no-time] [--max-insn N] [--max-rep N] [--fs-base A]\n"
        "               [--resync-log FILE] [--cv-audit FILE] [-v]\n"
        "\n"
        "  --spec / --sitemap / --orig-image are REPEATABLE: give one group per rewritten image\n"
        "  (python3.12, libc, libm, every extension .so).  They are matched to the process's\n"
        "  mappings by the site map's own \"image\" field, and --orig-image applies to the\n"
        "  --spec/--sitemap given just before it (it also defaults to the map's \"orig_image\").\n"
        "  --cv FILE is the buffer sink's value stream (cv.<pid>.<tid>.bin) of the traced thread.\n"
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
        "  --warm-mb MB is how much of the PREVIOUS chunk each chunk decodes first, with output\n"
        "  suppressed, so its registers/delta table/shadow are warm when its own range begins\n"
        "  (default 32; 0 disables).  --fs-base overrides the sideband's TLS base, which a chunk\n"
        "  child seeds because the analyzer anchors `fs_base' only once, at `main'.\n"
        "  --gt-in FILE is the gt.<pid>.<tid>.bin a `rewrite.py --gt-all' build wrote ITSELF:\n"
        "  one { effective address, original ip } record per dynamic access, in program order.\n"
        "  It is the SAME execution, so the summary's \"gt\" block compares it to the\n"
        "  reconstruction record by record (identical / unknown / wrong / excluded), with no\n"
        "  alignment.  --gt-out writes it as an mtrace index-aligned with -o.\n"
        "  --gt-continue reconstructs through PT end after oracle EOF, counting the unverified\n"
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
        else if (a == "--cv") { std::string f = next();
                                if (!o.cvfile.empty()) { fprintf(stderr, "ptrecon: --cv given twice; a per-task capture has one value stream\n"); return 2; }
                                o.cvfile = f; }
        else if (a == "-o") o.out = next();
        else if (a == "--summary") o.summary = next();
        else if (a == "--jobs" || a == "-j") o.jobs = atoi(next().c_str());
        else if (a == "--skip-bytes") o.skip_bytes = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--end-bytes") o.end_bytes = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--text") o.text = true;
        else if (a == "--decode-only") o.decode_only = true;
        else if (a == "--no-time") o.no_time = true;
        else if (a == "--max-insn") o.max_insn = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--max-rep") o.max_rep = strtoull(next().c_str(), nullptr, 0);
        else if (a == "--fs-base") { o.fs_base = strtoull(next().c_str(), nullptr, 0); }
        else if (a == "--warm-mb") o.warm_bytes = strtoull(next().c_str(), nullptr, 0) << 20;
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
        else if (a == "-v") o.verbose++;
        else { usage(); return 2; }
    }
    if (o.aux.empty() || o.sideband.empty()) { usage(); return 2; }
    while (o.orig_images.size() < std::max(o.specs.size(), o.sitemaps.size())) o.orig_images.push_back("");
    try {
        if (o.jobs > 1 && !o.gt_in.empty()) { fprintf(stderr, "ptrecon: --gt-in needs serial reconstruction (--jobs 1): the gt stream is one program-order sequence\n"); return 2; }
        if (o.jobs > 1) return Recon::run_parallel(o);
        Recon r(o); return r.run();
    } catch (std::exception& e) { fprintf(stderr, "ptrecon: %s\n", e.what()); return 1; }
}
