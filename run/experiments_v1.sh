#!/usr/bin/env bash
# Entry point, version 1: everything a reviewer runs, in one go, into one directory.
#
#   bash docker/run.sh run/experiments_v1.sh            # inside the container (recommended)
#   bash run/experiments_v1.sh [--dry-run]              # bare metal with the same dependencies
#
# Produces exactly three things to read, under out/experiments_v1/ (host side):
#   six_suite_overhead.png   Figure 5  (runtime overhead)         run/01_overhead.sh
#   ablation.png             Figure 6  (ablation of the new techniques)  run/03_ablation.sh
#   inaccuracy.md            Section 5.6 (inaccuracy table)       run/02_accuracy.sh
#
# Idempotent: finished experiments are skipped, an interrupted one resumes from its rows.
# A later version of the artifact adds run/experiments_v2.sh next to this file; the
# per-experiment scripts it calls are frozen once shipped.
set -eu
. "$(dirname "$0")/lib/common.sh"
cd "$ROOT"
pick_python
pick_out
DRY=0; [ "${1:-}" = "--dry-run" ] && DRY=1
[ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ] && { sed -n 2,14p "$0"; exit 0; }

cat <<EOF
== PTracer artifact, experiments v1 ==
Output directory: $RUN_OUT
Estimated total time: 4-8 compute-h on a quiet machine, ~15 human-min in all.
  E1 runtime overhead (Figure 5)      2-4 compute-h + ~1 compute-h to build the Fast images
  E2 inaccuracy table (Section 5.6)   see run/02_accuracy.sh --dry-run
  E3 ablation (Figure 6)              ~30 compute-min
EOF

echo "-- environment --"
ok=1
[ -d /sys/bus/event_source/devices/intel_pt ] && echo "  intel_pt: present" || { echo "  intel_pt: MISSING (an Intel CPU with Intel PT and a kernel with perf support is required)"; ok=0; }
grep -q ptwrite /proc/cpuinfo && echo "  ptwrite: present" || echo "  ptwrite: absent (not needed: both modes use the software-buffer sink)"
"$PYBIN" -c 'import pyvex, matplotlib, numpy' 2>/dev/null && echo "  python: pyvex, matplotlib, numpy ok" || { echo "  python: pyvex/matplotlib/numpy missing in $PYBIN"; ok=0; }
[ -x "$ROOT/third_party/pin-4.4/pin" ] && echo "  Pin 4.4 kit: present" || { echo "  Pin 4.4 kit: missing under third_party/"; ok=0; }
free_gb=$(df -BG --output=avail "$RUN_OUT" | tail -1 | tr -dc 0-9)
[ "${free_gb:-0}" -ge 60 ] && echo "  disk: ${free_gb} GB free" || echo "  disk: only ${free_gb:-?} GB free under $RUN_OUT (E2 needs tens of GB)"
quiet_machine_hints
[ "$ok" = 1 ] || { echo "FATAL: environment check failed"; exit 2; }

echo "-- build --"
if [ -x ptracer/offline/ptrecon ] && [ -x ptracer/offline/pt_capture2 ] && [ -f ptracer/runtime/pinjit/obj-intel64/hifitool.so ] \
   && [ -f ptracer/runtime/e9plugin/ptlog.so ] && [ -f ptracer/runtime/jit/jithook.node ] && [ -f ptracer/runtime/jit/java/ptjava.so ]; then
    echo "  already built"
elif [ "$DRY" = 1 ]; then
    echo "  would run: bash ptracer/build.sh"
else
    bash ptracer/build.sh
fi

stage() {  # <label> <final output> <script>
    if [ -s "$RUN_OUT/$2" ]; then echo "-- $1: done ($RUN_OUT/$2)"; return 0; fi
    if [ "$DRY" = 1 ]; then echo "-- $1: would run $3"; return 0; fi
    bash "$3"
}
stage "E1 Figure 5"    six_suite_overhead.png run/01_overhead.sh
stage "E2 Section 5.6" accuracy/inaccuracy.md run/02_accuracy.sh
stage "E3 Figure 6"    ablation.png           run/03_ablation.sh
[ "$DRY" = 1 ] && exit 0

cat <<EOF

== experiments v1 complete ==
  Figure 5 (runtime overhead):                 $RUN_OUT/six_suite_overhead.png
  Figure 6 (ablation of the new techniques):   $RUN_OUT/ablation.png
  Section 5.6 (inaccuracy table):              $RUN_OUT/accuracy/inaccuracy.md
EOF
cat "$RUN_OUT/accuracy/inaccuracy.md"
