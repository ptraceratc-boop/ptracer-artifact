#!/usr/bin/env python3
"""trad_par.py -- the dynamic traditional tracers of Figure 5 (memorytracer, libdft, Valgrind), one repslice.py run
per (tracer, suite, cell), several at once on disjoint pinned cores (called by run/fig5_traditional.sh).

    trad_par.py --csv BASELINES.csv --tracers memtrace,libdft,valgrind --cells poly=2mm,atax;pyperf=...;...
                [--reps 1] [--ceiling 0] [--singles "4 5 .. 19"] [--wide "20-23,68-71 ..."]

First every cell runs uninstrumented once with the traditional step's own workload (label `trad'): the reference of
the ratio and of the cap.  Then each tracer run is stopped at 200x the wall time of that reference run (process
start-up included) and counted as 200x (cap = "cap"); a cell whose reference failed gives error rows
(cap = "no-reference").  --ceiling S (QUICK only) also stops a run after S seconds (cap = "stopped": not a 200x
result, plotted as an error).
Single-core lanes (the tracee pinned to the lane's core) run PolyBench, pyperformance, Rust Stream and Node.js; wide
slots run Java (the JVM on the first half of the slot's cpus).  Memcached runs last and alone, its server on
MC_SRV_CORES (4,5,6,7) and its load client on MC_CLI_CORES (8-11), other physical cores, as in its Figure 5 rows.  Each lane writes its own CSV (BASELINES.t<lane>.csv), merged into BASELINES.csv at the end; a row
already in any of them is not re-run.  The timing mutex is taken once for the whole pass.
"""
import argparse
import csv
import json
import glob
import os
import queue
import statistics
import subprocess
import sys
import threading
import time

LIB = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(LIB))
sys.path.insert(0, LIB)
TP = os.path.join(ROOT, "third_party")
PINX = ("-pin_memory_range 0x1000000000:0x1400000000 -enforce_pin_range_allocations 1 "
        "-assert_on_memory_conflict FIXED_ONLY")
TOOL = {
    "vanilla": [],
    "memtrace": ["--pin-root", os.path.join(TP, "pin-4.4"), "--pin-extra", PINX,
                 "--memtrace-tool", os.path.join(TP, "memtrace", "p4", "obj-intel64", "memtrace.so")],
    "libdft": ["--libdft-pin-root", os.path.join(TP, "pin-3.20"),
               "--libdft-tool", os.path.join(TP, "libdft64", "tools", "obj-intel64", "track.so")],
    "valgrind": ["--valgrind", os.environ.get("VALGRIND") or os.path.join(TP, "valgrind-build", "bin", "valgrind")],
}
WIDE_SUITES = ("java",)
# Memcached runs alone after the parallel lanes, its server and load client on different physical cores (the layout of
# its Figure 5 rows: MC_SRV_CORES / MC_CLI_CORES), so neither the reference nor a tracer run shares a core with the client
SOLO_SUITES = ("mc",)
# measurement version of a suite's rows in this step (absent = 1); older rows are moved to <csv>.stale.csv and re-measured
TRAD_VERSION = {"mc": 2}   # 2: Memcached alone, client on its own physical cores (1: client on the server cores' SMT siblings)
REF = "trad"                       # label of the reference (uninstrumented) rows
COLS = ["suite", "cell", "config", "rep", "ktime", "wall", "rc", "ck", "mutex", "load0", "load1", "label", "ts", "tps",
        "gate", "cap", "acalls", "pt_lost_bytes", "pt_trunc", "pt_aux_bytes", "pt_peak_fill", "topa", "iters",
        "inherited", "srv_ucpu"]
# the workload of every run of this step (reference and tracers alike): Java and Node.js 3 iterations, the first
# one dropped; pyperformance 1 untimed + 2 timed calls of the benchmark (PPF_WARM=1, --pyloops 2)
WORKLOAD = ["--java-pin-iters", "3", "--java-pin-drop", "1", "--pyloops", "2"]


def expand(spec):
    out = []
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            out += list(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    return out


def rows(paths):
    for p in paths:
        try:
            with open(p) as f:
                yield from csv.DictReader(f)
        except (OSError, KeyError):
            pass


def kind_of(s):
    return "solo" if s in SOLO_SUITES else "wide" if s in WIDE_SUITES else "single"


def prune_stale(base, paths, dry):
    """Move the rows of every suite whose recorded version is older than TRAD_VERSION to <base>.stale.csv."""
    ledger = base + ".versions.json"
    try:
        with open(ledger) as f:
            have = json.load(f)
    except (OSError, ValueError):
        have = {}
    old = [s for s, v in TRAD_VERSION.items() if int(have.get(s, 1)) < v]
    moved = 0
    for p in paths if old else []:
        try:
            with open(p) as f:
                rd = csv.DictReader(f)
                fields, rs = rd.fieldnames, list(rd)
        except (OSError, KeyError):
            continue
        drop = [r for r in rs if r.get("suite") in old]
        if not drop or not fields:
            continue
        moved += len(drop)
        if dry:
            continue
        new = not os.path.exists(base + ".stale.csv")
        with open(base + ".stale.csv", "a", newline="") as f:
            w = csv.DictWriter(f, fieldnames=fields, restval="", extrasaction="ignore")
            if new:
                w.writeheader()
            w.writerows(drop)
        with open(p + ".tmp", "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=fields, restval="")
            w.writeheader()
            w.writerows(r for r in rs if r.get("suite") not in old)
        os.replace(p + ".tmp", p)
    if old:
        print("[trad_par] %s: %d rows of an older version %s %s.stale.csv; re-measured in this run"
              % (",".join(old), moved, "would be moved to" if dry else "moved to", os.path.basename(base)), flush=True)
    if not dry:
        with open(ledger, "w") as f:
            json.dump(dict(have, **{s: v for s, v in TRAD_VERSION.items()}), f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True)
    ap.add_argument("--tracers", required=True)
    ap.add_argument("--cells", required=True)
    ap.add_argument("--reps", default="1")
    ap.add_argument("--ceiling", default="0")
    ap.add_argument("--singles", default="4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19")
    ap.add_argument("--wide", default="20-23,68-71 24-27,72-75 28-31,76-79 32-35,80-83 36-39,84-87 40-43,88-91 "
                                      "44-47,92-95")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    py = os.environ.get("PYBIN") or sys.executable
    base = os.path.splitext(os.path.abspath(a.csv))[0]
    files = lambda: [a.csv] + glob.glob(base + ".t*.csv")
    cells = {}
    for part in a.cells.split(";"):
        if "=" in part:
            s, cs = part.split("=", 1)
            cells[s] = [c for c in cs.split(",") if c]
    singles, wides = a.singles.split(), a.wide.split()
    solo_env = dict(MC_SRV_CORES=os.environ.get("MC_SRV_CORES", "4,5,6,7"), MC_CLI_CORES=os.environ.get("MC_CLI_CORES", "8-11"))
    solo_env.update(TIMED_CORE=solo_env["MC_SRV_CORES"].split(",")[0], HELPER_CORES=solo_env["MC_CLI_CORES"])
    prune_stale(base, files(), a.dry_run)

    def ref_walls():
        acc = {}
        for r in rows(files()):
            if r["config"] == "vanilla" and r.get("label") == REF and r["rc"] == "0" and r.get("wall") and r["ktime"]:
                acc.setdefault((r["suite"], r["cell"]), []).append(float(r["wall"]))
        return {k: statistics.median(v) for k, v in acc.items()}

    def have():
        return {(r["suite"], r["cell"], r["config"], r.get("label")) for r in rows(files())}

    lk = threading.Lock()

    def run(lane, env_up, job):
        est, t, s, c = job
        lcsv = "%s.t%s.csv" % (base, lane)
        args = [py, os.path.join(LIB, "repslice.py"), "--suites", s, "--configs", t, "--reps", a.reps,
                "--java-reps", a.reps, "--cells", c, "--csv", lcsv, "--label", REF if t == "vanilla" else t,
                "--no-lock", "--mutex-inherit", "--load-gate", "0", "--timeout", "86400"] + WORKLOAD + TOOL[t]
        if os.environ.get("MC_OPS"):                # QUICK: a shorter Memcached load run (reference and tracers alike)
            args += ["--mc-ops", os.environ["MC_OPS"]]
        if t != "vanilla":
            args += ["--cap-x", "200", "--cap-slack", "0", "--cap-label", REF, "--cap-from", ",".join(files()),
                     "--cap-ceiling", a.ceiling, "--stop-after-fail"]
        t0 = time.time()
        with open("%s.t%s.log" % (base, lane), "a") as lg:
            rc = subprocess.call(args, env=dict(os.environ, **env_up), stdout=lg, stderr=subprocess.STDOUT, cwd=ROOT)
        with lk:
            print("[trad_par] lane %s: %s %s/%s rc=%d %.0f s" % (lane, t, s, c, rc, time.time() - t0), flush=True)

    def lanes(jobs):
        for k in jobs:
            jobs[k].sort(reverse=True)          # the longest first
        qs = {k: queue.Queue() for k in jobs}
        for k in jobs:
            for j in jobs[k]:
                qs[k].put(j)

        def worker(kind, lane, env_up):
            while True:
                try:
                    job = qs[kind].get_nowait()
                except queue.Empty:
                    return
                run(lane, env_up, job)
        th = [threading.Thread(target=worker, args=("single", "s%d" % i, dict(TIMED_CORE=core, HELPER_CORES=core)))
              for i, core in enumerate(singles)]
        for i, spec in enumerate(wides):
            cs = expand(spec)
            lo, hi = cs[:len(cs) // 2], cs[len(cs) // 2:]
            th.append(threading.Thread(target=worker, args=("wide", "w%d" % i, dict(
                JAVA_CORES=",".join(map(str, lo)), MC_SRV_CORES=",".join(map(str, lo)),
                MC_CLI_CORES=",".join(map(str, hi)), TIMED_CORE=str(lo[0]), HELPER_CORES=",".join(map(str, hi))))))
        for x in th:
            x.start()
        for x in th:
            x.join()
        # then the solo jobs, one at a time on an otherwise idle machine
        worker("solo", "m0", solo_env)

    def plan(kind_jobs, what):
        for k, n in (("single", len(singles)), ("wide", len(wides)), ("solo", 1)):
            js = kind_jobs[k]
            tot = sum(j[0] for j in js)
            print("[trad_par] %s, %s lanes: %d runs on %d lanes, <= %.1f h if every run reaches its cap"
                  % (what, k, len(js), n, tot / 3600.0 / max(1, n) + max([j[0] for j in js] or [0]) / 3600.0),
                  flush=True)

    # 1. the reference runs
    done = have()
    ref_jobs = {"single": [], "wide": [], "solo": []}
    for s, cs in cells.items():
        for c in cs:
            if (s, c, "vanilla", REF) not in done:
                ref_jobs[kind_of(s)].append((0.0, "vanilla", s, c))
    print("[trad_par] reference (uninstrumented) runs: %d" % sum(len(v) for v in ref_jobs.values()), flush=True)
    if a.dry_run:
        walls = ref_walls()
        jobs = {"single": [], "wide": [], "solo": []}
        for t in a.tracers.split(","):
            for s, cs in cells.items():
                for c in cs:
                    jobs[kind_of(s)].append((200 * walls.get((s, c), 0.0), t, s, c))
        if any(j[0] for v in jobs.values() for j in v):
            plan(jobs, "tracers")
        return 0
    import repslice
    repslice.mutex_take(int(os.environ.get("MUTEX_WAIT", "36000")), "Figure 5 traditional tracers (parallel lanes)")
    try:
        lanes(ref_jobs)
        # 2. the tracers, each run capped at 200x its cell's reference wall time
        walls, done = ref_walls(), have()
        jobs = {"single": [], "wide": [], "solo": []}
        noref = []
        for t in a.tracers.split(","):
            for s, cs in cells.items():
                for c in cs:
                    if (s, c, t, t) in done:
                        continue
                    if (s, c) not in walls:
                        noref.append((s, c, t))
                        continue
                    jobs[kind_of(s)].append((200 * walls[(s, c)], t, s, c))
        if noref:
            new = not os.path.exists(base + ".tref.csv")
            with open(base + ".tref.csv", "a", newline="") as f:
                w = csv.DictWriter(f, fieldnames=COLS, restval="")
                if new:
                    w.writeheader()
                for s, c, t in noref:
                    w.writerow(dict(suite=s, cell=c, config=t, rep=1, rc=-3, cap="no-reference", label=t,
                                    ts=int(time.time())))
            print("[trad_par] %d tracer runs without a valid reference run -> error rows" % len(noref), flush=True)
        plan(jobs, "tracers")
        lanes(jobs)
    finally:
        repslice.mutex_give()
    parts = sorted(glob.glob(base + ".t*.csv"))
    if parts:
        subprocess.call([py, os.path.join(LIB, "merge_csv.py"), a.csv] + parts, cwd=ROOT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
