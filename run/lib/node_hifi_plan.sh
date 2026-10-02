#!/usr/bin/env bash
# Node.js HiFi, whole-process plan: the HiFi specs of node and the seven native images every Web Tooling process maps
# (libc, libm, libstdc++, libgcc_s, libdl, libpthread, the loader), resync sites dropped (Pin needs none), flattened by
# mkplan.py into one PLAN.  JIT-generated code gets its sites at run time from the V8 hook (Pin bridge), not from here.
#
#   bash run/lib/node_hifi_plan.sh        -> $RUN_OUT/build/plans/node_hifi.plan   (prints the path last)
#
# Idempotent (an existing plan is kept).  node itself dominates the analysis time; NODE_HIFI_JOBS
# overrides the analyzer's parallelism (default: all cpus).
set -eu
. "$(dirname "$0")/common.sh"
cd "$ROOT"
pick_python
pick_out
P="$RUN_OUT/build/plans/node_hifi.plan"
if [ -s "$P" ]; then echo "$P"; exit 0; fi
W="$RUN_OUT/build/node_hifi"; mkdir -p "$W/specs" "$W/plan_specs" "$W/cache" "$(dirname "$P")"
L=/lib/x86_64-linux-gnu
IMGS="$ROOT/suites/node/bin/node $L/libc.so.6 $L/libm.so.6 $L/libstdc++.so.6 $L/libgcc_s.so.1 $L/libdl.so.2 $L/libpthread.so.0 /lib64/ld-linux-x86-64.so.2"
J="${NODE_HIFI_JOBS:-$(nproc)}"
PL=()
for img in $IMGS; do
    n=$(basename "$img"); s=$(date +%s)
    [ -s "$W/specs/$n.spec.json" ] || \
        "$PYBIN" ptracer/static/analyze.py "$img" -o "$W/specs/$n.spec.json" --mode hifi --keyframe 1024 \
            --jobs "$J" --cache "$W/cache" > "$W/$n.analyze.log" 2>&1 \
        || { echo "FATAL: HiFi analysis of $img failed (see $W/$n.analyze.log)" >&2; exit 2; }
    echo "  node HiFi spec: $n ($(( $(date +%s) - s )) s)" >&2
    "$PYBIN" - "$W/specs/$n.spec.json" "$W/plan_specs/$n.spec.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
d["sites"] = [s for s in d["sites"] if not s.get("resync")]
json.dump(d, open(sys.argv[2], "w"))
PY
    PL+=("$img=$W/plan_specs/$n.spec.json")
done
"$PYBIN" ptracer/runtime/pinjit/mkplan.py -o "$P.tmp" "${PL[@]}" >&2
mv "$P.tmp" "$P"
echo "$P"
