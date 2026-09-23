#!/usr/bin/env bash
# Shared helpers for the run/ scripts. Source this: `. "$(dirname "$0")/lib/common.sh"`.
# It resolves the repo root, picks a Python, and provides warn-only environment checks so
# the scripts run on an ordinary machine (a bare-metal re-measure just degrades or warns).

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export ROOT

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
