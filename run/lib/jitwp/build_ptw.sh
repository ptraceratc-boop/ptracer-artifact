#!/usr/bin/env bash
# build_ptw.sh -- the Fast-PTWRITE (e9fast_ptw) stage of the JIT suites, after build_stage.sh.
#
#   bash run/lib/jitwp/build_ptw.sh [java] [node]        (default: the groups build_stage.sh built)
#
# The runtime's main ELF image (node; libjvm.so) is rewritten again from the SAME spec, with the SAME flags and
# keyframe-counter base as its e9fast build, but with the MIXED sink: the sites of the addresses its buffer list names
# (data/ptw_sites/jit/<name>.buf: the addresses over the per-site PTWRITE budget of 5 M values/s per benchmark, from
# an untimed count profile of every cell, run/lib/jitwp/ptw_budget.py) log through the buffer, every other site through
# `ptwrite'.  The trampolines' sizes change, so E9Patch cannot place a few sites of the e9fast layout: they are avoided as in every Fast build (dropped from this build's copy of the spec,
# $JITWP_DIR/ptw/<group>/specs) and the build is repeated -- never an incomplete site map.  The other images (C libraries, JDK libraries) are the e9fast images, and the
# runtime hooks keep every JIT-code site on the buffer (repslice.py e9fast_ptw), so e9fast and e9fast_ptw log the same
# values and differ only where PTWRITE is used.  A list applies to the image it was made for (sha256 checked).
# Its sync markers use the `ptwrite' carrier (the rewriter's only one for a PTWRITE sink; PTW packets are on in this
# configuration).
# Output: $JITWP_DIR/ptw/{<group>/build, stage} (stage/manifest.json, read by repslice.py for e9fast_ptw).  Cost: one
# rewrite per image (minutes).  Env: JITWP_DIR, CPUS, PYBIN, NODE_IMAGE, JDK_HOME (as build_stage.sh).
set -u
. "$(dirname "$0")/../common.sh"
cd "$ROOT"
pick_out
pick_python
W="${JITWP_DIR:-$RUN_OUT/jitwp}"; P="$W/ptw"
JDK="${JDK_HOME:-$ROOT/suites/java/jdk17}"; NODE_IMG="${NODE_IMAGE:-$ROOT/suites/node/bin/node}"
RW=$ROOT/ptracer/runtime/jit_toolchain/rewrite.py; DIC=$ROOT/run/lib/jitwp/dataincode.py; LST=$ROOT/data/ptw_sites/jit
CPUS=${CPUS:-$(awk '/^Cpus_allowed_list/{print $2}' /proc/self/status)}
export E9PATCH_DIR=${E9PATCH_DIR:-$ROOT/third_party/e9patch}
GROUPS_=${*:-}
[ -n "$GROUPS_" ] || for g in java node; do [ -f "$W/$g/build/kfplan.json" ] && GROUPS_="$GROUPS_ $g"; done
[ -f "$W/common/build/kfplan.json" ] || { echo "FATAL: $W not built (run run/lib/jitwp/build_stage.sh first)"; exit 2; }
mkdir -p "$P"; ln -sfn ../common "$P/common"

mixed_one () {   # mixed_one GROUP NAME IMAGE -> $P/GROUP/build/NAME.e9 (+ kfplan.json pointing at it)
  local g=$1 n=$2 img=$3 B="$P/$1/build" S="$P/$1/specs" shared="" r ok=0
  local spec0="$W/$g/specs/$n.spec.json" log="$P/$g/logs/$n.mixed.log" ids="$B/$n.ptw.ids" lst="$LST/$n.buf"
  local spec="$S/$n.spec.json" av="$B/$n.mixed.avoid"
  [ -L "$S" ] && rm -f "$S"          # (never write through a symlink to the e9fast specs)
  mkdir -p "$B" "$S" "$P/$g/logs"
  for f in "$W/$g/specs/"*.spec.json; do [ "$(basename "$f")" = "$n.spec.json" ] || ln -sfn "$f" "$S/"; done
  [ -f "$lst" ] || { echo "[$n] FATAL: no PTWRITE list $lst"; return 1; }
  if [ -s "$B/$n.e9.sitemap.json" ] && grep -q '^\[rewrite\] --sink mixed' "$log" 2>/dev/null && ! grep -q FAILED "$log"; then
    echo "[$n] mixed image already built"; return 0; fi
  readelf -h "$img" | grep -q 'DYN' && ! readelf -d "$img" | grep -q '(FLAGS_1).*PIE' && shared="--shared"
  touch "$av"
  local base need
  base=$("$PYBIN" -c "import json,sys; print(json.load(open(sys.argv[1]))['images'][sys.argv[2]]['base'])" "$W/$g/build/kfplan.json" "$n")
  need=$(cat "$W/$g/build/$n.kfplan")
  for r in $(seq 1 ${PTW_AVOID_ROUNDS:-8}); do
    # the spec of the e9fast build minus the addresses E9Patch could not place in an earlier mixed round (the avoid
    # rule of every Fast build), and the PTWRITE ids from the buffer list (by address)
    "$PYBIN" - "$spec0" "$lst" "$av" "$spec" "$ids" <<'PY' || return 1
import json, sys
spec0, lst, av, spec, out = sys.argv[1:6]
d = json.load(open(spec0)); rows = open(lst).read().splitlines()
sha = [l.split()[2] for l in rows if l.startswith("# spec_sha256 ")]
if sha != [d["sha256"]]:
    sys.exit("[build_ptw] %s was made for another image (sha256 %s, local %s): regenerate it (ptw_budget.py)"
             % (lst, sha, d["sha256"]))
avoid = {int(l, 16) for l in open(av) if l.strip()}
n0 = len(d["sites"])
d["sites"] = [s for s in d["sites"] if s["addr"] not in avoid]
d.setdefault("dropped_sites", [])
json.dump(d, open(spec, "w"))
buf = {int(l.split()[0], 16) for l in rows if l.strip() and not l.startswith("#")}
ids = [s["id"] for s in d["sites"] if s["addr"] not in buf]
print("[build_ptw] %s: %d of %d sites PTWRITE, the rest buffer; %d avoided in the mixed layout"
      % (spec.split("/")[-1], len(ids), len(d["sites"]), n0 - len(d["sites"])))
open(out, "w").write("".join("%d\n" % i for i in ids))
PY
    taskset -c "$CPUS" "$PYBIN" "$RW" "$spec" "$img" -o "$B/$n.e9" --sink mixed --ptw-sites "$ids" --sync 4096 \
        --keyframe "$FAST_KEYFRAME" --space "$PTW_SPACE" --kf-gs --kf-base "$base" $shared \
        --reserve-cursor --dead-regs off $(cat "$W/$g/build/$n.dic.e9excl" 2>/dev/null) > "$log" 2>&1 \
      && ! grep -q FAILED "$log" && { ok=1; break; }
    local before; before=$(wc -l < "$av")
    grep -hoE '^0x[0-9a-fA-F]+' "$B/$n.e9.unpatched" >> "$av" 2>/dev/null
    # every site patched, but a `jmp' landed inside a displaced window: avoid the site and that instruction (as
    # build_images.sh does)
    if grep -q 'the site map is incomplete' "$log" && [ -s "$B/$n.e9.sitemap.problems" ]; then
      { grep -v '^#' "$B/$n.e9.sitemap.problems" | awk '{print $1}'
        grep -oE 'original at 0x[0-9a-fA-F]+' "$B/$n.e9.sitemap.problems" | awk '{print $3}'; } \
        | grep -E '^0x[0-9a-fA-F]+$' >> "$av"
    fi
    sort -u -o "$av" "$av"
    [ "$(wc -l < "$av")" -gt "$before" ] || break
    echo "[build_ptw] $n round $r: $(( $(wc -l < "$av") - before )) address(es) avoided"
  done
  [ $ok = 1 ] || { echo "[$n] MIXED REWRITE FAILED ($log)"; return 1; }
  local kfn; kfn=$(grep -oE '^\[rewrite\] --kf-gs: [0-9]+ keyframe counter' "$log" | grep -oE '[0-9]+' | head -1)
  [ -n "$kfn" ] && [ "$kfn" -le "$need" ] || { echo "[$n] keyframe counters ($kfn) exceed the e9fast range ($need)"; return 1; }
  if [ "$(cat "$W/$g/build/$n.dic.prot.json" 2>/dev/null)" != "[]" ] && [ -f "$W/$g/build/$n.dic.prot.json" ]; then
    "$PYBIN" "$DIC" verify "$img" "$B/$n.e9" "$W/$g/build/$n.dic.prot.json" || { echo "[$n] protected bytes CHANGED"; return 1; }
  fi
  "$PYBIN" - "$W/$g/build/kfplan.json" "$B/kfplan.json" "$n" "$B/$n.e9" <<'PY'
import json, os, sys
src, dst, n, e9 = sys.argv[1:5]
d = json.load(open(src)); d["images"][n]["e9"] = os.path.realpath(e9); json.dump(d, open(dst, "w"), indent=1)
PY
  echo "[$n] mixed OK: $(grep -oE '[0-9]+ logged value\(s\) -> PTWRITE, [0-9]+ -> buffer \([0-9.]+ % PTWRITE\)' "$log")"
}

fail=0
for g in $GROUPS_; do
  case $g in
    node) mixed_one node node "$NODE_IMG" || fail=1 ;;
    java) mixed_one java libjvm.so "$JDK/lib/server/libjvm.so" || fail=1 ;;
    *) echo "usage: $0 [java] [node]"; exit 2 ;;
  esac
done
[ $fail = 0 ] || { echo "BUILD INCOMPLETE (Fast-PTWRITE)"; exit 1; }
"$PYBIN" run/lib/jitwp/stage.py "$P" && echo "[jitwp] Fast-PTWRITE stage: $P/stage"
