#!/usr/bin/env bash
# Java HiFi, whole-process plan: the HiFi specs of every native image a Renaissance JVM maps (libjvm and the JDK
# libraries, libc/libm/libstdc++/libgcc_s/librt, the loader, and netty's epoll library that the finagle-* benchmarks
# load), resync sites dropped (Pin needs none), flattened by mkplan.py into one PLAN.  JIT-generated code gets its
# sites at run time from the JVMTI agent (Pin bridge), not from here.
#
#   bash run/lib/java_hifi_plan.sh        -> $RUN_OUT/build/plans/java_hifi.plan   (prints the path last)
#
# netty: its epoll library is extracted once from renaissance.jar into $RUN_OUT/build/java_hifi/netty; every arm of
# the Java HiFi sweep gets -Djava.library.path=<that directory>, so the JVM maps it at a fixed path the plan names
# (otherwise netty copies it to a random temporary name).
# Idempotent (an existing plan is kept).  libjvm dominates the analysis time; JAVA_HIFI_JOBS overrides the
# analyzer's parallelism (default: all cpus).
set -eu
. "$(dirname "$0")/common.sh"
cd "$ROOT"
pick_python
pick_out
P="$RUN_OUT/build/plans/java_hifi.plan"
W="$RUN_OUT/build/java_hifi"; NETTY="$W/netty"
mkdir -p "$W/specs" "$W/plan_specs" "$W/cache" "$NETTY" "$(dirname "$P")"
NS=libnetty_transport_native_epoll_x86_64.so
if [ ! -s "$NETTY/$NS" ]; then
    "$PYBIN" - "$ROOT/suites/java/renaissance/renaissance.jar" "$NETTY/$NS" <<'PY'
import io, sys, zipfile
outer = zipfile.ZipFile(sys.argv[1])
inner = [n for n in outer.namelist() if "netty-transport-native-epoll" in n and n.endswith("linux-x86_64.jar")]
if not inner: sys.exit("netty epoll jar not found in renaissance.jar")
z = zipfile.ZipFile(io.BytesIO(outer.read(inner[0])))
so = [n for n in z.namelist() if n.endswith("/libnetty_transport_native_epoll_x86_64.so")]
open(sys.argv[2], "wb").write(z.read(so[0]))
PY
fi
if [ -s "$P" ]; then echo "$P"; exit 0; fi
L=/lib/x86_64-linux-gnu; J="$ROOT/suites/java/jdk17/lib"
IMGS="$L/libc.so.6 $L/libm.so.6 $L/libstdc++.so.6 $L/libgcc_s.so.1 $L/librt.so.1 /lib64/ld-linux-x86-64.so.2
      $J/server/libjvm.so $J/libjava.so $J/libjli.so $J/libzip.so $J/libnio.so $J/libnet.so $J/libjimage.so
      $J/libverify.so $J/libmanagement.so $J/libextnet.so $J/libjaas.so $J/libmanagement_ext.so $J/libjsvml.so
      $NETTY/$NS"
JOBS="${JAVA_HIFI_JOBS:-$(nproc)}"
PL=()
for img in $IMGS; do
    n=$(basename "$img"); s=$(date +%s)
    [ -s "$W/specs/$n.spec.json" ] || \
        "$PYBIN" ptracer/static/analyze.py "$img" -o "$W/specs/$n.spec.json" --mode hifi --keyframe 1024 \
            --jobs "$JOBS" --cache "$W/cache" > "$W/$n.analyze.log" 2>&1 \
        || { echo "FATAL: HiFi analysis of $img failed (see $W/$n.analyze.log)" >&2; exit 2; }
    echo "  java HiFi spec: $n ($(( $(date +%s) - s )) s)" >&2
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
