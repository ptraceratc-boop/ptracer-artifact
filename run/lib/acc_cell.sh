#!/usr/bin/env bash
# One cell of run/02_accuracy.sh:  bash run/lib/acc_cell.sh MODE SUITE CELL CORE HELPER     (MODE: tnt | ptw)
#   capture  pt_capture2 (the Figure 5 Fast capture: --no-ptw for Fast, --ptw for Fast-PTWRITE) of the ground-truth twin
#            images ($ACC_DIR/gt/MODE) on core CORE (pt_capture2 itself on HELPER), ASLR off, with the value stream and
#            the ground-truth ring of every thread kept in the cell directory (the main thread's first ACC_GTMAX bytes);
#   compare  ptrecon --gt-in <the main thread's ground truth>, with the site map of every rewritten image the process
#            mapped;
#   row      appended to $ACC_DIR/cells.jsonl.  The cell directory is deleted unless ACC_KEEP=1.
set -u
. "$(dirname "$0")/common.sh"
cd "$ROOT"
pick_python
pick_out
mode=$1 suite=$2 cell=$3 core=$4 helper=$5
A="${ACC_DIR:?}"; T="$A/gt/$mode"; name="$mode.$suite.$cell"
d="${ACC_TMP:-$A/tmp}/$name"; RES="$A/cells.jsonl"
PC=$ROOT/ptracer/offline/pt_capture2; RECON=$ROOT/ptracer/offline/ptrecon; MT=$ROOT/ptracer/runtime/rt/ptlogmt.so
rm -rf "$d"; mkdir -p "$d"
ptw=--no-ptw; [ "$mode" = ptw ] && ptw=--ptw
E=(--child-env "PTLOG_DIR=$d" --child-env PTLOG_GT=1 --child-env "PTLOG_GT_DIR=$d"
   --child-env "PTLOG_GT_MAX=${ACC_GTMAX:-4294967296}" --child-env "LD_PRELOAD=$MT")
case $suite in
  poly) lib=$T/lib; argv=("$T/poly/${cell}_large") ;;
  py)   lib=$T/py/lib
        argv=("$T/py/pyfast/bin/python3.12" "$ROOT/suites/pyperformance/pyperf/run_one_full.py" "$cell")
        E+=(--child-env PPF_LOOPS=1 --child-env PPF_WARM=0 --child-env PYTHONHASHSEED=0
            --child-env SOURCE_DATE_EPOCH=1700000000 --child-env "PYTHONPATH=$T/py/pyext:$T/py/pysite"
            --child-env PYTHONDONTWRITEBYTECODE=1) ;;
  *) echo "unknown suite $suite"; exit 2 ;;
esac
E+=(--child-env "LD_LIBRARY_PATH=$lib")
echo "-- $name: capture (core $core)"
t0=$(date +%s.%N)
# a fixed, minimal environment: the traced program's memory layout must not depend on the caller's variables
( cd "$d" && env -i PATH=/usr/local/bin:/usr/bin:/bin HOME=/root LANG=C.UTF-8 \
    timeout "${ACC_TIMEOUT:-3600}" taskset -c "$helper" setarch -R "$PC" --aux-mb 512 $ptw --no-decode \
    --aux-out "$d/aux" --child-core "$core" --sideband "$d/sb.json" "${E[@]}" --map-poll-gate -- "${argv[@]}" \
    > "$d/run.out" 2> "$d/run.err" ); crc=$?
t1=$(date +%s.%N)
# the traced process = the pid of the largest ground-truth ring; its main thread = tid == pid
gt=$(ls -S "$d"/gt.*.bin 2>/dev/null | head -1); cv=""
if [ -n "$gt" ]; then pid=$(basename "$gt" | cut -d. -f2); gt="$d/gt.$pid.$pid.bin"; cv="$d/cv.$pid.$pid.bin"; fi
R=(--aux "$d/aux" --sideband "$d/sb.json")
[ -n "$cv" ] && [ -f "$cv" ] && R+=(--cv "$cv")
while read -r img orig; do R+=(--sitemap "$img.sitemap.json" --orig-image "$orig"); done < <("$PYBIN" - "$d/sb.json" <<'PY'
import json, os, re, sys
seen = set()
for p in re.findall(r'"(/[^"]+)"', open(sys.argv[1]).read()):
    if p in seen:
        continue
    for q in (p, os.path.realpath(p)):
        if os.path.exists(q + ".sitemap.json"):
            seen.add(p)
            print(q, json.load(open(q + ".sitemap.json")).get("orig_image"))
            break
PY
)
rrc=-1
if [ -n "$gt" ] && [ -f "$gt" ]; then
    echo "-- $name: reconstruct and compare"
    timeout "${ACC_RTIMEOUT:-7200}" taskset -c "$core" "$RECON" "${R[@]}" --gt-in "$gt" --summary "$d/recon.json" \
        > /dev/null 2> "$d/recon.err"; rrc=$?
fi
t2=$(date +%s.%N)
"$PYBIN" - "$d" "$RES" "$mode" "$suite" "$cell" "$crc" "$rrc" "$t0" "$t1" "$t2" "$gt" <<'PY'
import glob, json, os, sys
d, res, mode, suite, cell, crc, rrc, t0, t1, t2, gt = sys.argv[1:12]
row = dict(mode=mode, suite=suite, cell=cell, capture_rc=int(crc), recon_rc=int(rrc),
           capture_s=round(float(t1) - float(t0), 2), recon_s=round(float(t2) - float(t1), 2),
           gt_bytes=os.path.getsize(gt) if gt and os.path.exists(gt) else 0,
           pids=len({os.path.basename(f).split(".")[1] for f in glob.glob(os.path.join(d, "gt.*.bin"))}))
try:
    j = json.load(open(os.path.join(d, "recon.json")))
    g = j.get("gt") or {}
    for k in ("records_compared", "identical", "unknown", "wrong", "excluded", "gt_only", "recon_only", "gt_before_anchor",
              "gt_tail", "gt_records", "gt_rewound_after_loss", "unknown_in_overflow", "wrong_in_overflow"):
        row[k] = g.get(k)
    for k in sorted(j):
        if "overflow" in k.lower():
            row["recon_" + k] = j[k]
except Exception as e:
    row["error"] = str(e)[:200]
    for f in ("recon.err", "run.err"):
        try:
            t = open(os.path.join(d, f), errors="replace").read().strip().splitlines()
            if t:
                row["error_tail"] = t[-3:]
                break
        except OSError:
            pass
with open(res, "a") as f:
    f.write(json.dumps(row) + "\n")
n = row.get("gt_records") or 0
print("-- %s %s %s: %s ground-truth records, %s unknown, %s wrong%s" % (mode, suite, cell, n, row.get("unknown"),
      row.get("wrong"), "" if n else " (no comparison: %s)" % (row.get("error_tail") or row.get("error"))))
PY
[ "${ACC_KEEP:-0}" = 1 ] || rm -rf "$d"
