#!/usr/bin/env bash
# Run a command inside the artifact container.
#   docker/run.sh run/fig5.sh           # a run/ script (and its arguments)
#   docker/run.sh                       # interactive shell
# Results land in $OUT (default: ./out next to this repo), mounted at /artifact/out.
#
# Minimum: --privileged (Intel PT capture via perf_event_open needs CAP_SYS_ADMIN; this also
# bypasses the host's perf_event_paranoid) and an unlimited memlock for the PT ring buffers.
# Nothing else on the host is changed.
#
# Java HiFi (run/fig5.sh, run/01_overhead.sh): kernel.yama.ptrace_scope is set to 0 for the run with sudo and restored
# on exit (see below).
#
# QUIET=1 (optional, needs root or sudo): disables turbo for the duration of the run and puts
# the previous value back on exit.  Without it overhead MAGNITUDES vary more run to run
# (frequency scaling); orderings and shapes do not depend on it.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${IMAGE:-ptracer-artifact}"
OUT="${OUT:-$ROOT/out}"
mkdir -p "$OUT"

SUDO=""; [ "$(id -u)" != 0 ] && SUDO=sudo
RESTORE=()
trap 'for c in "${RESTORE[@]}"; do eval "$c"; done' EXIT

NO_TURBO=/sys/devices/system/cpu/intel_pstate/no_turbo
if [ "${QUIET:-0}" = 1 ]; then
  if [ -f "$NO_TURBO" ]; then
    PREV=$(cat "$NO_TURBO")
    if echo 1 | $SUDO tee "$NO_TURBO" >/dev/null; then
      echo "QUIET=1: no_turbo=1 (was $PREV; restored on exit)"
      RESTORE+=("echo $PREV | $SUDO tee $NO_TURBO >/dev/null && echo 'restored no_turbo=$PREV'")
    else
      echo "WARN: could not write $NO_TURBO; turbo stays on (magnitudes will vary more)"
    fi
  else
    echo "WARN: $NO_TURBO not present; turbo not locked (magnitudes will vary more)"
  fi
else
  echo "note: turbo not locked (set QUIET=1 to lock it for this run; needs sudo)"
fi

# The Java HiFi bars run the JVM under Pin, which must ptrace-attach to the child processes the JVM starts:
# kernel.yama.ptrace_scope must be 0 for the run (set here with sudo, the previous value restored on exit).
PTS_F=/proc/sys/kernel/yama/ptrace_scope
case "${1:-}" in run/fig5.sh|run/01_overhead.sh)
  if [ "${SKIP_JAVA_HIFI:-0}" != 1 ] && [ "${SKIP_JIT:-0}" != 1 ] && [ -f "$PTS_F" ] && [ "$(cat "$PTS_F")" != 0 ] \
     && [ "${1:-}" != "run/fig5.sh" -o "${2:-}" != "--dry-run" ]; then
    PREV_PTS=$(cat "$PTS_F")
    if $SUDO sysctl -q -w kernel.yama.ptrace_scope=0; then
      echo "kernel.yama.ptrace_scope=0 for this run (was $PREV_PTS; restored on exit)"
      RESTORE+=("$SUDO sysctl -q -w kernel.yama.ptrace_scope=$PREV_PTS && echo 'restored kernel.yama.ptrace_scope=$PREV_PTS'")
    else
      echo "WARN: could not set kernel.yama.ptrace_scope=0; the Java HiFi bars will be missing (SKIP_JAVA_HIFI=1 silences this)"
    fi
  fi ;;
esac

# Experiment knobs set on the host are passed into the container (see run/01_overhead.sh).
ENVS=()
for v in REPS JIT_REPS JREPS TIMED_LANES HELPER_LANES TIMED_CORE HELPER_CORES RUST_CORES MC_SRV_CORES MC_CLI_CORES \
         POLY_CELLS RUST_CELLS PYPERF_LIST NONJIT_CONFIGS SKIP_JIT SKIP_POLY SKIP_PYPERF SKIP_RUST SKIP_MC WP_TARGETS JOBS \
         SKIP_NODE_HIFI SKIP_JAVA_HIFI JAVA_HIFI_CONFIGS JAVA_HIFI_CELLS JAVA_HIFI_ITERS JAVA_HIFI_DROP JAVA_HIFI_TIMEOUT JAVA_HIFI_JOBS JAVA_DRAIN_MS JAVA_LANE_HELPER HIFI_PTW_ALL HIFI_PTW_BUDGET NODE_HIFI_CONFIGS NODE_HIFI_CELLS NODE_HIFI_TIMEOUT NODE_HIFI_JOBS NODE_JIT_CORES JAVA_JIT_CORES \
         JIT_FAST_CONFIGS JIT_FAST_NODE_CELLS JIT_FAST_JAVA_CELLS JAVA_LANES JAVA_LANE_CORES JAVA_LANE_JIT PTW_JOBS TRAD_REPS TRAD_SUITES TRAD_POLY TRAD_PYPERF TRAD_RUST TRAD_NODE TRAD_JAVA TRAD_CEILING BASELINES_CSV \
         NODE_WARM_SLOTS JAVA_WARM_SLOTS JIT_WARM_PASS PTW_REMEASURE_FULL PTW_REMEASURE_REPS PTW_REMEASURE_POLY TRAD_CORES TRAD_WIDE TRAD_SPINDLE_CORES QUICK PROGRESS_SECS; do
  [ -n "${!v:-}" ] && ENVS+=(-e "$v=${!v}")
done
TTY=""; [ -t 0 ] && TTY="-it"
docker run --rm $TTY --privileged \
  --ulimit memlock=-1:-1 \
  -v "$OUT:/artifact/out" -e OUT=/artifact/out "${ENVS[@]}" \
  "$IMAGE" "$@"
