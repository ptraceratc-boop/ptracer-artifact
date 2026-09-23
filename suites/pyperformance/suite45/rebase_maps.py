#!/usr/bin/env python3
"""rebase_maps.py VARIANT OUTDIR -- write the site maps of shadow.<VARIANT> with absolute
`image` / `orig_image` paths (the reconstruction matches images by the path the process maps)."""
import json, os, sys
v, out = sys.argv[1], sys.argv[2]
here = os.path.dirname(os.path.abspath(__file__))
base = os.path.dirname(here)
os.makedirs(out, exist_ok=True)
for n in sorted(os.listdir(os.path.join(here, "maps." + v))):
    d = json.load(open(os.path.join(here, "maps." + v, n)))
    for k in ("image", "orig_image"):
        if isinstance(d.get(k), str):
            d[k] = os.path.normpath(os.path.join(base, d[k]))
    json.dump(d, open(os.path.join(out, n), "w"), separators=(",", ":"))
print("%d site maps -> %s" % (len(os.listdir(out)), out))
