#!/usr/bin/env bash
# build_stage.sh -- build and stage the whole-program Fast ("e9fast") images of the JIT suites.
#
#   bash run/lib/jitwp/build_stage.sh [java] [node]        (default: both)
#
# Groups (exact flags of the reference build; every image with --sync 4096, --sink buffer, --keyframe $FAST_KEYFRAME (128), --kf-gs):
#   common  libc.so.6 libm.so.6 libstdc++.so.6 libgcc_s.so.1                  RWARGS="--reserve-cursor"
#   java    libjvm.so + libjava libjli libzip libnio libnet libjimage libverify libmanagement libextnet libjaas
#           libmanagement_ext libjsvml (13 images)   RWARGS="--reserve-cursor --dead-regs off", AVOID_ROUNDS=16
#   node    the node binary                           RWARGS="--reserve-cursor --dead-regs off", AVOID_ROUNDS=20
# java and node start their keyframe-counter bases at common's PTLOG_KF_N (KF_BASE_START).  Not rewritten: the
# runtime hooks jithook.node / ptjava.so (tool code), and netty's epoll .so that the finagle-* benchmarks extract
# from the Renaissance jar at run time (loaded from a temporary path; its accesses are not in the trace).
# Every avoid list starts from data/avoid/jit/<group>/ (see INPUTS.sha256 there); never --keep-going.
# Output: $JITWP_DIR (default $RUN_OUT/jitwp): <group>/{specs,build,logs}, stage/ (manifest.json for repslice.py
# --jitwp-stage).  Idempotent: converged images are kept.  Cost with the shipped seeds on the image's own inputs (one
# analysis round each): libjvm (~280 k sites) and node (~640 k sites) ~10-20 min each, built concurrently; the other
# images minutes.  On other inputs the seeds are only a head start (more rounds: up to ~1.5 h for node).
# Env: JITWP_DIR, JOBS / CPUS (analyzer processes / cores), LIBDIR (/lib/x86_64-linux-gnu), JDK_HOME
# (suites/java/jdk17), NODE_IMAGE (suites/node/bin/node), PYBIN (python with angr).
set -u
. "$(dirname "$0")/../common.sh"
cd "$ROOT"
pick_out
HERE="$ROOT/run/lib/jitwp"
pick_python
W="${JITWP_DIR:-$RUN_OUT/jitwp}"; export W
stale_build "$W"
L="${LIBDIR:-/lib/x86_64-linux-gnu}"
JDK="${JDK_HOME:-$ROOT/suites/java/jdk17}"; J="$JDK/lib"; export JDK_HOME="$JDK"
NODE_IMG="${NODE_IMAGE:-$ROOT/suites/node/bin/node}"
TARGETS="${*:-java node}"
mkdir -p "$W/logs"

# ---- preflight: the JIT-suite toolchain (runtime/jit_toolchain: --kf-gs/--kf-base/--reserve-cursor rewriter and its
# runtime, built by ptracer/build.sh) and the tools this recipe stages -----------------------------------------------
TC=ptracer/runtime/jit_toolchain
for f in rewrite.py e9plugin/ptlog.so rt/ptlogrt.e9rt rt/ptlogrt_nofini.e9rt rt/ptlogrt.so rt/ptlogmt.so; do
    [ -f "$TC/$f" ] || { echo "FATAL: $TC/$f missing (bash ptracer/build.sh builds the JIT-suite toolchain)"; exit 3; }
done
( cd "$TC" && sed -n 's/^\([0-9a-f]\{32\}\)  /\1  /p' SOURCES.md5 | md5sum --quiet -c - ) \
    || { echo "FATAL: $TC sources differ from SOURCES.md5"; exit 3; }
[ -f ptracer/runtime/jit/jithook.node ] && [ -f ptracer/runtime/jit/java/ptjava.so ] \
    || { echo "FATAL: JIT hooks not built (bash ptracer/build.sh)"; exit 2; }
case " $TARGETS " in *" java "*)
    [ -f ptracer/runtime/jit/java/ptjdrain/ptjdrain.jar ] || { echo "FATAL: ptjdrain.jar not built (bash ptracer/runtime/jit/java/build.sh)"; exit 2; } ;;
esac
case " $TARGETS " in *" node "*)
    [ -x "$NODE_IMG" ] || { echo "FATAL: no node binary at $NODE_IMG (suites/reassemble.sh)"; exit 2; } ;;
esac

# ---- the inputs against the seeds' reference inputs (a mismatch only costs analysis rounds) ------------------------
inp() { case "$1" in common/*) echo "$L/${1#common/}";; java/libjvm.so) echo "$J/server/libjvm.so";;
                     java/*) echo "$J/${1#java/}";; node/node) echo "$NODE_IMG";; esac; }
while read -r h n; do
    case "$h" in \#*|"") continue;; esac
    f=$(inp "$n"); [ -f "$f" ] || continue
    [ "$(sha256sum < "$(readlink -f "$f")" | cut -d' ' -f1)" = "$h" ] \
        || echo "note: $n differs from the seed's reference input; the build converges from the seed with more rounds"
done < data/avoid/jit/INPUTS.sha256

B="$HERE/build_images.sh"
GROUP=common W="$W" KF_BASE_START=0 RWARGS="--reserve-cursor" bash "$B" \
    libc.so.6="$L/libc.so.6" libm.so.6="$L/libm.so.6" libstdc++.so.6="$(readlink -f "$L/libstdc++.so.6")" \
    libgcc_s.so.1="$L/libgcc_s.so.1" 2>&1 | tee "$W/logs/common.log"
[ "${PIPESTATUS[0]}" = 0 ] || { echo "FATAL: common images incomplete ($W/logs/common.log)"; exit 1; }
N=$("$PYBIN" -c "import json,sys; print(json.load(open(sys.argv[1]))['PTLOG_KF_N'])" "$W/common/build/kfplan.json")
echo "[jitwp] common PTLOG_KF_N=$N (reference build: 13611)"
# java and node are independent (both start at common's PTLOG_KF_N): with both targets they build concurrently, each
# with half of the analyzer processes (the rewrite of libjvm / node is one E9Patch process each).
build_group() {
  case $1 in
  java) GROUP=java W="$W" KF_BASE_START="$N" RWARGS="--reserve-cursor --dead-regs off" AVOID_ROUNDS=16 bash "$B" \
            libjvm.so="$J/server/libjvm.so" libjava.so="$J/libjava.so" libjli.so="$J/libjli.so" libzip.so="$J/libzip.so" \
            libnio.so="$J/libnio.so" libnet.so="$J/libnet.so" libjimage.so="$J/libjimage.so" libverify.so="$J/libverify.so" \
            libmanagement.so="$J/libmanagement.so" libextnet.so="$J/libextnet.so" libjaas.so="$J/libjaas.so" \
            libmanagement_ext.so="$J/libmanagement_ext.so" libjsvml.so="$J/libjsvml.so" > "$W/logs/java.log" 2>&1 ;;
  node) GROUP=node W="$W" KF_BASE_START="$N" RWARGS="--reserve-cursor --dead-regs off" AVOID_ROUNDS=20 bash "$B" \
            node="$(readlink -f "$NODE_IMG")" > "$W/logs/node.log" 2>&1 ;;
  *) echo "usage: $0 [java] [node]"; return 2 ;;
  esac
  local rc=$?
  sed "s/^/[$1] /" "$W/logs/$1.log" | grep -E 'OK after|FAILED|ABORT|DID NOT|INCOMPLETE|PTLOG_KF_N|already converged' || true
  [ $rc = 0 ] || echo "FATAL: $1 images incomplete ($W/logs/$1.log)"
  return $rc
}
set -- $TARGETS
if [ $# -gt 1 ]; then
  export JOBS="${JOBS:-$(( ($(nproc) + 1) / 2 ))}"
  pids=(); for t in "$@"; do build_group "$t" & pids+=($!); done
  ok=1; for p in "${pids[@]}"; do wait "$p" || ok=0; done
  [ $ok = 1 ] || exit 1
else
  build_group "$1" || exit 1
fi

"$PYBIN" "$HERE/stage.py" "$W" || exit 1
# sync/sink agreement: the runtime sets PTLOG_SYNC=4096 for the tracee (repslice.py e9fast_child_env); an image
# built with another period would misalign reconstruction
for e in "$W"/*/build/*.e9.ptlog.env; do
    grep -qx 'PTLOG_SYNC=4096' "$e" && grep -qx 'PTLOG_SINK=buffer' "$e" || { echo "FATAL: sync/sink mismatch in $e"; exit 1; }
done
echo "[jitwp] images built and staged under $W/stage; sync/sink agree"
