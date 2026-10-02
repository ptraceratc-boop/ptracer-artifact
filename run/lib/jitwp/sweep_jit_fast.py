#!/usr/bin/env python3
"""sweep_jit_fast.py -- the whole-program Fast TIMING sweep of the JIT suites: stock runtime ("vanilla") against
whole-program Fast ("e9fast") and, with --configs, Fast-PTWRITE ("e9fast_ptw": the same images, the JIT-code
trampolines execute `ptwrite'), one repslice.py row per call, the arms alternated inside every repetition (odd
repetition: in --configs order; even: reversed), so drift affects every arm of a benchmark alike.

    python3 run/lib/jitwp/sweep_jit_fast.py --stage DIR --out DIR --csv CSV [--suites node,java]
        [--node-reps 5] [--java-reps 3] [--node-cells a,b,..] [--java-cells a,b,..] [--mutex-inherit]
        [--configs vanilla,e9fast[,e9fast_ptw]]

Configuration (identical on both arms unless stated):
  Node  18 Web Tooling benchmarks; V8 flags PTJ_NODE_V8FLAGS (default "--no-short-builtin-calls
        --max-semi-space-size=128 --single-threaded-gc": the first is required by the rewritten node, the
        other two are global tuning flags); 5 untimed warm-up + 10 timed iterations (WTB_WARMUP / WTB_ITERS);
        before the timed window an untimed wait for pending JIT plans, bounded by WTB_DRAIN_MS (120000; the vanilla
        arm has no hook, so it is a no-op there).  Analyzer services: 4 on NODE_JIT_CORES (16,17,18,19).
  Java  25 Renaissance benchmarks, iterations/warm-up drop from java_iters.txt (JAVA_ITERS/JAVA_DROP override it for a
        functional smoke only); JAVA_XFLAGS (default
        "-XX:UseAVX=2", every arm) plus a GC log per row; the ptjdrain plugin in every arm, bounded by
        JAVA_DRAIN_MS (1800000) in the timed rows.  Analyzer services: 8 on JAVA_JIT_CORES (8,9,10,11,16,17,18,19).
  Before repetition 1 of each benchmark, one UNTIMED e9fast process (label wpfast_r0) fills the persistent JIT plan
  cache (content-hash keyed); for Java its drain bound is JAVA_WARM_DRAIN_MS (1800000: the warm-up process drains
  its whole queue, which raises the timed processes' cache hit rate).  The cache is OUT/jitcache/<suite>: keep it
  across repetitions, delete it to start a sweep cold.  It is shared by the e9fast and e9fast_ptw arms (same images,
  same plans; only the trampolines' sink differs), so one warm-up serves both.
  --warm-only --warm-slots "CORES/JITCORES CORES/JITCORES ..." runs just these warm-up processes, several at once
  (one per slot: the runtime on CORES -- one core for Node, the JVM's cores for Java -- and the analyzer services on
  JITCORES), UNTIMED and before any timed row (run/01_overhead.sh calls it ahead of the timed sweep); each finished
  warm-up leaves OUT/warm/<suite>.<cell>.ok and the timed sweep then skips that benchmark's inline warm-up.  The
  warm-up processes themselves are unchanged (same flags, same drain bound, same cache).
Rows are labelled wpfast_r<k> (both arms); rows already in the CSV are skipped, so the sweep resumes.  The timing
mutex is taken ONCE for the whole sweep (unless --mutex-inherit).  Per-run sideband and stats directories are deleted
after the hook/agent stats are copied to OUT/stats; the sweep stops when the output disk has < MIN_FREE_GB (12).
Gates to apply to every row (see the README): rc 0; Node checksum equal on both arms; 0 JS-thread analyses inside the
window (acalls column); PT lost = 0 and topa = 0 (OUT/stats/*.pt.err); Java: no new hs_err (sweep.log `newhs=').
"""
import argparse
import csv
import glob
import os
import queue
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(LIB))
sys.path.insert(0, LIB)
REPSLICE = os.path.join(LIB, "repslice.py")
WTB_ALL = ("acorn babel babel-minify babylon buble chai coffeescript espree esprima jshint lebab postcss "
           "prepack prettier source-map terser typescript uglify-js").split()


def java_iters():
    t = {}
    for l in open(os.path.join(HERE, "java_iters.txt")):
        f = l.split()
        if f and not f[0].startswith("#"):
            t[f[0]] = (int(f[1]), int(f[2]))
    return t


def free_gb(path):
    st = os.statvfs(path)
    return st.f_bavail * st.f_frsize / 2 ** 30


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--csv", required=True)
    ap.add_argument("--suites", default="node,java")
    ap.add_argument("--node-reps", type=int, default=5)
    ap.add_argument("--java-reps", type=int, default=3)
    ap.add_argument("--node-cells", default=",".join(WTB_ALL))
    ap.add_argument("--java-cells", default="")
    ap.add_argument("--timeout", type=int, default=3600)
    ap.add_argument("--mutex-inherit", action="store_true")
    ap.add_argument("--configs", default="vanilla,e9fast")
    ap.add_argument("--warm-only", action="store_true", help="run only the untimed warm-up processes (see --warm-slots)")
    ap.add_argument("--warm-slots", default="", help="CORES/JITCORES per parallel warm-up slot, space separated")
    a = ap.parse_args()
    E = os.environ
    out = os.path.abspath(a.out)
    for d in ("stats", "gclog", "jitcache/node", "jitcache/java", "warm"):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    py = E.get("PYBIN") or sys.executable
    log = open(os.path.join(out, "sweep.log"), "a")
    lk = threading.Lock()

    def say(m):
        line = "=== %s %s load1=%.2f" % (time.strftime("%Y-%m-%dT%H:%M:%S%z"), m, os.getloadavg()[0])
        with lk:
            print(line, flush=True)
            log.write(line + "\n"); log.flush()

    def warm_ok(path, suite, cell, cfg):
        """the warm-up process reached its steady-state iterations (a steady-state time), so the plan cache is filled;
        a JVM that then exits non-zero at shutdown still counts as warmed (its rc stays in the row and in sweep.log)"""
        try:
            with open(path) as f:
                return any(r["suite"] == suite and r["cell"] == cell and r["config"] == cfg and
                           r.get("label") == "wpfast_r0" and r["ktime"] for r in csv.DictReader(f))
        except (OSError, KeyError):
            return False

    def warm_mark(suite, cell):
        return os.path.join(out, "warm", "%s.%s.ok" % (suite, cell))

    jit = java_iters()
    cfgs = [c for c in a.configs.split(",") if c]
    warm = [c for c in cfgs if c.startswith("e9fast")]
    java_cells = [c for c in (a.java_cells.split(",") if a.java_cells else list(jit)) if c]
    node_cells = [c for c in a.node_cells.split(",") if c]
    for c in java_cells:
        if c not in jit:
            sys.exit("sweep_jit_fast: %s is not in java_iters.txt" % c)

    def row(suite, cell, cfg, r, slot=None):
        """One repslice call (one row); returns its exit code.  slot = (k, cores, jitcores) of a parallel warm-up."""
        env = dict(E, F5_WARMPROC="0", PTJ_ITERLOG=os.path.join(out, "iters.jsonl"))
        csvp = os.path.abspath(a.csv) if slot is None else os.path.join(out, "warm", "warm%d.csv" % slot[0])
        args = [py, REPSLICE, "--suites", suite, "--configs", cfg, "--cells", cell, "--reps", "1",
                "--java-reps", "1", "--label", "wpfast_r%d" % r, "--mutex-inherit", "--timeout", str(a.timeout),
                "--csv", csvp]
        jd = os.path.join(out, "jd_%s_%s%s" % (suite, cfg, "" if slot is None else "_w%d" % slot[0]))
        if suite == "node":
            env.update(PTJ_NODE_V8FLAGS=E.get("PTJ_NODE_V8FLAGS", "--no-short-builtin-calls --max-semi-space-size=128 "
                                                                  "--single-threaded-gc"),
                       WTB_WARMUP=E.get("WTB_WARMUP", "5"), WTB_ITERS=E.get("WTB_ITERS", "10"),
                       WTB_DRAIN_MS=E.get("WTB_DRAIN_MS", "120000"), WTB_JS=os.path.join(HERE, "wtb.js"))
            workers, cores = "4", E.get("NODE_JIT_CORES", "16,17,18,19")
            if slot:
                env["TIMED_CORE"], cores = slot[1], slot[2]
        else:
            it, drop = jit[cell]
            if E.get("JAVA_ITERS"):   # smoke/functional runs only: override java_iters.txt (not a paper configuration)
                it = int(E["JAVA_ITERS"]); drop = min(drop, int(E.get("JAVA_DROP", "0")), it - 1)
            ms = E.get("JAVA_WARM_DRAIN_MS", "1800000") if r == 0 else E.get("JAVA_DRAIN_MS", "1800000")
            # a full drain wait never turns a valid row into a timeout: the drain bound is added to the run's limit
            args[args.index("--timeout") + 1] = str(a.timeout + int(ms) // 1000)
            # a JVM crash log goes to this row's own directory (not the shared Renaissance directory, where another
            # lane's crash would be attributed to this row)
            hsd = os.path.join(out, "hs_err", "%s.%s.r%d%s" % (cell, cfg, r, "" if slot is None else ".w%d" % slot[0]))
            os.makedirs(hsd, exist_ok=True)
            env.update(PTJ_JAVA_ITERS_ALL="1", PTJ_JAVA_BENCH=cell, PTJ_DRAIN_MS=ms,
                       PTJ_JAVA_XOPTS="%s -Xlog:gc:file=%s:uptimemillis -XX:ErrorFile=%s" % (
                           E.get("JAVA_XFLAGS", "-XX:UseAVX=2"),
                           os.path.join(out, "gclog", "%s.r%d.%s.log" % (cell, r, cfg)),
                           os.path.join(hsd, "hs_err_pid%p.log")))
            args += ["--java-pin-iters", str(it), "--java-pin-drop", str(drop)]
            workers, cores = "8", E.get("JAVA_JIT_CORES", "8,9,10,11,16,17,18,19")
            if slot:
                env["JAVA_CORES"], cores = slot[1], slot[2]
        if cfg.startswith("e9fast"):
            args += ["--jit-cache", os.path.join(out, "jitcache", suite), "--jit-workers", workers,
                     "--jit-cores", cores, "--jitwp-stage", a.stage, "--jit-dir", jd]
        hsglob = os.path.join(hsd, "hs_err_pid*.log") if suite == "java" else ""
        hs0 = set(glob.glob(hsglob)) if hsglob else set()
        with open(os.path.join(out, "host.log" if slot is None else "host.warm%d.log" % slot[0]), "a") as hl:
            rc = subprocess.call(args, env=env, stdout=hl, stderr=subprocess.STDOUT, cwd=ROOT)
        newhs = sorted(set(glob.glob(hsglob)) - hs0) if hsglob else []
        row_rc, row_kt = "?", ""              # the timed process's own outcome: repslice exits 0 after a crashed run
        try:
            with open(csvp) as f:
                for x in csv.DictReader(f):
                    if x["suite"] == suite and x["cell"] == cell and x["config"] == cfg and x.get("label") == "wpfast_r%d" % r:
                        row_rc, row_kt = x["rc"], x["ktime"]
        except (OSError, KeyError):
            pass
        bad = not (row_rc == "0" and row_kt) or bool(newhs)
        say("%s %s %s r%d rc=%d row_rc=%s ktime=%s newhs=%s%s%s" % (suite, cell, cfg, r, rc, row_rc, row_kt or "none",
                                                " ".join(newhs), "" if slot is None else " slot=%d" % slot[0],
                                                " ROW_FAILED" if bad else ""))
        if r == 0 and rc == 0 and warm_ok(csvp, suite, cell, cfg):
            open(warm_mark(suite, cell), "w").close()
        if os.path.isdir(jd):
            for f in glob.glob(os.path.join(jd, "*.json")) + glob.glob(os.path.join(jd, "*.pt.err")):
                if not os.path.basename(f).startswith("sb."):
                    shutil.copy(f, os.path.join(out, "stats", "r%d.%s" % (r, os.path.basename(f))))
            shutil.rmtree(jd, ignore_errors=True)   # sideband (~1 GB of switch records on Java) + stats
        return rc

    if a.warm_only:     # untimed: the warm-up processes only, one per slot at a time
        slots = [s.split("/") for s in a.warm_slots.split()]
        if not warm or not slots or any(len(s) != 2 for s in slots):
            sys.exit("sweep_jit_fast: --warm-only needs an e9fast configuration and --warm-slots CORES/JITCORES ...")
        done = set()      # benchmarks warmed by an earlier run (their wpfast_r0 row is in the CSV): never re-run
        try:
            with open(a.csv) as f:
                done = {(r["suite"], r["cell"]) for r in csv.DictReader(f)
                        if r.get("label") == "wpfast_r0" and r["ktime"]}
        except (OSError, KeyError):
            pass
        todo = queue.Queue()
        for suite in a.suites.split(","):
            for c in (node_cells if suite == "node" else java_cells):
                if not os.path.exists(warm_mark(suite, c)) and (suite, c) not in done:
                    todo.put((suite, c))
        say("warm-up only: %d processes on %d slots (untimed)" % (todo.qsize(), len(slots)))

        def slot_main(k):
            while free_gb(out) >= float(E.get("MIN_FREE_GB", "12")):
                try:
                    suite, c = todo.get_nowait()
                except queue.Empty:
                    return
                row(suite, c, warm[0], 0, slot=(k, slots[k][0], slots[k][1]))
        th = [threading.Thread(target=slot_main, args=(k,)) for k in range(len(slots))]
        for t in th:
            t.start()
        for t in th:
            t.join()
        say("warm-up only: end")
        return 0

    held = False
    if not a.mutex_inherit:
        import repslice
        repslice.mutex_take(int(E.get("MUTEX_WAIT", "36000")), "JIT whole-program Fast sweep (vanilla/e9fast)")
        held = repslice.MUTEX_HELD[0]
    try:
        reps_s = " ".join("%s_reps=%d" % (su, a.node_reps if su == "node" else a.java_reps)
                          for su in ("node", "java") if su in a.suites.split(","))   # only the suites this sweep runs
        say("start suites=%s %s stage=%s" % (a.suites, reps_s, a.stage))
        for suite in a.suites.split(","):
            cells, reps = (node_cells, a.node_reps) if suite == "node" else (java_cells, a.java_reps)
            for r in range(1, reps + 1):
                for c in cells:
                    if free_gb(out) < float(E.get("MIN_FREE_GB", "12")):
                        say("disk below MIN_FREE_GB, stopping")
                        return 1
                    if r == 1 and warm and not os.path.exists(warm_mark(suite, c)):
                        row(suite, c, warm[0], 0)          # untimed warm-up process (label wpfast_r0)
                    order = cfgs if r % 2 == 1 else cfgs[::-1]
                    for cfg in order:
                        row(suite, c, cfg, r)
        say("end")
    finally:
        if held:
            import repslice
            repslice.mutex_give()
    return 0


if __name__ == "__main__":
    sys.exit(main())
