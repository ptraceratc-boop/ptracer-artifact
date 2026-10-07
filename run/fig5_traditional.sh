#!/usr/bin/env bash
# Figure 5, traditional tracers (a separate step after run/fig5.sh): memorytracer, libdft, Valgrind and
# Spindle-plus on every benchmark of every suite.
#
#   bash docker/run.sh run/fig5_traditional.sh [--dry-run] [memtrace] [libdft] [valgrind] [spindle]   (default: all)
#
#   memtrace  "memorytracer": Pin 4.4 fill buffer over every memory operand       all six suites
#   libdft    byte-level taint tracking (Pin 3.20)                                  all six suites
#   valgrind  Valgrind 3.22 lackey, patched into a binary memory tracer             all six suites
#   spindle   Spindle-plus (LLVM pass, compile-time): PolyBench/C and pyperformance, each against its own clang -O2
#             build (pyperformance: CPython 3.12.13 with its interpreter core compiled through the pass; extension
#             modules and shared libraries are not traced, run/lib/build_spindle_cpython.sh); other suites NA
#
# Workload of every run (tracer and reference alike): PolyBench/C, Rust Stream (the paper's seq + rayon recipe) and
# Memcached are single-shot programs (1 run); Node.js and Java run 3 iterations, the first dropped as warm-up, the
# steady state from the other 2; pyperformance 1 untimed + 2 timed calls of the benchmark in one process.
# Each cell first runs uninstrumented once with that workload (label "trad"): the denominator of its ratios and the
# reference of the paper's rule -- a tracer run is stopped at 200x the wall time of that reference run (process
# start-up included) and counted as 200x (cap = "cap").  A tracer that crashes or refuses the program gives rc != 0
# (plotted "Err"), a tracer that is not built (third_party/BASELINES_ABSENT.txt) rc = 127 (cap = "tool-missing",
# "Err"), a cell whose reference run failed cap = "no-reference" ("Err"); cap = "na" = not applicable ("NA").
# Rows -> $RUN_OUT/baselines.csv; finished rows are skipped, so an interrupted run resumes.
# Lanes (run/lib/trad_par.py): single-core lanes TRAD_CORES ("4 .. 19": PolyBench, pyperformance, Rust, Node.js) and
# wide slots TRAD_WIDE (7 x 4 cores + their SMT siblings: Java); Memcached last, alone (MC_SRV_CORES / MC_CLI_CORES); Spindle-plus alongside on
# TRAD_SPINDLE_CORES ("2 3": PolyBench/C, pyperformance).  Knobs: TRAD_REPS (1), TRAD_SUITES, TRAD_POLY / TRAD_PYPERF /
# TRAD_RUST / TRAD_NODE / TRAD_JAVA (explicit cell lists), TRAD_CEILING (QUICK only: stop a run after this many
# seconds; cap = "stopped", plotted "Err", never as 200x).
set -eu
. "$(dirname "$0")/lib/common.sh"
cd "$ROOT"
quick_mode
pick_python
pick_out

DRY=0; [ "${1:-}" = --dry-run ] && { DRY=1; shift; }
WHICH="${*:-memtrace libdft valgrind spindle}"
CSV="${BASELINES_CSV:-$RUN_OUT/baselines.csv}"
MAIN_CSV="$CSV"   # spindle-plus writes its own part when parallel; rows already in the main CSV are not re-run
REPS="${TRAD_REPS:-1}"
SUITES="${TRAD_SUITES:-poly,pyperf,rust,mc,node,java}"
TP="$ROOT/third_party"
ABSENT="$TP/BASELINES_ABSENT.txt"

# ---- cells --------------------------------------------------------------------------------------------------------
PYD="$ROOT/suites/pyperformance"
POLY_ALL="2mm 3mm adi atax bicg cholesky correlation covariance deriche doitgen durbin fdtd-2d floyd-warshall gemm gemver gesummv gramschmidt heat-3d jacobi-1d jacobi-2d lu ludcmp mvt nussinov seidel-2d symm syr2k syrk trisolv trmm"
WTB_ALL="acorn babel babel-minify babylon buble chai coffeescript espree esprima jshint lebab postcss prepack prettier source-map terser typescript uglify-js"
JAVA_ALL="$(grep -v '^#' run/lib/jitwp/java_iters.txt | awk '{print $1}' | tr '\n' ' ')"
PY_LIST="${PYPERF_LIST:-$PYD/pyperf/bench94.lst}"; [ -f "$PY_LIST" ] || PY_LIST="$PYD/pyperf/bench97.lst"
POLY="$POLY_ALL"; PYB="$(tr '\n' ' ' < "$PY_LIST")"; NODE="$WTB_ALL"; JAVA="$JAVA_ALL"
# Rust Stream: the paper's traditional-tracer recipe, seq and rayon (4000x4000 image, 100+100 iterations, rayon with
# 4 threads) on the one timed core, whole-process wall clock
RUSTC="seq-paper rayon-paper"
# explicit per-suite cell lists override the set (space-separated): TRAD_POLY TRAD_PYPERF TRAD_RUST TRAD_NODE TRAD_JAVA
POLY="${TRAD_POLY:-$POLY}"; PYB="${TRAD_PYPERF:-$PYB}"; RUSTC="${TRAD_RUST:-$RUSTC}"; NODE="${TRAD_NODE:-$NODE}"; JAVA="${TRAD_JAVA:-$JAVA}"
cells_of() { case "$1" in poly) echo "$POLY";; pyperf) echo "$PYB";; rust) echo "$RUSTC";; mc) echo memslap;;
                          node) echo "$NODE";; java) echo "$JAVA";; esac; }
export FIG5_POLY="$POLY" FIG5_RUST="$RUSTC" FIG5_PYPERF_LIST="$PY_LIST" FIG5_PYRUN="$PYD/pyperf/run_one_full.py" \
       FIG5_PYLOOPS="" FIG5_PYPATH_VAN="$PYD/wp/extmods:$PYD/wp/site"
export PTJ_JAVA_BENCH="$(echo $JAVA | tr ' ' ',')"
export PTLOG_DIR=/dev/null
# The JIT suites with the same V8 / JVM flags as in run/fig5.sh; Node.js 1 untimed + 2 timed iterations in the process,
# Java 3 iterations with the first dropped (run/lib/trad_par.py passes the Java and pyperformance counts).
export PTJ_NODE_V8FLAGS="${PTJ_NODE_V8FLAGS:---no-short-builtin-calls --max-semi-space-size=128 --single-threaded-gc}" \
       WTB_WARMUP=1 WTB_ITERS=2 WTB_DRAIN_MS=0 WTB_JS="$ROOT/run/lib/jitwp/wtb.js" \
       PTJ_JAVA_XOPTS="${JAVA_XFLAGS:--XX:UseAVX=2}" PTJ_JAVA_ITERS_ALL=1 F5_WARMPROC=0

echo "== Figure 5, traditional tracers: $WHICH; suites $SUITES; every benchmark; $REPS repetition(s); each run stopped at 200x =="
echo "Rows -> $CSV"
quiet_machine_hints
[ "$DRY" = 1 ] || progress_figure

# rows for cells a tracer cannot run at all: `row <suite> <cell> <config> <rc> <cap>'
row() {
    "$PYBIN" - "$CSV" "$@" <<'PY'
import csv, os, sys, time
path, suite, cell, cfg, rc, cap = sys.argv[1:7]
cols = ["suite", "cell", "config", "rep", "ktime", "wall", "rc", "ck", "mutex", "load0", "load1", "label", "ts", "tps",
        "gate", "cap", "acalls", "pt_lost_bytes", "pt_trunc", "pt_aux_bytes", "pt_peak_fill", "topa", "iters",
        "inherited", "srv_ucpu"]
new = not os.path.exists(path)
if not new:
    for r in csv.DictReader(open(path)):
        if (r["suite"], r["cell"], r["config"]) == (suite, cell, cfg):
            sys.exit(0)
with open(path, "a", newline="") as f:
    w = csv.DictWriter(f, fieldnames=cols, restval="")
    if new:
        w.writeheader()
    w.writerow(dict(suite=suite, cell=cell, config=cfg, rep=1, rc=rc, cap=cap, label=cfg, ts=int(time.time())))
PY
}
missing() { grep -q "^$1 " "$ABSENT" 2>/dev/null; }

spindle_time() {  # <suite> <cell> <vanilla exe> <spindle exe> [args...]: one row pair per repetition
    "$PYBIN" - "$CSV" "${TIMED_CORE:-5}" "$REPS" "$@" <<'PY'
import csv, os, re, signal, subprocess, sys, time
path, core, reps, suite, k, van_exe, str_exe = sys.argv[1], sys.argv[2], int(sys.argv[3]), *sys.argv[4:8]
args = sys.argv[8:]
cols = ["suite", "cell", "config", "rep", "ktime", "wall", "rc", "ck", "mutex", "load0", "load1", "label", "ts", "tps",
        "gate", "cap", "acalls", "pt_lost_bytes", "pt_trunc", "pt_aux_bytes", "pt_peak_fill", "topa", "iters",
        "inherited", "srv_ucpu"]
env = dict(os.environ, STRACE_OUT="/dev/null")
if suite == "pyperf":          # the step's pyperformance workload: 1 untimed + 2 timed calls
    env.update(PPF_LOOPS="2", PPF_WARM="1", PYTHONPATH=os.environ["FIG5_PYPATH_VAN"])
def one(exe, timeout):
    t0 = time.monotonic()
    p = subprocess.Popen(["taskset", "-c", core, exe] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         env=env, start_new_session=True)
    try:
        out, err = p.communicate(timeout=timeout)
        hit = False
    except subprocess.TimeoutExpired:
        hit = True
    wall = time.monotonic() - t0
    while True:                # the run's whole session (children included) is gone before the next run
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            break
        time.sleep(0.1)
    if hit:
        p.communicate()
        return dict(rc=-9, wall=None, ktime=None, cap="cap")
    p.stdout, p.stderr = out, err
    if suite == "pyperf":      # the harness's KTIME (timed loops, after the warm-up), as in run/lib/repslice.py
        v = re.findall(r"KTIME ([0-9.]+)", p.stderr)
    else:                      # PolyBench's POLYBENCH_TIME kernel timer
        v = re.findall(r"^\s*([0-9]+\.[0-9]+)\s*$", p.stdout, re.M)
    return dict(rc=p.returncode, wall=wall, ktime=float(v[-1]) if v else None, cap="")
new = not os.path.exists(path)
with open(path, "a", newline="") as f:
    w = csv.DictWriter(f, fieldnames=cols, restval="")
    if new:
        w.writeheader()
    for r in range(1, reps + 1):
        van = one(van_exe, 3600)
        if van["rc"] != 0 or van["wall"] is None:       # no reference: no budget for the paper's rule
            st = dict(rc=-3, wall=None, ktime=None, cap="no-reference")
        else:
            st = one(str_exe, 200 * van["wall"])        # stopped at 200x the reference wall time = counted 200x
        for cfg, res in (("spindle_vanilla", van), ("spindle", st)):
            w.writerow(dict(suite=suite, cell=k, config=cfg, rep=r, rc=res["rc"], cap=res["cap"], label="spindle",
                            ktime="" if res["ktime"] is None else "%.6f" % res["ktime"],
                            wall="" if res["wall"] is None else "%.3f" % res["wall"], ts=int(time.time())))
        print("   spindle %-16s rep%d vanilla %s s  spindle %s s  rc=%s %s" % (k, r, van["ktime"], st["ktime"], st["rc"], st["cap"]))
PY
}

spindle() {  # PolyBench/C: each kernel built twice with clang-18 -O2 through bitcode (plain / through the pass);
             # pyperformance: CPython 3.12.13 built twice with clang-18 -O2 (run/lib/build_spindle_cpython.sh)
    local S="$TP/spindle-plus" PB="$ROOT/suites/polybench/polybench-c" W="$RUN_OUT/spindle" su k
    for su in ${SUITES//,/ }; do
        case "$su" in poly|pyperf) continue;; esac
        for c in $(cells_of "$su"); do row "$su" "$c" spindle -2 na; done
    done
    case ",$SUITES," in *,pyperf,*)
        local PYS="$S/cpython/bin"
        if ! missing spindle && ! missing spindle_py && [ ! -x "$PYS/python3.12s" ]; then
            echo "-- spindle: building CPython through the pass (run/lib/build_spindle_cpython.sh)"
            bash run/lib/build_spindle_cpython.sh || echo "spindle_py CPython build failed" >> "$ABSENT"
        fi
        if missing spindle || missing spindle_py || [ ! -x "$PYS/python3.12s" ]; then
            for b in $PYB; do row pyperf "$b" spindle 127 tool-missing; done; echo "-- spindle/pyperf: CPython not built -> error rows"
        else
            for b in $PYB; do
                grep -q "^pyperf,$b,spindle," "$CSV" "$MAIN_CSV" 2>/dev/null && continue
                spindle_time pyperf "$b" "$PYS/python3.12" "$PYS/python3.12s" "$FIG5_PYRUN" "$b"
            done
        fi ;;
    esac
    case ",$SUITES," in *,poly,*) ;; *) return 0;; esac
    if missing spindle; then for k in $POLY; do row poly "$k" spindle 127 tool-missing; done; echo "-- spindle: not built -> error rows"; return 0; fi
    mkdir -p "$W"
    for k in $POLY; do
        if grep -q "^poly,$k,spindle," "$CSV" "$MAIN_CSV" 2>/dev/null; then continue; fi
        local src d F
        src=$(find "$PB" -name "$k.c" | head -1); d="$W/$k"; mkdir -p "$d"
        F="-O2 -I $PB/utilities -I $(dirname "$src") -DPOLYBENCH_USE_C99_PROTO -DLARGE_DATASET -DPOLYBENCH_TIME"
        if ! { clang-18 $F -emit-llvm -c "$PB/utilities/polybench.c" -o "$d/pb.bc" && clang-18 $F -emit-llvm -c "$src" -o "$d/k.bc" \
               && llvm-link-18 "$d/pb.bc" "$d/k.bc" -o "$d/whole.bc" && clang-18 -O2 "$d/whole.bc" -lm -o "$d/van" \
               && opt-18 -load-pass-plugin="$S/pass/libSTracerPlusPass.so" -passes=stracerplus "$d/whole.bc" -o "$d/instr.bc" \
               && clang-18 -O2 "$d/instr.bc" "$S/stracer_lib_mt.c" -lm -pthread -o "$d/str"; } > "$d/build.log" 2>&1; then
            row poly "$k" spindle 1 build-failed; echo "   spindle $k: build failed ($d/build.log)"; continue
        fi
        spindle_time poly "$k" "$d/van" "$d/str"
    done
}

DYN=""; SP=0
for w in $WHICH; do case "$w" in
    memtrace|libdft|valgrind) DYN="$DYN${DYN:+,}$w" ;; spindle) SP=1 ;;
    *) echo "unknown tracer: $w (memtrace libdft valgrind spindle)" ;; esac; done
CELLS=""; for su in ${SUITES//,/ }; do CELLS="$CELLS${CELLS:+;}$su=$(echo $(cells_of "$su") | tr ' ' ',')"; done
PAR=(--csv "$CSV" --cells "$CELLS" --reps "$REPS" --ceiling "${TRAD_CEILING:-0}" --singles "${TRAD_CORES:-4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19}")
[ -z "${TRAD_WIDE:-}" ] || PAR+=(--wide "$TRAD_WIDE")
read -r -a SPC <<< "${TRAD_SPINDLE_CORES:-2 3}"
if [ "$DRY" = 1 ]; then
    echo "  cells: $CELLS"
    [ -z "$DYN" ] || "$PYBIN" run/lib/trad_par.py --tracers "$DYN" "${PAR[@]}" --dry-run
    [ "$SP" = 0 ] || echo "  spindle-plus: PolyBench/C on core ${SPC[0]}, pyperformance on core ${SPC[1]:-${SPC[0]}}, alongside"
    exit 0
fi
SPIDS=()
if [ "$SP" = 1 ]; then
    echo "-- spindle-plus: PolyBench/C (core ${SPC[0]}) and pyperformance (core ${SPC[1]:-${SPC[0]}}), other suites NA; logs $RUN_OUT/spindle.*.log"
    i=0
    for su in poly pyperf; do
        case ",$SUITES," in *,$su,*) ;; *) continue ;; esac
        ( CSV="${CSV%.csv}.tsp$su.csv"; SUITES=$su; TIMED_CORE="${SPC[$i]:-${SPC[0]}}" spindle ) > "$RUN_OUT/spindle.$su.log" 2>&1 &
        SPIDS+=($!); i=$((i + 1))
    done
    ( SUITES="$(echo ",$SUITES," | sed 's/,poly,/,/; s/,pyperf,/,/; s/^,//; s/,$//')"; [ -z "$SUITES" ] || spindle )
fi
if [ -n "$DYN" ]; then
    for w in ${DYN//,/ }; do   # a tracer that is not built: error rows, no lane time
        if missing "$w"; then
            for su in ${SUITES//,/ }; do for c in $(cells_of "$su"); do row "$su" "$c" "$w" 127 tool-missing; done; done
            echo "-- $w: tracer not built -> error rows"
        fi
    done
    "$PYBIN" run/lib/trad_par.py --tracers "$DYN" "${PAR[@]}" || echo "WARNING: trad_par exited $? (rows so far kept)"
fi
for p in "${SPIDS[@]}"; do wait "$p" || echo "WARNING: spindle-plus exited non-zero (see $RUN_OUT/spindle.*.log)"; done
ls "${CSV%.csv}".t*.csv > /dev/null 2>&1 && "$PYBIN" run/lib/merge_csv.py "$CSV" "${CSV%.csv}".t*.csv
echo "== traditional tracers done: $CSV =="
progress_stop
"$PYBIN" figures/six_suite_overhead.py --csv "$RUN_OUT/overhead.csv" --baselines "$CSV" --outdir "$RUN_OUT" \
    && echo "== Figure 5 with the traditional tracers: $RUN_OUT/six_suite_overhead.png =="
