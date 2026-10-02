#!/usr/bin/env python3
"""PTracer v2 Stage 2 -- rewrite an ELF image so that it logs its critical values.

Input : a spec v2 JSON (SPEC_FORMAT.md section 1) and the ELF it describes.
Output: the rewritten ELF + the site map JSON (SPEC_FORMAT.md section 2).

    ./rewrite.py SPEC.json IMAGE -o OUT [--sink ptwrite|buffer] [--shared]
                 [--exclude-ranges a-b,c-d] [--space 3] [--sync 4096]

What it does
    1. Reads the spec, drops sites in --exclude-ranges, groups sites by
       instruction address and orders each group by the SPEC_FORMAT rule
       (all `before` sites by ascending id, the instruction, then all `after`
       sites by ascending id; registers within a `reg` site in listed order).
    2. Emits the flat site file consumed by runtime/e9plugin/ptlog.cpp and runs
       e9tool with that plugin.  Every listed address must be patched:
       `num_patched != expected` is a hard error (no silent drops).
    3. Rebuilds the rewritten binary's *virtual* address space (E9Patch does not
       use program headers for its trampolines -- the loader mmap()s them from
       the table in the embedded "E9PATCH" config), follows the patch jump at
       every instrumented address into its trampoline, disassembles the
       trampoline and records where each logging instruction and each displaced
       original instruction ended up.  That is the site map.

Addresses
    Spec addresses are link-time virtual addresses, which is exactly what
    e9tool matches on, for both PIE (offsets from the load base) and non-PIE
    (absolute).  No adjustment is applied; the ELF type is cross-checked
    against the spec's "pie" field instead.
"""

import argparse
import json
import os
import re
import struct
import subprocess
import sys
from typing import NamedTuple

HERE        = os.path.dirname(os.path.abspath(__file__))
# The E9Patch checkout (e9tool, e9patch, e9compile.sh): <root>/third_party/e9patch
# by default, relative to this file's location; E9PATCH_DIR overrides it.
E9PATCH_DIR = os.environ.get(
    "E9PATCH_DIR",
    os.path.realpath(os.path.join(HERE, "..", "..", "..", "third_party", "e9patch")))
# %gs offset of the ground-truth address cursor -- the ABI shared with
# runtime/rt/ptlogrt.c (`PTLOG_GT_OFF', checked there by a static assert).
GT_OFF      = 512
PLUGIN      = os.path.join(HERE, "e9plugin", "ptlog.so")
RT_E9       = os.path.join(HERE, "rt", "ptlogrt.e9rt")
RT_E9_NOFINI = os.path.join(HERE, "rt", "ptlogrt_nofini.e9rt")
LDFIX_E9    = os.path.join(HERE, "rt", "ldfix.e9rt")

PAGE = 4096

GP = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
      "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]


# --------------------------------------------------------------------------
# ELF helpers (just enough; no external dependency)
# --------------------------------------------------------------------------

class Elf:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 2:
            raise SystemExit("%s: not a 64-bit ELF" % path)
        self.etype = struct.unpack_from("<H", d, 16)[0]        # 2=EXEC 3=DYN
        self.phoff = struct.unpack_from("<Q", d, 32)[0]
        self.phentsize, self.phnum = struct.unpack_from("<HH", d, 54)
        self.loads = []          # (vaddr, memsz, off, filesz, flags)
        for i in range(self.phnum):
            o = self.phoff + i * self.phentsize
            p_type, p_flags = struct.unpack_from("<II", d, o)
            p_off, p_vaddr = struct.unpack_from("<QQ", d, o + 8)[0], \
                             struct.unpack_from("<Q", d, o + 16)[0]
            p_filesz, p_memsz = struct.unpack_from("<QQ", d, o + 32)
            if p_type == 1:      # PT_LOAD
                self.loads.append((p_vaddr, p_memsz, p_off, p_filesz, p_flags))

    @property
    def pie(self):
        return self.etype == 3

    @property
    def has_fini(self):
        """Does the image have a DT_FINI or DT_FINI_ARRAY entry?

        `libc.so.6' has NEITHER (only DT_INIT_ARRAY), and E9Patch has to hook one
        of them to run an injected runtime's `fini()':

            error: failed to replace finalization point; no DT_FINI or
                   DT_FINI_ARRAY entry found

        so the buffer sink's runtime has to be given to libc in a flavour that
        exports no `fini' at all (rt/ptlogrt_nofini.e9rt).
        """
        d = self.data
        for i in range(self.phnum):
            o = self.phoff + i * self.phentsize
            p_type = struct.unpack_from("<I", d, o)[0]
            if p_type != 2:                       # PT_DYNAMIC
                continue
            off = struct.unpack_from("<Q", d, o + 8)[0]
            filesz = struct.unpack_from("<Q", d, o + 32)[0]
            for k in range(0, filesz - 15, 16):
                tag = struct.unpack_from("<q", d, off + k)[0]
                if tag == 0:                      # DT_NULL
                    break
                if tag in (13, 26):               # DT_FINI, DT_FINI_ARRAY
                    return True
        return False

    @property
    def end(self):
        """Link-time vaddr just past the last PT_LOAD."""
        return max(v + m for v, m, _o, _f, _fl in self.loads)


# --------------------------------------------------------------------------
# The rewritten binary's virtual address space
# --------------------------------------------------------------------------

E9_MAGIC = b"E9PATCH\x00"


def parse_e9_config(data):
    """Locate the embedded E9Patch loader config; return (file_off, dict)."""
    off = data.find(E9_MAGIC)
    if off < 0:
        return None, None
    # struct e9_config_s (src/e9patch/e9loader.h)
    #   char magic[8]; char version[16]; u32 flags; u32 size; intptr base;
    #   intptr entry; intptr fini; intptr mmap; u32 num_maps[2]; u32 maps[2];
    #   ... u32 num_traps; u32 traps; u32 handler;
    b = off
    flags, size = struct.unpack_from("<II", data, b + 24)
    base, entry, fini, mmapf = struct.unpack_from("<qqqq", data, b + 32)
    num_maps = struct.unpack_from("<II", data, b + 64)
    maps = struct.unpack_from("<II", data, b + 72)
    (num_preinits, preinits, num_postinits, postinits,
     num_inits, inits, num_finis, finis,
     num_traps, traps, handler) = struct.unpack_from("<11I", data, b + 80)
    return off, dict(flags=flags, size=size, base=base, entry=entry,
                     num_maps=num_maps, maps=maps,
                     num_traps=num_traps, traps=traps)


class VirtualImage:
    """Byte-accurate model of the rewritten process image (ASLR base 0)."""

    def __init__(self, path):
        self.elf = Elf(path)
        d = self.elf.data
        self.regions = []        # (vaddr, size, file_off, exec)
        for vaddr, memsz, off, filesz, flags in self.elf.loads:
            self.regions.append((vaddr, filesz, off, bool(flags & 1)))
        self.cfg_off, self.cfg = parse_e9_config(d)
        self.tramp_regions = []
        if self.cfg is not None:
            for k in (0, 1):
                n, moff = self.cfg["num_maps"][k], self.cfg["maps"][k]
                # struct e9_map_s: int32 addr; uint32 offset;
                #                  uint32 size:20, type:2, rsvd:6, r,w,x,abs
                for i in range(n):
                    o = self.cfg_off + moff + i * 12
                    addr, offset, bits = struct.unpack_from("<iII", d, o)
                    size = bits & 0xfffff
                    typ = (bits >> 20) & 0x3
                    x = (bits >> 30) & 1
                    va = addr * PAGE           # base is 0 in our model
                    reg = (va, size * PAGE, offset * PAGE, bool(x))
                    self.regions.append(reg)   # later maps override earlier
                    if x and typ == 0:
                        self.tramp_regions.append(reg)

    # An address -> region index.  A linear scan of `self.regions' is fine for
    # a spec-sized rewrite (a few hundred regions) and quadratic for a
    # `--gt-all' one: E9Patch gives every trampoline group its own map, and
    # `build_sitemap' does many reads per trampoline.
    # The index is page -> the regions covering that page IN ORDER, so `read'
    # keeps the "later regions win" rule and touches only the (usually one)
    # region that actually covers the address.
    def _index(self):
        pages = getattr(self, "_pages", 0)
        if pages != 0:
            return pages
        pages = {}
        for reg in self.regions:
            va, size, _off, _x = reg
            if size <= 0:
                continue
            p0, p1 = va >> 12, (va + size + PAGE - 1) >> 12
            if p1 - p0 > (1 << 22):        # implausible: fall back to scanning
                self._pages = None
                return None
            for p in range(p0, p1):
                pages.setdefault(p, []).append(reg)
        self._pages = pages
        return pages

    def read(self, vaddr, n):
        """Read up to n bytes at vaddr, STITCHING consecutive mappings.

        Later regions win (the loader mmap()s them MAP_FIXED, in order), so the
        winner for a given address is the LAST region that contains it.

        E9Patch maps the *same file page* at several consecutive virtual pages
        (physical-page aliasing), so a trampoline body that crosses a 4 KiB
        boundary is valid at run time; clamping the read to the single region
        containing `vaddr` would make it undecodable.  The read therefore
        continues into the adjacent mapping until it has `n` bytes or hits a
        hole.
        """
        pages = self._index()
        out = []
        got = 0
        addr = vaddr
        while got < n:
            cands = self.regions if pages is None else pages.get(addr >> 12, ())
            piece = None
            for va, size, off, _x in cands:             # later regions win
                if va <= addr < va + size:
                    fo = off + (addr - va)
                    avail = min(n - got, va + size - addr)
                    piece = self.elf.data[fo:fo + avail]
            if not piece:
                break
            out.append(piece)
            got += len(piece)
            addr += len(piece)
        return b"".join(out) if out else None

    def traps(self):
        """The rip -> trampoline table used by E9Patch's T1/T2/T3 tactics."""
        res = {}
        if self.cfg is None or self.cfg["num_traps"] == 0:
            return res
        d = self.elf.data
        for i in range(self.cfg["num_traps"]):
            o = self.cfg_off + self.cfg["traps"] + i * 16
            rip, tramp = struct.unpack_from("<qq", d, o)
            res[rip] = tramp
        return res


# --------------------------------------------------------------------------
# Spec -> site file
# --------------------------------------------------------------------------

def parse_ranges(s):
    out = []
    if not s:
        return out
    for part in s.split(","):
        part = part.strip()
        if not part:
            continue
        lo, _, hi = part.partition("-")
        out.append((int(lo, 0), int(hi, 0)))
    return out


class Op(NamedTuple):
    """One logged VALUE of one site, as the site file describes it.

    `dead` is EFLAGS liveness; `dregs` is a bitmask of the 64-bit GP registers
    the analyzer says are DEAD at this program point (bit i = x86 encoding i,
    never %rsp) -- the emitter takes its scratch registers from there instead
    of pushing them.  `noguard` says "never wrap
    this value in a log-on-change guard", which is a DIFFERENT statement from
    "EFLAGS is live here": the delta profile disqualifies values whose flags
    are perfectly dead, and the buffer sink's cursor update wants to know that.
    """
    after:   bool
    kind:    str
    arg:     str
    sid:     int
    dead:    bool
    resync:  bool
    kf:      int
    dregs:   int = 0
    noguard: bool = False
    # MIXED SINK: which sink logs THIS value.
    # ""     = the build's global --sink
    # "ptw"  = Intel PT `ptwrite'   (site-file field `:W')
    # "buf"  = the per-thread value buffer (site-file field `:B')
    sink:    str = ""


def build_groups(spec, excludes, keyframe=None, keyframe_flags_live="drop"):
    """addr -> ordered list of `Op`s, following the SPEC_FORMAT ordering rule.

    A spec site with `"resync": true` (ANALYZER_VERSION v2.15) is a
    RE-ANCHOR site at a loop back-edge header: its registers are logged
    unconditionally, but only every K-th execution, so a PT overflow or decoder
    resync inside a long-running loop costs at most K iterations of unknown
    addresses instead of the rest of the run.
    `keyframe` overrides the spec's per-site K; `keyframe=0` drops the resync
    sites altogether, which is how the same spec measures with and without them.
    """
    groups = {}
    dropped = []
    n_resync = n_resync_live = 0
    for s in spec["sites"]:
        addr = int(s["addr"])
        if any(lo <= addr < hi for lo, hi in excludes):
            dropped.append((addr, s.get("id"), "excluded range"))
            continue
        kind = s["kind"]
        when = s.get("when", "before" if kind != "load" else "after")
        sid = int(s.get("id", -1))
        # ANALYZER_VERSION v2.8+: is EFLAGS dead at this site?  The log-on-change
        # trampoline compares the value against its cache slot, which clobbers
        # the flags, so only a site with dead flags can be guarded for free.
        dead = bool(s.get("flags_dead", False))
        # ANALYZER_VERSION v2.17: GP registers that are DEAD at this site, so a
        # trampoline may use them as scratch without push/pop.  Re-verified
        # against the image's own bytes by `verify_dead_regs'.
        dregs = regmask(s.get("dead_regs") or ())
        resync = bool(s.get("resync", False))
        kf = 0
        if resync:
            kf = int(s.get("keyframe", 0)) if keyframe is None else int(keyframe)
            n_resync += 1
            if kf == 0:
                dropped.append((addr, sid, "resync site, --keyframe 0"))
                continue
            # The keyframe counter's `dec' clobbers EFLAGS exactly as the
            # log-on-change `cmp' does.  A resync site is a pure accuracy bonus,
            # never a correctness requirement, and it sits at a LOOP HEADER --
            # logging it unconditionally would cost one PTWRITE per iteration of
            # the hottest loop in the program.  So the default for a site whose
            # flags are live is to drop it, not to log it.
            if not dead:
                n_resync_live += 1
                if keyframe_flags_live == "drop":
                    dropped.append((addr, sid, "resync site with live EFLAGS"))
                    continue
                kf = 0          # `log': no counter guard, log on every execution
        ops = []
        if kind == "reg":
            for r in s["regs"]:
                ops.append(Op(when == "after", "r", r, sid, dead, resync, kf,
                              dregs))
        elif kind == "load":
            ops.append(Op(True, "r", s["reg"], sid, dead, resync, kf, dregs))
        elif kind == "memop":
            ops.append(Op(when == "after", "m", str(int(s["size"])), sid, dead,
                          resync, kf, dregs))
        else:
            raise SystemExit("spec: unknown site kind %r (site %d)" % (kind, sid))
        groups.setdefault(addr, []).append((sid, ops))
    ordered = {}
    for addr, entries in groups.items():
        befores, afters = [], []
        for sid, ops in sorted(entries, key=lambda e: e[0]):
            for op in ops:
                (afters if op.after else befores).append(op)
        ordered[addr] = befores + afters
    if n_resync:
        print("[rewrite] %d resync (keyframe) site(s) in the spec, %d of them "
              "with EFLAGS the SPEC calls live (--keyframe-flags-live %s); the "
              "rewriter re-checks the rest itself"
              % (n_resync, n_resync_live, keyframe_flags_live))
    return ordered, dropped


def expected_values(groups, spec=None):
    """How many logged VALUES the spec's groups must produce.

    One per op, except an `xmm` register, which is logged as two 64-bit halves
    (`lo` then `hi`).  This is the number of `entries` a complete site map has;
    a shorter one means the scanner lost a site.
    """
    n = 0
    for ops in groups.values():
        for op in ops:
            kind, arg = op.kind, op.arg
            n += 2 if (kind == "r" and arg.startswith("xmm")) else 1
    return n


# --------------------------------------------------------------------------
# Is EFLAGS really dead here?  (independent check of the spec's `flags_dead')
# --------------------------------------------------------------------------
#
# The log-on-change guard's `cmp' clobbers CF PF AF ZF SF OF, so it may only be
# emitted where those six are dead.  The analyzer says so per site
# (`flags_dead'), but that claim can be wrong where the recovered CFG misses a
# successor -- e.g. the very next instruction on the fall-through path reads a
# flag the guard destroys.  The rewriter therefore checks the claim itself, from the bytes it already has: a
# bounded exploration of the local CFG from the point where the `cmp' would sit,
# following both arms of a conditional branch, that answers "may one of the six
# be READ before it is rewritten?".  Anything it cannot follow (an indirect
# jump, an address outside the image, too much code) counts as LIVE.  A site it
# rejects is logged unconditionally, exactly as `flags_dead: false' is.


PLUGIN_STATS = {}        # the emitter's liveness report (see below)
GT_STATS = {}            # the emitter's --gt-all report (see below)
GT_DROP_REFUSED = []     # Oracle probes a retry wanted to drop, refused
LD_SO_MEM_LB = -0x10000000
LD_SO_MEM_UB = -0x100000

_FLAGS6 = ("CF", "PF", "AF", "ZF", "SF", "OF")


# --------------------------------------------------------------------------
# Which GP registers may the trampoline clobber? 
# --------------------------------------------------------------------------
#
# The analyzer publishes `dead_regs` per site, from a backward liveness
# pass on its recovered CFG.  Using a register that is NOT dead corrupts the
# program silently, and the CFG it recovers can miss a successor.  So the rewriter re-derives the answer from the bytes
# it is about to patch and keeps only the INTERSECTION.
#
# The check is a bounded forward exploration of the local CFG from the site,
# then a backward fixpoint on that sub-graph:
#
#   * a `ret`     -- live-out = the callee-saved registers plus %rax (SysV);
#   * a `call`, a `syscall`, an `int` -- live-out = EVERYTHING (the callee may
#     read any argument register);
#   * an indirect jump, an address outside the image, or a budget overrun --
#     live-out = EVERYTHING;
#   * a conditional branch -- both arms.
#
# A partial write (`al`, `ax`) leaves the upper bits, so it counts as a read as
# well as a write; a 32-bit write zero-extends and therefore kills.

_GP64 = ("rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
         "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15")
_GPIDX = {n: i for i, n in enumerate(_GP64)}
_GP_ALL = ((1 << 16) - 1) & ~(1 << 4)          # every GP register but %rsp
# SysV: what a `ret` leaves live.
_GP_RET = ((1 << _GPIDX["rax"]) | (1 << _GPIDX["rbx"]) | (1 << _GPIDX["rbp"]) |
           (1 << _GPIDX["r12"]) | (1 << _GPIDX["r13"]) | (1 << _GPIDX["r14"]) |
           (1 << _GPIDX["r15"])) & _GP_ALL


def regmask(names):
    """{'rax','r11'} -> bitmask (bit i = x86 encoding i).  %rsp is never in it."""
    m = 0
    for n in names:
        i = _GPIDX.get(n)
        if i is not None and i != 4:
            m |= 1 << i
    return m


def regnames(mask):
    return [_GP64[i] for i in range(16) if mask & (1 << i)]


def _build_subreg_table():
    """capstone register name -> (index, kills the whole 64-bit register)."""
    t = {}
    for i, r in enumerate(_GP64):
        t[r] = (i, True)
        if r[1:].isdigit():                     # r8 .. r15
            t[r + "d"] = (i, True)              # 32-bit write zero-extends
            t[r + "w"] = (i, False)
            t[r + "b"] = (i, False)
        else:
            body = r[1:]                        # ax, cx, ..., sp, bp, si, di
            t["e" + body] = (i, True)
            t[body] = (i, False)
            if body in ("ax", "cx", "dx", "bx"):
                t[body[0] + "l"] = (i, False)
                t[body[0] + "h"] = (i, False)
            else:
                t[body + "l"] = (i, False)
    return t


_SUBREG = _build_subreg_table()


class RegLiveness:
    """`dead(addr)` -> bitmask of the GP registers provably dead at `addr`."""

    def __init__(self, cs, md, read_at, max_insn=128, max_nodes=48):
        self.cs, self.md, self.read_at = cs, md, read_at
        self.max_insn, self.max_nodes = max_insn, max_nodes
        self._facts = {}                        # addr -> (read, kill, kind, succs)
        self._cache = {}                        # addr -> dead mask

    # kind: 0 normal, 1 ret, 2 call/syscall/int, 3 unknown (everything live)
    def _fact(self, addr):
        f = self._facts.get(addr)
        if f is not None:
            return f
        b = self.read_at(addr)
        ins = next(self.md.disasm(b, addr), None) if b else None
        if ins is None:
            f = (0, 0, 3, ())
        else:
            try:
                rd, wr = ins.regs_access()
            except Exception:
                rd, wr = (), ()
            read = kill = 0
            for r in rd:
                e = _SUBREG.get(ins.reg_name(r) or "")
                if e:
                    read |= 1 << e[0]
            for r in wr:
                e = _SUBREG.get(ins.reg_name(r) or "")
                if e:
                    if e[1]:
                        kill |= 1 << e[0]
                    else:
                        read |= 1 << e[0]       # partial write: upper bits survive
            g = ins.group
            cs = self.cs
            if g(cs.CS_GRP_RET) or _mn(ins) in ("ret", "retf", "iret", "iretq"):
                f = (read, kill, 1, ())
            elif g(cs.CS_GRP_CALL) or g(cs.CS_GRP_INT) or \
                    _mn(ins) in ("syscall", "sysenter", "hlt", "ud2"):
                f = (read, kill, 2, ())
            elif g(cs.CS_GRP_JUMP):
                t = _jmp_target(ins)
                if _mn(ins) == "jmp":
                    f = ((read, kill, 0, (t,)) if t is not None
                         else (read, kill, 3, ()))
                elif t is not None:
                    f = (read, kill, 0, (t, ins.address + ins.size))
                else:
                    f = (read, kill, 3, ())
            else:
                f = (read, kill, 0, (ins.address + ins.size,))
        self._facts[addr] = f
        return f

    def dead(self, start):
        m = self._cache.get(start)
        if m is not None:
            return m
        # ---- forward exploration ------------------------------------------
        nodes, order, work = {}, [], [start]
        budget = self.max_insn
        overrun = False
        while work:
            a = work.pop()
            if a in nodes:
                continue
            if len(nodes) >= self.max_nodes or budget <= 0:
                overrun = True
                break
            budget -= 1
            read, kill, kind, succs = self._fact(a)
            nodes[a] = (read, kill, kind, succs)
            order.append(a)
            if kind == 0:
                for t in succs:
                    if t not in nodes:
                        work.append(t)
        if overrun or start not in nodes:
            self._cache[start] = 0
            return 0
        # ---- backward fixpoint --------------------------------------------
        live = {a: 0 for a in nodes}
        for _ in range(self.max_nodes + 2):
            changed = False
            for a in reversed(order):
                read, kill, kind, succs = nodes[a]
                if kind == 1:
                    out = _GP_RET
                elif kind >= 2:
                    out = _GP_ALL
                else:
                    out = 0
                    for t in succs:
                        # a successor the exploration never expanded (it hit the
                        # budget) is unknown: everything is live there
                        out |= live[t] if t in live else _GP_ALL
                n = read | (out & ~kill)
                if n != live[a]:
                    live[a] = n
                    changed = True
            if not changed:
                break
        m = _GP_ALL & ~live[start]
        self._cache[start] = m
        return m


def verify_dead_regs(image, groups, mode="both", report=None):
    """Keep only the dead registers the image's own bytes confirm.

    Mutates `groups` in place.  Returns (n_offered, n_kept, n_ops_with_scratch).
    `mode` is `spec` (trust the analyzer), `verify` (use only this check) or
    `both` (the intersection, the default).
    """
    cs, md = load_capstone()
    md.detail = True
    elf = Elf(image)

    def read_at(addr, n=16):
        for vaddr, memsz, off, filesz, flags in elf.loads:
            if flags & 1 and vaddr <= addr < vaddr + filesz:
                fo = off + (addr - vaddr)
                return elf.data[fo:fo + n]
        return None

    rl = RegLiveness(cs, md, read_at)
    n_off = n_keep = n_any = 0
    at_cache = {}
    for addr, ops in groups.items():
        for i, op in enumerate(ops):
            spec = op.dregs
            n_off += bin(spec).count("1")
            if mode == "spec":
                if spec:
                    n_any += 1
                n_keep += bin(spec).count("1")
                continue
            key = (addr, op.after)
            at = at_cache.get(key)
            if at is None:
                at = addr
                if op.after:
                    ins = _decode_one(md, read_at(addr) or b"", addr)
                    at = -1 if ins is None else addr + ins.size
                at_cache[key] = at
            got = 0 if at < 0 else rl.dead(at)
            m = got if mode == "verify" else (spec & got)
            if m != spec:
                ops[i] = op._replace(dregs=m)
            n_keep += bin(m).count("1")
            if m:
                n_any += 1
    md.detail = False
    if report:
        with open(report, "w") as f:
            f.write("# scratch registers per logged value (mode=%s): the "
                    "analyzer's `dead_regs' and\n# the rewriter's own check of "
                    "the image's bytes.\n# addr\twhen\tsite\tkept\n" % mode)
            for addr in sorted(groups):
                for op in groups[addr]:
                    f.write("0x%x\t%s\t%d\t%s\n"
                            % (addr, "after" if op.after else "before", op.sid,
                               ",".join(regnames(op.dregs)) or "-"))
    return n_off, n_keep, n_any



def _flag_masks(capstone_mod):
    x86 = capstone_mod.x86
    test, write = [], []
    for f in _FLAGS6:
        test.append(getattr(x86, "X86_EFLAGS_TEST_" + f, 0))
        m = 0
        for k in ("MODIFY", "RESET", "SET", "UNDEFINED", "PRIOR"):
            m |= getattr(x86, "X86_EFLAGS_%s_%s" % (k, f), 0)
        write.append(m)
    return test, write


class FlagsChecker:
    """`live(addr)`: may `cmp`'s six flags be read at `addr` before rewritten?"""

    ALL = (1 << 6) - 1

    def __init__(self, cs, md, read_at, max_insn=192, max_nodes=48):
        self.cs, self.md, self.read_at = cs, md, read_at
        self.test, self.write = _flag_masks(cs)
        self.max_insn, self.max_nodes = max_insn, max_nodes
        self._cache = {}

    # Instructions whose EFLAGS writes capstone's `eflags` field does not model
    # (or models incompletely).  The value is the set of the six flags the
    # instruction REdefines, as a bit mask over `_FLAGS6`; anything not listed
    # keeps capstone's answer.
    #
    #  * SSE4.2's string compares set CF ZF SF OF (and clear AF PF), which is
    #    the whole point of the `ja`/`jc` that follows them; without this the
    #    checker believes the flags survive from before the `pcmpistri` and
    #    calls the site live.
    #  * The scalar FP compares `comiss/comisd/ucomiss/ucomisd` set ZF PF CF
    #    from the comparison and clear OF SF AF, i.e. they redefine all six.
    #    Capstone models the SSE forms but reports `eflags == 0` for every VEX
    #    form (`vcomisd`, ...).
    #  * `fcomi/fcomip/fucomi/fucomip` set ZF PF CF; capstone reports PF AF SF
    #    OF for `fcomi` and nothing at all for `fucomip`.  Only ZF PF CF are
    #    claimed here -- the SDM also clears OF SF AF, but claiming less is the
    #    sound direction (a flag we do not claim is simply treated as still
    #    live), and ZF PF CF are the ones the following `ja`/`jbe` reads.
    _CF, _PF, _AF, _ZF, _SF, _OF = (1 << i for i in range(6))
    _WRITES_MASK = {}
    for _m in ("pcmpistri", "pcmpistrm", "pcmpestri", "pcmpestrm",
               "vpcmpistri", "vpcmpistrm", "vpcmpestri", "vpcmpestrm",
               "comiss", "comisd", "ucomiss", "ucomisd",
               "vcomiss", "vcomisd", "vucomiss", "vucomisd"):
        _WRITES_MASK[_m] = (1 << 6) - 1
    for _m in ("fcomi", "fcomip", "fucomi", "fucomip",
               # capstone spells the popping forms `fcompi'/`fucompi'
               "fcompi", "fucompi"):
        _WRITES_MASK[_m] = _CF | _PF | _ZF
    del _m

    def _decode(self, addr):
        ins = self._cache.get(addr)
        if ins is None:
            b = self.read_at(addr)
            ins = next(self.md.disasm(b, addr), False) if b else False
            self._cache[addr] = ins
        return ins or None

    def live(self, start):
        """None if the six flags are provably dead at `start`, else WHY not.

        The reason matters: `read CF at 0x...` says the spec is wrong, while
        `gave up: indirect jump` only says this check could not follow the code
        and answered LIVE to stay sound.  `verify_flags_dead` writes it out, so
        `<out>.flags_live` distinguishes an analyzer error from the rewriter's
        own conservatism.
        """
        cs = self.cs
        work = [(start, 0)]
        seen = set()
        n = 0
        while work:
            addr, w = work.pop()
            while True:
                if (addr, w) in seen:
                    break
                seen.add((addr, w))
                if len(seen) > self.max_nodes:
                    return "gave up: too many CFG states"
                n += 1
                if n > self.max_insn:
                    return "gave up: too much code"
                ins = self._decode(addr)
                if ins is None:
                    return "gave up: cannot decode %#x (outside the image?)" % addr
                for i in range(6):
                    if (ins.eflags & self.test[i]) and not (w & (1 << i)):
                        return "read %s at %#x (%s %s)" % (
                            _FLAGS6[i], ins.address, ins.mnemonic, ins.op_str)
                w |= self._WRITES_MASK.get(_mn(ins), 0)
                for i in range(6):
                    if ins.eflags & self.write[i]:
                        w |= 1 << i
                if w == self.ALL:
                    break                       # all six redefined: dead
                g = ins.group
                if g(cs.CS_GRP_RET) or g(cs.CS_GRP_CALL) or g(cs.CS_GRP_INT):
                    # SysV leaves the arithmetic flags scratch across a call, and
                    # a `ret' ends the region: dead either way.
                    break
                if g(cs.CS_GRP_JUMP):
                    if ins.mnemonic == "jmp":
                        t = _jmp_target(ins)
                        if t is None:
                            return "gave up: indirect jump at %#x (%s %s)" % (
                                ins.address, ins.mnemonic, ins.op_str)
                        addr = t
                        continue
                    # conditional: both arms
                    try:
                        t = int(ins.op_str, 0)
                    except ValueError:
                        return "gave up: indirect branch at %#x" % ins.address
                    work.append((t, w))
                    addr += ins.size
                    continue
                addr += ins.size
        return None


def verify_flags_dead(image, groups, report=None):
    """Downgrade every op whose `flags_dead` the local CFG contradicts.

    Mutates `groups` in place (the op tuple's 5th field) and returns how many
    ops were downgraded.  The point where the guard's `cmp` sits is the site
    address for a `before` op and just past the instruction for an `after` one.
    """
    cs, md = load_capstone()
    md.detail = True                     # eflags/groups need the detail decoder
    elf = Elf(image)

    def read_at(addr, n=16):
        for vaddr, memsz, off, filesz, flags in elf.loads:
            if flags & 1 and vaddr <= addr < vaddr + filesz:
                fo = off + (addr - vaddr)
                return elf.data[fo:fo + n]
        return None

    fc = FlagsChecker(cs, md, read_at)
    live_cache, n = {}, 0
    bad = []
    for addr, ops in groups.items():
        for i, op in enumerate(ops):
            after, sid, dead = op.after, op.sid, op.dead
            if not dead:
                continue
            key = (addr, after)
            if key not in live_cache:
                at = addr
                if after:
                    ins = _decode_one(md, read_at(addr) or b"", addr)
                    at = None if ins is None else addr + ins.size
                live_cache[key] = ("gave up: cannot decode the site"
                                   if at is None else fc.live(at))
            why = live_cache[key]
            if why:
                ops[i] = op._replace(dead=False)
                bad.append((addr, "after" if after else "before", sid, why))
                n += 1
    md.detail = False
    kinds = {}
    for _a, _w, _s, why in bad:
        kinds[why.split(" at ")[0]] = kinds.get(why.split(" at ")[0], 0) + 1
    if report and bad:
        with open(report, "w") as f:
            f.write("# %d logged value(s) the spec marks `flags_dead' that this "
                    "rewriter's own CFG check could not confirm.\n"
                    "# A `read ...' reason means the SPEC IS WRONG; a `gave up: "
                    "...' one means only that this\n# check could not follow the "
                    "code and answered LIVE to stay sound.\n" % len(bad))
            f.write("# addr\twhen\tspec site id\treason\n")
            for addr, when, sid, why in bad:
                f.write("0x%x\t%s\t%s\t%s\n" % (addr, when, sid, why))
    return n, kinds


def apply_keyframe_flags_policy(groups, policy):
    """A resync op whose EFLAGS is not (any longer) confirmed dead cannot carry a
    keyframe counter: the guard's `dec' clobbers the flags exactly as the
    log-on-change `cmp' does.  `drop' removes the op (the default -- a resync
    site is an accuracy bonus and it sits at a LOOP HEADER, where logging
    unconditionally costs one PTWRITE per iteration); `log' keeps it without the
    guard.  Returns (dropped ops, ops downgraded to unconditional).

    This runs AFTER `verify_flags_dead', which is what makes it more than a
    re-statement of `build_groups': the rewriter's own CFG check rejects sites
    the spec calls `flags_dead'.
    """
    n_drop = n_log = 0
    for addr in list(groups):
        keep = []
        for op in groups[addr]:
            if op.resync and op.kf and not op.dead:
                if policy == "drop":
                    n_drop += 1
                    continue
                op = op._replace(kf=0)
                n_log += 1
            keep.append(op)
        if keep or not groups[addr]:
            # `not groups[addr]' = a count-only BLOCK-PROFILE site
            # (`--count-blocks'), which has no ops by construction and must not
            # be mistaken for a site whose every op was just dropped.
            groups[addr] = keep
        else:
            del groups[addr]
    return n_drop, n_log


def load_delta_profile(path, image_filter="", key="site"):
    """`ptrecon --site-stats` CSV -> {(site id | addr, arg): (executions, repeats)}.

    The CSV has one row per logged VALUE:
      image,site,orig_addr,tramp_addr,kind,arg,payload_bits,count,repeats,skipped
    `count` is how often the logging instruction ran and `skipped` how often a
    log-on-change guard skipped it, so `count + skipped` is how often the site
    executed and `repeats + skipped` how often the value was unchanged.  A
    profile from a PLAIN build has `skipped = 0` and is just as usable, which is
    the point: the repeat ratio is measurable before the guards exist.

    `key` selects what a row is matched on:
      `site`  the spec site id, which is what the profiled build was built from.
      `addr`  the site's LINK-TIME address.  Site ids are POSITIONAL, so any
              change to the spec -- adding the keyframe `resync' sites, for one
              -- renumbers them and a profile taken from an older spec of the
              SAME BINARY then lands on the wrong values.  Addresses do not
              move.  Two sites at one address logging the same register are
              merged, which is conservative (both see the union of the counts).
    """
    import csv
    prof = {}
    with open(path) as f:
        rd = csv.DictReader(f)
        if "repeats" not in (rd.fieldnames or []):
            raise SystemExit("[rewrite] %s has no `repeats' column: it was "
                             "written by an older ptrecon, re-run "
                             "`ptrecon --site-stats'" % path)
        for row in rd:
            if image_filter and image_filter not in row["image"]:
                continue
            k = (int(row["site"]) if key == "site"
                 else int(row["orig_addr"], 0), row["arg"])
            ex = int(row["count"]) + int(row["skipped"])
            rp = int(row["repeats"]) + int(row["skipped"])
            e, r = prof.get(k, (0, 0))
            prof[k] = (e + ex, r + rp)
    return prof


def apply_delta_profile(groups, prof, min_repeat, min_exec, default_on,
                        report=None, key="site"):
    """Downgrade the ops the profile says the guard would not pay for.

    A guard costs a little when the value changes and saves more when it does
    not, so it breaks even at a low repeat ratio.  A site the profile never saw executing costs nothing either way,
    and is left unguarded by default so it cannot pay the D-cache price of a slot
    it never reads.
    """
    stats = dict(kept=0, low_repeat=0, rare=0, unseen=0)
    rows = []
    for addr, ops in groups.items():
        for i, op in enumerate(ops):
            kind, arg, sid = op.kind, op.arg, op.sid
            if not op.dead or op.resync or op.noguard:
                continue                # a resync site is never log-on-change
            # an xmm register is two logged values; sum them
            k0 = sid if key == "site" else addr
            keys = ([(k0, arg + ".lo"), (k0, arg + ".hi")]
                    if kind == "r" and arg.startswith("xmm") else [(k0, arg)])
            ex = rp = 0
            seen = False
            for k in keys:
                if k in prof:
                    seen = True
                    ex += prof[k][0]
                    rp += prof[k][1]
            if not seen:
                why = None if default_on else "unseen"
            elif ex < min_exec:
                why = "rare"
            elif ex and (rp / float(ex)) < min_repeat:
                why = "low_repeat"
            else:
                why = None
            if why is None:
                stats["kept"] += 1
                continue
            # `noguard', NOT `dead=False': the flags really are dead here,
            # and the buffer sink's cursor update wants to know that.
            ops[i] = op._replace(noguard=True)
            stats[why] += 1
            rows.append((addr, sid, arg, ex, rp, why))
    if report and rows:
        with open(report, "w") as f:
            f.write("# %d logged value(s) the delta profile disqualified "
                    "(min_repeat=%g, min_exec=%d, unseen=%s)\n"
                    % (len(rows), min_repeat, min_exec,
                       "on" if default_on else "off"))
            f.write("# addr\tsite\targ\texecutions\trepeats\treason\n")
            for addr, sid, arg, ex, rp, why in rows:
                f.write("0x%x\t%d\t%s\t%d\t%d\t%s\n" % (addr, sid, arg, ex, rp, why))
    return stats


def count_counters(groups):
    """How many 8-byte keyframe countdown counters the plugin will hand out.

    One per RESYNC SITE (all of its registers share it, so they are logged in
    the same execution), and one per keyframed log-on-change VALUE (an `xmm`
    register is two values, hence two counters).  The plugin checks its own
    total against this number and fails loudly if the two ever disagree.
    """
    n = 0
    for addr in sorted(groups):
        seen = set()
        for op in groups[addr]:
            if not op.kf:
                continue
            if op.resync:
                if (op.after, op.sid) not in seen:
                    seen.add((op.after, op.sid))
                    n += 1
            elif op.dead and not op.noguard:
                n += 2 if (op.kind == "r" and op.arg.startswith("xmm")) else 1
    return n


def op_token(op):
    """One `<when>:<kind>:<arg>:<site>[:D|:L][:R<K>|:K<K>][:N][:S<mask>]` field."""
    t = "%s:%s:%s:%d:%s" % ("A" if op.after else "B", op.kind, op.arg, op.sid,
                            "D" if op.dead else "L")
    if op.resync and op.kf:
        t += ":R%d" % op.kf
    elif op.kf and op.dead and not op.noguard:
        # a log-on-change keyframe is only emitted where the guard is
        t += ":K%d" % op.kf
    if op.noguard:
        t += ":N"
    if op.dregs:
        t += ":S%#x" % op.dregs
    if op.sink == "ptw":
        t += ":W"
    elif op.sink == "buf":
        t += ":B"
    return t


THREAD_SYMS = ("clone", "clone3", "__clone", "pthread_create",
               "__pthread_create_2_1", "thrd_create")


def image_can_clone(path):
    """Does this ELF define or import a thread-creation primitive?

    Used only to decide whether a SHARED keyframe countdown is dangerous here.
    Deliberately generous: a false positive
    costs a warning, a false negative costs silent wrong addresses.  Any failure
    to parse is treated as "yes"."""
    try:
        from elftools.elf.elffile import ELFFile
        from elftools.elf.sections import SymbolTableSection
        with open(path, "rb") as f:
            elf = ELFFile(f)
            for sec in elf.iter_sections():
                if not isinstance(sec, SymbolTableSection):
                    continue
                for sym in sec.iter_symbols():
                    if sym.name in THREAD_SYMS:
                        return True
        return False
    except Exception:
        return True


def write_site_file(path, groups, sink, space, sync, delta=0, slotbase=0,
                    counterbase=0, nslots=0, ncounters=0, liveness=1,
                    gt=False, gtoff=GT_OFF, gtstack=True,
                    count_base=0, count_atomic=False, gt_skip=(),
                    nt_store=False, fixed_slot=False, kf_gs=None,
                    reserve=False, sync_cursor=False, cursor_store=False,
                    sync_carrier="ptwrite"):
    with open(path, "w") as f:
        f.write("# generated by runtime/rewrite.py -- do not edit\n")
        # A MIXED build names `buffer' as the file's
        # global sink -- so the plugin sets up the %gs cursor, the sync markers
        # and the injected runtime -- and overrides the PTWRITE values one by
        # one with the op field `:W'.
        f.write("sink %s\n" % ("buffer" if sink == "mixed" else sink))
        # COUNT SINK: this image's first global
        # counter index (the build gives every image a disjoint range) and
        # whether the increment is a locked RMW (a target whose per-thread lazy
        # setup cannot be relied on; the per-thread default is lock-free).
        if sink == "count":
            f.write("countbase %d\n" % count_base)
            f.write("countatomic %d\n" % (1 if count_atomic else 0))
        # BANDWIDTH EXPERIMENTS: `movnti' value stores,
        # and the DEBUG fixed-slot sink whose value stream is meaningless.
        # PER-THREAD KEYFRAME COUNTERS.
        # `kfgs B' puts this image's keyframe countdowns in the THREAD's own %gs
        # region at global counter index B, B+1, ...  Without it they are one
        # cell per site shared by every thread, which is UNSOUND for the buffer
        # sink (positional cv cursor -> wrong addresses, not just a missed
        # keyframe).
        if kf_gs is not None:
            f.write("kfgs %d\n" % kf_gs)
        # RESERVE-BEFORE-FILL: advance %gs:CURSOR
        # to the end of the run BEFORE storing the values, so the slot is never
        # behind the register and a signal handler -- which is instrumented code
        # in a whole-program build -- cannot log over the interrupted site.
        if reserve:
            f.write("reserve 1\n")
        if nt_store:
            f.write("ntstore 1\n")
        if fixed_slot:
            f.write("fixedslot 1\n")
        f.write("space %d\n" % space)
        f.write("sync %d\n" % sync)
        if sync_cursor:
            f.write("sync_cursor 1\n")
        if sync_carrier != "ptwrite":
            f.write("synccarrier %s\n" % sync_carrier)
        if cursor_store:
            f.write("cursor_store 1\n")
        # NB: no `liveness' line.  `--dead-regs off' simply emits no `:S<mask>'
        # fields, which has the same effect and keeps the site file readable by
        # a plugin without liveness support.
        if delta:
            f.write("delta %d\n" % delta)
        f.write("slotbase 0x%x\n" % slotbase)
        f.write("counterbase 0x%x\n" % counterbase)
        f.write("nslots %d\n" % nslots)
        f.write("ncounters %d\n" % ncounters)
        # GROUND-TRUTH ADDRESS LOGGING: the
        # plugin then patches EVERY memory-accessing instruction it can
        # re-encode, not only the sites listed below.
        if gt:
            f.write("gt 1\n")
            f.write("gtoff %d\n" % gtoff)
            f.write("gtstack %d\n" % (1 if gtstack else 0))
            # gt-only instructions that get NO gt sequence (--gt-skip /
            # --gt-retry): they are `excluded' in the comparison, and nothing
            # a critical-value site logs changes.
            for addr in sorted(gt_skip):
                f.write("gtskip 0x%x\n" % addr)
        for addr in sorted(groups):
            f.write("0x%x %s\n" % (addr, ",".join(
                op_token(op) for op in groups[addr])))


# --------------------------------------------------------------------------
# Site map construction
# --------------------------------------------------------------------------

def load_capstone():
    try:
        import capstone
    except ImportError:
        raise SystemExit(
            "capstone is required to build the site map "
            "(pip install capstone)")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = False
    return capstone, md


def follow_patch(img, md, addr, traps, orig_bytes=None):
    """Return the trampoline address the instrumented instruction jumps to.

    E9Patch overwrites the instrumented instruction with an unconditional jump,
    but "instruction punning" may prepend harmless prefix bytes of the original
    instruction (e.g. `48 e9 rel32' -- a REX.W that the jmp ignores), so the
    jump is decoded rather than pattern-matched.  A short 2-byte `jmp rel8' is
    also used, landing on a nearby stub that carries the real `jmp rel32', so
    the chain is followed (bounded) until the target no longer starts with an
    unconditional jump.  The trap tactics (T1/T2/T3) leave an int3 instead and
    record the target in the loader's trap table.
    """
    if addr in traps:
        return traps[addr]
    cur = addr
    for _hop in range(4):
        b = img.read(cur, 16)
        if b is None:
            return None
        nxt = None
        for ins in md.disasm(b, 0):
            # capstone reports an unsigned target relative to our zero base --
            # and formats a SMALL one in decimal ("jmp 9"), so the target is
            # parsed with base 0, not 16 (a short `jmp rel8' stub that lands
            # within 16 bytes is how E9Patch chains a punned 2-byte jump).
            if ins.mnemonic == "jmp":
                try:
                    t = int(ins.op_str, 0)
                except ValueError:
                    break                       # indirect jump: not a patch
                if t >= (1 << 63):
                    t -= (1 << 64)
                nxt = cur + t
            break
        if nxt is None:
            return (None if cur == addr else cur)
        if orig_bytes is not None and cur != addr and \
                b[:len(orig_bytes)] == orig_bytes:
            # We have arrived at the trampoline body and it happens to start
            # with the relocated original instruction, which is itself a jump.
            return cur
        cur = nxt
    return cur


def parse_pattern(arg):
    """`-4 P` / `-5 Q` plugin rows: a hex byte string with `..` wildcards."""
    if not arg or arg == "-":
        return []
    out = []
    for i in range(0, len(arg) - 1, 2):
        h = arg[i:i + 2]
        out.append(None if h == ".." else int(h, 16))
    return out


def match_pattern(data, off, pat):
    if not pat or off < 0 or off + len(pat) > len(data):
        return False
    for i, b in enumerate(pat):
        if b is not None and data[off + i] != b:
            return False
    return True


# Original mnemonics whose copy E9Patch may expand into SEVERAL instructions.
# Only a `call' is: `e9x86_64.cpp:relocateInstr' emulates it as
# `pushReturnAddress' (1, 3 or 4 instructions, depending on -Ocall/-Oscratch-
# stack/PIC) followed by the transfer.  Everything else -- a `jmp'/`jcc'
# relaxed to rel32, a rip-relative operand with a corrected disp32 -- stays
# exactly one instruction.
_CALLS = ("call", "lcall")


def instr_expansion(md, data, at_off, va, orig_ins, post_pat, max_bytes=64):
    """Byte length of E9Patch's copy of `orig_ins`, placed at `data[at_off]`.

    This is the one number the plugin's layout rows do not contain, and getting
    it wrong shifts every logged value AFTER the original instruction.  It is therefore *located*, not guessed:

      * the plugin's own body resumes right after the copy, and it told us its
        first bytes (`post_pat`), so the copy ends at the first instruction
        boundary at which those bytes match -- exact, whatever E9Patch did;
      * when nothing follows `$instr` in the body (the site has no `after` op)
        there is no pattern to match, and the copy is one instruction unless the
        original is a `call`, which E9Patch emulates up to and including the
        transfer it ends with.

    Returns (length, problem_or_None).
    """
    bounds = []
    k = 0
    for _ in range(8):
        ins = _decode_one(md, data[at_off + k:at_off + k + 16], va + k)
        if ins is None:
            break
        k += ins.size
        bounds.append((k, ins))
        if k > max_bytes:
            break
    if not bounds:
        return None, "cannot decode the relocated original instruction"
    if post_pat:
        for k, _ins in bounds:
            if match_pattern(data, at_off + k, post_pat):
                return k, None
        return None, ("cannot locate the end of E9Patch's copy of the original "
                      "instruction (the plugin's body does not resume within "
                      "%d bytes)" % bounds[-1][0])
    if orig_ins is not None and _mn(orig_ins) in _CALLS:
        for k, ins in bounds:
            if _mn(ins) in _XFER:
                return k, None
        return None, ("cannot locate the end of E9Patch's emulation of the "
                      "original `%s'" % orig_ins.mnemonic)
    return bounds[0][0], None


def _decode_one(md, data, va):
    """Decode exactly one instruction from `data` (placed at `va`)."""
    for ins in md.disasm(data, va):
        return ins
    return None


def _jmp_target(ins):
    """Absolute target of a direct unconditional jump, or None."""
    if ins.mnemonic != "jmp":
        return None
    try:
        t = int(ins.op_str, 0)          # capstone prints a small one in decimal
    except ValueError:
        return None                     # indirect (`jmp *%rax')
    return t - (1 << 64) if t >= (1 << 63) else t


# Original control-transfer instructions E9Patch cannot copy verbatim; it emits
# a multi-instruction *emulation* instead (save a scratch register below the red
# zone, materialise the return address, push it, restore the scratch, jump).
# Parts of that emulation are genuine program accesses -- the pushed return
# address, the indirect jump's own load -- so the whole emulation is recorded as
# ONE relocated instruction and the reconstructor replays the ORIGINAL
# instruction's semantics over it.
_XFER = ("call", "jmp", "lcall", "ljmp", "ret", "retf")

# Unconditional control transfers: after one of them E9Patch stops copying, so
# what follows in the trampoline is padding or the NEXT trampoline, not more of
# this one.  The test is on the BASE mnemonic: capstone prefixes CET/MPX/string
# forms ("notrack jmp rax", "bnd ret", "rep movsq"), and treating `notrack jmp'
# as an ordinary instruction would make the tail scanner run off the end of the
# trampoline and report a bogus mismatch against whatever follows in memory.
_UNCOND = ("jmp", "ljmp", "ret", "retf", "iret", "iretd", "iretq", "ud2", "hlt")


def _mn(ins):
    """Base mnemonic: capstone renders prefixes as part of the mnemonic."""
    return ins.mnemonic.rsplit(" ", 1)[-1] if ins is not None else ""


def scan_trampoline(img, md, tramp, orig_bytes, orig_addr, layout,
                    orig_at=None, max_bytes=4096):
    """Map one trampoline body back onto the original program.

    `layout` is what the plugin recorded for this site (see ptlog.cpp
    `e9_plugin_patch`): the byte offset of "$instr", the total length of the
    plugin-emitted body, the offset of every logging instruction and of every
    sync marker.  Those offsets do not count the "$instr" macro, so everything
    at or past `before_len` is shifted by the relocated instruction's real
    encoded length, which is measured here.

    Returns (log_addrs, reloc, sync_addrs, tail, problems) where
      log_addrs  = [(vaddr, "mnemonic op_str"), ...] in emission order,
      reloc      = (vaddr, tramp_len) of the relocated original instruction,
      tail       = [(vaddr, orig_addr, orig_len, tramp_len), ...] for every
                   FOLLOWING original instruction E9Patch's "$BREAK"
                   optimisation copied into the trampoline.
    """
    problems = []
    data = img.read(tramp, max_bytes)
    if data is None:
        return [], None, [], [], ["trampoline body is not mapped"]

    before_len = layout["before"]
    if layout.get("pre") and not match_pattern(data, 0, layout["pre"]):
        problems.append("the trampoline body does not start with the bytes the "
                        "plugin emitted (E9Patch moved or rewrote it)")
    oi0 = _decode_one(md, orig_bytes or b"", orig_addr) if orig_bytes else None
    reloc_size, why = instr_expansion(md, data, before_len, tramp + before_len,
                                      oi0, layout.get("post"))
    if reloc_size is None:
        return [], None, [], [], problems + [why]
    shift = lambda off: off + (reloc_size if off >= before_len else 0)
    # A guard's JOIN LABEL can sit at body offset `before_len` exactly -- the
    # guarded body is the last thing the plugin emits before "$instr", so
    # jumping over it lands ON the relocated original instruction.  The plugin's
    # numbering does not count "$instr", so that offset is indistinguishable
    # from "the first byte of the body AFTER the original instruction", and
    # `shift` (which shifts everything at or past `before_len`) puts the label
    # one whole instruction too far.  The phase disambiguates it: every offset
    # of a `before` op is at or before `before_len` and is never shifted, every
    # offset of an `after` op is at or past it and always is.
    # The same holds for the log-on-change `join_addr': otherwise a
    # single-value `before` site's SKIPPED log would not be recognised.
    def shiftp(off, after):
        return off + (reloc_size if after else 0)

    logs = []
    for off, _meta in layout["logs"]:
        after = (_meta.get("when") == "after")
        va = tramp + shiftp(off, after)
        ins = _decode_one(md, img.read(va, 16) or b"", va)
        guard = None
        if _meta.get("cmp_off") is not None:
            guard = (tramp + shiftp(_meta["cmp_off"], after),
                     tramp + shiftp(_meta["je_off"], after),
                     tramp + shiftp(_meta["join_off"], after))
        kguard = None
        if _meta.get("kfbr_off") is not None:
            kguard = (tramp + shiftp(_meta["kfbr_off"], after),
                      tramp + shiftp(_meta["kfjoin_off"], after))
        logs.append((va, ("%s %s" % (ins.mnemonic, ins.op_str)).strip()
                     if ins else "?", guard, kguard))
    syncs = [tramp + shift(o) for o in layout["syncs"]]
    if layout.get("tnt"):
        layout["tnt_va"] = [(r, tramp + shift(o)) for r, o in layout["tnt"]]
    reloc = (tramp + before_len, reloc_size)

    # ---- the "$BREAK" tail: copies of the instructions AFTER the site -------
    tail = []
    if orig_at is None or not orig_bytes:
        return logs, reloc, syncs, tail, problems
    cur = tramp + shift(layout["total"])
    nxt = orig_addr + len(orig_bytes)
    limit = tramp + max_bytes
    while cur < limit:
        buf = img.read(cur, 24)
        if not buf or buf[0] == 0xcc:           # E9Patch pads with int3
            break
        ins = _decode_one(md, buf, cur)
        if ins is None:
            break
        if _jmp_target(ins) == nxt:             # E9Patch's jump back: done
            break
        ob = orig_at(nxt)
        oi = _decode_one(md, ob, nxt) if ob else None
        if oi is None:
            problems.append("cannot decode the original instruction at %#x" % nxt)
            break
        if _mn(ins) == _mn(oi):                 # copied one-for-one
            tail.append((cur, nxt, oi.size, ins.size))
            cur += ins.size
            nxt += oi.size
            # An unconditional transfer ends the copied run: control leaves the
            # trampoline, and what follows in memory is the NEXT trampoline.
            if _mn(oi) in _UNCOND:
                break
            continue
        if _mn(oi) in _XFER:                    # emulated control transfer
            span = cur
            while cur < limit:
                j = _decode_one(md, img.read(cur, 24) or b"", cur)
                if j is None:
                    break
                cur += j.size
                if _mn(j) in _UNCOND:
                    break
            tail.append((span, nxt, oi.size, cur - span))
            break
        problems.append("trampoline instruction %#x (%s) does not match the "
                        "original at %#x (%s)"
                        % (cur, ins.mnemonic, nxt, oi.mnemonic))
        break
    return logs, reloc, syncs, tail, problems


def build_sitemap(out_path, orig_path, groups, sink, plugin_csv,
                  sync_period=0, site_kind=None, allow_negative=False,
                  gt=False, counter_gs=False):
    """Correlate the plugin's emission record with the rewritten binary.

    `site_kind` maps a spec site id to the site's kind, so the site map records
    the SPEC's kind, as SPEC_FORMAT section 2 requires, instead of guessing it
    from the logging instruction's position (a `reg` site with `when: "after"`
    -- a pseudo-load at a call return site -- would otherwise come out as
    `load`)."""
    site_kind = site_kind or {}
    # MIXED SINK: which sink logs each spec site.
    # The assignment is per SITE (all of a site's values share it), so one
    # lookup table built from `groups' is enough to stamp every entry.
    site_sink = {}
    for _ops in groups.values():
        for _op in _ops:
            if _op.sink:
                site_sink[_op.sid] = ("ptwrite" if _op.sink == "ptw"
                                      else "buffer")
    cs, md = load_capstone()
    img = VirtualImage(out_path)
    traps = img.traps()

    orig = Elf(orig_path)

    def orig_window(addr, n=16):
        """The ORIGINAL bytes at addr (read from the input ELF: the rewritten
        image has that page remapped to the patched copy)."""
        for vaddr, memsz, off, filesz, flags in orig.loads:
            if flags & 1 and vaddr <= addr < vaddr + filesz:
                fo = off + (addr - vaddr)
                return orig.data[fo:fo + n]
        return None

    def orig_insn(addr):
        b = orig_window(addr)
        if not b:
            return None
        for ins in md.disasm(b, addr):
            return bytes(ins.bytes)
        return None

    # plugin CSV: addr,site,when,kind,arg,body_offset -- one row per logged
    # VALUE plus the layout pseudo-rows I (-1) / E (-2) / S (-3).
    layout = {}
    if plugin_csv and os.path.exists(plugin_csv):
        with open(plugin_csv) as f:
            for line in f:
                if line.startswith("#"):
                    continue
                parts = line.strip().split(",")
                a, sid, when, kind, arg, off = parts[:6]
                # keyed extras: `d=slot:cmp:je:join' (log-on-change guard),
                # `k=counter:period:jnz:join' (keyframe counter guard)
                extra = {}
                for x in parts[6:]:
                    if len(x) > 2 and x[1] == "=":
                        extra[x[0]] = x[2:].split(":")
                addr, sid, off = int(a, 16), int(sid), int(off)
                L = layout.setdefault(addr, dict(before=None, total=None,
                                                 logs=[], syncs=[],
                                                 pre=[], post=[], count=None))
                if when == "I":
                    L["before"] = off
                elif when == "E":
                    L["total"] = off
                elif when == "C":
                    # COUNT SINK: this address's
                    # global counter index.  `arg' is the index, `off' the body
                    # offset of the `incq'.
                    L["count"] = int(arg)
                elif when == "S":
                    L["syncs"].append(off)
                elif when == "T":
                    # `--sync-carrier tnt': a TNT sync marker's loop roles
                    # (s/b/z; its END is the `S' row).  Never in a default build.
                    L.setdefault("tnt", []).append((arg, off))
                elif when == "P":
                    L["pre"] = parse_pattern(arg)
                elif when == "Q":
                    L["post"] = parse_pattern(arg)
                else:
                    half = None
                    if arg.endswith(".lo") or arg.endswith(".hi"):
                        arg, half = arg[:-3], arg[-2:]
                    d = dict(site=sid, half=half,
                             when="after" if when == "A" else "before",
                             kind=arg if kind == "r" else None,
                             reg=arg if kind == "r" else None,
                             size=(int(arg) if kind == "m" else None),
                             ismem=(kind == "m"), isgt=(kind == "g"),
                             # `--log-blocks': this slot is a compile-time
                             # BASIC-BLOCK IDENTIFIER, not a critical value.
                             isblk=(kind == "i"),
                             blockid=(int(arg) if kind == "i" else None),
                             slot=None, cmp_off=None, je_off=None, join_off=None,
                             counter=None, keyframe=None, kfbr_off=None,
                             kfjoin_off=None)
                    if "d" in extra:
                        v = extra["d"]
                        d["slot"] = int(v[0], 16)
                        d["cmp_off"] = int(v[1])
                        d["je_off"] = int(v[2])
                        d["join_off"] = int(v[3])
                    if "k" in extra:
                        v = extra["k"]
                        d["counter"] = int(v[0], 16)
                        d["keyframe"] = int(v[1])
                        d["kfbr_off"] = int(v[2])
                        d["kfjoin_off"] = int(v[3])
                    L["logs"].append((off, d))
    for L in layout.values():
        L["logs"].sort(key=lambda e: e[0])
        L["syncs"].sort()

    entries, relocated, problems, sync_markers = [], [], [], []
    sync_tnt = []                       # `--sync-carrier tnt' only
    negative = []
    tramps_used = []
    n_gt = 0
    n_blk = 0           # `--log-blocks' control-flow records (role="blockid")
    # A `--gt-all' build patches instructions the spec never named, so the set
    # of trampolines to scan is the union of the spec's sites and everything
    # the plugin reports having emitted a body for.
    all_addrs = sorted(set(groups) | (set(layout) if gt else set()))
    # Every address at which the plugin emitted a gt record, INCLUDING the ones
    # whose trampoline cannot be scanned below: `eval/e2e/gtsame.py' needs the
    # complete set to tell "this instruction is excluded from the ground truth"
    # from "the two streams have lost each other".
    gt_addrs = sorted(a for a, L in layout.items()
                      if any(m.get("isgt") for _o, m in L["logs"])) if gt else []
    gt_problems = []
    gt_unpatched = set()
    n_gt_unpatched = 0
    for addr in all_addrs:
        cv_site = addr in groups
        probs_out = problems if cv_site else gt_problems
        ob = orig_insn(addr)
        tramp = follow_patch(img, md, addr, traps, ob)
        tramps_used.append(tramp)
        if tramp is None:
            probs_out.append((addr, "could not follow the patch jump"))
            if not cv_site:
                n_gt_unpatched += 1
                gt_unpatched.add(addr)
            continue
        L = layout.get(addr)
        if L is None or L["before"] is None or L["total"] is None:
            probs_out.append((addr, "the plugin recorded no body layout for this "
                                    "site (--sitemap CSV missing or stale)"))
            continue
        logs, reloc, syncs, tail, probs = scan_trampoline(
            img, md, tramp, ob, addr, L, orig_at=orig_window)
        for why in probs:
            probs_out.append((addr, why))
        if reloc is not None:
            relocated.append(dict(tramp_addr=reloc[0], orig_addr=addr,
                                  len=len(ob) if ob else reloc[1],
                                  tramp_len=reloc[1]))
        else:
            probs_out.append((addr, "relocated original instruction not found"))
        relocated.extend(dict(tramp_addr=tva, orig_addr=oa, len=ln,
                              tramp_len=tl) for tva, oa, ln, tl in tail)
        sync_markers.extend(dict(tramp_addr=va, orig_addr=addr) for va in syncs)
        sync_tnt.extend(dict(role=r, tramp_addr=va, orig_addr=addr)
                        for r, va in L.get("tnt_va", ()))

        if len(L["logs"]) != len(logs):
            probs_out.append((addr, "internal: %d logged values, %d addresses"
                              % (len(L["logs"]), len(logs))))
        for (_off, meta), (va, insn, guard, kguard) in zip(L["logs"], logs):
            # GROUND TRUTH: this logging
            # instruction stores { effective address, original ip } into the gt
            # ring.  It is NOT a critical value: `offline/ptrecon' must treat it
            # as instrumentation (no value consumed, no trace record) and only
            # `eval/e2e/gtsame.py' reads it.
            if meta.get("isgt"):
                entries.append(dict(tramp_addr=va, site=-1, orig_addr=addr,
                                    role="gt", kind="gtaddr", when="before",
                                    payload_bits=128, insn=insn))
                n_gt += 1
                continue
            # SPEC_FORMAT section 2: `kind' is the SPEC site kind this value
            # belongs to.  Fall back to the positional guess only for a value
            # whose site id is not in the spec (an evicted instruction has none).
            if meta.get("isblk"):
                # CONTROL-FLOW LOG (`--log-blocks').
                # It occupies one 8-byte slot of the cv stream like any other
                # value, but it is not a critical value: its `role' says so, so
                # a consumer that only wants critical values skips it and a
                # consumer that follows control flow from the stream reads it.
                entries.append(dict(tramp_addr=va, site=meta["site"],
                                    orig_addr=addr, role="blockid",
                                    kind="blockid", when="before",
                                    blockid=meta["blockid"], payload_bits=64,
                                    insn=insn))
                n_blk += 1
                continue
            kind = site_kind.get(meta["site"])
            if kind is None:
                kind = "memop" if meta["ismem"] else \
                       ("load" if meta["when"] == "after" else "reg")
            ent = dict(tramp_addr=va, site=meta["site"], orig_addr=addr,
                       role="cv", kind=kind, when=meta["when"])
            # SPEC_FORMAT section 2: in a MIXED build every entry names its own
            # sink, and `offline/ptrecon' pulls the value from the PT stream or
            # from cv.*.bin per entry.  A single-sink build says nothing here
            # and the map's top-level `sink' answers for every entry.
            esink = sink
            if sink == "mixed":
                esink = site_sink.get(meta["site"], "buffer")
                ent["sink"] = esink
            if meta["ismem"]:
                ent["size"] = meta["size"]
                # `ptwrite m32' is emitted for 4-byte memory operands, so the
                # PTW payload is 4 bytes wide there; the buffer sink always
                # stores a zero-extended 64-bit slot.  A log-on-change memop is
                # loaded into a scratch register first (there is no
                # memory-to-memory `cmp'), so its payload is 64 bits wide.
                ent["payload_bits"] = (32 if (esink == "ptwrite" and
                                              meta["size"] == 4 and
                                              guard is None) else 64)
            else:
                ent["reg"] = meta["reg"]
                if meta["half"]:
                    ent["half"] = meta["half"]
                ent["payload_bits"] = 64
            ent["insn"] = insn
            if guard is not None:
                ent["delta"] = True
                ent["slot"] = meta["slot"]
                ent["cmp_addr"], ent["je_addr"], ent["join_addr"] = guard
            if kguard is not None:
                # SPEC_FORMAT section 2: the keyframe counter guard.  Seeing
                # `kf_join_addr' as the instruction after `kf_branch_addr' means
                # this execution was NOT a keyframe and nothing was logged.
                ent["keyframe"] = meta["keyframe"]
                ent["counter"] = meta["counter"]
                # --kf-gs: `counter' is a %gs DISPLACEMENT into the thread's own
                # counter array, not a link-time address -- consumers must not
                # rebase it (offline/recon.cpp).
                if counter_gs:
                    ent["counter_gs"] = 1
                ent["kf_branch_addr"], ent["kf_join_addr"] = kguard
                ent["resync"] = (kind == "reg" and guard is None)
            entries.append(ent)

    # ---- instructions E9Patch displaced on its OWN account ------------------
    # To squeeze a jump into a short instruction E9Patch may "evict" a
    # neighbouring/overlapping instruction into a trampoline of its own (the
    # T2/T3 tactics of the E9Patch paper): e.g. a 2-byte `jmp rel8' punned over
    # `push %rbp' can land in the MIDDLE of an instruction 0x52 bytes later,
    # which then has to be moved out of the way.  Those copies execute and
    # appear as IPs in the PT stream exactly like the ones above, so the
    # reconstructor needs them too -- without them their accesses simply
    # vanish from the trace.
    # They are found by looking for a `jmp rel32' into a trampoline region at an
    # address whose bytes the rewriter changed, and confirming that the
    # trampoline starts with the original instruction from that address.
    known_tramps = set(t for t in tramps_used if t is not None)
    site_tramps = set(known_tramps)

    def in_tramp_region(a):
        return any(va <= a < va + size for va, size, _o, _x in img.tramp_regions)

    n_evicted = 0
    for vaddr, memsz, off, filesz, flags in orig.loads:
        if not (flags & 1):
            continue
        ob_all = orig.data[off:off + filesz]
        nb_all = img.read(vaddr, filesz)
        if nb_all is None or len(nb_all) < filesz:
            continue
        # The candidate instruction does NOT always start at the `e9'
        # byte: E9Patch also plants the REX-punned `48 e9 <rel32>' patch jump
        # and then the `e9' is one byte INSIDE the jump.  Taking
        # it as the instruction start puts `a' inside the ORIGINAL instruction,
        # the byte confirmation below fails, and the evicted instruction never
        # reaches the `relocated' list -- after which `ptrecon' skips its little
        # trampoline as pure instrumentation and silently DROPS its effect.
        # So each `e9' is tried as the last byte of up to four candidate
        # instruction starts.  `back = 0' is tried FIRST and every later test
        # is the same, so a prefixed candidate is only reached when the bare
        # one fails.
        # The candidate must be an ORIGINAL INSTRUCTION START.  An `e9'
        # inside the rel32 of a real patch jump can decode as a jump into the
        # trampoline region, and the original byte there can decode as a 1-byte
        # instruction that matches the first byte of SOME unrelated evictee
        # block, so the byte confirmation below passes, and the scan then either
        # reports a mismatch (a fatal map problem) or, if the block happens to
        # be a lone 1-byte instruction, maps somebody else's evictee block onto
        # a data byte.  A linear sweep from the
        # nearest known instruction start (a spec or gt site, which e9tool
        # decoded) within 64 bytes settles it; with no start in range the
        # permissive check stands.
        import bisect
        known_starts = sorted(set(groups) | (set(layout) if gt else set()))

        def boundary(a):
            """Tri-state: True = `a' is PROVEN an original instruction start,
            False = proven NOT one, None = cannot be decided (no decoded start
            within 64 bytes below `a', or the sweep hit an undecodable byte).

            Treating `None' as `True' is right for the permissive
            byte-confirmation path below (an accidental match is then still
            rejected on the bytes) but wrong for the `call' branch: there the
            only check is E9Patch's relocated-call SHAPE, so an unproven
            candidate that is really a mid-instruction byte would be reported
            as a site-map PROBLEM.  Keeping the three states lets the call branch
            demand proof before it calls anything a problem."""
            k = bisect.bisect_right(known_starts, a)
            if k == 0:
                return None
            p = known_starts[k - 1]
            if p == a:
                return True
            if a - p > 64:
                return None
            while p < a:
                off = p - vaddr
                if off < 0 or off + 16 > len(ob_all) + 15:
                    return None
                ins = _decode_one(md, ob_all[off:off + 16], p)
                if ins is None or ins.size == 0:
                    return None
                p += ins.size
            return p == a

        def on_boundary(a):
            return boundary(a) is not False

        def try_evicted(a, j, t):
            """Confirm and map one evicted instruction.  Returns True if mapped."""
            if not on_boundary(a):
                return False
            ob = orig_insn(a)
            ti = _decode_one(md, img.read(t, 16) or b"", t)
            oi = _decode_one(md, ob or b"", a)
            if ob is None or ti is None or oi is None:
                return False
            if _mn(oi) == "call":
                # E9Patch does not copy an evicted CALL verbatim: it
                # materialises the return address (e9x86_64.cpp,
                # pushReturnAddress) and turns the call into a `jmp' with the
                # same operand, so the block starts with a `mov'/`push'/`lea',
                # never with a `call', and the byte confirmation below rejects
                # it.  The block then stays undescribed and the reconstructor's
                # byte-matching fallback attributes E9Patch's own spill store
                # and reload to the call and to the two ORIGINAL instructions
                # after it.
                # The plugin's own copy of a call has the same shape and is
                # already recorded as ONE `relocated' entry (len = the call,
                # tramp_len = the whole sequence), which the reconstructor
                # understands; an evictee block gets the same entry.
                ent = _evicted_call(img, md, a, oi, t)
                if ent is None:
                    # An unrecognised shape at an UNPROVEN candidate
                    # start is not an eviction at all -- it is an 0xe9 byte in
                    # the middle of some other instruction whose leading bytes
                    # happen to decode as a `call'.  Treat it as a non-candidate
                    # (the byte-confirmation path below does the same for every
                    # other mnemonic) instead of failing the build.  Only a
                    # PROVEN instruction start whose shape we cannot read is a
                    # real site-map problem.
                    if boundary(a) is not True:
                        return False
                    problems.append((a, "evicted call: E9Patch's relocated-call "
                                        "shape not recognised at %#x" % t))
                    return False
                known_tramps.add(t)
                relocated.append(ent)
                return True
            # The confirmation has to be on the BYTES, not on the mnemonic: `a'
            # is only a candidate, found by scanning for a changed 0xe9 byte,
            # and inside a longer instruction that byte is data whose "target"
            # can land on a trampoline instruction with the same mnemonic.
            # E9Patch copies an evicted instruction verbatim unless it is
            # PC-relative, so exact bytes are the rule and a re-encoding is
            # only allowed for a branch or a rip-relative operand.
            if bytes(ti.bytes) != bytes(oi.bytes) and not (
                    _mn(ti) == _mn(oi) and
                    (_mn(oi) in _XFER or _mn(oi).startswith("j") or
                     "rip" in oi.op_str)):
                return False
            # An ALL-ZERO match carries no information and is a false positive
            # waiting to happen: `00 00' decodes to `add byte ptr [rax],al'
            # everywhere, and both sides of this comparison have plenty of zero
            # bytes -- E9Patch pads its trampolines with them, and a rel32 field
            # is mostly zeroes (e.g. a retargeted rel32 can put an `e9' whose
            # `jmp' lands on padding zeroes while the original bytes there are
            # `00 00' too).  Requiring one non-zero byte
            # costs nothing real: an evicted `add byte ptr [rax],al' would be a
            # zero instruction in executable code.
            if not any(oi.bytes):
                return False
            known_tramps.add(t)
            _logs, reloc, _syncs, tail, probs = scan_trampoline(
                img, md, t, ob, a, dict(before=0, total=0, logs=[], syncs=[]),
                orig_at=orig_window)
            for why in probs:
                problems.append((a, "evicted instruction: " + why))
            if reloc is None:
                problems.append((a, "E9Patch displaced this instruction but its "
                                    "copy was not found in the trampoline"))
                return False
            relocated.append(dict(tramp_addr=reloc[0], orig_addr=a,
                                  len=len(ob), tramp_len=reloc[1]))
            relocated.extend(dict(tramp_addr=tva, orig_addr=oa, len=ln,
                                  tramp_len=tl) for tva, oa, ln, tl in tail)
            return True

        i = -1
        while True:
            i = nb_all.find(b"\xe9", i + 1)
            if i < 0 or i + 5 > filesz:
                break
            if nb_all[i:i + 5] == ob_all[i:i + 5]:
                continue                      # unchanged byte: not a patch
            for back in range(0, 4):
                if i - back < 0:
                    break
                a = vaddr + i - back
                if a in groups or (gt and a in layout):
                    continue                  # already scanned above
                j = _decode_one(md, nb_all[i - back:i - back + 16], a)
                if j is None or j.mnemonic != "jmp":
                    continue
                if i - back + j.size <= i:
                    continue                  # the jump does not cover the `e9'
                t = _jmp_target(j)
                if t is None or not in_tramp_region(t) or t in known_tramps:
                    continue
                if try_evicted(a, j, t):
                    n_evicted += 1
                    break
    if n_evicted:
        print("[rewrite] %d instruction(s) E9Patch displaced on its own account "
              "(eviction) also mapped" % n_evicted)

    for lst in (entries, relocated, sync_markers, sync_tnt):
        for e in lst:
            if e["tramp_addr"] < 0:
                negative.append(e["tramp_addr"])
    if negative and not allow_negative:
        problems.append((0, "%d trampoline addresses are BELOW the image base "
                            "(e.g. %#x); re-run without --no-mem-lb so E9Patch "
                            "places trampolines above the image, or the site "
                            "map cannot use unsigned link-time vaddrs"
                         % (len(negative), negative[0])))
    elif negative:
        print("[rewrite] %d trampoline addresses are below the image base "
              "(e.g. %#x): the map records them as NEGATIVE link-time offsets, "
              "which the reconstructor adds to the load base like any other "
              "(SPEC_FORMAT section 2)" % (len(negative), negative[0]))

    entries.sort(key=lambda e: (e["orig_addr"], e["tramp_addr"]))
    relocated.sort(key=lambda e: e["tramp_addr"])
    sync_markers.sort(key=lambda e: e["tramp_addr"])
    n_delta = sum(1 for e in entries if e.get("delta"))
    n_kf = sum(1 for e in entries if e.get("keyframe"))
    sm = dict(version=2, image=os.path.abspath(out_path),
              orig_image=os.path.abspath(orig_path), sink=sink,
              pie=Elf(out_path).pie, sync=sync_period,
              entries=entries, relocated=relocated,
              sync_markers=sync_markers,
              trampolines=sorted(known_tramps))
    if sync_tnt:
        # `--sync-carrier tnt': each marker's count
        # is carried by the TNT bits of a `shr/jc/nop/jnz' loop, LSB first:
        # role s = the marker's first instruction (start collecting), b = the
        # `jc' (a bit; 1 unless the `nop' follows), z = the `nop' (that bit is
        # 0).  The marker's END -- the instruction after the loop -- is its
        # `sync_markers' entry, where the collected count is the payload.
        sync_tnt.sort(key=lambda e: e["tramp_addr"])
        sm["sync_carrier"] = "tnt"
        sm["sync_tnt"] = sync_tnt
    if n_blk:
        # CONTROL-FLOW LOG (`--log-blocks'): how many
        # `role="blockid"' entries the stream carries.  Their presence is what
        # tells a consumer that this cv stream records control flow itself and
        # needs no PT.  A build without --log-blocks emits none of these keys,
        # so every existing site map is byte-identical.
        sm["block_log"] = True
        sm["block_log_sites"] = n_blk
    if gt:
        # SPEC_FORMAT section 2: every entry carries a `role'.  A `gt' entry is
        # NOT a critical value -- ptrecon skips it like any other trampoline
        # instruction -- and `gt_sites' counts the instructions whose effective
        # address the run records losslessly.
        sm["gt"] = True
        sm["gt_sites"] = n_gt
        sm["gt_off"] = GT_OFF
        sm["gt_record_bytes"] = 16
        # An instruction E9Patch could not patch logs nothing, so it is NOT a
        # gt site at run time: leaving it in would make every one of its
        # reconstructed records look like a lost ground-truth record instead of
        # what it is -- an instruction excluded from the ground truth.
        sm["gt_site_addrs"] = [a for a in gt_addrs if a not in gt_unpatched]
        sm["gt_unpatched"] = n_gt_unpatched
        sm["gt_problem_sites"] = len(gt_problems)
        # A gt-only site is NOT a critical value: a trampoline the scanner
        # could not describe costs the ground truth those records (and
        # `gtsame.py' counts them), but it cannot make the reconstruction
        # silently wrong the way a missing `cv' entry does.  So they are
        # counted and listed, not fatal.
        if gt_problems:
            gpath = out_path + ".sitemap.gtproblems"
            counts = {}
            with open(gpath, "w") as f:
                f.write("# %d gt-only site-map problems from %s\n"
                        % (len(gt_problems), out_path))
                for a, w in gt_problems:
                    f.write("0x%x\t%s\n" % (a, w))
                    k = re.sub(r"0x[0-9a-f]+", "0x...", w)
                    counts[k] = counts.get(k, 0) + 1
            print("[rewrite] --gt-all: %d gt-only site(s) the scanner could not "
                  "describe (%d of them unpatched by E9Patch) -> %s"
                  % (len(gt_problems), n_gt_unpatched, gpath))
            for w, n in sorted(counts.items(), key=lambda kv: -kv[1])[:6]:
                print("   %6d x %s" % (n, w))
    if sink == "count":
        # COUNT SINK: one per-thread execution
        # counter per patched address.  Record, per address, its global counter
        # index, the site ids that share the trampoline and the number of
        # critical VALUES those sites log per execution -- so
        # eval/ptw_budget_count.py can turn a raw execution count into the
        # values/s a PTWRITE assignment of that address would put on the PT bus.
        cnt = []
        for addr in sorted(groups):
            idx = layout.get(addr, {}).get("count")
            if idx is None:
                continue
            ops = groups[addr]
            nvals = sum(2 if (op.kind == "r" and op.arg.startswith("xmm"))
                        else 1 for op in ops)
            sids = sorted(set(op.sid for op in ops))
            cnt.append(dict(addr=addr, index=idx, n_values=nvals, sites=sids))
        sm["count"] = cnt
        sm["count_sites"] = len(cnt)
        # --count-blocks bookkeeping
        sm["count_unpatched"] = list(COUNT_UNPATCHED)
        if COUNT_MOVED:
            sm["count_moved"] = {"0x%x" % k: "0x%x" % v
                                 for k, v in sorted(COUNT_MOVED.items())}
        if COUNT_UNPROFILED:
            sm["count_unprofiled"] = sorted(COUNT_UNPROFILED)
    if sink == "mixed":
        sm["ptw_values"] = sum(1 for e in entries if e.get("sink") == "ptwrite")
        sm["buffer_values"] = len(entries) - sm["ptw_values"]
    if PLUGIN_STATS:
        sm["emitter"] = dict(PLUGIN_STATS)
    if n_kf:
        sm["keyframe"] = True
        sm["keyframe_values"] = n_kf
        sm["resync_values"] = sum(1 for e in entries if e.get("resync"))
    if n_delta:
        sm["delta"] = True
        sm["delta_values"] = n_delta
        # One slot per value, shared by every thread: sound only for a
        # single-threaded target.
        sm["delta_single_threaded"] = True
    return sm, problems


# --------------------------------------------------------------------------
# Which sites did e9tool / E9Patch fail to instrument? 
# --------------------------------------------------------------------------

def _lea_rip_target(ins):
    """The absolute target of `lea reg, [rip +/- disp]', else None."""
    m = re.search(r"\[rip ([+-]) (0x[0-9a-fA-F]+)\]", ins.op_str)
    if not m:
        return None
    d = int(m.group(2), 16)
    return ins.address + ins.size + (d if m.group(1) == "+" else -d)


def _evicted_call(img, md, a, oi, t):
    """Recognise E9Patch's relocation of an evicted `call' at block T
    (e9x86_64.cpp pushReturnAddress, -Ocall=false): the return address is
    materialised -- non-PIC `push imm32'; PIC `push %rax; lea ret(%rip),%rax;
    xchg %rax,(%rsp)'; with -Oscratch-stack `mov %rax,-0x4000(%rsp);
    lea ret(%rip),%rax; push %rax; mov -0x3ff8(%rsp),%rax' -- and the call
    becomes a `jmp' with the call's operand.  Returns the `relocated' entry
    (len = the call, tramp_len = through the jmp) or None."""
    insns = list(md.disasm(img.read(t, 64) or b"", t))
    ret = a + oi.size

    def ins(k):
        return insns[k] if k < len(insns) else None
    ok, k = False, 0
    i0, i1, i2, i3 = ins(0), ins(1), ins(2), ins(3)

    def lea_is_ret(ins_):
        # With `--ld-so' / `--no-mem-lb' E9Patch places trampolines
        # BELOW the image base, and capstone reports those addresses as
        # unsigned wrap-around values while `ret' is a plain link-time vaddr.
        # Comparing them without masking would reject an ordinary evicted
        # call.
        target = _lea_rip_target(ins_)
        return target is not None and (target - ret) % (1 << 64) == 0

    if i0 is not None and i0.mnemonic == "push" and i0.op_str.startswith("0x"):
        ok = (int(i0.op_str, 16) & 0xffffffff) == (ret & 0xffffffff); k = 1
    elif (i0 is not None and i0.mnemonic == "push" and i0.op_str == "rax" and
          i1 is not None and i1.mnemonic == "lea" and
          i2 is not None and i2.mnemonic == "xchg"):
        ok = lea_is_ret(i1); k = 3
    elif (i0 is not None and i0.mnemonic == "mov" and "rsp - 0x4000" in i0.op_str and
          i1 is not None and i1.mnemonic == "lea" and
          i2 is not None and i2.mnemonic == "push" and
          i3 is not None and i3.mnemonic == "mov" and "rsp - 0x3ff8" in i3.op_str):
        ok = lea_is_ret(i1); k = 4
    if not ok:
        return None
    jj = ins(k)
    if jj is None or jj.mnemonic != "jmp":
        return None
    return dict(tramp_addr=t, orig_addr=a, len=oi.size,
                tramp_len=(jj.address + jj.size) - t)


def close_ld_so_gap(path, loader_base):
    """Leave the kernel no room for the vdso below E9Patch's loader.

    `--ld-so' parks E9Patch's loader segment above the image with the injected
    ldfix and runtime ELFs in between -- but those two are mapped by the
    E9Patch loader at RUN time, so as far as the KERNEL is concerned the output
    ELF has a 14-page hole between its last PT_LOAD and the loader segment.
    The kernel fills the highest >= 8-page gap at or below the top of the ELF
    mapping with the [vvar][vvar_vclock][vdso] block, so the block lands
    exactly there -- and the loader's very first act is
    mmap(loader_base - 4096, MAP_FIXED), which then tries to split the vdso
    VMA and fails with EINVAL ("mmap() scratch failed (errno=22)").  Without
    the runtime ELF (a --sink ptwrite build) the hole is 4 pages and the block
    does not fit, so only buffer-sink and --gt-all loader builds are affected.

    The fix needs no new mapping and no e9patch change: grow the p_memsz of the
    image's last PT_LOAD so it reaches the loader segment.  The extra pages are
    anonymous zero pages the kernel maps with the image (p_filesz is unchanged,
    exactly like .bss), the gap disappears, the vdso goes below the image as it
    does for a ptwrite build, and the loader's own MAP_FIXED calls replace
    those pages with the ldfix/runtime mappings when it runs.
    """
    with open(path, "r+b") as f:
        data = bytearray(f.read())
        if data[:4] != b"\x7fELF" or data[4] != 2:
            raise SystemExit("[rewrite] --ld-so: not a 64-bit ELF: %s" % path)
        e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
        e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)
        best = None
        for i in range(e_phnum):
            off = e_phoff + i * e_phentsize
            p_type, p_flags = struct.unpack_from("<II", data, off)
            if p_type != 1:                                   # PT_LOAD
                continue
            p_vaddr, = struct.unpack_from("<Q", data, off + 0x10)
            p_filesz, p_memsz = struct.unpack_from("<QQ", data, off + 0x20)
            end = p_vaddr + p_memsz
            if end > loader_base:                             # the loader itself
                continue
            if best is None or end > best[1]:
                best = (off, end, p_vaddr, p_memsz)
        if best is None:
            raise SystemExit("[rewrite] --ld-so: no PT_LOAD below the loader base")
        off, end, p_vaddr, p_memsz = best
        if end >= loader_base:
            return 0
        grown = loader_base - p_vaddr
        struct.pack_into("<Q", data, off + 0x28, grown)       # p_memsz
        f.seek(0); f.write(data); f.truncate()
    return loader_base - end


def load_gt_skip(path):
    """`--gt-skip FILE': one `0x...' address per line, `#' comments."""
    out = set()
    if not path:
        return out
    with open(path) as f:
        for line in f:
            m = re.match(r"\s*(0x[0-9a-fA-F]+)", line)
            if m:
                out.add(int(m.group(1), 16))
    return out


# Why a ground-truth (oracle) observation was given up at an address.  The
# distinction is load-bearing for any conservative accuracy bound: a dropped
# oracle probe turns reconstructed accesses into EXCLUDED ones, and the
# conservative rule charges every excluded access as an error, so a single
# dropped probe at a hot address can dominate the bound.
GT_SKIP_EXPLICIT = "explicit"        # the operator listed it in --gt-skip FILE
GT_SKIP_MID_INSN = "mid_insn"        # Unavoidable, a patch jump would kill the process
GT_SKIP_RETRY = "retry"              # --gt-retry gave it up to place a neighbouring spec site
GT_SKIP_REASONS = (GT_SKIP_EXPLICIT, GT_SKIP_MID_INSN, GT_SKIP_RETRY)


def gt_skip_by_reason(reasons, addresses=None):
    """{reason: sorted addresses}, restricted to ADDRESSES when given."""
    out = {r: [] for r in GT_SKIP_REASONS}
    for addr in sorted(addresses if addresses is not None else reasons):
        out.setdefault(reasons.get(addr, GT_SKIP_EXPLICIT), []).append(addr)
    return out


def write_gt_skip(path, addresses, reasons=None):
    """Retain the actual oracle exclusions even when critical placement fails."""
    reasons = reasons or {}
    with open(path, "w") as f:
        f.write("# Oracle observations omitted; critical values are NOT omitted.\n")
        f.write("# These addresses remain unverified/excluded in comparison,\n")
        f.write("# and a conservative bound charges every access at them as an error.\n")
        by = gt_skip_by_reason(reasons, addresses)
        f.write("# %d explicit (--gt-skip), %d mid-insn branch target, "
                "%d dropped by --gt-retry\n"
                % (len(by[GT_SKIP_EXPLICIT]), len(by[GT_SKIP_MID_INSN]),
                   len(by[GT_SKIP_RETRY])))
        for addr in sorted(addresses):
            f.write("0x%x  # %s\n" % (addr, reasons.get(addr, GT_SKIP_EXPLICIT)))


def mid_insn_branch_targets(image_path):
    """Instructions that a DIRECT branch enters in the MIDDLE, and which must
    therefore never be replaced by a patch jump.

    glibc's single-thread lock elision is written as

        cmpl $0x0,%fs:0x18          ; SINGLE_THREAD_P
        je   1f                     ; `74 01' -- jumps over the LOCK PREFIX
        lock andl $0xfffffffe,0x4(%rbp)
      1:                            ; ... i.e. INTO the middle of that insn

    so the `je' target is the second byte of the `lock'-prefixed instruction.
    E9Patch replaces a patched instruction with a 5-byte `jmp', so patching the
    `lock' instruction leaves the `je' jumping into the middle of that `jmp':
    `e9 de 70 82 70' entered at +1 is `fidivr', and the process dies with
    SIGSEGV before it ever reaches main.  A `--ptw'/CV build survives only
    because its spec puts no site there; `--gt-all' patches EVERY
    memory-accessing instruction and therefore always hits it.

    The scan is a linear sweep of every executable section: decode, remember
    each instruction's [addr, addr+size) extent, and collect the targets of
    every direct branch/call.  Any target that is not an instruction START but
    falls strictly inside one names that instruction as unsafe.  Returned as a
    set of link-time addresses to be added to the `--gt-all' skip list, where
    the affected accesses are `excluded' from the comparison rather than lost.
    """
    try:
        import capstone
    except ImportError:
        print("[rewrite] --gt-all: capstone is not available; cannot scan for "
              "branch targets inside instructions (see mid_insn_branch_targets)",
              file=sys.stderr)
        return set()
    from elftools.elf.elffile import ELFFile
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    GRP = (capstone.x86.X86_GRP_JUMP, capstone.x86.X86_GRP_CALL)
    starts, extent, targets = set(), [], set()
    with open(image_path, "rb") as f:
        elf = ELFFile(f)
        for sec in elf.iter_sections():
            if sec["sh_type"] == "SHT_NOBITS":
                continue
            if not (sec["sh_flags"] & 0x4):        # SHF_EXECINSTR
                continue
            base, data = sec["sh_addr"], sec.data()
            for insn in md.disasm(data, base):
                starts.add(insn.address)
                if insn.size > 1:
                    extent.append((insn.address, insn.size))
                if not (set(insn.groups) & set(GRP)):
                    continue
                ops = insn.operands
                if len(ops) == 1 and ops[0].type == capstone.x86.X86_OP_IMM:
                    targets.add(ops[0].imm)
    bad = set()
    inside = sorted(t for t in targets if t not in starts)
    if inside:
        extent.sort()
        import bisect
        addrs = [a for a, _ in extent]
        for t in inside:
            i = bisect.bisect_right(addrs, t) - 1
            if i >= 0:
                a, n = extent[i]
                if a < t < a + n:
                    bad.add(a)
    return bad


def gt_skip_candidates(image_path, failed, attempt, plugin_csv, groups, include_critical=False):
    """Gt-only instructions whose gt patch may be what keeps E9Patch from
    patching the spec sites in FAILED (the
    buffer/mixed convergence case).

    Every spec site a `--gt-all' build leaves unpatched is a 1-2 byte
    instruction (`push %rbp' at a function entry, a padding `nop').  E9Patch's
    only tactic for a single-byte instruction is T3b: the SUCCESSOR must be
    unpatched (its first byte is read as the rel8 of a short jump), the
    instruction that rel8 LANDS in must be unpatched at that byte and
    evictable, and the punned trampoline address must be free.  In an
    ordinary build the successor of a `push %rbp' is rarely a site; under
    `--gt-all' it usually is (a `push %rbx' or a load), and E9Patch patches in
    reverse address order, so the neighbour wins and the site is lost.  For a
    2-4 byte instruction the general T3 scans every unpatched neighbour within
    rel8 range for a byte it can pun a jump into.

    So: attempt 1 gives up the gt sequence at exactly the successor and the
    T3b landing instruction; attempt 2 widens that to +-24 bytes and adds the
    site's own neighbourhood; attempt 3 to +-128 bytes (the rel8 range).  Only
    gt-only instructions (never a spec site) are normally returned. With
    include_critical, a spec site's ORACLE sequence may also be omitted;
    its critical-value sequence remains mandatory and is never removed."""
    import bisect
    matched = set()
    if plugin_csv and os.path.exists(plugin_csv):
        with open(plugin_csv) as f:
            for line in f:
                if line.startswith("#"):
                    continue
                try:
                    matched.add(int(line.split(",")[0], 16))
                except ValueError:
                    pass
    matched = sorted(matched if include_critical else matched - set(groups))
    if not matched or not failed:
        return set()
    img = VirtualImage(image_path)
    _cs, md = load_capstone()

    def insn_at(a):
        b = img.read(a, 16)
        if not b:
            return None
        for ins in md.disasm(b, a):
            return ins
        return None

    win = {1: 0, 2: 24}.get(attempt, 128)
    cand = set()

    def add_range(lo, hi):
        # every gt-only matched instruction that overlaps [lo, hi]
        i = bisect.bisect_left(matched, lo - 15)
        while i < len(matched) and matched[i] <= hi:
            m = matched[i]
            i += 1
            ins = insn_at(m)
            sz = ins.size if ins is not None else 1
            if m + sz > lo:
                cand.add(m)

    for A in failed:
        ins = insn_at(A)
        if ins is None:
            continue
        s = ins.size
        pts = [A + s]                       # the successor (T3b reads its byte)
        if s == 1:
            b = img.read(A + 1, 1)
            if b:
                rel8 = b[0] - 256 if b[0] >= 128 else b[0]
                pts.append(A + 2 + rel8)    # T3b's short-jump landing
        for p in pts:
            add_range(p - win, p + win)
        if attempt >= 2:
            add_range(A - win, A + s + win) # T3 (2-4 bytes) scans rel8 range
    return cand


# --count-blocks bookkeeping: block leader ->
# end of its block (link-time), counters moved off an unpatchable leader
# (new address -> leader), blocks left without a counter, and the counted
# addresses the final e9tool pass still could not patch (their counts are 0
# for the wrong reason, so counts_to_weights.py must not emit them).
BLOCK_EXT = {}
COUNT_MOVED = {}
COUNT_UNPROFILED = []
COUNT_UNPATCHED = []
# `--log-blocks': the block-identifier log sites, as
# {link-time leader address -> block id}.  Same placement machinery as
# `--count-blocks' -- a leader E9Patch cannot patch is moved to the next
# instruction of the same block -- but a DIFFERENT fallback rule: a block whose
# leader is unpatchable is NOT covered by some other spec site inside it (a spec
# site logs a critical value, not the block identifier), so `covered' does not
# apply and the site really must move or the block goes unlogged.
BLOCK_LOG = {}
BLOCK_MOVE_LOST = [0]


def block_extents(spec, exclude):
    """({block leaders}, {leader -> link-time end of its block}, n_noblocks)
    from the spec's per-function `blocks_list' (ANALYZER_VERSION >= 2.23)."""
    leaders, n_noblocks = set(), 0
    for fn in (spec.get("functions") or ()):
        bl = fn.get("blocks_list")
        if bl is None:
            n_noblocks += 1
            continue
        for b in bl:
            b = int(b)
            if not any(lo <= b < hi for lo, hi in exclude):
                leaders.add(b)
    ext = {}
    for fn in (spec.get("functions") or ()):
        bl = fn.get("blocks_list")
        if not bl:
            continue
        bl = sorted(int(b) for b in bl)
        end = int(fn.get("end") or 0)
        for i, b in enumerate(bl):
            hi = bl[i + 1] if i + 1 < len(bl) else end
            ext[b] = max(hi, b + 1)
    return leaders, ext, n_noblocks


def move_block_counters(image_path, groups, not_patched, not_matched,
                        logging=False):
    """`--count-blocks': a block-profile counter E9Patch could not place at a
    block's LEADER (typically a 1-byte instruction whose neighbours are
    patched too) is moved to the next instruction of the SAME block that is
    not already counted -- a block executes as a unit, so any of its
    instructions counts it, and analyze.py's SiteCost takes a block's weight
    from whichever of its instructions carries one.  A block that already has
    another patched counted address (a spec site) needs nothing: that address
    is its count.  A block with no usable instruction (a lone `jmp'/`ret')
    stays unprofiled and is recorded.  Returns (moved, covered, unprofiled).

    With `logging=True' this serves `--log-blocks' instead: the site carries one
    `i' op (the block identifier) rather than no ops, that op MOVES with the
    site, and the `another counted address already covers this block' shortcut
    is disabled -- no other site in the block logs the identifier."""
    bad = set(not_patched) | set(not_matched)
    if logging:
        failed = sorted(A for A in bad
                        if A in groups and A in BLOCK_LOG and A in BLOCK_EXT)
    else:
        failed = sorted(A for A in bad if A in groups and not groups[A] and A in BLOCK_EXT)
    if not failed:
        return 0, 0, 0
    img = VirtualImage(image_path)
    _cs, md = load_capstone()

    def insn_size(addr):
        b = img.read(addr, 16)
        if not b:
            return None
        for ins in md.disasm(b, addr):
            return ins.size
        return None

    moved = covered = unprof = 0
    for A in failed:
        hi = BLOCK_EXT[A]
        if not logging and any(x in groups and x not in bad
                               for x in range(A + 1, hi)):
            del groups[A]                # the block is counted by that address
            covered += 1
            continue
        a, new = A, None
        while a < hi:
            sz = insn_size(a)
            if not sz:
                break
            a += sz
            if a >= hi:
                break
            if a not in groups and a not in bad:
                new = a
                break
        ops = groups[A]
        del groups[A]
        bid = BLOCK_LOG.pop(A) if logging else None
        if new is None:
            COUNT_UNPROFILED.append(A)
            unprof += 1
        else:
            if logging:
                # The identifier follows its block: `new' is inside the SAME
                # block, so the block still logs exactly once per execution.
                # A spec op that happened to share the leader does NOT move --
                # its liveness (dead EFLAGS, dead registers) was established at
                # the leader's program point and is not valid two instructions
                # later -- so those values are dropped and counted.  The new
                # site takes the conservative flag-free emitter.
                blk = [o for o in ops if o.kind == "i"]
                BLOCK_MOVE_LOST[0] += len(ops) - len(blk)
                groups[new] = [o._replace(dead=False, dregs=0) for o in blk]
                BLOCK_LOG[new] = bid
            else:
                groups[new] = []
            COUNT_MOVED[new] = A
            moved += 1
    return moved, covered, unprof


def unpatched_addresses(out_path, groups, plugin_csv):
    """Return (not_matched, not_patched) spec addresses of a failed rewrite.

    Two COUNTS are useless for a large image: `analyze.py --avoid` -- the
    documented remedy -- needs the addresses.  They are recovered from the
    artefacts the run has already produced:

      * NOT MATCHED = a spec group address that never appears in the plugin's
        emitted CSV, i.e. e9tool's disassembler did not see an instruction
        there (usually because the address is in data, or in the middle of an
        instruction e9tool decoded differently);
      * NOT PATCHED = a matched address at which the output binary holds no
        jump into a trampoline, i.e. every E9Patch tactic (B1/B2, T1/T2/T3)
        failed -- typically a 1- or 2-byte instruction with no punnable
        neighbour.
    """
    matched = set()
    if plugin_csv and os.path.exists(plugin_csv):
        with open(plugin_csv) as f:
            for line in f:
                if line.startswith("#"):
                    continue
                try:
                    matched.add(int(line.split(",")[0], 16))
                except ValueError:
                    pass
    want = set(groups)
    not_matched = sorted(want - matched)
    not_patched = []
    try:
        img = VirtualImage(out_path)
        _cs, md = load_capstone()
        traps = img.traps()
        for a in sorted(want & matched):
            try:
                t = follow_patch(img, md, a, traps)
            except Exception:
                t = None
            if t is None:
                not_patched.append(a)
    except Exception as e:                      # the output may not even exist
        print("[rewrite] (could not scan %s for unpatched sites: %s)"
              % (out_path, e))
    return not_matched, not_patched


def write_unpatched(path, not_matched, not_patched):
    """`<out>.unpatched`: one `0x...` per line, ready for `analyze.py --avoid`."""
    with open(path, "w") as f:
        f.write("# addresses the rewriter could not instrument -- feed to "
                "static/analyze.py --avoid\n")
        f.write("# %d not matched by e9tool, %d not patched by E9Patch\n"
                % (len(not_matched), len(not_patched)))
        for a in not_matched:
            f.write("0x%x  # not matched\n" % a)
        for a in not_patched:
            f.write("0x%x  # not patched\n" % a)
    return path


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec")
    ap.add_argument("image")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--sink", choices=["ptwrite", "buffer", "mixed", "count"],
                    default="ptwrite",
                    help="`mixed' logs the sites "
                         "named by --ptw-sites through Intel PT `ptwrite' and "
                         "everything else into the per-thread buffer, so the "
                         "PTWRITE traffic stays under a chosen budget.  `count' "
                         "logs NOTHING -- each "
                         "patched address just bumps a per-thread execution "
                         "counter (no PT, no cv), dumped as CSV at exit to size "
                         "the mixed-sink budget from EXACT whole-run counts")
    ap.add_argument("--count-base", type=int, default=0,
                    help="--sink count: this image's first GLOBAL counter index "
                         "(the build gives every image a disjoint range so the "
                         "per-thread counter arrays never collide)")
    ap.add_argument("--count-blocks", action="store_true",
                    help="--sink count: ALSO put a count-only trampoline at "
                         "every BASIC-BLOCK LEADER the analyzer recovered (the "
                         "spec's per-function `blocks_list', ANALYZER_VERSION "
                         "v2.23+), so the count CSV is a per-BLOCK execution "
                         "profile of the whole run.  Feed it through "
                         "static/tools/counts_to_weights.py to "
                         "`analyze.py --site-weights' and the hitting-set "
                         "solver minimises MEASURED executions instead of the "
                         "static 10^loop_depth estimate"
                         ".  A block-leader "
                         "address that is also a spec site shares the one "
                         "trampoline and the one counter")
    ap.add_argument("--count-blocks-only", action="store_true",
                    help="--sink count: as --count-blocks but WITHOUT the "
                         "spec's own sites -- the image is patched at block "
                         "leaders only (a smaller, cheaper profiling build "
                         "whose counts are still a complete block profile)")
    # ---- CONTROL-FLOW LOGGING -----------------
    # The "PTracer without Intel PT" ablation: control flow is
    # INSTRUMENTED instead of hardware-traced.  The image logs a compile-time
    # BLOCK IDENTIFIER through the ordinary buffer sink at every basic-block
    # leader, so the value stream carries the executed path interleaved with the
    # critical values, and no PT capture is needed.  Indirect branches and
    # returns are not special-cased here: their TARGET is the leader of the
    # block that runs next, and that block logs its own identifier.
    ap.add_argument("--log-blocks", action="store_true",
                    help="--sink buffer: ALSO log a compile-time BASIC-BLOCK "
                         "IDENTIFIER at every block leader the analyzer "
                         "recovered (the spec's per-function `blocks_list', "
                         "ANALYZER_VERSION v2.23+), through the ordinary "
                         "buffer sink.  The value stream then records CONTROL "
                         "FLOW as well as critical values, which is the "
                         "paper's `PTracer w/o Intel PT' ablation arm"
                         ".  One extra 8-byte record "
                         "per executed block; the identifier is stored as an "
                         "immediate, so a leader costs one cursor load, one "
                         "store and one cursor bump")
    ap.add_argument("--log-blocks-only", action="store_true",
                    help="as --log-blocks but WITHOUT the spec's own value "
                         "sites: the image logs control flow and nothing else. "
                         "Isolates the control-flow component of the w/o-PT "
                         "bar, and gives a stream whose per-block execution "
                         "counts can be checked against a "
                         "`--sink count --count-blocks-only' build of the same "
                         "spec")
    ap.add_argument("--log-blocks-map", metavar="FILE",
                    help="--log-blocks: write the {block id -> link-time leader "
                         "address} table as JSON.  A reconstructor that follows "
                         "control flow from the value stream instead of from PT "
                         "needs it; nothing in the PT-driven path reads it")
    ap.add_argument("--count-blocks-retry", type=int, default=2, metavar="N",
                    help="--count-blocks: when E9Patch cannot place a "
                         "block-profile counter at a block leader, move it to "
                         "the next instruction of the same block and re-run "
                         "e9tool, at most N times (default 2); blocks with no "
                         "usable instruction are listed in the site map's "
                         "`count_unprofiled'")
    ap.add_argument("--count-plan", default=None, metavar="FILE",
                    help="--sink count: write the number of counters this "
                         "image will take (its `--count-base' demand) to FILE "
                         "and exit WITHOUT rewriting, so a build can give "
                         "several images disjoint counter ranges before "
                         "rewriting them in parallel")
    ap.add_argument("--kf-gs", dest="kf_gs", action="store_true",
                    help="put the KEYFRAME countdown counters in the thread's "
                         "own %%gs region instead of one cell per site shared by "
                         "every thread.  REQUIRED for a threaded target with "
                         "--keyframe on the buffer sink: a shared countdown "
                         "fires at positions the reconstructor does not expect "
                         "and every later value is consumed against the wrong "
                         "site.  Needs --kf-base, and the "
                         "runtime needs PTLOG_KF_N >= the total across images.")
    ap.add_argument("--kf-base", type=int, default=None, metavar="N",
                    help="--kf-gs: this image's first index in the PROCESS-WIDE "
                         "keyframe counter array.  Every image mapped into one "
                         "process must get a disjoint range (use --kf-plan to "
                         "size each one first), exactly like --count-base.")
    ap.add_argument("--kf-plan", default=None, metavar="FILE",
                    help="write the number of keyframe counters this image "
                         "needs to FILE and exit, so a multi-image build can "
                         "lay out disjoint --kf-base ranges.")
    ap.add_argument("--keyframe-mt", choices=["warn", "refuse", "ok"],
                    default="warn",
                    help="what to do when --keyframe is used on an image that "
                         "can create threads and the countdowns are SHARED "
                         "(no --kf-gs): warn loudly (default), refuse to build, "
                         "or stay quiet.")
    ap.add_argument("--sync-carrier", choices=("ptwrite", "tnt"), default="ptwrite",
                    help="buffer sink: how a sync marker carries its count. "
                         "`ptwrite' (default) = one PTWRITE; `tnt' = the TNT bits "
                         "of a short branch loop, so the image executes NO PTWRITE "
                         "(PT-capable CPUs without PTWRITE)")
    ap.add_argument("--sync-cursor", action="store_true",
                    help="EXPERIMENTAL buffer-only sync derived from the committed "
                         "cursor rather than a per-site countdown. Requires a rebuilt "
                         "ptlogrt and the SAME option in every instrumented image; "
                         "cannot be mixed with the default synchronization. Default off.")
    ap.add_argument("--reserve-cursor", dest="reserve", action="store_true",
                    help="advance %%gs:CURSOR to the end of a site's run BEFORE "
                         "storing its values instead of after.  Closes the "
                         "window in which an asynchronous signal whose handler "
                         "runs instrumented code logs at the stale cursor and "
                         "OVERWRITES the interrupted site's values -- in a "
                         "whole-program build the handler is always "
                         "instrumented, so the precondition holds on every "
                         "shipped target.  Same instruction count.  Needs the "
                         "matching rt/ptlogrt.c (signed store displacement, and "
                         "the reservation re-established on a guard-page "
                         "fault).")
    ap.add_argument("--nt-store", action="store_true",
                    help="--sink buffer: emit the per-value ring store as "
                         "`movnti' (non-temporal) instead of `mov'.  Skips the "
                         "read-for-ownership of the ring line and does not "
                         "allocate it in the cache; WEAKLY ORDERED, so it needs "
                         "the matching rt/ptlogrt.c (sfence before every buffer "
                         "hand-off, and a guard-page decoder that knows "
                         "REX.W 0F C3).")
    ap.add_argument("--fixed-slot", action="store_true",
                    help="DEBUG, NOT SHIPPABLE: --sink buffer with every value "
                         "stored to the SAME 8-byte slot and no cursor advance. "
                         "Isolates the cost of ISSUING the stores from the cost "
                         "of MOVING the bytes.  The cv stream of such a build is "
                         "meaningless and must never be reconstructed or quoted.")
    ap.add_argument("--count-atomic", action="store_true",
                    help="--sink count: emit `lock incq' instead of `incq' for "
                         "a target whose per-thread lazy %%gs setup cannot be "
                         "relied on; the "
                         "per-thread default is lock-free and exact")
    ap.add_argument("--ptw-sites", default="",
                    help="--sink mixed: file of spec SITE IDs (one per line, "
                         "`#' comments and blank lines ignored) to log through "
                         "PTWRITE.  Every other site uses the buffer sink")
    ap.add_argument("--space", type=int, default=3,
                    help="min non-PTWRITE instructions between PTWRITEs")
    ap.add_argument("--sync", type=int, default=4096,
                    help="buffer sink: PTWRITE a running count every N values")
    ap.add_argument("--shared", action="store_true",
                    help="the image is a shared library (e9tool --shared)")
    ap.add_argument("--exclude-ranges", default="",
                    help="lo-hi[,lo-hi...] address ranges to skip (unsafe funcs)")
    ap.add_argument("--rt", default=RT_E9,
                    help="buffer sink runtime injected with e9tool's init "
                         "mechanism (empty = expect LD_PRELOAD instead)")
    ap.add_argument("--plugin", default=PLUGIN)
    ap.add_argument("--ldfix", default=LDFIX_E9)
    ap.add_argument("--fix-at-entry", action="store_true",
                    help="inject runtime/rt/ldfix.e9rt, which restores the "
                         "auxiliary vector's AT_ENTRY to the ORIGINAL entry "
                         "point at start-up.  Required to rewrite "
                         "ld-linux-x86-64.so.2: glibc decides "
                         "whether it is the interpreter or the program by "
                         "comparing AT_ENTRY with its own `_start', and "
                         "E9Patch's new e_entry breaks that test")
    ap.add_argument("--delta", action="store_true",
                    help="LOG-ON-CHANGE trampolines"
                         ": each logged value gets "
                         "an 8-byte cache slot and the logging instruction runs "
                         "only when the value differs from the last one logged "
                         "at that site.  The `je' that skips it is a real "
                         "branch, so Intel PT records the decision and "
                         "offline/ptrecon reconstructs the skipped values from "
                         "its own copy of the table.  ONLY SOUND FOR A "
                         "SINGLE-THREADED TARGET (one slot per value, shared)")
    ap.add_argument("--delta-profile", default=None,
                    help="PROFILE-GUIDED --delta: a `ptrecon --site-stats' CSV "
                         "(from a plain or a delta build) whose `count' / "
                         "`repeats' / `skipped' columns say how often each "
                         "logged value repeated.  Only values above "
                         "--delta-min-repeat, and executed at least "
                         "--delta-min-exec times, get a guard; the rest are "
                         "logged unconditionally.")
    ap.add_argument("--delta-min-repeat", type=float, default=0.1,
                    help="profile-guided --delta: minimum repeat ratio "
                         "(repeats+skipped)/(count+skipped) (default 0.1, the "
                         "break-even)")
    ap.add_argument("--delta-min-exec", type=int, default=1000,
                    help="profile-guided --delta: a value the profile saw fewer "
                         "than this many times is not worth a slot (default "
                         "1000)")
    ap.add_argument("--delta-profile-image", default="",
                    help="profile-guided --delta: only use the CSV rows whose "
                         "`image' contains this substring (a whole-program "
                         "profile covers several images and their spec site ids "
                         "overlap)")
    ap.add_argument("--delta-profile-key", choices=["site", "addr"],
                    default="site",
                    help="profile-guided --delta: match the CSV rows to the "
                         "spec by spec SITE ID (default) or by the site's "
                         "LINK-TIME ADDRESS.  Site ids are positional, so a "
                         "profile taken from a DIFFERENT SPEC of the same "
                         "binary (one without the keyframe `resync' sites, "
                         "say) must be matched by address or it lands on the "
                         "wrong values")
    ap.add_argument("--delta-profile-unseen", choices=["off", "on"],
                    default="off",
                    help="profile-guided --delta: what to do with a value the "
                         "profile never saw execute (default `off': no guard -- "
                         "it costs nothing either way)")
    ap.add_argument("--keyframe", type=int, default=None,
                    help="KEYFRAME period K for the spec's `resync' sites "
                         "(ANALYZER_VERSION v2.15, analyze.py --keyframe): a "
                         "re-anchor site at a loop back-edge header logs its "
                         "registers unconditionally, but only every K-th "
                         "execution, so a PT overflow or decoder resync inside "
                         "a long-running loop costs at most K iterations of "
                         "unknown addresses instead of the rest of the run"
                         ".  Default: the K the "
                         "spec records per site.  `--keyframe 0' DROPS the "
                         "resync sites, which is how the same spec is built "
                         "with and without them")
    ap.add_argument("--keyframe-flags-live", choices=["drop", "log"],
                    default="drop",
                    help="what to do with a resync site whose EFLAGS are LIVE "
                         "(the counter's `dec' would clobber them): `drop' = do "
                         "not instrument it (default -- a resync site is an "
                         "accuracy bonus, and it sits at a LOOP HEADER where "
                         "unconditional logging would cost a PTWRITE per "
                         "iteration); `log' = log it on every execution, with "
                         "no counter guard at all.  Applied to what the SPEC "
                         "calls live AND to what the rewriter's own EFLAGS "
                         "check will not confirm")
    ap.add_argument("--delta-keyframe", type=int, default=4096,
                    help="--delta KEYFRAME period: a log-on-change value also "
                         "logs UNCONDITIONALLY every N-th execution, so the "
                         "reconstructor's copy of the cache table re-validates "
                         "within N executions of a PT overflow or resync "
                         "instead of never.  0 "
                         "turns it off")
    ap.add_argument("--dead-regs", choices=["both", "spec", "verify", "off"],
                    default="both",
                    help="where the trampoline's scratch registers come from"
                         ": `both' (default) = the "
                         "spec's `dead_regs' INTERSECTED with the rewriter's "
                         "own check of the image's bytes; `spec' = trust the "
                         "analyzer; `verify' = the rewriter's check alone "
                         "(works with a spec that has no `dead_regs'); `off' = "
                         "push and pop everything")
    ap.add_argument("--trust-flags-dead", action="store_true",
                    help="skip the rewriter's own check of the spec's "
                         "`flags_dead' (see verify_flags_dead): use it to "
                         "measure how wrong the analyzer is, not to build "
                         "something you intend to run")
    ap.add_argument("--delta-flags-live", choices=["log", "pushfq"],
                    default="log",
                    help="what --delta does at a site whose EFLAGS are LIVE "
                         "(the `cmp' would clobber them): `log' = log it "
                         "unconditionally, as without --delta (default, and "
                         "cheaper); `pushfq' = save and restore the "
                         "flags around the guard (4 extra instructions)")
    ap.add_argument("--under-ld-so", action="store_true",
                    help="the rewritten binary will be started by an "
                         "EXPLICITLY invoked loader (`./ld.so.e9 prog.e9'), so "
                         "/proc/self/exe names the loader, not this file.  "
                         "Clears E9_FLAG_EXE in the embedded loader config, "
                         "which makes E9Patch's loader find its own file "
                         "through /proc/self/map_files/ (the path it already "
                         "uses for shared objects) instead.  Without this the "
                         "trampolines are mapped from the WRONG file and the "
                         "process dies with SIGILL")
    ap.add_argument("--ld-so", action="store_true",
                    help="rewrite the dynamic loader: implies --fix-at-entry "
                         "and the memory layout the E9Patch loader needs when "
                         "the kernel maps the image at the top of the address "
                         "space (trampolines BELOW the image, the loader one "
                         "page above it, so the scratch page is free and the "
                         "vdso cannot be placed next to it).  Run the result "
                         "as `./ld.so.e9 --library-path DIR prog'")
    ap.add_argument("-O", "--opt", default=None, help="e9tool -O level")
    # e9tool -100 = e9patch --tactic-B0, the last-resort tactic that replaces
    # the instruction with the illegal opcode 0x27 and recovers in a SIGILL
    # handler installed by the E9Patch loader.
    #
    # It would close the "E9Patch could not patch this site" class of --avoid
    # rounds, but it is OFF by default because **B0 is unsound for any
    # target that ever blocks a signal**:
    # Linux delivers a synchronous fault with force_sig_info_to_task(), which
    # RESETS the handler to SIG_DFL (and marks it SA_IMMUTABLE) whenever the
    # signal is blocked in the faulting thread -- so a B0 site executed with
    # SIGILL blocked kills the process instead of reaching E9Patch's handler.
    # glibc's pthread_create blocks ALL signals around clone(2), in the parent
    # and in the new thread until start_thread restores the mask, so a rewritten
    # libc reaches B0 sites inside that window.  It cannot be fixed from outside
    # E9Patch, and it cannot help the NOT MATCHED class either (those are
    # addresses e9tool's disassembler does not consider instruction starts).
    ap.add_argument("--full-coverage", dest="full_coverage",
                    action="store_true", default=False,
                    help="pass e9tool -100 (= e9patch --tactic-B0), which "
                         "patches the last few otherwise un-patchable sites "
                         "with an illegal instruction + SIGILL handler.  UNSAFE "
                         "for any target that blocks signals (every "
                         "pthread_create does).  Single-threaded "
                         "targets only")
    ap.add_argument("--no-full-coverage", dest="full_coverage",
                    action="store_false",
                    help="explicitly disable -100 (this is the default)")
    ap.add_argument("--e9tool-arg", action="append", default=[],
                    help="extra argument passed straight to e9tool "
                         "(repeatable), e.g. --e9tool-arg=--option "
                         "--e9tool-arg=--tactic-T3=false")
    ap.add_argument("--e9-option", action="append", default=[],
                    help="option passed through to the e9patch BACKEND "
                         "(repeatable): `--e9-option loader-base=0x...' becomes "
                         "e9tool `--option --loader-base=0x...'.  Needed for "
                         "ld.so, whose E9Patch loader would otherwise land above "
                         "the 47-bit user address limit")
    ap.add_argument("--mem-lb", default=None,
                    help="minimum trampoline address (e9patch --mem-lb); "
                         "default = just past the image's last PT_LOAD, which "
                         "keeps every trampoline address a positive link-time "
                         "vaddr so the site map needs no signed offsets")
    ap.add_argument("--no-mem-lb", action="store_true",
                    help="let E9Patch place trampolines anywhere, including "
                         "BELOW the image base (site-map addresses may then be "
                         "negative and rewrite.py will report it as a problem)")
    ap.add_argument("--keep-going", action="store_true",
                    help="do not abort on a num_patched mismatch: write "
                         "<out>.unpatched and build the site map anyway "
                         "(the rewritten binary is still correct, it just "
                         "logs fewer values than the spec asks for)")
    ap.add_argument("--gt-all", action="store_true",
                    help="GROUND TRUTH: also "
                         "patch every memory-accessing instruction of the image "
                         "and make it log its effective address and its own ip "
                         "into a lossless per-thread ring, so a run can be "
                         "compared against itself.  This is a MEASUREMENT "
                         "build: it is much slower than a normal one and it "
                         "needs PTLOG_GT=1 in the environment.")
    ap.add_argument("--gt-stack", dest="gt_stack", action="store_true",
                    default=True,
                    help="--gt-all: also instrument push/pop/call/ret/leave, "
                         "whose stack access is implicit (default)")
    ap.add_argument("--no-gt-stack", dest="gt_stack", action="store_false")
    ap.add_argument("--gt-skip", default=None, metavar="FILE",
                    help="--gt-all: addresses (one `0x...' per line, `#' "
                         "comments) at which NO gt sequence is emitted.  A gt "
                         "patch on a critical-value site's neighbour can steal "
                         "the successor / rel8-landing instruction E9Patch's "
                         "single-byte tactic needs for the site itself; giving "
                         "up the ground truth at a few gt-only neighbours keeps "
                         "the critical value")
    ap.add_argument("--gt-retry", type=int, default=3, metavar="N",
                    help="--gt-all: when a spec site is left unpatched, drop "
                         "the gt sequence at its neighbouring gt-only "
                         "instructions (successor, T3b landing, then a widening "
                         "window) and re-run e9tool, up to N times, before "
                         "failing (default %(default)s; 0 = never)")
    ap.add_argument("--allow-gt-drop", action="store_true",
                    help="--gt-all: permit --gt-retry to GIVE UP the ground "
                         "truth at a neighbouring instruction in order to "
                         "place a spec site.  OFF by default: a "
                         "dropped oracle probe makes every access at that "
                         "address an excluded, unverified access, which a "
                         "conservative accuracy bound charges as an error.  "
                         "When on, the drops are counted and "
                         "reported in <out>.gtskip and the site map's "
                         "`gt_skip_by_reason'.")
    ap.add_argument("--gt-retry-critical-oracles", action="store_true",
                    help="allow retry to omit neighbouring spec sites' ORACLE "
                         "observations as well; critical values stay mandatory. "
                         "Omitted oracle coverage remains unverified.")
    ap.add_argument("--no-sitemap", action="store_true")
    ap.add_argument("--keep-temps", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--cursor-store", action="store_true",
                    help="experimental cursor-update-only ablation; preserves countdown sync")
    a = ap.parse_args()
    if a.cursor_store and (a.sink != "buffer" or a.reserve or a.fixed_slot or a.sync_cursor):
        ap.error("--cursor-store requires --sink buffer and no reserve/fixed-slot/cursor-sync")
    if a.sync_carrier == "tnt" and (a.sink != "buffer" or a.sync <= 0):
        ap.error("--sync-carrier tnt needs --sink buffer (no PTWRITE-sink value) and --sync > 0")
    if a.sync_cursor and (a.sink != "buffer" or a.reserve or a.fixed_slot or
                          not 0 < a.sync <= (2**31 - 1) // 8):
        ap.error("--sync-cursor requires --sink buffer, positive sync <= INT32_MAX/8, "
                 "and no --reserve-cursor/--fixed-slot")

    with open(a.spec) as f:
        spec = json.load(f)
    if spec.get("version") != 2:
        raise SystemExit("%s: not a v2 spec" % a.spec)

    elf = Elf(a.image)
    if "pie" in spec and bool(spec["pie"]) != elf.pie:
        raise SystemExit("spec says pie=%s but %s is ET_%s -- the spec was "
                         "produced for a different image"
                         % (spec["pie"], a.image, "DYN" if elf.pie else "EXEC"))

    groups, dropped = build_groups(spec, parse_ranges(a.exclude_ranges),
                                   a.keyframe, a.keyframe_flags_live)
    if dropped:
        print("[rewrite] %d sites dropped:" % len(dropped))
        for addr, sid, why in dropped[:10]:
            print("   0x%x (site %s): %s" % (addr, sid, why))
        if len(dropped) > 10:
            print("   ... +%d more" % (len(dropped) - 10))

    # ---- BLOCK PROFILE ----------------------
    # `--count-blocks' adds a count-only site at every basic-block LEADER the
    # analyzer recovered (the spec's per-function `blocks_list'), so that a
    # `--sink count' run measures how often every BLOCK of the image ran, not
    # only how often its critical-value sites ran.  That is what
    # `analyze.py --site-weights' wants: a block executes as a unit, so one
    # measured leader count prices every instruction of the block, including
    # the ones the current spec does not log (and which a profile-guided
    # re-analysis may want to log instead).
    #
    # A block-only site carries NO ops: it logs nothing, it only bumps its
    # counter.  The plugin gives such a site the conservative emitter (before
    # the instruction, flag-free, one pushed scratch) because there is no
    # liveness to trust at an address the analyzer never chose as a site.
    if a.count_blocks or a.count_blocks_only:
        if a.sink != "count":
            raise SystemExit("[rewrite] --count-blocks%s needs --sink count"
                             % ("-only" if a.count_blocks_only else ""))
        _ex = parse_ranges(a.exclude_ranges)
        leaders = set()
        n_noblocks = 0
        for fn in (spec.get("functions") or ()):
            bl = fn.get("blocks_list")
            if bl is None:
                n_noblocks += 1
                continue
            for b in bl:
                b = int(b)
                if not any(lo <= b < hi for lo, hi in _ex):
                    leaders.add(b)
        if not leaders:
            raise SystemExit("[rewrite] --count-blocks: this spec carries no "
                             "`blocks_list' (analyzer older than v2.23) -- "
                             "re-analyze the image with the current analyzer")
        n_site_only = sum(1 for a_ in groups if a_ not in leaders)
        n_shared = sum(1 for a_ in groups if a_ in leaders)
        if a.count_blocks_only:
            groups = {addr: [] for addr in sorted(leaders)}
        else:
            for addr in sorted(leaders):
                groups.setdefault(addr, [])
        print("[rewrite] --count-blocks%s: %d block leader(s) from %d "
              "function(s)%s; %d of them are also spec sites, %d spec site(s) "
              "are inside a block -> %d counted address(es)"
              % ("-only" if a.count_blocks_only else "", len(leaders),
                 len(spec.get("functions") or ()),
                 "" if not n_noblocks else
                 " (%d function(s) had no `blocks_list')" % n_noblocks,
                 n_shared, n_site_only, len(groups)))
        # block extents (leader -> end of block) for move_block_counters
        BLOCK_EXT.clear()
        for fn in (spec.get("functions") or ()):
            bl = fn.get("blocks_list")
            if not bl:
                continue
            bl = sorted(int(b) for b in bl)
            end = int(fn.get("end") or 0)
            for i, b in enumerate(bl):
                hi = bl[i + 1] if i + 1 < len(bl) else end
                BLOCK_EXT[b] = max(hi, b + 1)
    # ---- CONTROL-FLOW LOGGING --------------------
    # `--log-blocks' adds, at every basic-block LEADER, one `i' op: the buffer
    # sink stores a compile-time block identifier into the next 8-byte slot.
    # The value stream then interleaves "which block is running" with the
    # critical values, so control flow is recorded by INSTRUMENTATION rather
    # than by Intel PT -- the paper's `PTracer w/o PT' ablation arm.
    #
    # LIVENESS.  A leader is an address the analyzer never had to reason about,
    # so by default the op claims nothing: EFLAGS live, no dead registers, and
    # the emitter takes the conservative red-zone + pushed-scratch path.  Where
    # the leader IS also a spec site, the spec's `before' ops describe THE SAME
    # PROGRAM POINT, so the block op inherits their EFLAGS verdict and the
    # INTERSECTION of their dead-register sets -- which is what lets a leader
    # that is already instrumented log its identifier in the same run, sharing
    # one cursor load and one cursor bump with the values.
    #
    # ORDER.  The identifier is emitted FIRST at its address: a consumer reads
    # "which block" and only then the values that block's sites logged.
    if a.log_blocks or a.log_blocks_only:
        if a.sink not in ("buffer", "mixed"):
            raise SystemExit("[rewrite] --log-blocks%s needs --sink buffer "
                             "(`ptwrite' has no immediate form, so a block "
                             "identifier cannot go through the PTWRITE sink)"
                             % ("-only" if a.log_blocks_only else ""))
        _ex = parse_ranges(a.exclude_ranges)
        leaders, ext, n_noblocks = block_extents(spec, _ex)
        if not leaders:
            raise SystemExit("[rewrite] --log-blocks: this spec carries no "
                             "`blocks_list' (analyzer older than v2.23) -- "
                             "re-analyze the image with the current analyzer")
        BLOCK_EXT.clear()
        BLOCK_EXT.update(ext)
        # Site ids for the block ops start above every spec site id, so nothing
        # in the site map or the delta profile can confuse the two.
        sid_base = 1 + max([int(x.get("id", -1)) for x in (spec.get("sites") or ())]
                           + [-1])
        if a.log_blocks_only:
            groups = {addr: [] for addr in sorted(leaders)}
        n_shared = 0
        BLOCK_LOG.clear()
        for bid, addr in enumerate(sorted(leaders)):
            before = [o for o in groups.get(addr, ()) if not o.after]
            if before and not a.log_blocks_only:
                n_shared += 1
                dead = all(o.dead for o in before)
                dregs = 0xffff
                for o in before:
                    dregs &= o.dregs
            else:
                dead, dregs = False, 0
            op = Op(after=False, kind="i", arg=str(bid), sid=sid_base + bid,
                    dead=dead, resync=False, kf=0, dregs=dregs, noguard=True)
            groups.setdefault(addr, [])
            groups[addr] = [op] + groups[addr]
            BLOCK_LOG[addr] = bid
        print("[rewrite] --log-blocks%s: %d block leader(s) from %d "
              "function(s)%s; %d of them are also spec sites -> %d logged "
              "address(es), site ids %d..%d"
              % ("-only" if a.log_blocks_only else "", len(leaders),
                 len(spec.get("functions") or ()),
                 "" if not n_noblocks else
                 " (%d function(s) had no `blocks_list')" % n_noblocks,
                 n_shared, len(groups), sid_base, sid_base + len(leaders) - 1))

    if a.count_plan:
        if a.sink != "count":
            raise SystemExit("[rewrite] --count-plan needs --sink count")
        with open(a.count_plan, "w") as f:
            f.write("%d\n" % len(groups))
        print("[rewrite] --count-plan: this image takes %d counter(s) -> %s"
              % (len(groups), a.count_plan))
        return

    base = a.output
    site_file = base + ".sites"
    csv_file = base + ".emitted.csv"
    # The log-on-change cache slots live in a read-write, zero-filled region
    # E9Patch's loader maps for us, placed just above the image so the
    # trampolines (--mem-lb, below) still start above it.
    delta_mode = 0
    if a.delta:
        if a.ld_so:
            raise SystemExit("[rewrite] --delta and --ld-so are mutually "
                             "exclusive for now: --ld-so already spends the "
                             "space just above the image on the AT_ENTRY shim "
                             "and the E9Patch loader, which is where --delta "
                             "puts its cache slots")
        delta_mode = 2 if a.delta_flags_live == "pushfq" else 1
    n_values = expected_values(groups)
    data_base = (elf.end + PAGE - 1) & ~(PAGE - 1)
    slot_base = data_base
    n_resync_ops = sum(1 for ops in groups.values() for op in ops
                       if op.resync and op.kf)
    # The rewriter's own EFLAGS check is needed by BOTH guards -- the
    # log-on-change `cmp' and the keyframe `dec' clobber the same six flags --
    # so it runs whenever either is going to be emitted.  The BUFFER SINK needs
    # it too: its cursor update and its sync countdown are `add'/`sub' wherever
    # the flags are dead, and `lea'/`jrcxz' where
    # they are not.
    if (delta_mode or n_resync_ops or a.sink in ("buffer", "mixed", "count")) \
            and not a.trust_flags_dead:
        n_down, kinds = verify_flags_dead(a.image, groups,
                                          report=a.output + ".flags_live")
        if n_down:
            n_read = sum(v for k, v in kinds.items() if k.startswith("read"))
            print("[rewrite] EFLAGS: %d logged value(s) the spec calls "
                  "`flags_dead' were NOT confirmed dead by the rewriter's own "
                  "check; they get no guard (a log-on-change value is logged "
                  "unconditionally, a resync value follows "
                  "--keyframe-flags-live).  %d of them really "
                  "do have live flags (an analyzer error), %d are this check "
                  "giving up (its own conservatism).  See %s.flags_live"
                  % (n_down, n_read, n_down - n_read, a.output))
            for k, v in sorted(kinds.items(), key=lambda kv: -kv[1]):
                print("   %6d x %s" % (v, k))
    # Scratch registers.
    if a.dead_regs == "off":
        for ops in groups.values():
            for i, op in enumerate(ops):
                if op.dregs:
                    ops[i] = op._replace(dregs=0)
        print("[rewrite] --dead-regs off: every trampoline pushes and pops its "
              "scratch registers")
    else:
        n_off, n_keep, n_any = verify_dead_regs(
            a.image, groups, mode=a.dead_regs,
            report=a.output + ".dead_regs")
        n_ops_all = sum(len(v) for v in groups.values())
        print("[rewrite] --dead-regs %s: %d/%d logged value(s) have at least "
              "one scratch register that needs no push/pop (%d register-slots "
              "offered by the spec, %d kept) -> %s.dead_regs"
              % (a.dead_regs, n_any, n_ops_all, n_off, n_keep, a.output))
        if a.dead_regs == "both" and n_off == 0 and n_ops_all:
            print("[rewrite] note: this spec carries no `dead_regs' at all "
                  "(analyzer older than v2.17), so every trampoline pushes its "
                  "scratch.  Re-analyze, or pass --dead-regs verify to use the "
                  "rewriter's own check alone.")

    if n_resync_ops:
        n_kdrop, n_klog = apply_keyframe_flags_policy(groups,
                                                      a.keyframe_flags_live)
        if n_kdrop or n_klog:
            print("[rewrite] --keyframe-flags-live %s: %d resync value(s) "
                  "dropped, %d logged unconditionally (their EFLAGS is not "
                  "confirmed dead, so they cannot carry a counter guard)"
                  % (a.keyframe_flags_live, n_kdrop, n_klog))
    if delta_mode and a.delta_profile:
        prof = load_delta_profile(a.delta_profile, a.delta_profile_image,
                                  a.delta_profile_key)
        st = apply_delta_profile(groups, prof, a.delta_min_repeat,
                                 a.delta_min_exec,
                                 a.delta_profile_unseen == "on",
                                 report=a.output + ".delta_profile",
                                 key=a.delta_profile_key)
        print("[rewrite] --delta-profile %s: %d value(s) kept guarded, "
              "%d below %.0f%% repeats, %d executed < %d times, %d not in the "
              "profile -> %s.delta_profile"
              % (os.path.basename(a.delta_profile), st["kept"],
                 st["low_repeat"], 100 * a.delta_min_repeat, st["rare"],
                 a.delta_min_exec, st["unseen"], a.output))
    # A log-on-change value additionally logs UNCONDITIONALLY every
    # --delta-keyframe executions, so the reconstructor's copy of the table
    # re-validates within that many executions of a PT overflow or resync.
    if delta_mode and a.delta_keyframe > 1:
        for ops in groups.values():
            for i, op in enumerate(ops):
                if op.dead and not op.resync and not op.noguard:
                    ops[i] = op._replace(kf=a.delta_keyframe)
    n_dead = sum(1 for ops in groups.values() for op in ops if op.dead)
    n_ops = sum(len(v) for v in groups.values())
    n_slots = n_values if delta_mode else 0
    n_counters = count_counters(groups)
    if a.kf_plan:
        with open(a.kf_plan, "w") as f:
            f.write("%d\n" % n_counters)
        print("[rewrite] --kf-plan: this image needs %d keyframe counter(s) -> %s"
              % (n_counters, a.kf_plan))
        return
    # PER-THREAD KEYFRAME COUNTERS and the THREAD-SAFETY GATE.
    # A keyframe countdown in the image's
    # data region is ONE CELL SHARED BY EVERY THREAD.  On the PTWRITE sink that
    # costs at most a keyframe interval, because every value is self-locating in
    # the packet stream.  On the BUFFER sink the cv cursor is POSITIONAL, so a
    # keyframe that fires on another thread's countdown makes the reconstructor
    # consume every later value against the wrong site.
    if a.kf_gs:
        if a.kf_base is None or a.kf_base < 0:
            raise SystemExit("[rewrite] --kf-gs needs --kf-base N (this image's "
                             "first index in the process-wide keyframe counter "
                             "array; every image in one process must get a "
                             "disjoint range -- use --kf-plan to size them)")
        print("[rewrite] --kf-gs: %d keyframe counter(s) are PER-THREAD at "
              "%%gs:[4096 + 8*(%d .. %d)]; the runtime needs PTLOG_KF_N >= %d"
              % (n_counters, a.kf_base, a.kf_base + max(n_counters - 1, 0),
                 a.kf_base + n_counters))
    elif n_counters and a.keyframe_mt != "ok" and image_can_clone(a.image):
        msg = ("[rewrite] *** --keyframe on %s, whose symbols include a thread "
               "primitive (clone/clone3/pthread_create), with the countdown "
               "counters SHARED BY ALL THREADS.  On the buffer sink this "
               "produces WRONG addresses, not merely missed keyframes.  "
               "Use --kf-gs --kf-base N, or "
               "--keyframe 0, or --keyframe-mt ok if the target really is "
               "single-threaded. ***" % os.path.basename(a.image))
        if a.keyframe_mt == "refuse":
            raise SystemExit(msg)
        print(msg)
    if n_counters and a.ld_so:
        raise SystemExit("[rewrite] --keyframe and --ld-so are mutually "
                         "exclusive for now: --ld-so spends the space just "
                         "above the image on the AT_ENTRY shim and the E9Patch "
                         "loader, which is where the keyframe counters go "
                         "(same constraint as --delta)")
    counter_base = data_base + 8 * n_slots
    data_bytes = ((8 * (n_slots + n_counters)) + PAGE - 1) & ~(PAGE - 1)
    if delta_mode:
        print("[rewrite] --delta: %d cache slots (%d KiB) at %#x; %d/%d sites "
              "have dead flags%s"
              % (n_values, (8 * n_slots) // 1024, slot_base, n_dead, n_ops,
                 "" if delta_mode == 1 else
                 " (the rest save them with pushfq/popfq)"))
        print("[rewrite] --delta: the cache slots are SHARED BY ALL THREADS -- "
              "this build is only sound for a single-threaded target")
    if n_counters:
        n_res = sum(1 for ops in groups.values() for op in ops
                    if op.resync and op.kf)
        print("[rewrite] keyframes: %d counter(s) (%d KiB) at %#x -- %d resync "
              "value(s), delta keyframe period %d"
              % (n_counters, (8 * n_counters) // 1024, counter_base, n_res,
                 a.delta_keyframe if delta_mode else 0))
    # ---- MIXED SINK ---------------------------
    # Stamp every value of every site named by --ptw-sites with `:W' and all
    # the rest with `:B'.  The assignment is made OUTSIDE the rewriter (from
    # the per-site execution profile and a values/s budget) and handed in as a
    # plain list of spec site ids, so the same mechanism serves any policy.
    if a.sink == "mixed":
        want = set()
        if a.ptw_sites:
            with open(a.ptw_sites) as f:
                for line in f:
                    line = line.split("#", 1)[0].strip()
                    if line:
                        want.add(int(line, 0))
        n_ptw = n_buf = 0
        for addr, ops in groups.items():
            for i, op in enumerate(ops):
                if op.sid in want:
                    ops[i] = op._replace(sink="ptw"); n_ptw += 1
                else:
                    ops[i] = op._replace(sink="buf"); n_buf += 1
        print("[rewrite] --sink mixed: %d logged value(s) -> PTWRITE, %d -> "
              "buffer (%.2f %% PTWRITE) from %d site id(s) in %s"
              % (n_ptw, n_buf, 100.0 * n_ptw / max(1, n_ptw + n_buf),
                 len(want), a.ptw_sites or "(empty)"))
        if n_ptw == 0:
            print("[rewrite] --sink mixed: NOTE: no site got the PTWRITE sink; "
                  "this build is identical to --sink buffer")

    gt_skip = load_gt_skip(a.gt_skip) if a.gt_all else set()
    # Every skipped oracle observation is attributed to a reason, and
    # the reason decides whether the build may succeed (see --allow-gt-drop).
    gt_skip_reason = {addr: GT_SKIP_EXPLICIT for addr in gt_skip}
    if a.gt_all:
        # Never patch an instruction a direct branch enters in the
        # middle (glibc's `je 1f; lock; 1:' single-thread idiom).  The patch
        # jump would be entered at its second byte and the process dies.
        mid = mid_insn_branch_targets(a.image) - gt_skip
        if mid:
            gt_skip |= mid
            for addr in mid:
                gt_skip_reason[addr] = GT_SKIP_MID_INSN
            print("[rewrite] --gt-all: %d instruction(s) are entered in the "
                  "MIDDLE by a direct branch and cannot carry a patch "
                  "jump; their gt sequence is skipped" % len(mid))
    # A stale `<out>.unpatched' from an earlier run must not survive a run
    # that fails for a DIFFERENT reason: a caller's exclude-and-retry loop
    # would read it again and "exclude the same sites round after round".
    for stale in (a.output + ".unpatched", a.output + ".gtskip"):
        if os.path.exists(stale):
            os.remove(stale)
    if a.gt_all:
        write_gt_skip(a.output + ".gtskip", gt_skip, gt_skip_reason)
    write_site_file(site_file, groups, a.sink, a.space, a.sync,
                    delta_mode, slot_base, counter_base, n_slots, n_counters,
                    liveness=0 if a.dead_regs == "off" else 1,
                    gt=a.gt_all, gtoff=GT_OFF, gtstack=a.gt_stack,
                    count_base=a.count_base, count_atomic=a.count_atomic,
                    nt_store=a.nt_store, fixed_slot=a.fixed_slot, kf_gs=a.kf_base if a.kf_gs else None,
                    reserve=a.reserve, sync_cursor=a.sync_cursor, cursor_store=a.cursor_store,
                    sync_carrier=a.sync_carrier,
                    gt_skip=gt_skip)
    if a.gt_all:
        print("[rewrite] --gt-all: every memory-accessing instruction is "
              "patched as well; the run needs PTLOG_GT=1 (and PTLOG_GT_DIR) in "
              "its environment, and writes gt.<pid>.<tid>.bin next to the cv "
              "files")

    argv = [os.path.join(E9PATCH_DIR, "e9tool")]
    if a.opt:
        argv += ["-O" + a.opt]
    if a.full_coverage:
        argv += ["-100"]
    argv += list(a.e9tool_arg)
    ld_so_fix_base = 0
    ld_so_rt_base = 0
    ld_so_loader_base = 0
    if a.ld_so:
        # ---- Rewriting the dynamic loader --------------------
        # The kernel maps ld.so at the very top of the user address space and
        # E9Patch puts its loader at image_base + 0x20e9e9000 (8.2 GB up), which
        # is past the 47-bit limit -- "mmap() scratch failed (errno=22)".  Even
        # a modest positive loader base does not help: the kernel reserves the
        # WHOLE span top-down, so the loader segment always ends up at the top
        # of the mmap area, and the kernel then places the 8-page
        # [vvar][vvar_vclock][vdso] block immediately below it -- exactly on the
        # page the E9Patch loader wants for its scratch (`loader_base - 4096`),
        # and splitting the vdso VMA is EINVAL.
        #
        # So: put the loader ONE page above the image (the hole below it is then
        # a single page -- too small for the vdso block, which therefore goes
        # below the image) and put the trampolines BELOW the image base, where
        # E9Patch is happy to place them and where there is 1 MB of headroom
        # left for the vdso.  The site map then holds negative link-time
        # offsets, which is fine: the reconstructor adds the load base.
        # The injected AT_ENTRY shim (--fix-at-entry, added below) has to go in
        # the same window: E9Patch resolves its `init' symbol through an
        # address whose SIGN it uses as a marker, so an ELF injected at a
        # negative base silently loses its init.  It therefore sits directly
        # above the image, with the loader one page above IT.
        lb = (elf.end + PAGE - 1) & ~(PAGE - 1)
        fix_span = (Elf(a.ldfix).end + PAGE - 1) & ~(PAGE - 1) \
            if os.path.exists(a.ldfix) else 0
        ld_so_fix_base = lb
        # A --gt-all (or buffer/count) build also injects the runtime ELF, which
        # the plugin maps at 0x70000000 by default.  e9patch requires
        # `--loader-base' to be at least the END of every mapping it emits, so
        # with the loader parked one page above a 1.4 MB ld.so that default is
        # fatal ("loader base address ... must not exceed maximum mapping
        # address").  Put the runtime directly above the ldfix ELF instead and
        # raise the loader above it; the hole below the loader stays one page,
        # which is what keeps the trampolines below the image base.
        rt_span = 0
        if (a.sink in ("buffer", "mixed", "count") or a.gt_all) and a.rt:
            _rt = a.rt
            if _rt == RT_E9 and not Elf(a.image).has_fini:
                _rt = RT_E9_NOFINI
            if os.path.exists(_rt):
                rt_span = (Elf(_rt).end + PAGE - 1) & ~(PAGE - 1)
                ld_so_rt_base = lb + fix_span
        ld_so_loader_base = lb + fix_span + rt_span + PAGE
        argv += ["--option", "--loader-base=%#x" % ld_so_loader_base]
        argv += ["--option", "--mem-lb=%#x" % LD_SO_MEM_LB]
        argv += ["--option", "--mem-ub=%#x" % LD_SO_MEM_UB]
    for opt in a.e9_option:
        if not opt.startswith("-"):
            opt = "--" + opt
        argv += ["--option", opt]
    if not a.no_mem_lb and not a.ld_so:
        # E9Patch is free to map trampolines at NEGATIVE offsets from the image
        # base (observed: -0x16fff000).  Bounding it from below by the end of
        # the image keeps every trampoline address a plain unsigned link-time
        # vaddr, which is what the site map promises.
        lb = a.mem_lb if a.mem_lb is not None else \
            hex((data_base + data_bytes + 0x200000) & ~0x1fffff)
        argv += ["--option", "--mem-lb=%s" % lb]
    if a.shared:
        argv += ["--shared"]
    argv += ["--plugin=%s:--sites=%s" % (a.plugin, site_file),
             "--plugin=%s:--sitemap=%s" % (a.plugin, csv_file)]
    if (a.sink in ("buffer", "mixed", "count") or a.gt_all) and a.rt:
        rt = a.rt
        # An image with no DT_FINI/DT_FINI_ARRAY (libc.so.6 has neither) cannot
        # be given a runtime that exports `fini' -- E9Patch has nothing to hook.
        # The no-fini flavour is safe because rt/ptlogrt.c drives its teardown
        # off a shared control block, so any OTHER image's fini() flushes the
        # buffers.
        if rt == RT_E9 and not Elf(a.image).has_fini:
            rt = RT_E9_NOFINI
            if not os.path.exists(rt):
                raise SystemExit(
                    "[rewrite] %s has no DT_FINI/DT_FINI_ARRAY, so it needs the "
                    "no-fini runtime %s, which is missing.  Build it with:\n"
                    "  cd $E9PATCH_DIR && NO_SIMD_CHECK=1 "
                    "./e9compile.sh %s -DPTLOG_E9RT -DPTLOG_NO_FINI && cp "
                    "ptlogrt %s" % (a.image, rt,
                                    os.path.join(HERE, "rt", "ptlogrt.c"), rt))
            print("[rewrite] %s has no DT_FINI: using the no-fini runtime %s"
                  % (os.path.basename(a.image), os.path.basename(rt)))
        argv += ["--plugin=%s:--rt=%s" % (a.plugin, rt)]
    if a.fix_at_entry or a.ld_so:
        if not os.path.exists(a.ldfix):
            raise SystemExit("[rewrite] --fix-at-entry needs %s (make -C "
                             "runtime/e9plugin)" % a.ldfix)
        argv += ["--plugin=%s:--ldfix=%s" % (a.plugin, a.ldfix)]
        if a.ld_so:
            argv += ["--plugin=%s:--ldfix-base=%#x" % (a.plugin, ld_so_fix_base)]
            if ld_so_rt_base:
                argv += ["--plugin=%s:--rt-base=%#x"
                         % (a.plugin, ld_so_rt_base)]
    argv += ["-M", 'plugin("%s").match()' % a.plugin,
             "-P", 'replace plugin("%s").patch()' % a.plugin,
             a.image, "-o", a.output]

    print("[rewrite] %d instrumented instructions, %d logged values, sink=%s"
          % (len(groups), sum(len(v) for v in groups.values()), a.sink))
    if a.verbose:
        print("[rewrite] " + " ".join("'%s'" % t if " " in t else t for t in argv))
    attempt = 0
    cb_attempt = 0
    while True:
      r = subprocess.run(argv, capture_output=True, text=True)
      sys.stderr.write(r.stderr)
      # The plugin reports how much of the liveness information it could use;
      # keep it for the site map.
      global PLUGIN_STATS, GT_STATS
      mg = re.search(r"gt: (\d+) ground-truth site\(s\) \((\d+) explicit "
                     r"memory operand, (\d+) implicit stack operand\), (\d+) "
                     r"memory-accessing instruction\(s\) refused", r.stderr)
      if mg:
          GT_STATS = dict(sites=int(mg.group(1)), mem=int(mg.group(2)),
                          stack=int(mg.group(3)), refused=int(mg.group(4)))
      m = re.search(r"liveness=(\d+), push/pop-free sites (\d+)/(\d+) "
                    r"\([\d.]+ %\), scratch registers (\d+) dead / (\d+) pushed",
                    r.stderr)
      if m:
          PLUGIN_STATS = dict(liveness=bool(int(m.group(1))),
                              sites_pushpop_free=int(m.group(2)),
                              sites_total=int(m.group(3)),
                              scratch_dead=int(m.group(4)),
                              scratch_pushed=int(m.group(5)))
      if r.returncode != 0:
          print(r.stdout[-2000:])
          raise SystemExit("[rewrite] FAILED: e9tool exited %d" % r.returncode)

      m = re.search(r"num_patched\s+=\s*(\d+)\s*/\s*(\d+)", r.stdout)
      if m is None:
          print(r.stdout[-2000:])
          raise SystemExit("[rewrite] FAILED: no num_patched in e9tool output")
      got, want = int(m.group(1)), int(m.group(2))
      print("[rewrite] num_patched = %d / %d" % (got, want))
      if a.gt_all:
          # A --gt-all build matches far more instructions than the spec names
          # (every memory-accessing one), and E9Patch has no tactic for some of
          # them.  That is a MEASURED quantity here, not an error: only the
          # spec's own sites have to be matched and patched, because only they
          # feed the reconstruction.  `unpatched_addresses' answers for those.
          nm, np_ = unpatched_addresses(a.output, groups, csv_file)
          print("[rewrite] --gt-all: e9tool matched %d instructions (%d spec "
                "sites), E9Patch patched %d (%.2f %%); %d spec site(s) not "
                "matched, %d not patched"
                % (want, len(groups), got, 100.0 * got / max(1, want),
                   len(nm), len(np_)))
          if np_ and attempt < a.gt_retry:
              # Drop the gt sequence at the neighbours that block the site
              # (see gt_skip_candidates) and run e9tool again. Critical-value
              # sequences are untouched, including with critical-oracle retry.
              attempt += 1
              new = gt_skip_candidates(a.image, np_, attempt, csv_file,
                                       groups, a.gt_retry_critical_oracles) - gt_skip
              if new and not a.allow_gt_drop:
                  # A silent drop would leave an oracle hole nothing downstream
                  # reports, and every access at the dropped addresses would
                  # then be charged as an error by the conservative bound.
                  # Refuse by default: the
                  # blocking SPEC sites go to <out>.unpatched, which is exactly
                  # what analyze.py --avoid / matched_converge.py consume, so
                  # the caller re-places the critical value instead of losing
                  # the oracle.  --allow-gt-drop opts back in, loudly.
                  print("[rewrite] --gt-retry %d/%d: %d spec site(s) not "
                        "patched; placing them would mean DROPPING the ground "
                        "truth at %d instruction(s) -- refusing.\n"
                        "           Every access at a dropped address becomes "
                        "an EXCLUDED, unverified access that a conservative "
                        "bound charges as an error.\n"
                        "           Re-place the critical value instead: "
                        "static/analyze.py --avoid %s (matched_converge.py "
                        "does this automatically), or pass --allow-gt-drop to "
                        "accept the oracle gap, which is then counted and "
                        "reported in the site map."
                        % (attempt, a.gt_retry, len(np_), len(new),
                           a.output + ".unpatched"))
                  GT_DROP_REFUSED[:] = sorted(new)
              elif new:
                  gt_skip |= new
                  for addr in new:
                      gt_skip_reason[addr] = GT_SKIP_RETRY
                  write_gt_skip(a.output + ".gtskip", gt_skip, gt_skip_reason)
                  print("[rewrite] --gt-retry %d/%d: %d spec site(s) not "
                        "patched; dropping the gt sequence at %d neighbouring "
                        "instruction(s), never CVs (%d in the skip list), and "
                        "re-running e9tool"
                        % (attempt, a.gt_retry, len(np_), len(new),
                           len(gt_skip)))
                  write_site_file(site_file, groups, a.sink, a.space, a.sync,
                                  delta_mode, slot_base, counter_base,
                                  n_slots, n_counters,
                                  liveness=0 if a.dead_regs == "off" else 1,
                                  gt=a.gt_all, gtoff=GT_OFF,
                                  gtstack=a.gt_stack,
                                  count_base=a.count_base,
                                  count_atomic=a.count_atomic,
                                  nt_store=a.nt_store,
                                  fixed_slot=a.fixed_slot,
                                  kf_gs=a.kf_base if a.kf_gs else None,
                                  reserve=a.reserve, sync_cursor=a.sync_cursor, cursor_store=a.cursor_store,
                    sync_carrier=a.sync_carrier,
                                  gt_skip=gt_skip)
                  continue
              if not new:
                  print("[rewrite] --gt-retry %d/%d: no further gt-only neighbour "
                        "to skip for the %d unpatched site(s)"
                        % (attempt, a.gt_retry, len(np_)))
          if nm or np_:
              path = write_unpatched(a.output + ".unpatched", nm, np_)
              msg = ("[rewrite] FAILED: --gt-all: %d spec site(s) not matched, "
                     "%d not patched -> %s" % (len(nm), len(np_), path))
              if GT_DROP_REFUSED:
                  msg += ("\n           %d of them could only be placed by "
                          "dropping the ground truth at %d instruction(s); "
                          "that was REFUSED.  Re-place with --avoid, "
                          "or re-run with --allow-gt-drop."
                          % (len(np_), len(GT_DROP_REFUSED)))
              if a.keep_going:
                  print(msg)
              else:
                  raise SystemExit(msg)
      elif want != len(groups) or got != want:
          nm, np_ = unpatched_addresses(a.output, groups, csv_file)
          _blk = a.count_blocks or a.count_blocks_only
          _log = a.log_blocks or a.log_blocks_only
          if (_blk or _log) and cb_attempt < a.count_blocks_retry:
              cb_attempt += 1
              n_mv, n_cov, n_unp = move_block_counters(a.image, groups, np_, nm,
                                                       logging=_log)
              if n_mv or n_cov or n_unp:
                  print("[rewrite] --%s retry %d/%d: %d block site(s) "
                        "could not be placed at the leader -> %d "
                        "moved to the next instruction of the block, %d "
                        "already counted by another address of the block, %d "
                        "block(s) left un%s; re-running e9tool"
                        % ("log-blocks" if _log else "count-blocks",
                           cb_attempt, a.count_blocks_retry,
                           n_mv + n_cov + n_unp, n_mv, n_cov, n_unp,
                           "logged" if _log else "profiled"))
                  if n_mv or n_cov:
                      write_site_file(site_file, groups, a.sink, a.space,
                                      a.sync, delta_mode, slot_base,
                                      counter_base, n_slots, n_counters,
                                      liveness=0 if a.dead_regs == "off" else 1,
                                      gt=a.gt_all, gtoff=GT_OFF,
                                      gtstack=a.gt_stack,
                                      count_base=a.count_base,
                                      count_atomic=a.count_atomic,
                                      nt_store=a.nt_store,
                                      fixed_slot=a.fixed_slot,
                                      kf_gs=a.kf_base if a.kf_gs else None,
                                      reserve=a.reserve, sync_cursor=a.sync_cursor, cursor_store=a.cursor_store,
                    sync_carrier=a.sync_carrier,
                                      gt_skip=gt_skip)
                      continue
          if a.sink == "count":
              COUNT_UNPATCHED[:] = sorted(set(nm) | set(np_))
          path = write_unpatched(a.output + ".unpatched", nm, np_)
          print("[rewrite] %d site(s) not matched by e9tool, %d not patched by "
                "E9Patch -- written to %s" % (len(nm), len(np_), path))
          for label, lst in (("not matched", nm), ("not patched", np_)):
              for addr in lst[:20]:
                  print("   0x%x  %s" % (addr, label))
              if len(lst) > 20:
                  print("   ... +%d more %s (see %s)"
                        % (len(lst) - 20, label, path))
          msg = ("[rewrite] FAILED: e9tool matched %d instructions but the spec "
                 "lists %d" % (want, len(groups))) if want != len(groups) else \
                ("[rewrite] FAILED: only %d/%d sites were patched" % (got, want))
          msg += ("\n           NOT PATCHED sites: E9Patch had no tactic for "
                  "them; try --e9tool-arg=-O0, or exclude them.  (--full-coverage "
                  "= e9tool -100 would patch most of them with the B0 tactic, but "
                  "B0 is UNSAFE for a target that blocks signals.)"
                  "\n           NOT MATCHED sites: e9tool's disassembler does "
                  "not see an instruction start there and no tactic can change "
                  "that -- the spec has to move."
                  "\n           Either way: re-run static/analyze.py --avoid %s "
                  "(or pass --exclude-ranges)." % path)
          if a.keep_going:
              print(msg)
              print("[rewrite] --keep-going: continuing with the sites that WERE "
                    "patched (the site map will be short by that many)")
          else:
              raise SystemExit(msg)
      break
    if gt_skip:
        write_gt_skip(a.output + ".gtskip", gt_skip, gt_skip_reason)
        by = gt_skip_by_reason(gt_skip_reason, gt_skip)
        print("[rewrite] --gt-all: %d ORACLE OBSERVATION(S) SKIPPED (no ground "
              "truth there; every access at them is excluded and a "
              "conservative bound charges it as an error) -> %s"
              % (len(gt_skip), a.output + ".gtskip"))
        print("[rewrite] --gt-all: gt_skip by reason: %d explicit (--gt-skip), "
              "%d mid-insn branch target (unavoidable), %d DROPPED BY "
              "--gt-retry%s"
              % (len(by[GT_SKIP_EXPLICIT]), len(by[GT_SKIP_MID_INSN]),
                 len(by[GT_SKIP_RETRY]),
                 " (--allow-gt-drop)" if by[GT_SKIP_RETRY] else ""))
    os.chmod(a.output, 0o755)

    if a.ld_so and ld_so_loader_base:
        grew = close_ld_so_gap(a.output, ld_so_loader_base)
        print("[rewrite] --ld-so: grew the last PT_LOAD by %d page(s) up to the "
              "loader base %#x, so the kernel cannot place the vdso block in "
              "the gap the injected ELFs occupy only at run time"
              % (grew // PAGE, ld_so_loader_base))

    if a.under_ld_so:
        with open(a.output, "r+b") as f:
            data = f.read()
            off = data.find(E9_MAGIC)
            if off < 0:
                raise SystemExit("[rewrite] --under-ld-so: no E9PATCH config in "
                                 "%s" % a.output)
            flags = struct.unpack_from("<I", data, off + 24)[0]
            f.seek(off + 24)
            f.write(struct.pack("<I", flags & ~0x1))       # E9_FLAG_EXE
        print("[rewrite] --under-ld-so: cleared E9_FLAG_EXE; the loader will "
              "find its own file through /proc/self/map_files/")

    # ---- B0 / SIGILL trap audit --------------------------
    # E9Patch's B0 tactic replaces the instruction with the illegal opcode 0x27
    # and recovers in the SIGILL handler its loader installs.  Linux delivers a
    # synchronous fault through force_sig_info_to_task(), which resets the
    # disposition to SIG_DFL as soon as the signal is BLOCKED in the faulting
    # thread -- and glibc blocks every signal across pthread_create().  So a B0
    # site is a latent SIGILL for any multithreaded target.  The count is
    # reported here (and recorded in the site map) whatever produced it, because
    # --e9tool-arg=-100 can turn B0 on behind --full-coverage's back.
    n_b0 = 0
    try:
        n_b0 = len(VirtualImage(a.output).traps())
    except Exception as e:
        print("[rewrite] (could not audit %s for B0 traps: %s)" % (a.output, e))
    if n_b0:
        print("[rewrite] *** WARNING: %d B0 (illegal-instruction) trap site(s) "
              "in %s.\n"
              "           The E9Patch loader recovers from them in a SIGILL "
              "handler, but Linux resets SIGILL to SIG_DFL whenever the signal "
              "is BLOCKED in the faulting thread, and glibc blocks all signals "
              "across pthread_create().  This binary WILL die with SIGILL, "
              "intermittently, on a multithreaded target.\n"
              "           Drop --full-coverage (and any -100) and let "
              "static/analyze.py --avoid move the sites instead."
              % (n_b0, a.output))

    env_file = a.output + ".ptlog.env"
    with open(env_file, "w") as f:
        f.write("PTLOG_SYNC=%d\n" % a.sync)
        f.write("PTLOG_SINK=%s\n" % a.sink)
        if a.sink == "count":
            # The count runtime is armed by PTLOG_COUNT; the run must also give
            # it PTLOG_COUNT_N (>= max global index+1 across ALL images) and a
            # PTLOG_COUNT_DIR for count.<pid>.csv.
            f.write("PTLOG_COUNT=1\n")
            f.write("PTLOG_COUNT_BASE=%d\n" % a.count_base)
    if (a.sink in ("buffer", "mixed", "count") or a.gt_all) and not a.rt:
        print("[rewrite] NOTE: sink=%s without --rt; run with "
              "LD_PRELOAD=%s" % (a.sink,
                                       os.path.join(HERE, "rt", "ptlogrt.so")))

    if not a.no_sitemap:
        site_kind = {int(x.get("id", -1)): x["kind"] for x in spec["sites"]}
        sm, problems = build_sitemap(a.output, a.image, groups, a.sink,
                                     csv_file,
                                     a.sync if a.sink in ("buffer", "mixed") else 0,
                                     counter_gs=bool(a.kf_gs),
                                     site_kind=site_kind,
                                     allow_negative=a.ld_so or a.no_mem_lb,
                                     gt=a.gt_all)
        # A non-zero count means the image is not safe to run
        # multithreaded.  Recorded so a consumer (or a later audit) can tell.
        sm["b0_traps"] = n_b0
        sm["sync_mode"] = "cursor" if a.sync_cursor else "countdown"
        sm["cursor_update"] = "store" if a.cursor_store or a.sync_cursor else "default"
        if a.gt_all:
            sm["gt_skip"] = sorted(gt_skip)
            # A consumer must be able to tell an unavoidable mid-insn
            # skip from a retry that gave the oracle up to place a spec site.
            by = gt_skip_by_reason(gt_skip_reason, gt_skip)
            sm["gt_skip_by_reason"] = by
            sm["gt_skip_counts"] = {k: len(v) for k, v in by.items()}
            sm["gt_skip_dropped_by_retry"] = len(by[GT_SKIP_RETRY])
            sm["allow_gt_drop"] = bool(a.allow_gt_drop)
        if a.gt_all and GT_STATS:
            # Static coverage of the ground truth, for the accuracy report:
            # how many memory-accessing instructions the emitter accepted, how
            # many it refused (a form it cannot re-encode as one record) and
            # how many E9Patch then failed to patch.
            sm["gt_static"] = dict(GT_STATS)
            sm["gt_static"]["e9_matched"] = want
            sm["gt_static"]["e9_patched"] = got
        n_want = sum(len(v) for v in groups.values())
        # `gt' entries are not critical values; the completeness check is
        # about the values the reconstruction consumes.  A COUNT build consumes
        # nothing (it logs only per-address counters), so it has no cv entries
        # and the completeness check does not apply -- its own check is that
        # every patched address got a counter (below).
        if a.sink == "count":
            missing = 0
            n_addr = len(groups)
            n_cnt = len(sm.get("count", []))
            if n_cnt != n_addr:
                print("[rewrite] --sink count: WARNING: %d of %d patched "
                      "addresses have no counter row" % (n_addr - n_cnt, n_addr))
        else:
            missing = (expected_values(groups, spec)
                       - sum(1 for e in sm["entries"] if e.get("role") != "gt"))
        if problems:
            # Every problem is a logged value the reconstructor cannot
            # interpret, i.e. a silent accuracy loss -- so the FULL list goes to
            # a file (a large image can have hundreds of them).
            ppath = a.output + ".sitemap.problems"
            counts = {}
            with open(ppath, "w") as f:
                f.write("# %d site-map problems from %s\n"
                        % (len(problems), a.output))
                f.write("# addr\tproblem\n")
                for addr, why in problems:
                    f.write("0x%x\t%s\n" % (addr, why))
                    key = re.sub(r"0x[0-9a-f]+", "0x...", why)
                    counts[key] = counts.get(key, 0) + 1
            print("[rewrite] %d site-map problems -> %s"
                  % (len(problems), ppath))
            for why, n in sorted(counts.items(), key=lambda kv: -kv[1]):
                print("   %6d x %s" % (n, why))
        # A site map that is SHORT is not a warning.
        # `offline/ptrecon` consumes the buffer sink's value file positionally, so
        # one omitted site shifts every value after it and the reconstruction is
        # silently wrong with ZERO unknown addresses -- the worst failure mode
        # there is.  In the PTWRITE sink the effect is the same: the payloads
        # of an undescribed logging instruction queue up and are attached to the
        # WRONG site.  So it is FATAL unless --keep-going says otherwise.
        if problems or missing:
            msg = ("[rewrite] FAILED: the site map is incomplete -- %d problem(s), "
                   "%d logged value(s) missing of %d the spec asks for.\n"
                   "           A short site map makes the reconstruction "
                   "SILENTLY WRONG (values are consumed positionally), so this "
                   "is an error, not a warning.\n"
                   "           See %s.sitemap.problems; --keep-going writes the "
                   "map anyway."
                   % (len(problems), missing, n_want, a.output))
            if not a.keep_going:
                raise SystemExit(msg)
            print(msg)
            print("[rewrite] --keep-going: writing the incomplete site map anyway")
        path = a.output + ".sitemap.json"
        with open(path, "w") as f:
            json.dump(sm, f, indent=1)
        print("[rewrite] site map: %s (%d logged values, %d relocated "
              "instructions, %d sync markers)"
              % (path, len(sm["entries"]), len(sm["relocated"]),
                 len(sm["sync_markers"])))

    # ---- CONTROL-FLOW LOG bookkeeping ------------
    if a.log_blocks or a.log_blocks_only:
        if COUNT_UNPROFILED or BLOCK_MOVE_LOST[0]:
            print("[rewrite] --log-blocks: %d block(s) left WITHOUT an "
                  "identifier (E9Patch could patch no instruction of the "
                  "block), %d spec value(s) dropped from a moved leader"
                  % (len(COUNT_UNPROFILED), BLOCK_MOVE_LOST[0]))
        if a.log_blocks_map:
            blocks = [dict(id=b, leader=COUNT_MOVED.get(A, A), site_addr=A,
                           moved=(A in COUNT_MOVED))
                      for A, b in sorted(BLOCK_LOG.items(), key=lambda kv: kv[1])]
            with open(a.log_blocks_map, "w") as f:
                json.dump(dict(image=os.path.abspath(a.image),
                               output=os.path.abspath(a.output),
                               n_blocks=len(blocks),
                               unlogged=sorted(COUNT_UNPROFILED),
                               blocks=blocks), f, indent=1)
            print("[rewrite] --log-blocks-map: %d block(s) -> %s"
                  % (len(blocks), a.log_blocks_map))

    if not a.keep_temps:
        for p in (csv_file,):
            try:
                os.unlink(p)
            except OSError:
                pass
    print("[rewrite] OK -> %s" % a.output)


if __name__ == "__main__":
    main()
