#!/usr/bin/env python3
"""ptw_budget.py -- the per-site PTWRITE budget of a JIT runtime's main ELF image (node, libjvm.so) for e9fast_ptw.

    ptw_budget.py [--budget 5e6] SPEC COUNT_SITEMAP OUT.buf RUNDIR [RUNDIR ...]

COUNT_SITEMAP is the site map of the image rewritten with `--sink count' from SPEC (one per-thread execution counter
per patched address: its counter index, the spec site ids behind it and the values it logs per execution).  Each
RUNDIR holds one benchmark's untimed count run: count.<pid>.csv (`index,count', dumped by the runtime at exit with
PTLOG_COUNT=1) and wall.txt (the run's wall seconds, first field).  rate_b(addr) = executions x values / wall_b.
Addresses are taken in ascending order of their largest rate and use PTWRITE while EVERY benchmark's PTWRITE rate
stays <= the budget (the rule of the other Fast-PTWRITE lists, data/ptw_sites); an address the count build could not
patch has no count and stays on the buffer.  OUT.buf names the BUFFER sites (the smaller set) as `addr when
hash-of-spec-entry' and records the image's sha256: run/lib/jitwp/build_ptw.sh applies it only to that image's spec.
"""
import argparse, collections, glob, hashlib, json, os, sys

ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
ap.add_argument("--budget", type=float, default=5e6, help="PTWRITE values/s per benchmark (default 5e6)")
ap.add_argument("spec"); ap.add_argument("count_sitemap"); ap.add_argument("out"); ap.add_argument("runs", nargs="+")
a = ap.parse_args()

sites = {s["id"]: s for s in json.load(open(a.spec))["sites"]}
cmap = json.load(open(a.count_sitemap))["count"]          # [{addr, index, n_values, sites}]
runs = []
for rd in a.runs:
    cnt = collections.Counter()
    files = glob.glob(os.path.join(rd, "count.*.csv"))
    if not files:
        sys.exit("ptw_budget: no count.*.csv in %s" % rd)
    for f in files:
        for ln in open(f):
            if ln[:1].isdigit():
                i, c = ln.split(","); cnt[int(i)] += int(c)
    wall = float(open(os.path.join(rd, "wall.txt")).read().split()[0])
    runs.append((os.path.basename(rd.rstrip("/")), cnt, wall))

rate = [{e["index"]: n.get(e["index"], 0) * e["n_values"] / w for e in cmap} for _, n, w in runs]
order = sorted(cmap, key=lambda e: max(r[e["index"]] for r in rate))
tot = [0.0] * len(runs); ptw = []
for e in order:
    if any(t + r[e["index"]] > a.budget for t, r in zip(tot, rate)):
        break
    tot = [t + r[e["index"]] for t, r in zip(tot, rate)]
    ptw.append(e)


def key(s):
    t = {k: v for k, v in s.items() if k != "id"}
    return "%#x %s %s" % (s["addr"], s["when"], hashlib.sha1(json.dumps(t, sort_keys=True).encode()).hexdigest()[:12])


# The list names the BUFFER sites (the far smaller set): the sites of the addresses over the budget and every spec site
# with no count (not patched by the count build).
ptw_ids = {i for e in ptw for i in e["sites"]}
keys = sorted(key(s) for i, s in sites.items() if i not in ptw_ids)
allv = [sum(r.values()) for r in rate]
with open(a.out, "w") as o:
    spec = json.load(open(a.spec))
    o.write("# %s: BUFFER sites at a %.3g values/s PTWRITE budget per benchmark: %d of %d spec sites (PTWRITE: %d of %d"
            " patched addresses)\n" % (os.path.basename(a.spec).replace(".spec.json", ""), a.budget, len(keys),
                                        len(sites), len(ptw), len(cmap)))
    o.write("# spec_sha256 %s\n" % spec["sha256"])
    for (nm, _, w), t, v in zip(runs, tot, allv):
        o.write("# %s: PTWRITE %.3g of %.3g values/s (%.2f %%), wall %.1f s\n" % (nm, t, v, 100.0 * t / v if v else 0, w))
    o.writelines(k + "\n" for k in keys)
print("".join(open(a.out).readlines()[:2 + len(runs)]), end="")
