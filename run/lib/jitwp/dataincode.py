#!/usr/bin/env python3
"""dataincode.py -- protect code bytes that code READS AS DATA.

Some images keep data inside their executable sections and read it with rip-relative loads.  For
example, V8's x64 embedded builtins in node reuse the imm64 of an earlier `movabs` by a rip-relative
`mov r, [rip+d]`.  Any rewriter byte over such a range (the site's
own jmp+int3 fill, an E9Patch eviction of a neighbouring NON-site instruction) silently changes a
constant some other code loads.  The rule is general, not node-specific: every rip-relative memory
operand (except lea/nop/prefetch, which do not read) whose target lies in an executable section is
protected, for any image.

  scan  IMG SPEC OUTPREFIX   -> OUTPREFIX.prot.json   [[lo,hi),...]   the read ranges
                                OUTPREFIX.drop.txt    spec sites to avoid (in, or <= 2 instructions before,
                                                      a protected range)
                                OUTPREFIX.e9excl      rewrite.py args: --e9tool-arg=--exclude=LB..UB over
                                                      every instruction overlapping a protected range
  verify IMG E9 PROT          -> prints `protected ranges changed: N`; exit 1 if N != 0
"""
import bisect, json, re, subprocess, sys


def exec_sections(img):
    out = subprocess.run(['readelf', '-SW', img], capture_output=True, text=True).stdout
    secs = []
    for l in out.splitlines():
        m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\S+\s+(\S*X\S*)\s', l)
        if m and int(m.group(3), 16):
            lo = int(m.group(2), 16)
            secs.append((m.group(1), lo, lo + int(m.group(3), 16)))
    return secs


def width(ops):
    return 32 if '%ymm' in ops else 16 if '%xmm' in ops else 8


def scan(img, spec, pre):
    secs = exec_sections(img)
    ins, reads = [], []
    for name, lo, hi in secs:
        p = subprocess.Popen(['objdump', '-d', '-w', '--no-show-raw-insn', '-j', name, img],
                             stdout=subprocess.PIPE, text=True)
        for l in p.stdout:
            m = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', l)
            if not m:
                continue
            a = int(m.group(1), 16)
            ins.append(a)
            ops = m.group(3)
            if '(%rip)' not in ops or m.group(2).startswith(('lea', 'nop', 'prefetch')):
                continue
            mm = re.search(r'# ([0-9a-f]+)', ops)
            if not mm:
                continue
            t = int(mm.group(1), 16)
            if any(slo <= t < shi for _, slo, shi in secs):
                reads.append((t, t + width(ops), a))
        p.wait()
    ins = sorted(set(ins))
    prot = sorted({(t, h) for t, h, _ in reads})
    # instructions overlapping a protected range -> e9tool --exclude (merged, half-open LB..UB)
    ex = []
    for t, h in prot:
        k = max(0, bisect.bisect_right(ins, t) - 1)
        lb = ins[k]
        j = k
        while j + 1 < len(ins) and ins[j + 1] < h:
            j += 1
        ub = ins[j + 1] if j + 1 < len(ins) else h
        ex.append((lb, max(ub, h)))
    ex.sort()
    merged = []
    for lb, ub in ex:
        if merged and lb <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], ub)
        else:
            merged.append([lb, ub])
    # spec sites whose patch could reach the protected bytes
    sites = sorted(s['addr'] for s in json.load(open(spec))['sites'])
    drop = set()
    for t, h in prot:
        k = max(0, bisect.bisect_right(ins, t) - 1)
        lo = ins[max(0, k - 2)]
        i = bisect.bisect_left(sites, lo)
        while i < len(sites) and sites[i] < h:
            drop.add(sites[i]); i += 1
    json.dump([[t, h] for t, h in prot], open(pre + '.prot.json', 'w'))
    open(pre + '.drop.txt', 'w').write(''.join('%#x\n' % s for s in sorted(drop)))
    open(pre + '.e9excl', 'w').write(' '.join('--e9tool-arg=--exclude=%#x..%#x' % (a, b) for a, b in merged))
    print('[dataincode] %s: %d rip-relative reads into code, %d ranges, %d exclude ranges (%d B), %d sites dropped'
          % (img, len(reads), len(prot), len(merged), sum(b - a for a, b in merged), len(drop)))


def verify(img, e9, protf):
    import os
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'ptracer', 'runtime'))
    import rewrite as R
    a, b = R.VirtualImage(e9), R.VirtualImage(img)
    bad = [t for t, h in json.load(open(protf)) if a.read(t, h - t) != b.read(t, h - t)]
    print('[dataincode] %s: protected ranges changed: %d %s' % (e9, len(bad), ' '.join('%#x' % t for t in bad[:20])))
    return 1 if bad else 0


if __name__ == '__main__':
    if sys.argv[1] == 'scan':
        scan(*sys.argv[2:5])
    elif sys.argv[1] == 'verify':
        sys.exit(verify(*sys.argv[2:5]))
    else:
        sys.exit(__doc__)
