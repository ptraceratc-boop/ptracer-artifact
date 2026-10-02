#!/usr/bin/env python3
"""stage.py W -- stage the whole-program Fast images of the JIT suites built by build_images.sh under W.

    W/stage/libs/<soname>  -> the rewritten C libraries (symlinks: the kernel reports the TARGET path in
                              /proc/PID/maps, which is the path each site map's `image' names)
    W/stage/node           -> W/node/build/node.e9
    W/stage/jdk/           a symlink farm of the JDK with a REAL bin/java (the launcher derives the JDK home from
                           /proc/self/exe) and a HARD-LINKED lib/server/libjvm.so (HotSpot derives java.home from
                           realpath(dladdr(libjvm)), so libjvm must physically live in the shadow tree; its site map
                           is re-pointed at that path); the other rewritten JDK libraries are symlinked in
    W/stage/jithook.node, W/stage/ptjava.so -> the runtime hooks built by ptracer/build.sh.  They are TOOL code and
                           stay unrewritten: their own accesses are not part of the workload.
    W/stage/manifest.json  paths, site maps and the keyframe-counter totals (PTLOG_KF_N, PTLOG_KF_N_group)
                           that run/lib/repslice.py (--jitwp-stage) reads.
Site maps embed absolute paths: keep W where it was built (or restage).
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ARTIFACT_ROOT") or os.path.abspath(os.path.join(HERE, "..", "..", ".."))
JDK = os.environ.get("JDK_HOME") or os.path.join(ROOT, "suites", "java", "jdk17")
JITHOOK = os.path.join(ROOT, "ptracer", "runtime", "jit", "jithook.node")
PTJAVA = os.path.join(ROOT, "ptracer", "runtime", "jit", "java", "ptjava.so")


def images(root, group):
    p = os.path.join(root, group, "build", "kfplan.json")
    return json.load(open(p))["images"] if os.path.exists(p) else {}


def kf_n(root, groups):
    """PTLOG_KF_N over the given groups = the highest keyframe-counter index any of their images uses + 1."""
    n = 0
    for g in groups:
        p = os.path.join(root, g, "build", "kfplan.json")
        if os.path.exists(p):
            n = max(n, json.load(open(p))["PTLOG_KF_N"])
    return n


def stage(root):
    root = os.path.realpath(root)
    st = os.path.join(root, "stage")
    shutil.rmtree(st, ignore_errors=True)
    os.makedirs(os.path.join(st, "libs"))
    common = images(root, "common")
    if not common:
        raise SystemExit("stage: no common images under %s/common/build" % root)
    for name, im in common.items():
        os.symlink(im["e9"], os.path.join(st, "libs", name))
    manifest = {"libs": {n: im["e9"] for n, im in common.items()}, "sitemaps": [],
                "PTLOG_KF_N": kf_n(root, ("common", "node", "java"))}
    for name, im in common.items():
        manifest["sitemaps"].append(dict(image=im["e9"], sitemap=im["e9"] + ".sitemap.json",
                                         spec=os.path.join(root, "common", "specs", name + ".spec.json"),
                                         orig=im["image"]))
    node = images(root, "node")
    if node:
        os.symlink(node["node"]["e9"], os.path.join(st, "node"))
        os.symlink(JITHOOK, os.path.join(st, "jithook.node"))
        for name, im in node.items():
            manifest["sitemaps"].append(dict(image=im["e9"], sitemap=im["e9"] + ".sitemap.json",
                                             spec=os.path.join(root, "node", "specs", name + ".spec.json"),
                                             orig=im["image"], group="node"))
        manifest["node"] = os.path.join(st, "node")
        manifest["jithook"] = os.path.join(st, "jithook.node")
    java = images(root, "java")
    if java:
        jdk = os.path.join(st, "jdk")
        subprocess.run(["cp", "-as", os.path.realpath(JDK), jdk], check=True)   # symlink farm
        os.unlink(os.path.join(jdk, "bin", "java"))
        shutil.copy2(os.path.join(JDK, "bin", "java"), os.path.join(jdk, "bin", "java"))
        for name, im in java.items():
            if name == "libjvm.so":
                dst = os.path.join(jdk, "lib", "server", "libjvm.so")
                os.unlink(dst)
                os.link(im["e9"], dst)
                sm = os.path.join(st, "libjvm.so.sitemap.json")
                d = json.load(open(im["e9"] + ".sitemap.json"))
                d["image"] = os.path.realpath(dst)
                json.dump(d, open(sm, "w"))
                im = dict(im, e9=os.path.realpath(dst))
            else:
                dst = os.path.join(jdk, "lib", name)
                os.unlink(dst)
                os.symlink(im["e9"], dst)
                sm = im["e9"] + ".sitemap.json"
            manifest["sitemaps"].append(dict(image=im["e9"], sitemap=sm,
                                             spec=os.path.join(root, "java", "specs", name + ".spec.json"),
                                             orig=im["image"], group="java"))
        os.symlink(PTJAVA, os.path.join(st, "ptjava.so"))
        manifest["java"] = os.path.join(jdk, "bin", "java")
        manifest["ptjava"] = os.path.join(st, "ptjava.so")
    # per-suite counter totals: the node and java groups both start their bases at common's N, so each process
    # needs only max(common, its own group) cells, not the process-wide max
    manifest["PTLOG_KF_N_group"] = {g: kf_n(root, ("common", g)) for g in ("java", "node") if images(root, g)}
    json.dump(manifest, open(os.path.join(st, "manifest.json"), "w"), indent=1)
    print("staged %s: %d site maps, PTLOG_KF_N=%d %s" % (st, len(manifest["sitemaps"]), manifest["PTLOG_KF_N"],
                                                        manifest["PTLOG_KF_N_group"]))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    stage(sys.argv[1])
