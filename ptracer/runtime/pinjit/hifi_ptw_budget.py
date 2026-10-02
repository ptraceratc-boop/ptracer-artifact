#!/usr/bin/env python3
"""hifi_ptw_budget.py -- the per-site PTWRITE budget of the HiFi-PTWRITE (mixed) sink.

    hifi_ptw_budget.py [--budget 5e6] OUT.buf PROFILE_DIR[:SECONDS] [PROFILE_DIR[:SECONDS] ...]

Each PROFILE_DIR holds the <prefix>.<pid>.tsv files of one benchmark's run with `hifitool -sink count' (one per
process: `# wall_s W', then image, link address, values per execution, executions).  A site's rate in a benchmark
is sum over its processes of executions x values / wall; with :SECONDS (the benchmark's measured time, e.g. the
timed iterations only), all of the benchmark's values are divided by SECONDS instead (conservative: start-up and
warm-up values are counted as if they ran in the measured interval).  Sites are taken in ascending order of their largest
rate over the benchmarks and use PTWRITE while EVERY benchmark's PTWRITE rate stays <= the budget (the rule of the
Fast-PTWRITE site lists, data/ptw_sites); the rest are written to OUT.buf, the list `-ptwbuffer' keeps on the
buffer.  Sites no profile executed stay on PTWRITE (zero rate).  Generated-code (JIT) sites always use the buffer.
"""
import argparse, collections, glob, os, sys

ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
ap.add_argument("--budget", type=float, default=5e6, help="PTWRITE values/s per benchmark (default 5e6)")
ap.add_argument("out")
ap.add_argument("profiles", nargs="+")
a = ap.parse_args()

rates = []          # per benchmark: {(img, addr): values/s}
names = []
for d in a.profiles:
    secs = None
    if ":" in d:
        d, secs = d.rsplit(":", 1); secs = float(secs)
    names.append(os.path.basename(d.rstrip("/")))
    r = collections.defaultdict(float)
    files = glob.glob(os.path.join(d, "*.tsv"))
    if not files:
        sys.exit("no count profile in %s" % d)
    for f in files:
        wall = None
        for line in open(f):
            if line.startswith("# wall_s"):
                wall = max(float(line.split()[2]), 1e-3)
                continue
            img, addr, nval, n = line.split("\t")
            r[(img, int(addr, 16))] += int(n) * int(nval) / (secs or wall)
    rates.append(r)

sites = set().union(*rates)
order = sorted(sites, key=lambda s: max(r.get(s, 0.0) for r in rates))
tot = [0.0] * len(rates); nptw = 0
for i, s in enumerate(order):
    if any(t + r.get(s, 0.0) > a.budget for t, r in zip(tot, rates)):
        buf = order[i:]
        break
    tot = [t + r.get(s, 0.0) for t, r in zip(tot, rates)]; nptw += 1
else:
    buf = []
allv = [sum(r.values()) for r in rates]
with open(a.out, "w") as o:
    o.write("# HiFi-PTWRITE buffer sites (budget %.3g values/s): %d executed sites, %d PTWRITE, %d buffer\n"
            % (a.budget, len(order), nptw, len(buf)))
    for p, t, v in zip(names, tot, allv):
        o.write("# %s: PTWRITE %.3g of %.3g values/s (%.2f %%)\n" % (p, t, v,
                                                                     100.0 * t / v if v else 0.0))
    for img, addr in sorted(buf):
        o.write("%s %x\n" % (img, addr))
print("".join(open(a.out).readlines()[:1 + len(names)]), end="")
