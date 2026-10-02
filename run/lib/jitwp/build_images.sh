#!/usr/bin/env bash
# build_images.sh -- analyze + E9Patch-rewrite the NATIVE ELF images of one group of a JIT runtime process, for
# the whole-program Fast mode of the JIT suites (called by build_stage.sh; one call per group).
#
#   GROUP=<common|java|node> W=<workdir> bash run/lib/jitwp/build_images.sh NAME=PATH [NAME=PATH ...]
#
# Per image: `analyze.py --mode fast --keyframe K' -> `rewrite.py --sink buffer --sync 4096 --keyframe K --kf-gs',
# looping `--avoid' on the rewriter's unpatched list until EVERY spec site is patched.  The avoid list starts from
# the shipped seed data/avoid/jit/GROUP/NAME.avoid.txt (the converged list of the reference build), so a build on
# the same inputs converges in one or two rounds.  Then a SECOND rewrite pass gives every image a disjoint
# `--kf-base' range in the per-thread keyframe-counter array and records the group's PTLOG_KF_N in
# W/GROUP/build/kfplan.json.  A build is complete only when every image's rewrite succeeds (never --keep-going): an
# incomplete site map misaligns the positional value stream silently.
#
# Bytes of an executable section that code READS AS DATA (rip-relative loads into .text; node 22's V8
# builtins reuse movabs imm64s) must stay byte-identical.  dataincode.py scans every image -> NAME.dic.{prot.json,
# drop.txt,e9excl}; the dropped sites join the avoid list, the covering instructions go to e9tool as --exclude
# ranges, and every built image is verified.  node's dropped sites are shipped in data/avoid/jit/node/node.dic.drop.txt
# (already part of the seed avoid list).
#
# Env: KF_BASE_START (first free counter index: common's PTLOG_KF_N for the java/node groups), RWARGS (extra
# rewrite.py flags), AVOID_ROUNDS (12), JOBS (analyzer processes, default nproc), CPUS (taskset list, default all),
# PYBIN (python with angr), CACHE (analysis cache, default W/cache), MODE (fast) KF (FAST_KEYFRAME, 128) SINK (buffer) SYNC (4096).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="${ROOT:-$(cd "$HERE/../../.." && pwd)}"
GROUP=${GROUP:?GROUP=<common|java|node>}; W=${W:?W=<workdir>}
VPY=${PYBIN:-python3}
AN=$ROOT/ptracer/static/analyze.py
RW=$ROOT/ptracer/runtime/jit_toolchain/rewrite.py   # the JIT-suite toolchain (--kf-gs/--kf-base), not the non-JIT rewriter
DIC=$HERE/dataincode.py
SEED=$ROOT/data/avoid/jit/$GROUP
CACHE=${CACHE:-$W/cache}
CPUS=${CPUS:-$(awk '/^Cpus_allowed_list/{print $2}' /proc/self/status)}   # default: every cpu this process may use
JOBS=${JOBS:-$(nproc)}
MODE=${MODE:-fast}; KF=${KF:-${FAST_KEYFRAME:-128}}; SINK=${SINK:-buffer}; SYNC=${SYNC:-4096}
# sync-marker carrier (run/lib/common.sh PT_SYNC_CARRIER): tnt = no PTWRITE in the images
CARRIER=""; [ "$SINK" = buffer ] && [ "$SYNC" != 0 ] && CARRIER="--sync-carrier ${PT_SYNC_CARRIER:-tnt}"
AVOID_ROUNDS=${AVOID_ROUNDS:-12}
ANDIR=$W/$GROUP/specs; BUILD=$W/$GROUP/build; LOGS=$W/$GROUP/logs
mkdir -p "$ANDIR" "$BUILD" "$LOGS" "$CACHE"
export E9PATCH_DIR=${E9PATCH_DIR:-$ROOT/third_party/e9patch}

is_shared () {   # ET_DYN images are rewritten with --shared (a PIE executable is not: it is exec'd)
  local t; t=$(readelf -h "$1" | awk '/Type:/{print $2}')
  [ "$t" = DYN ] && ! readelf -d "$1" 2>/dev/null | grep -q '(FLAGS_1).*PIE' && return 0
  return 1
}
kf_counters () {   # the "[rewrite] --kf-gs: N keyframe counter(s)" line of a build log -> N
  grep -oE '^\[rewrite\] --kf-gs: [0-9]+ keyframe counter' "$1" | grep -oE '[0-9]+' | head -1
}
merge_avoid () {   # merge_avoid AVOID NEW -> prints how many addresses NEW added to AVOID
  local before; before=$(wc -l < "$1")
  cat "$1" "$2" | grep -E '^0x[0-9a-fA-F]+$' | sort -u > "$1.t" && mv "$1.t" "$1"
  echo $(( $(wc -l < "$1") - before ))
}
dic_scan () { "$VPY" "$DIC" scan "$2" "$ANDIR/$1.spec.json" "$BUILD/$1.dic"; }
dic_args () { cat "$BUILD/$1.dic.e9excl" 2>/dev/null; }
dic_verify () {  # 0 iff no protected byte differs in the built image
  [ "$(cat "$BUILD/$1.dic.prot.json" 2>/dev/null)" = "[]" ] && return 0
  "$VPY" "$DIC" verify "$2" "$BUILD/$1.e9" "$BUILD/$1.dic.prot.json"
}

build_one () {   # build_one NAME PATH -> converged $BUILD/NAME.e9 (+ .sitemap.json), or return 1
  local name=$1 img=$2 shared="" it rc nsites nnew t0 t1
  local av=$BUILD/$name.avoid.txt
  is_shared "$img" && shared="--shared"
  if [ ! -f "$av" ]; then
    if [ -f "$SEED/$name.avoid.txt" ]; then cp "$SEED/$name.avoid.txt" "$av"; echo "[$name] seeded: $(wc -l < "$av") avoided"
    else : > "$av"; fi
  fi
  [ -s "$BUILD/$name.dic.drop.txt" ] && echo "[$name] +$(merge_avoid "$av" "$BUILD/$name.dic.drop.txt") data-in-code sites avoided"
  for it in $(seq 0 $((AVOID_ROUNDS - 1))); do
    local A=""; [ -s "$av" ] && A="--avoid $av"
    t0=$(date +%s)
    taskset -c "$CPUS" "$VPY" "$AN" "$img" -o "$ANDIR/$name.spec.json" --mode "$MODE" --keyframe "$KF" \
        --jobs "$JOBS" --cache "$CACHE" $A > "$LOGS/$name.analyze.$it.log" 2>&1 \
        || { echo "[$name] ANALYZE FAILED (round $it, $LOGS/$name.analyze.$it.log)"; return 1; }
    t1=$(date +%s)
    if [ ! -f "$BUILD/$name.dic.prot.json" ]; then   # first analysis of this image: scan; re-plan only if it adds sites
      dic_scan "$name" "$img" || return 1
      if [ -s "$BUILD/$name.dic.drop.txt" ]; then
        nnew=$(merge_avoid "$av" "$BUILD/$name.dic.drop.txt")
        if [ "$nnew" -gt 0 ]; then echo "[$name] +$nnew data-in-code sites avoided; re-analysing"; continue; fi
      fi
    fi
    # a REAL build at kf-base 0; the counter count is taken from the build's own log line and re-based in phase 2
    taskset -c "$CPUS" "$VPY" "$RW" "$ANDIR/$name.spec.json" "$img" -o "$BUILD/$name.e9" \
        --sink "$SINK" --sync "$SYNC" $CARRIER --keyframe "$KF" --kf-gs --kf-base 0 \
        $shared ${RWARGS:-} $(dic_args "$name") > "$LOGS/$name.rewrite.$it.log" 2>&1
    rc=$?
    if [ $rc -eq 0 ] && ! grep -q 'FAILED' "$LOGS/$name.rewrite.$it.log" && [ -s "$BUILD/$name.e9.sitemap.json" ]; then
      dic_verify "$name" "$img" || { echo "[$name] protected data-in-code bytes CHANGED"; return 1; }
      touch "$BUILD/$name.dic.ok"
      "$VPY" - "$BUILD/kfplan.json" "$name" <<'PY'
import json, sys, os
if os.path.exists(sys.argv[1]):   # built at kf-base 0: forget an earlier phase-2 base so phase 2 re-bases it
    d = json.load(open(sys.argv[1])); d["images"].pop(sys.argv[2], None); json.dump(d, open(sys.argv[1], "w"), indent=1)
PY
      cp "$LOGS/$name.rewrite.$it.log" "$LOGS/$name.rewrite.log"
      kf_counters "$LOGS/$name.rewrite.log" > "$BUILD/$name.kfplan"
      echo "[$name] OK after $((it + 1)) round(s); analyze $((t1 - t0)) s; kf counters $(cat "$BUILD/$name.kfplan")"
      return 0
    fi
    grep -hoE '^0x[0-9a-fA-F]+' "$BUILD/$name.e9.unpatched" > "$BUILD/$name.avoid.new" 2>/dev/null
    # Every site patched, but the site-map check refused the build because E9Patch put a `jmp' inside a
    # displaced window ("trampoline instruction (jmp) does not match the original at ...").  Avoid both the site and
    # that instruction next round.
    if grep -q 'the site map is incomplete' "$LOGS/$name.rewrite.$it.log" && [ -s "$BUILD/$name.e9.sitemap.problems" ]; then
      { grep -v '^#' "$BUILD/$name.e9.sitemap.problems" | awk '{print $1}'
        grep -oE 'original at 0x[0-9a-fA-F]+' "$BUILD/$name.e9.sitemap.problems" | awk '{print $3}'; } \
        | grep -E '^0x[0-9a-fA-F]+$' >> "$BUILD/$name.avoid.new"
    fi
    [ -s "$BUILD/$name.avoid.new" ] || { echo "[$name] FAILED: no unpatched list (see $LOGS/$name.rewrite.$it.log)"; return 1; }
    nsites=$("$VPY" -c "import json,sys; print(len(json.load(open(sys.argv[1]))['sites']))" "$ANDIR/$name.spec.json")
    nnew=$(wc -l < "$BUILD/$name.avoid.new")
    # a round that refuses a large fraction of the sites is a rewriter problem, not a site problem;
    # feeding it back would empty the spec
    if [ "$nsites" -gt 0 ] && [ $((nnew * 100)) -gt $((nsites * ${AVOID_MAX_PCT:-20})) ] && [ "$nnew" -gt "${AVOID_MIN_SITES:-20}" ]; then
      echo "[$name] ABORT: $nnew of $nsites sites unpatchable in one round"; return 1
    fi
    merge_avoid "$av" "$BUILD/$name.avoid.new" > /dev/null
    echo "[$name] round $it: +$nnew avoid ($(wc -l < "$av") total), $nsites sites"
  done
  echo "[$name] DID NOT CONVERGE in $AVOID_ROUNDS rounds"; return 1
}

# ---- phase 1: converge every image (kf-base 0) --------------------------------------------------------------
declare -a NAMES=() PATHS=()
fail=0
for spec in "$@"; do
  n=${spec%%=*}; p=${spec#*=}
  [ -f "$p" ] || { echo "[$n] no such image: $p"; fail=1; continue; }
  NAMES+=("$n"); PATHS+=("$p")
  if [ -f "$BUILD/$n.e9.sitemap.json" ] && [ -f "$LOGS/$n.rewrite.log" ] && ! grep -q FAILED "$LOGS/$n.rewrite.log" \
     && [ -f "$BUILD/$n.dic.ok" ]; then
    echo "[$n] already converged"; continue
  fi
  build_one "$n" "$p" || fail=1
done
[ $fail -eq 0 ] || { echo "BUILD INCOMPLETE ($GROUP)"; exit 1; }

# ---- phase 2: disjoint keyframe-counter ranges (one rewrite per image, same spec) -----------------------------
base=${KF_BASE_START:-0}
"$VPY" - "$BUILD/kfplan.json" <<'PY'
import json, sys, os
d = json.load(open(sys.argv[1])) if os.path.exists(sys.argv[1]) else {"images": {}, "PTLOG_KF_N": 0}
json.dump(d, open(sys.argv[1], "w"))
PY
for i in "${!NAMES[@]}"; do
  n=${NAMES[$i]}; p=${PATHS[$i]}; shared=""; is_shared "$p" && shared="--shared"
  need=$(cat "$BUILD/$n.kfplan")
  cur=$("$VPY" -c "import json,sys; d=json.load(open(sys.argv[1])); print(d['images'].get(sys.argv[2],{}).get('base',-1))" "$BUILD/kfplan.json" "$n")
  if [ "$cur" != "$base" ]; then
    taskset -c "$CPUS" "$VPY" "$RW" "$ANDIR/$n.spec.json" "$p" -o "$BUILD/$n.e9" \
        --sink "$SINK" --sync "$SYNC" $CARRIER --keyframe "$KF" --kf-gs --kf-base "$base" \
        $shared ${RWARGS:-} $(dic_args "$n") > "$LOGS/$n.rewrite.log" 2>&1
    if [ $? -ne 0 ] || grep -q FAILED "$LOGS/$n.rewrite.log" || [ ! -s "$BUILD/$n.e9.sitemap.json" ]; then echo "[$n] FINAL REWRITE FAILED"; exit 1; fi
    [ "$(kf_counters "$LOGS/$n.rewrite.log")" = "$need" ] || { echo "[$n] keyframe counter count changed between passes"; exit 1; }
    dic_verify "$n" "$p" || { echo "[$n] protected data-in-code bytes CHANGED in the final build"; exit 1; }
  fi
  "$VPY" - "$BUILD/kfplan.json" "$n" "$base" "$need" "$p" "$BUILD/$n.e9" <<'PY'
import json, sys, os
f, n, base, need, p, e9 = sys.argv[1:7]
d = json.load(open(f)); d["images"][n] = dict(base=int(base), counters=int(need), image=os.path.realpath(p), e9=os.path.realpath(e9))
d["PTLOG_KF_N"] = max(d["PTLOG_KF_N"], int(base) + int(need))
json.dump(d, open(f, "w"), indent=1)
PY
  echo "[$n] kf-base $base (+$need)"
  base=$((base + need))
done
echo "[$GROUP] PTLOG_KF_N=$base ($BUILD/kfplan.json)"
