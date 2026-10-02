#!/usr/bin/env bash
# Figure 5 (runtime overhead), the PTracer bars of all six suites, in one go:
#
#   bash docker/run.sh run/fig5.sh [--dry-run]     # then: bash docker/run.sh run/fig5_traditional.sh
#
# = run/01_overhead.sh with the reviewer defaults below (every one can be overridden from the host, e.g.
# REPS=5 JIT_REPS=5 JREPS=3 bash docker/run.sh run/fig5.sh).  Output: out/experiments_v1/six_suite_overhead.png
# (+ .json, overhead.csv).  Idempotent: rows already in overhead.csv are not re-measured, built images are kept;
# after an update, only the rows of configurations whose measurement changed are re-measured (run/lib/row_versions.py).
#
# Defaults (sized for a 96-vCPU machine, ~1-1.5 days from scratch, see the README per-suite table):
#   REPS=3        non-JIT suites (PolyBench 30, pyperformance 94, Rust 6, Memcached), 5 configurations alternated
#   JIT_REPS=2    Node.js (HiFi sweep and Fast / Fast-PTWRITE sweep)
#   JREPS=1       Java (HiFi sweep and Fast / Fast-PTWRITE sweep, 2 lanes: JAVA_LANES / JAVA_LANE_CORES / JAVA_LANE_JIT)
#   TIMED_LANES / HELPER_LANES  8 lanes on cores 1,4,..,22 (timed) and 2,5,..,23 (helper) for PolyBench/pyperformance
set -eu
. "$(dirname "$0")/lib/common.sh"
cd "$ROOT"
quick_mode
export REPS="${REPS:-3}" JIT_REPS="${JIT_REPS:-2}" JREPS="${JREPS:-1}"
export TIMED_LANES="${TIMED_LANES:-1 4 7 10 13 16 19 22}" HELPER_LANES="${HELPER_LANES:-2 5 8 11 14 17 20 23}"
export NONJIT_CONFIGS="${NONJIT_CONFIGS:-vanilla,pinhifi,pinhifi_ptw,fast,fast_ptw}"
export PYPERF_LIST="${PYPERF_LIST:-$ROOT/suites/pyperformance/pyperf/bench94.lst}"
if [ "${1:-}" = "--dry-run" ]; then
    cat <<EOF
== Figure 5 (PTracer bars), plan ==
  builds: whole-process Fast images (+ Fast-PTWRITE variants), Node.js/Java HiFi plans, Node.js/Java whole-program Fast images
  non-JIT: poly/pyperf (lanes: timed ${TIMED_LANES}; helpers ${HELPER_LANES}), rust, mc x {${NONJIT_CONFIGS}} x ${REPS} reps
  Node.js: vanilla,pinhifi,pinhifi_ptw x ${JIT_REPS} reps; vanilla/e9fast/e9fast_ptw x ${JIT_REPS} reps (+ 1 untimed warm-up)
  Java:    vanilla,pinhifi,pinhifi_ptw x ${JREPS} reps (2 lanes sharing a work queue; needs kernel.yama.ptrace_scope=0);
           vanilla/e9fast/e9fast_ptw x ${JREPS} reps (+ 1 untimed warm-up)
  estimated: ~1-1.5 days from scratch on a 96-vCPU machine; then run/fig5_traditional.sh for the other tracers
EOF
    pick_python; pick_out
    echo "  results: $RUN_OUT/overhead.csv (rows already there are kept, the missing ones are measured)"
    "$PYBIN" run/lib/row_versions.py prune "$RUN_OUT" --dry-run
    if [ "${PTW_REMEASURE_FULL:-0}" != 1 ] && "$PYBIN" -c "import sys; sys.path.insert(0, 'run/lib'); import row_versions as v, contextlib, io; f = io.StringIO(); c = contextlib.redirect_stdout(f); c.__enter__(); d = v.prune(sys.argv[1], dry=True); c.__exit__(None, None, None); sys.exit(0 if any(k[1] == 'pinhifi_ptw' for k in d) else 1)" "$RUN_OUT"; then
        echo "  HiFi-PTWRITE re-measure (reduced; PTW_REMEASURE_FULL=1 = full): ${PTW_REMEASURE_REPS:-1} rep; PolyBench ${PTW_REMEASURE_POLY:-2mm atax correlation gemm jacobi-2d lu seidel-2d trmm};"
        echo "    Rust 6 runtimes; Memcached; pyperformance and Node.js all benchmarks; Java 1 rep (the other bars are not re-run)"
    fi
    exit 0
fi
exec bash run/01_overhead.sh
