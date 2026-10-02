#!/usr/bin/env python3
"""Measurement versions of the Figure 5 configurations.

When an update changes what a configuration measures, its entry in VERSION is bumped.  The version of the rows kept in
$RUN_OUT/overhead*.csv is recorded per configuration in the ledger $RUN_OUT/overhead.versions.json (a directory without
a ledger holds version-1 rows).  Before a run, `prune' drops the rows of every configuration whose recorded version is
older than VERSION (they are moved to overhead.stale.csv), so the resume logic re-measures exactly those rows and keeps
all others; the figure ignores stale rows as well.  The configurations whose rows were dropped are listed in
$RUN_OUT/overhead.remeasure.json: run/01_overhead.sh re-measures those with a reduced plan (see PTW_REMEASURE_* there;
PTW_REMEASURE_FULL=1 re-measures them in full).

    row_versions.py prune <RUN_OUT> [--dry-run]
    row_versions.py remeasure <RUN_OUT> <config>     # exit 0 iff <config> is being re-measured after an update
"""
import csv
import glob
import json
import os
import sys

# configuration -> version of its measurement (absent = 1)
VERSION = {
    "pinhifi_ptw": 2,   # 2: mixed PTWRITE sink with a per-cell budget (1: every site logged with ptwrite)
    "fast": 2,          # 2: keyframe period 128 on every image but ld.so, analyzer v2.38 (1: 1024 / none, v2.37)
    "fast_ptw": 2,      # 2: as fast, and PTWRITE spacing 12 (1: 3)
    "e9fast": 2,        # 2: keyframe period 128 in the images and the JIT-code hooks, analyzer v2.38
    "e9fast_ptw": 3,    # 3: as e9fast, and PTWRITE spacing 12; 2: mixed sink, budgeted PTWRITE sites in
                        #    node/libjvm.so, JIT-code sites on the buffer (1: every JIT-code site through ptwrite)
}
LEDGER = "overhead.versions.json"
REMEASURE = "overhead.remeasure.json"


def current(cfg):
    return VERSION.get(cfg, 1)


def recorded(run_out):
    """configuration -> version of the rows stored in run_out (default 1)."""
    p = os.path.join(run_out, LEDGER)
    try:
        return json.load(open(p))
    except (OSError, ValueError):
        return {}


def stale_configs(run_out):
    rec = recorded(run_out)
    return {c for c, v in VERSION.items() if rec.get(c, 1) < v}


def csvs(run_out):
    return sorted(p for p in glob.glob(os.path.join(run_out, "overhead*.csv"))
                  if not p.endswith("overhead.stale.csv"))


def prune(run_out, dry=False):
    stale = stale_configs(run_out)
    drop, keep = {}, {}
    for p in csvs(run_out):
        with open(p, newline="") as f:
            rows = list(csv.reader(f))
        if not rows:
            continue
        hdr, body = rows[0], rows[1:]
        try:
            ci, si = hdr.index("config"), hdr.index("suite")
        except ValueError:
            continue
        old = [r for r in body if len(r) > ci and r[ci] in stale]
        new = [r for r in body if not (len(r) > ci and r[ci] in stale)]
        for r in old:
            drop[(r[si], r[ci])] = drop.get((r[si], r[ci]), 0) + 1
        if os.path.basename(p) == "overhead.csv":
            for r in new:
                keep[(r[si], r[ci])] = keep.get((r[si], r[ci]), 0) + 1
        if old and not dry:
            sp = os.path.join(run_out, "overhead.stale.csv")
            fresh = not os.path.exists(sp)
            with open(sp, "a", newline="") as f:
                w = csv.writer(f)
                if fresh:
                    w.writerow(hdr + ["from"])
                for r in old:
                    w.writerow(r + [os.path.basename(p)])
            tmp = p + ".tmp"
            with open(tmp, "w", newline="") as f:
                csv.writer(f).writerows([hdr] + new)
            os.replace(tmp, p)
    if drop:
        print("[versions] %s rows of an older measurement version: %s -> re-measured"
              % ("would drop" if dry else "dropped", ", ".join(
                  "%s/%s (%d)" % (s, c, n) for (s, c), n in sorted(drop.items()))))
    if dry:
        print("[versions] kept in overhead.csv: " + (", ".join(
            "%s/%s (%d)" % (s, c, n) for (s, c), n in sorted(keep.items())) or "none"))
    elif os.path.isdir(run_out):
        rec = recorded(run_out)
        rec.update({c: current(c) for c in VERSION})
        with open(os.path.join(run_out, LEDGER), "w") as f:
            json.dump(rec, f, indent=1, sort_keys=True)
        if drop:
            rm = remeasured(run_out)
            rm.update({c: current(c) for _, c in drop})
            with open(os.path.join(run_out, REMEASURE), "w") as f:
                json.dump(rm, f, indent=1, sort_keys=True)
    return drop


def remeasured(run_out):
    """configuration -> version, for the configurations whose older rows prune() dropped in this directory."""
    try:
        return json.load(open(os.path.join(run_out, REMEASURE)))
    except (OSError, ValueError):
        return {}


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "remeasure":
        sys.exit(0 if sys.argv[3] in remeasured(sys.argv[2]) else 1)
    if len(sys.argv) < 3 or sys.argv[1] != "prune":
        sys.exit(__doc__)
    prune(sys.argv[2], dry="--dry-run" in sys.argv[3:])
