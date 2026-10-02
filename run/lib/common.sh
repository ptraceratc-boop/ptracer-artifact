#!/usr/bin/env bash
# Shared helpers for the run/ scripts. Source this: `. "$(dirname "$0")/lib/common.sh"`.
# It resolves the repo root, picks a Python, and provides warn-only environment checks so
# the scripts run on an ordinary machine (a bare-metal re-measure just degrades or warns).

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export ROOT

# How a Fast image's sync markers carry their count (rewrite.py --sync-carrier): `tnt' (default) = the TNT bits of
# a short branch loop, so no non-PTWRITE configuration executes PTWRITE; `ptwrite' = one PTWRITE per marker (needs
# a CPU with PTWRITE and a separate RUN_OUT, since images are built once).  Only the *-PTWRITE bars need PTWRITE.
PT_SYNC_CARRIER="${PT_SYNC_CARRIER:-tnt}"
export PT_SYNC_CARRIER
# pt_capture2's PTW switch for a capture of a Fast (non-PTWRITE) image: PTW packets only with the `ptwrite' carrier.
ptw_flag() { [ "$PT_SYNC_CARRIER" = ptwrite ] && echo --ptw || echo --no-ptw; }
carrier_args() {  # [rewrite.py flags]: --sync-carrier for a buffer build with sync markers (none for --sync 0)
    case " $* " in *" --sync 0 "*) ;; *) echo "--sync-carrier $PT_SYNC_CARRIER" ;; esac
}

# Fast / Fast-PTWRITE defaults: keyframe period of the rewritten images and the JIT-code hooks, and the number of
# filler instructions between two PTWRITEs of one trampoline.  A build tree made with other defaults (or by an earlier
# version of the analyzer) is removed and rebuilt (stale_build).
FAST_KEYFRAME="${FAST_KEYFRAME:-128}"; PTW_SPACE="${PTW_SPACE:-12}"
export FAST_KEYFRAME PTW_SPACE
FAST_BUILD_TAG="analyzer-$(sed -n "s/^CACHE_VERSION = '\(.*\)'/\1/p" "$ROOT/ptracer/static/analyze.py") kf$FAST_KEYFRAME space$PTW_SPACE"
stale_build() {  # <dir>...: remove a tree built with other Fast defaults, then tag it with the current ones
    local d
    for d in "$@"; do
        if [ -d "$d" ] && [ "$(cat "$d/.build_tag" 2>/dev/null)" != "$FAST_BUILD_TAG" ]; then
            echo "[build] $d was built with other defaults ($(cat "$d/.build_tag" 2>/dev/null || echo 'an earlier version')): removed, rebuilding"
            rm -rf "$d"
        fi
        mkdir -p "$d" && echo "$FAST_BUILD_TAG" > "$d/.build_tag"
    done
}

# QUICK=1 (run/fig5.sh, run/fig5_traditional.sh): a kick-the-tires run -- every benchmark of every suite and every
# bar, 1 repetition, minimal iteration counts, short JIT drains and a short traditional-tracer ceiling, written to
# <OUT>/quick so it never mixes with real results.  Its numbers only show that everything runs.
quick_mode() {
    [ "${QUICK:-0}" = 1 ] || return 0
    RUN_OUT="${RUN_OUT:-${OUT:-$ROOT/out}/quick}"
    export RUN_OUT REPS=1 JIT_REPS=1 JREPS=1 MC_OPS=100000 FIG5_PYLOOPS="" \
           WTB_WARMUP=1 WTB_ITERS=1 WTB_DRAIN_MS=5000 \
           JAVA_HIFI_ITERS=2 JAVA_HIFI_DROP=1 JAVA_ITERS=2 JAVA_DROP=1 JAVA_DRAIN_MS=5000 JAVA_WARM_DRAIN_MS=60000 \
           TRAD_CEILING=60
    echo "QUICK=1: functional check only (1 rep, minimal iterations) -> $RUN_OUT; the numbers are not for comparison"
}

# The figure is redrawn in the background while a run goes on: at the start (every bar slot "pending") and then
# whenever a results CSV changed, at most once a minute (PROGRESS_SECS), at the lowest CPU priority.  progress_stop before the final draw.
progress_figure() {
    local d="$RUN_OUT"
    ( last=""
      while :; do
          cur=$(ls -l --time-style=+%s "$d"/overhead*.csv "$d"/baselines*.csv "$d"/jhifi/*.csv 2>/dev/null | md5sum)
          if [ "$cur" != "$last" ]; then
              nice -n 19 "$PYBIN" "$ROOT/figures/six_suite_overhead.py" --progress --csv "$d/overhead.csv" --outdir "$d" \
                  > /dev/null 2>&1 || true
              last="$cur"
          fi
          sleep "${PROGRESS_SECS:-60}"
      done ) &
    PROGRESS_PID=$!
    trap progress_stop EXIT
}
progress_stop() {
    [ -z "${PROGRESS_PID:-}" ] || { kill "$PROGRESS_PID" 2>/dev/null; wait "$PROGRESS_PID" 2>/dev/null || true; PROGRESS_PID=""; }
    return 0
}

pick_python() {
    if [ -n "${PYBIN:-}" ] && "$PYBIN" -c 'import matplotlib,numpy' 2>/dev/null; then return; fi
    for p in /opt/venv/bin/python "$ROOT/venv/bin/python" python3; do
        if "$p" -c 'import matplotlib,numpy' 2>/dev/null; then PYBIN="$p"; export PYBIN; return; fi
    done
    echo "FATAL: no python with matplotlib+numpy (set PYBIN=/path/to/python)"; exit 2
}

# Every experiment writes into one directory: $RUN_OUT, default <OUT or ./out>/experiments_v1
# (docker/run.sh mounts ./out on the host at /artifact/out).
pick_out() {
    RUN_OUT="${RUN_OUT:-${OUT:-$ROOT/out}/experiments_v1}"
    export RUN_OUT
    mkdir -p "$RUN_OUT"
}

# Optional, warn-only quiet-machine hints for a bare-metal re-measure. Never fails, never
# needs sudo; it only tells the reviewer what would improve measurement stability.
quiet_machine_hints() {
    local pe turbo aslr
    pe=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo '?')
    [ "$pe" = '?' ] || [ "$pe" -le 1 ] 2>/dev/null || \
        echo "  hint: kernel.perf_event_paranoid=$pe (<=1 needed for Intel PT capture)"
    turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo '?')
    [ "$turbo" = '1' ] || echo "  hint: turbo not disabled (no_turbo=1 stabilises timing; optional)"
    aslr=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null || echo '?')
    [ "$aslr" = '0' ] || echo "  hint: ASLR on (randomize_va_space=0 stabilises timing; optional)"
    echo "  hint: measurement runs pin to specific cores (TIMED_CORE, HELPER_CORES); on a"
    echo "        busy machine leave them free or override those variables."
}
