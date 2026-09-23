#!/usr/bin/env python3
"""mkplan.py -- turn spec JSON files into the compact text PLAN that
runtime/pinjit/hifitool.cpp reads.

The Pintool runs inside Pin's CRT, which has no JSON parser, so the spec is flattened
here instead: one line per instrumented ADDRESS, holding the exact sequence of 64-bit
values that address must log, in the program order the spec defines (all `before` sites
in ascending id, then the instruction, then all `after` sites in ascending id; within a
`reg` site, the registers in the listed order).

    mkplan.py -o PLAN  IMAGE=SPEC [IMAGE=SPEC ...]
    mkplan.py -o PLAN  --auto SPEC [SPEC ...]      # image path taken from the spec's "image"
    mkplan.py -o PLAN  --sites IMAGE=SITES [...]   # from a rewrite.py `.e9.sites` file

PLAN format (text, one record per line):

    PTPLAN 2
    I <image-abs-path>                 image the following S lines belong to
    S <addr-hex> <B|A> <n> <item>...   n values logged at <addr>, before / after

item := r:<name>      log the 64-bit register <name>
        x:<name>:<lo|hi>  log one half of an xmm register
        m:<bytes>     log the instruction's memory operand value, zero-extended
        f             log the TCB self-pointer at %fs:0 (`fs_base`)

Addresses are link-time virtual addresses exactly as in the spec; the Pintool adds
IMG_LoadOffset() at image load.
"""
import argparse, json, os, sys

GP = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
      "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"}


def items_for_reg(name):
    n = name.lower()
    if n in GP:
        return ["r:" + n]
    if n.startswith("xmm"):
        return ["x:%s:lo" % n, "x:%s:hi" % n]
    if n == "fs_base":
        return ["f"]
    return None                      # gs_base and anything else: not loggable


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--auto", action="store_true")
    ap.add_argument("--sites", action="store_true",
                    help="inputs are runtime/rewrite.py `.e9.sites` files (IMAGE=FILE): "
                         "the sites the E9Patch build actually installed, in the order it "
                         "emits them, so the Pintool's cv stream is byte-comparable with "
                         "the static-rewrite build's.")
    ap.add_argument("pairs", nargs="+")
    a = ap.parse_args()
    out = open(a.out, "w")
    out.write("PTPLAN 2\n")
    nsite = nval = ndrop = 0
    if a.sites:
        for p in a.pairs:
            img, sf = p.split("=", 1)
            out.write("I %s\n" % os.path.abspath(img))
            for ln in open(sf):
                ln = ln.strip()
                if not ln or ln[0] == "#" or not ln.startswith("0x"):
                    continue
                addr, _, toks = ln.partition(" ")
                before, after = [], []
                for tok in toks.split(","):
                    f = tok.split(":")
                    if len(f) < 4:
                        continue
                    when, kind, arg = f[0], f[1], f[2]
                    if kind == "r":
                        it = items_for_reg(arg)
                        if it is None:
                            ndrop += 1
                            continue
                    elif kind == "m":
                        it = ["m:%d" % int(arg)]
                    else:
                        ndrop += 1          # `g` (ground truth) and anything else
                        continue
                    (after if when == "A" else before).extend(it)
                for w, v in (("B", before), ("A", after)):
                    if v:
                        out.write("S %x %s %d %s\n"
                                  % (int(addr, 16), w, len(v), " ".join(v)))
                        nsite += 1
                        nval += len(v)
        out.close()
        sys.stderr.write("[mkplan] %s: %d instrumented addresses, %d values/execution, "
                         "%d unloggable ops dropped\n" % (a.out, nsite, nval, ndrop))
        return

    for p in a.pairs:
        if a.auto:
            spec = p
            img = json.load(open(spec))["image"]
        else:
            img, spec = p.split("=", 1)
        d = json.load(open(spec))
        out.write("I %s\n" % os.path.abspath(img))
        # group by (addr, when); the order inside a group is ascending site id
        groups = {}
        for s in sorted(d["sites"], key=lambda s: s["id"]):
            when = "A" if s.get("when") == "after" or s["kind"] == "load" else "B"
            groups.setdefault((s["addr"], when), []).append(s)
        for (addr, when) in sorted(groups):
            items = []
            for s in groups[(addr, when)]:
                if s["kind"] == "reg":
                    for r in s["regs"]:
                        it = items_for_reg(r)
                        if it is None:
                            ndrop += 1
                            items = None
                            break
                        items += it
                elif s["kind"] == "load":
                    it = items_for_reg(s["reg"])
                    if it is None:
                        ndrop += 1
                        items = None
                        break
                    items += it
                elif s["kind"] == "memop":
                    items.append("m:%d" % int(s.get("size", 8)))
                else:
                    raise SystemExit("mkplan: unknown site kind %r" % s["kind"])
                if items is None:
                    break
            if not items:
                continue
            out.write("S %x %s %d %s\n" % (addr, when, len(items), " ".join(items)))
            nsite += 1
            nval += len(items)
    out.close()
    sys.stderr.write("[mkplan] %s: %d instrumented addresses, %d values/execution, "
                     "%d unloggable sites dropped\n" % (a.out, nsite, nval, ndrop))


if __name__ == "__main__":
    main()
