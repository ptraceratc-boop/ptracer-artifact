#!/usr/bin/env bash
# Run a command inside the artifact container.
#   docker/run.sh run/experiments_v1.sh  # a run/ script (and its arguments)
#   docker/run.sh                       # interactive shell
# Results land in $OUT (default: ./out next to this repo), mounted at /artifact/out.
#
# Minimum: --privileged (Intel PT capture via perf_event_open needs CAP_SYS_ADMIN; this also
# bypasses the host's perf_event_paranoid) and an unlimited memlock for the PT ring buffers.
# Nothing on the host is changed.
#
# QUIET=1 (optional, needs root or sudo): disables turbo for the duration of the run and puts
# the previous value back on exit.  Without it overhead MAGNITUDES vary more run to run
# (frequency scaling); orderings and shapes do not depend on it.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${IMAGE:-ptracer-artifact}"
OUT="${OUT:-$ROOT/out}"
mkdir -p "$OUT"

NO_TURBO=/sys/devices/system/cpu/intel_pstate/no_turbo
if [ "${QUIET:-0}" = 1 ]; then
  SUDO=""; [ "$(id -u)" != 0 ] && SUDO=sudo
  if [ -f "$NO_TURBO" ]; then
    PREV=$(cat "$NO_TURBO")
    if echo 1 | $SUDO tee "$NO_TURBO" >/dev/null; then
      echo "QUIET=1: no_turbo=1 (was $PREV; restored on exit)"
      trap 'echo "$PREV" | $SUDO tee "$NO_TURBO" >/dev/null && echo "restored no_turbo=$PREV"' EXIT
    else
      echo "WARN: could not write $NO_TURBO; turbo stays on (magnitudes will vary more)"
    fi
  else
    echo "WARN: $NO_TURBO not present; turbo not locked (magnitudes will vary more)"
  fi
else
  echo "note: turbo not locked (set QUIET=1 to lock it for this run; needs sudo)"
fi

TTY=""; [ -t 0 ] && TTY="-it"
docker run --rm $TTY --privileged \
  --ulimit memlock=-1:-1 \
  -v "$OUT:/artifact/out" -e OUT=/artifact/out \
  "$IMAGE" "$@"
