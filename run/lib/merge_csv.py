#!/usr/bin/env python3
"""merge_csv.py OUT PART...: append the rows of each lane's CSV to OUT (same header), skipping rows OUT has."""
import csv
import os
import sys

out, parts = sys.argv[1], sys.argv[2:]
have, hdr = set(), None
if os.path.exists(out) and os.path.getsize(out):
    rows = list(csv.reader(open(out)))
    hdr, have = rows[0], {tuple(r) for r in rows[1:]}
with open(out, "a", newline="") as fh:
    w = csv.writer(fh)
    for p in parts:
        rows = list(csv.reader(open(p))) if os.path.exists(p) else []
        if not rows:
            continue
        if hdr is None:
            hdr = rows[0]
            w.writerow(hdr)
        elif rows[0] != hdr:
            sys.exit("merge_csv: %s has a different header" % p)
        for r in rows[1:]:
            if tuple(r) not in have:
                w.writerow(r)
                have.add(tuple(r))
