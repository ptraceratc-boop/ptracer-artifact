#!/usr/bin/env python3
"""PTracer Stage 2 -- rewrite an ELF image so that it logs its critical values.

Input : a spec v2 JSON (SPEC_FORMAT section 1) and the ELF it describes.
Output: the rewritten ELF + the site map JSON (SPEC_FORMAT section 2).

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

HERE        = os.path.dirname(os.path.realpath(__file__))
# The E9Patch checkout (e9tool, e9patch, e9compile.sh): <root>/third_party/e9patch
# by default, relative to this file's location; E9PATCH_DIR overrides it.
E9PATCH_DIR = os.environ.get(
    "E9PATCH_DIR",
    os.path.realpath(os.path.join(HERE, "..", "..", "third_party", "e9patch")))
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

    # An address -> region index.  `read' used to scan `self.regions' linearly,
    # which is fine for a spec-sized rewrite (a few hundred regions) and
    # quadratic for a `--gt-all' one: E9Patch gives every trampoline group its
    # own map, so python3.12 goes from ~2 000 regions to ~46 000, and
    # `build_sitemap' does ~20 reads per trampoline for 226 000 trampolines.
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
        boundary is valid at run time even though no single mapping holds all
        of it; the read therefore continues into the adjacent mapping until it
        has `n` bytes or hits a hole.
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
    of pushing them.  `noguard` marks a value that must never be wrapped in a
    guard whatever its flags say (a compile-time constant such as a block
    identifier); the site file carries it as `:N'.
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


def build_groups(spec, excludes, keyframe=None):
    """addr -> ordered list of `Op`s, following the SPEC_FORMAT ordering rule.

    A spec site with `"resync": true` is a RE-ANCHOR site at a loop back-edge
    header: its registers are logged unconditionally, but only every K-th
    execution, so a PT overflow or decoder resync inside a long-running loop
    costs at most K iterations of unknown addresses instead of the rest of the
    run.  `keyframe` overrides the spec's per-site K; `keyframe=0` drops the
    resync sites altogether, which is how the same spec measures with and
    without them.
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
        # Is EFLAGS dead at this site?  The keyframe guard's `dec' and the
        # buffer sink's cursor/countdown `add'/`sub' clobber the flags, so
        # only a site with dead flags gets those forms.
        dead = bool(s.get("flags_dead", False))
        # GP registers that are DEAD at this site, so a trampoline may use them
        # as scratch without push/pop.  Re-verified against the image's own
        # bytes by `verify_dead_regs'.
        dregs = regmask(s.get("dead_regs") or ())
        resync = bool(s.get("resync", False))
        kf = 0
        if resync:
            kf = int(s.get("keyframe", 0)) if keyframe is None else int(keyframe)
            n_resync += 1
            if kf == 0:
                dropped.append((addr, sid, "resync site, --keyframe 0"))
                continue
            # The keyframe counter's `dec' clobbers EFLAGS.  A resync site is a
            # pure accuracy bonus,
            # never a correctness requirement, and it sits at a LOOP HEADER --
            # logging it unconditionally would cost one PTWRITE per iteration of
            # the hottest loop in the program.  So a resync site whose flags are
            # live is dropped, not logged.
            if not dead:
                n_resync_live += 1
                dropped.append((addr, sid, "resync site with live EFLAGS"))
                continue
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
              "dropped because the SPEC calls their EFLAGS live; the rewriter "
              "re-checks the rest itself"
              % (n_resync, n_resync_live))
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
# The keyframe guard's `dec' and the buffer sink's `add'/`sub' clobber
# CF PF AF ZF SF OF, so they may only be emitted where those six are dead.  The
# analyzer says so per site (`flags_dead'), but the CFG it recovers can miss a
# successor, and a flag-writing instruction emitted where the very next
# instruction on the fall-through path reads a flag corrupts the program
# silently.  So the rewriter checks the claim itself, from the bytes it
# already has: a bounded exploration of the local CFG from the point where the
# instruction would sit, following both arms of a conditional branch, that
# answers "may one of the six be READ before it is rewritten?".  Anything it
# cannot follow (an indirect jump, an address outside the image, too much
# code) counts as LIVE.  A site it rejects takes the flag-free forms, exactly
# as `flags_dead: false' does.

PLUGIN_STATS = {}        # the emitter's liveness report (see below)
GT_STATS = {}            # the emitter's --gt-all report (see below)
GT_DROP_REFUSED = []     # oracle probes a --gt-retry wanted to drop, refused
LD_SO_MEM_LB = -0x10000000
LD_SO_MEM_UB = -0x100000

_FLAGS6 = ("CF", "PF", "AF", "ZF", "SF", "OF")


# --------------------------------------------------------------------------
# Which GP registers may the trampoline clobber?
# --------------------------------------------------------------------------
#
# The analyzer publishes `dead_regs` per site, from a backward liveness pass on
# its recovered CFG.  Using a register that is NOT dead corrupts the program
# silently, and that CFG can miss a successor (see the EFLAGS check above).  So
# the rewriter re-derives the answer from the bytes it is about to patch and
# keeps only the INTERSECTION.
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
    """`live(addr)`: may one of the six arithmetic flags be read at `addr`
    before it is rewritten?"""

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
    #    calls the site live.  (Seen on `__strcasecmp_l_sse42+0x…`.)
    #  * The scalar FP compares `comiss/comisd/ucomiss/ucomisd` set ZF PF CF
    #    from the comparison and clear OF SF AF, i.e. they redefine all six.
    #    Capstone models the SSE forms but reports `eflags == 0` for every VEX
    #    form (`vcomisd`, ...), which makes the checker call the site live.
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
        `<out>.flags_live` distinguishes an analyzer defect from the rewriter's
        own conservatism (on an interpreter's computed-goto dispatch the latter
        is the usual reason).
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
    ops were downgraded.  The point where the flag-writing instruction sits is
    the site address for a `before` op and just past the instruction for an
    `after` one.
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


def drop_unconfirmed_resync(groups):
    """A resync op whose EFLAGS is not (any longer) confirmed dead cannot carry a
    keyframe counter: the guard's `dec' clobbers the flags.  The op is removed
    (a resync site is an accuracy
    bonus and it sits at a LOOP HEADER, where logging unconditionally costs one
    PTWRITE per iteration).  Returns the number of dropped ops.

    This runs AFTER `verify_flags_dead', which is what makes it more than a
    re-statement of `build_groups': the rewriter's own CFG check rejects sites
    the spec calls `flags_dead'.
    """
    n_drop = 0
    for addr in list(groups):
        keep = []
        for op in groups[addr]:
            if op.resync and op.kf and not op.dead:
                n_drop += 1
                continue
            keep.append(op)
        if keep:
            groups[addr] = keep
        else:
            del groups[addr]
    return n_drop


def count_counters(groups):
    """How many 8-byte keyframe countdown counters the plugin will hand out:
    one per RESYNC SITE (all of its registers share it, so they are logged in
    the same execution).  The plugin checks its own total against this number
    and fails loudly if the two ever disagree.
    """
    n = 0
    for addr in sorted(groups):
        seen = set()
        for op in groups[addr]:
            if op.kf and op.resync and (op.after, op.sid) not in seen:
                seen.add((op.after, op.sid))
                n += 1
    return n


def op_token(op):
    """One `<when>:<kind>:<arg>:<site>[:D|:L][:R<K>][:N][:S<mask>]` field."""
    t = "%s:%s:%s:%d:%s" % ("A" if op.after else "B", op.kind, op.arg, op.sid,
                            "D" if op.dead else "L")
    if op.resync and op.kf:
        t += ":R%d" % op.kf
    if op.noguard:
        t += ":N"
    if op.dregs:
        t += ":S%#x" % op.dregs
    return t


def write_site_file(path, groups, sink, space, sync, counterbase=0,
                    ncounters=0, gt=False, gtoff=GT_OFF, gtstack=True,
                    gt_skip=(), sync_carrier="ptwrite", call_sink=False):
    """The flat site file runtime/e9plugin/ptlog.cpp parses (grammar there)."""
    with open(path, "w") as f:
        f.write("# generated by runtime/rewrite.py -- do not edit\n")
        f.write("sink %s\n" % sink)
        f.write("space %d\n" % space)
        f.write("sync %d\n" % sync)
        if sync_carrier != "ptwrite":
            f.write("synccarrier %s\n" % sync_carrier)
        if call_sink:
            f.write("callsink 1\n")
        # `--dead-regs off' simply emits no `:S<mask>' fields; there is no
        # separate switch in the file.  The reserved data area holds only the
        # keyframe counters (no cache slots), so it starts at `counterbase'.
        f.write("slotbase 0x%x\n" % counterbase)
        f.write("counterbase 0x%x\n" % counterbase)
        f.write("nslots 0\n")
        f.write("ncounters %d\n" % ncounters)
        # GROUND-TRUTH ADDRESS LOGGING: the plugin then patches EVERY
        # memory-accessing instruction it can re-encode, not only the sites
        # listed below.
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
            "(pip install capstone, or run with the artifact's venv python)")
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
    it wrong shifts every logged value AFTER the original instruction.  It is
    therefore *located*, not guessed:

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
# as an ordinary instruction made the tail scanner run off the end of 60
# trampolines on python3.12 and report a bogus mismatch against whatever
# followed in memory.
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
    # A keyframe guard's JOIN LABEL can sit at body offset `before_len` exactly
    # -- the guarded body is the last thing the plugin emits before "$instr",
    # so jumping over it lands ON the relocated original instruction.  The
    # plugin's numbering does not count "$instr", so that offset is
    # indistinguishable from "the first byte of the body AFTER the original
    # instruction", and `shift` (which shifts everything at or past
    # `before_len`) puts the label one whole instruction too far.  The phase
    # disambiguates it: every offset of a `before` op is at or before
    # `before_len` and is never shifted, every offset of an `after` op is at or
    # past it and always is.
    def shiftp(off, after):
        return off + (reloc_size if after else 0)

    logs = []
    for off, _meta in layout["logs"]:
        after = (_meta.get("when") == "after")
        va = tramp + shiftp(off, after)
        ins = _decode_one(md, img.read(va, 16) or b"", va)
        kguard = None
        if _meta.get("kfbr_off") is not None:
            kguard = (tramp + shiftp(_meta["kfbr_off"], after),
                      tramp + shiftp(_meta["kfjoin_off"], after))
        logs.append((va, ("%s %s" % (ins.mnemonic, ins.op_str)).strip()
                     if ins else "?", kguard))
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
        if CFR is not None and nxt in CFR:      # --cfr: the next site's own
            break                               # (batch) body -- scanned there
        ob = orig_at(nxt)
        oi = _decode_one(md, ob, nxt) if ob else None
        if oi is None:
            problems.append("cannot decode the original instruction at %#x" % nxt)
            break
        if CFR is not None and _jmp_target(ins) is not None and \
                _mn(oi) != "jmp":
            break                               # --cfr: jump to a batch head
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


# ---- E9Patch -OCFR (tactic T0 "batches") ------------------------------------
# With `--cfr' e9tool runs E9Patch's control-flow recovery, and tactic T0 may
# patch a site I by planting the jump at an EARLIER instruction J (J..L are
# consecutive instructions none of which (after J) is a recovered jump target)
# and emitting ONE "batch" trampoline: for every K in J..L either K's own
# trampoline body (a patched site; its $BREAK becomes a fall-through unless K
# is L) or a relocated copy of K, then the usual break back to L.next.  The
# bytes of J+1..L in the original code are int3 (never executed).  So the body
# of a site inside a batch is not reached from the site's own address: it is
# found by walking the batch from its head.  Default off; without `--cfr'
# nothing below runs and the site map is byte-identical.
CFR = None              # set by main() when --cfr is in effect
CFR_ORIG = None         # --cfr: the input image (unpatched_addresses)
# --cfr: E9Patch's OWN record of every copy of an original instruction it wrote
# into a trampoline (third_party/e9patch, env E9PATCH_RELOCMAP).
# {tramp_addr: (orig_addr, tramp_len, ctx)} with ctx B (T0 batch
# member), I (a patch's own "$instr"), E ($BREAK epilogue copy), P (prologue),
# R (raw bytes); CFR_E9INSTR = {orig_addr: tramp_addr of its "$instr"}.  None =
# the e9patch in use does not write the file -> the CfrResolver heuristics.
CFR_E9MAP = None
CFR_E9INSTR = None


def _load_e9reloc(path):
    """Parse an E9PATCH_RELOCMAP file (see CFR_E9MAP); None if absent."""
    global CFR_E9MAP, CFR_E9INSTR
    if not path or not os.path.exists(path):
        CFR_E9MAP = CFR_E9INSTR = None
        return None
    m, ins = {}, {}
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                continue
            o, t, n, c = line.split()
            o, t, n = int(o, 16), int(t, 16), int(n)
            m[t] = (o, n, c)
            if c == "I":
                ins.setdefault(o, []).append(t)
    CFR_E9MAP = m
    # A "$instr" that occurs more than once is ambiguous as a body anchor.
    CFR_E9INSTR = {o: v[0] for o, v in ins.items() if len(v) == 1}
    return m
T0_LIMIT = 32           # e9patch.h


class CfrResolver:
    def __init__(self, img, md, orig, layout, traps, orig_window, orig_insn):
        import bisect
        self.bisect = bisect
        self.img, self.md, self.layout, self.traps = img, md, layout, traps
        self.orig_window, self.orig_insn = orig_window, orig_insn
        starts = []
        for vaddr, memsz, off, filesz, flags in orig.loads:
            if not flags & 1:
                continue
            code = orig.data[off:off + filesz]
            pos = 0
            while pos < filesz:
                last = pos
                for (a, sz, _m, _o) in md.disasm_lite(code[pos:], vaddr + pos):
                    starts.append(a)
                    last = a - vaddr + sz
                pos = last if last > pos else pos + 1
        self.starts = sorted(set(starts))
        self.batches = set()            # batch trampoline entry addresses
        self.exact = 0                  # bodies anchored on CFR_E9INSTR
        self.pre = {}                   # tramp_addr -> relocated entry

    def _same(self, ti, oi):
        return bytes(ti.bytes) == bytes(oi.bytes) or (
            _mn(ti) == _mn(oi) and (_mn(oi) in _XFER or _mn(oi).startswith("j")
                                    or "rip" in oi.op_str))

    def body(self, addr, ob):
        """Address of the trampoline body of site `addr` (None if not found)."""
        img, md = self.img, self.md
        L = self.layout.get(addr)
        # Exact: E9Patch says where it put this site's "$instr"; the body
        # starts `before' bytes earlier (the plugin's own layout).  Accepted
        # only if the body then scans cleanly.
        if CFR_E9INSTR is not None and addr in CFR_E9INSTR and L is not None \
                and L["before"] is not None and L["total"] is not None:
            t = CFR_E9INSTR[addr] - L["before"]
            _l, rel, _s, _t, pr = scan_trampoline(img, md, t, ob, addr, L)
            if rel is not None and not pr and rel[0] == CFR_E9INSTR[addr]:
                self.exact += 1
                return t
        t = follow_patch(img, md, addr, self.traps, ob)
        if t is not None and L is not None and L["before"] is not None:
            _l, rel, _s, _t, pr = scan_trampoline(img, md, t, ob, addr, L)
            if rel is not None and not pr:
                return t
        if L is None or L["before"] is None or L["total"] is None:
            return t
        i = self.bisect.bisect_left(self.starts, addr)
        if i >= len(self.starts) or self.starts[i] != addr:
            return t
        for k in range(i - 1, max(-1, i - 1 - T0_LIMIT), -1):
            J = self.starts[k]
            cands = []
            j0 = _decode_one(md, img.read(J, 16) or b"", J)
            if j0 is not None and _jmp_target(j0) is not None:
                cands.append(_jmp_target(j0))           # first hop only
            tf = follow_patch(img, md, J, self.traps, self.orig_insn(J))
            if tf is not None and tf not in cands:
                cands.append(tf)
            for tj in cands:
                r = self._walk(k, i, tj, addr, ob, L)
                if r is not None:
                    return r
        return t

    def _walk(self, k, i, tj, addr, ob, L):
        img, md = self.img, self.md
        if True:
            cur, pre, ok = tj, [], True
            for K in self.starts[k:i]:
                LK = self.layout.get(K)
                obK = self.orig_insn(K)
                if not obK:
                    ok = False
                    break
                if LK is not None and LK["before"] is not None \
                        and LK["total"] is not None:
                    _l, rel, _s, _t, pr = scan_trampoline(img, md, cur, obK, K, LK)
                    if rel is None or pr:
                        ok = False
                        break
                    cur += LK["total"] + rel[1]
                    continue
                oi = _decode_one(md, obK, K)
                ti = _decode_one(md, img.read(cur, 16) or b"", cur)
                if oi is None or ti is None:
                    ok = False
                    break
                if _mn(oi) in _CALLS and not self._same(ti, oi):
                    # E9Patch's call emulation (pushReturnAddress + jmp):
                    # ONE relocated entry spanning the whole sequence
                    span, c2 = cur, cur
                    for _n in range(8):
                        j = _decode_one(md, img.read(c2, 16) or b"", c2)
                        if j is None:
                            break
                        c2 += j.size
                        if _mn(j) in _UNCOND or _mn(j) in _CALLS:
                            break
                    pre.append(dict(tramp_addr=span, orig_addr=K, len=oi.size,
                                    tramp_len=c2 - span))
                    cur = c2
                    continue
                if not self._same(ti, oi):
                    ok = False
                    break
                pre.append(dict(tramp_addr=cur, orig_addr=K, len=oi.size,
                                tramp_len=ti.size))
                cur += ti.size
            if not ok:
                return None
            _l, rel, _s, _t, pr = scan_trampoline(img, md, cur, ob, addr, L)
            if rel is None or pr:
                return None
            self.batches.add(tj)
            for e in pre:
                self.pre[e["tramp_addr"]] = e
            return cur


def build_sitemap(out_path, orig_path, groups, sink, plugin_csv,
                  sync_period=0, site_kind=None, allow_negative=False,
                  gt=False):
    """Correlate the plugin's emission record with the rewritten binary.

    `site_kind` maps a spec site id to the site's kind, so the site map records
    the SPEC's kind, as SPEC_FORMAT section 2 requires, instead of guessing it
    from the logging instruction's position (a `reg` site with `when: "after"`
    -- a pseudo-load at a call return site -- is not a `load`)."""
    site_kind = site_kind or {}
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
    # VALUE plus the layout pseudo-rows I (-1) / E (-2) / S (-3) / P (-4) /
    # Q (-5).
    layout = {}
    if plugin_csv and os.path.exists(plugin_csv):
        with open(plugin_csv) as f:
            for line in f:
                if line.startswith("#"):
                    continue
                parts = line.strip().split(",")
                a, sid, when, kind, arg, off = parts[:6]
                # keyed extra: `k=counter:period:jnz:join' (keyframe guard)
                extra = {}
                for x in parts[6:]:
                    if len(x) > 2 and x[1] == "=":
                        extra[x[0]] = x[2:].split(":")
                addr, sid, off = int(a, 16), int(sid), int(off)
                L = layout.setdefault(addr, dict(before=None, total=None,
                                                 logs=[], syncs=[],
                                                 pre=[], post=[]))
                if when == "I":
                    L["before"] = off
                elif when == "E":
                    L["total"] = off
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
                             counter=None, keyframe=None, kfbr_off=None,
                             kfjoin_off=None)
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
    # whose trampoline cannot be scanned below: the ground-truth comparison
    # needs the complete set to tell "this instruction is excluded from the
    # ground truth" from "the two streams have lost each other".
    gt_addrs = sorted(a for a, L in layout.items()
                      if any(m.get("isgt") for _o, m in L["logs"])) if gt else []
    gt_problems = []
    gt_unpatched = set()
    n_gt_unpatched = 0
    global CFR
    cfr = None
    if CFR is not None:
        CFR = set(layout)
        cfr = CfrResolver(img, md, orig, layout, traps, orig_window, orig_insn)
    for addr in all_addrs:
        cv_site = addr in groups
        probs_out = problems if cv_site else gt_problems
        ob = orig_insn(addr)
        tramp = cfr.body(addr, ob) if cfr is not None else \
            follow_patch(img, md, addr, traps, ob)
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
        for (_off, meta), (va, insn, kguard) in zip(L["logs"], logs):
            # GROUND TRUTH: this logging instruction stores { effective
            # address, original ip } into the gt ring.  It is NOT a critical
            # value: `offline/ptrecon' must treat it as instrumentation (no
            # value consumed, no trace record); only the ground-truth
            # comparison reads it.
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
                # CONTROL-FLOW LOG (`--log-blocks').  It occupies one 8-byte
                # slot of the cv stream like any other value, but it is not a
                # critical value: its `role' says so, so a consumer that only
                # wants critical values skips it and a consumer that follows
                # control flow from the stream reads it.
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
            # SPEC_FORMAT section 2: the map's top-level `sink' answers for
            # every entry (a per-entry `sink' key is only defined for a build
            # that mixes sinks, which this rewriter does not produce).
            if meta["ismem"]:
                ent["size"] = meta["size"]
                # `ptwrite m32' is emitted for 4-byte memory operands, so the
                # PTW payload is 4 bytes wide there; the buffer sink always
                # stores a zero-extended 64-bit slot.
                ent["payload_bits"] = (32 if (sink == "ptwrite" and
                                              meta["size"] == 4) else 64)
            else:
                ent["reg"] = meta["reg"]
                if meta["half"]:
                    ent["half"] = meta["half"]
                ent["payload_bits"] = 64
            ent["insn"] = insn
            if kguard is not None:
                # SPEC_FORMAT section 2: the keyframe counter guard.  Seeing
                # `kf_join_addr' as the instruction after `kf_branch_addr' means
                # this execution was NOT a keyframe and nothing was logged.
                ent["keyframe"] = meta["keyframe"]
                ent["counter"] = meta["counter"]      # link-time address
                ent["kf_branch_addr"], ent["kf_join_addr"] = kguard
                ent["resync"] = (kind == "reg")
            entries.append(ent)

    # ---- instructions E9Patch displaced on its OWN account ------------------
    # To squeeze a jump into a short instruction E9Patch may "evict" a
    # neighbouring/overlapping instruction into a trampoline of its own (the
    # T2/T3 tactics of the E9Patch paper): e.g. a 2-byte `jmp rel8' punned over
    # `push %rbp' can land in the MIDDLE of an instruction 0x52 bytes later,
    # which then has to be moved out of the way.  Those copies execute and
    # appear as IPs in the PT stream exactly like the ones above, so the
    # reconstructor needs them too -- without them their accesses simply
    # vanish from the trace (`atax_small': 14 384 records, 12.5 % inaccuracy).
    # They are found by looking for a `jmp rel32' into a trampoline region at an
    # address whose bytes the rewriter changed, and confirming that the
    # trampoline starts with the original instruction from that address.
    known_tramps = set(t for t in tramps_used if t is not None)
    if cfr is not None:
        known_tramps |= cfr.batches
        seen = set(e["tramp_addr"] for e in relocated)
        relocated.extend(e for ta, e in sorted(cfr.pre.items()) if ta not in seen)
        print("[rewrite] --cfr: %d T0 batch trampoline(s), %d batched "
              "instruction(s) before a site mapped" % (len(cfr.batches), len(cfr.pre)))

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
        # The candidate instruction does NOT always start at the `e9' byte:
        # E9Patch also plants the REX-punned `48 e9 <rel32>' patch jump, and
        # then the `e9' is one byte INSIDE the jump.  Taking it as the
        # instruction start puts `a' inside the ORIGINAL instruction, the byte
        # confirmation below fails, and the evicted instruction never reaches
        # the `relocated' list -- after which `ptrecon' skips its trampoline as
        # pure instrumentation and silently DROPS its effect.  So each `e9' is
        # tried as the last byte of up to four candidate instruction starts;
        # `back = 0' is tried FIRST and a prefixed candidate is only reached
        # when the bare one fails.
        #
        # The candidate must also be an ORIGINAL INSTRUCTION START.  An `e9'
        # inside the rel32 of a real patch jump decodes as a jump into the
        # trampoline region often enough, and the original byte there may be a
        # 1-byte instruction that matches the first byte of SOME unrelated
        # evictee block, so the byte confirmation below passes and the scan
        # either reports a bogus mismatch (a fatal map problem) or maps somebody
        # else's evictee block onto a data byte.  A linear sweep from the
        # nearest known instruction start (a spec or gt site, which e9tool
        # decoded) within 64 bytes settles it; with no start in range the
        # permissive behaviour stands.
        import bisect
        known_starts = sorted(set(groups) | (set(layout) if gt else set()))

        def boundary(a):
            """Tri-state: True = `a' is PROVEN an original instruction start,
            False = proven NOT one, None = cannot be decided (no decoded start
            within 64 bytes below `a', or the sweep hit an undecodable byte).

            The three states matter for the `call' branch below: its only check
            is E9Patch's relocated-call SHAPE, so an unproven candidate that is
            really a mid-instruction byte (the second byte of a retargeted
            call's rel32, say) must not be reported as a site-map problem."""
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
                # never with a `call', and the byte confirmation below would
                # reject it.  The block would then stay undescribed and the
                # reconstructor's byte-matching fallback would attribute
                # E9Patch's own spill store and reload to the call and to the
                # ORIGINAL instructions after it.  The plugin's own copy of a
                # call has the same shape and is already recorded as ONE
                # `relocated' entry (len = the call, tramp_len = the whole
                # sequence), which the reconstructor understands; an evictee
                # block gets the same entry.
                ent = _evicted_call(img, md, a, oi, t)
                if ent is None:
                    # An unrecognised shape at an UNPROVEN candidate start is
                    # not an eviction at all -- it is an 0xe9 byte in the middle
                    # of some other instruction whose leading bytes happen to
                    # decode as a `call'.  Treat it as a non-candidate (the
                    # byte-confirmation path below does the same for every
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
            # can land in the middle of a real trampoline on an instruction
            # with the same mnemonic.  E9Patch copies an evicted instruction
            # verbatim unless it is PC-relative, so exact bytes are the rule
            # and a re-encoding is only allowed for a branch or a rip-relative
            # operand.
            if bytes(ti.bytes) != bytes(oi.bytes) and not (
                    _mn(ti) == _mn(oi) and
                    (_mn(oi) in _XFER or _mn(oi).startswith("j") or
                     "rip" in oi.op_str)):
                return False
            # An ALL-ZERO match carries no information and is a false positive
            # waiting to happen: `00 00' decodes to `add byte ptr [rax],al'
            # everywhere, and both sides of this comparison have plenty of zero
            # bytes -- E9Patch pads its trampolines with them, and a rel32 field
            # is mostly zeroes (the tail of a retargeted jump's old rel32 can
            # match padding in front of a sync marker).  Requiring one non-zero
            # byte costs nothing real: an evicted `add byte ptr [rax],al' would
            # be a zero instruction in executable code.
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

    if cfr is not None and CFR_E9MAP is not None:
        # --cfr: the `relocated' list is E9Patch's own record of every copy it
        # wrote (exact), not the scanners' reconstruction of it.  The scanners
        # miss copies (e.g. a $BREAK epilogue after a batch whose next
        # instruction is an unpatched site) and can attribute a body to the
        # wrong site; the differences are reported.
        heur = {e["tramp_addr"]: e["orig_addr"] for e in relocated}
        new_rel = []
        for ta, (oa, tl, _c) in sorted(CFR_E9MAP.items()):
            ob = orig_insn(oa)
            new_rel.append(dict(tramp_addr=ta, orig_addr=oa,
                                len=len(ob) if ob else tl, tramp_len=tl))
        n_add = sum(1 for e in new_rel if e["tramp_addr"] not in heur)
        n_chg = sum(1 for e in new_rel if e["tramp_addr"] in heur
                    and heur[e["tramp_addr"]] != e["orig_addr"])
        n_drop = sum(1 for ta in heur if ta not in CFR_E9MAP)
        relocated = new_rel
        print("[rewrite] --cfr: relocated = E9Patch's relocation record: %d "
              "copies (%d the scanners missed, %d attributed to another "
              "instruction, %d scanner entries not in it); %d site bodies "
              "anchored exactly" % (len(new_rel), n_add, n_chg, n_drop,
                                     cfr.exact))
    elif cfr is not None:
        print("[rewrite] --cfr: WARNING: no E9PATCH_RELOCMAP record (stock "
              "e9patch?) -- relocated copies come from the scanners only")
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
        # CONTROL-FLOW LOG (`--log-blocks'): how many `role="blockid"' entries
        # the stream carries.  Their presence is what tells a consumer that
        # this cv stream records control flow itself and needs no PT.  A build
        # without --log-blocks emits none of these keys.
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
    if PLUGIN_STATS:
        sm["emitter"] = dict(PLUGIN_STATS)
    if n_kf:
        sm["keyframe"] = True
        sm["keyframe_values"] = n_kf
        sm["resync_values"] = sum(1 for e in entries if e.get("resync"))
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
        # With `--ld-so' / `--no-mem-lb' E9Patch places trampolines BELOW the
        # image base, and capstone reports those addresses as unsigned
        # wrap-around values while `ret' is a plain link-time vaddr, so the
        # comparison is made modulo 2^64.
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
    ELF has a multi-page hole between its last PT_LOAD and the loader segment.
    The kernel fills the highest >= 8-page gap at or below the top of the ELF
    mapping with the [vvar][vvar_vclock][vdso] block, so the block lands
    exactly there -- and the loader's very first act is
    mmap(loader_base - 4096, MAP_FIXED), which then tries to split the vdso
    VMA and fails with EINVAL ("mmap() scratch failed (errno=22)").  A --sink
    ptwrite build survives only because without the runtime ELF the hole is
    4 pages and the block does not fit.

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
# conservative rule charges every excluded access as an error (one dropped
# probe on a hot inner-loop instruction can inflate a bound by orders of
# magnitude).
GT_SKIP_EXPLICIT = "explicit"        # the operator listed it in --gt-skip FILE
GT_SKIP_MID_INSN = "mid_insn"        # unavoidable: a patch jump would kill the process
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
    memory-accessing instruction and therefore always hits it (glibc has a
    couple of dozen of these across libc, ld.so and libpthread).

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


def gt_skip_candidates(image_path, failed, attempt, plugin_csv, groups):
    """Gt-only instructions whose gt patch may be what keeps E9Patch from
    patching the spec sites in FAILED.

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
    gt-only instructions (never a spec site) are returned: a critical-value
    sequence is never given up."""
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
    matched = sorted(matched - set(groups))
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


# `--log-blocks' bookkeeping: block leader -> end of its block (link-time),
# block sites moved off an unpatchable leader (new address -> leader), blocks
# left without an identifier, and the block-identifier log sites as
# {link-time leader address -> block id}.
BLOCK_EXT = {}
BLOCK_MOVED = {}
BLOCK_UNLOGGED = []
BLOCK_LOG = {}
BLOCK_MOVE_LOST = [0]


def block_extents(spec, exclude):
    """({block leaders}, {leader -> link-time end of its block}, n_noblocks)
    from the spec's per-function `blocks_list'."""
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


def move_block_sites(image_path, groups, not_patched, not_matched):
    """`--log-blocks': a block-identifier site E9Patch could not place at a
    block's LEADER (typically a 1-byte instruction whose neighbours are
    patched too) is moved to the next instruction of the SAME block that is
    not already a site -- a block executes as a unit, so any of its
    instructions logs it exactly once per execution.  No other site in the
    block logs the identifier, so the site really must move or the block goes
    unlogged; a block with no usable instruction (a lone `jmp'/`ret') stays
    unlogged and is recorded.  Returns (moved, unlogged)."""
    bad = set(not_patched) | set(not_matched)
    failed = sorted(A for A in bad
                    if A in groups and A in BLOCK_LOG and A in BLOCK_EXT)
    if not failed:
        return 0, 0
    img = VirtualImage(image_path)
    _cs, md = load_capstone()

    def insn_size(addr):
        b = img.read(addr, 16)
        if not b:
            return None
        for ins in md.disasm(b, addr):
            return ins.size
        return None

    moved = unlogged = 0
    for A in failed:
        hi = BLOCK_EXT[A]
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
        bid = BLOCK_LOG.pop(A)
        if new is None:
            BLOCK_UNLOGGED.append(A)
            unlogged += 1
        else:
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
            BLOCK_MOVED[new] = A
            moved += 1
    return moved, unlogged


def _parse_layout(plugin_csv):
    """--cfr: the plugin CSV layout rows (same parse as build_sitemap)."""
    layout = {}
    if plugin_csv and os.path.exists(plugin_csv):
        with open(plugin_csv) as f:
            for line in f:
                if line.startswith("#"):
                    continue
                parts = line.strip().split(",")
                a, sid, when, kind, arg, off = parts[:6]
                # keyed extra: `k=counter:period:jnz:join' (keyframe guard)
                extra = {}
                for x in parts[6:]:
                    if len(x) > 2 and x[1] == "=":
                        extra[x[0]] = x[2:].split(":")
                addr, sid, off = int(a, 16), int(sid), int(off)
                L = layout.setdefault(addr, dict(before=None, total=None,
                                                 logs=[], syncs=[],
                                                 pre=[], post=[]))
                if when == "I":
                    L["before"] = off
                elif when == "E":
                    L["total"] = off
                elif when == "S":
                    L["syncs"].append(off)
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
                             counter=None, keyframe=None, kfbr_off=None,
                             kfjoin_off=None)
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
    return layout


def _cfr_for(img, md, out_path, plugin_csv):
    """--cfr: a site -> body resolver for unpatched_addresses()."""
    orig = Elf(CFR_ORIG)

    def orig_window(addr, n=16):
        for vaddr, memsz, off, filesz, flags in orig.loads:
            if flags & 1 and vaddr <= addr < vaddr + filesz:
                fo = off + (addr - vaddr)
                return orig.data[fo:fo + n]
        return None

    def orig_insn(addr):
        b = orig_window(addr)
        for ins in (md.disasm(b, addr) if b else []):
            return bytes(ins.bytes)
        return None
    r = CfrResolver(img, md, orig, _parse_layout(plugin_csv), img.traps(),
                    orig_window, orig_insn)
    return lambda a: r.body(a, orig_insn(a))


def unpatched_addresses(out_path, groups, plugin_csv):
    """Return (not_matched, not_patched) spec addresses of a failed rewrite.

    `analyze.py --avoid` -- the documented remedy -- needs the addresses, not
    counts.  They are recovered from the artefacts the run has already
    produced:

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
        cfr = None
        if CFR is not None:
            cfr = _cfr_for(img, md, out_path, plugin_csv)
        for a in sorted(want & matched):
            try:
                t = cfr(a) if cfr else follow_patch(img, md, a, traps)
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
    ap.add_argument("--sink", choices=["ptwrite", "buffer"],
                    default="ptwrite",
                    help="`ptwrite' logs every value as an Intel PT PTWRITE "
                         "packet; `buffer' stores it into a per-thread ring "
                         "drained to cv.<pid>.<tid>.bin by the injected "
                         "runtime")
    # ---- CONTROL-FLOW LOGGING -------------------------------------------
    # The ablation arm "PTracer without Intel PT": control flow is INSTRUMENTED
    # instead of hardware-traced.  The image logs a compile-time BLOCK
    # IDENTIFIER through the ordinary buffer sink at every basic-block leader,
    # so the value stream carries the executed path interleaved with the
    # critical values, and no PT capture is needed.  Indirect branches and
    # returns are not special-cased here: their TARGET is the leader of the
    # block that runs next, and that block logs its own identifier.
    ap.add_argument("--log-blocks", action="store_true",
                    help="--sink buffer: ALSO log a compile-time BASIC-BLOCK "
                         "IDENTIFIER at every block leader the analyzer "
                         "recovered (the spec's per-function `blocks_list'), "
                         "through the ordinary buffer sink.  The value stream "
                         "then records CONTROL FLOW as well as critical "
                         "values (the `without Intel PT' ablation arm).  One "
                         "extra 8-byte record per executed block; the "
                         "identifier is stored as an immediate, so a leader "
                         "costs one cursor load, one store and one cursor bump")
    ap.add_argument("--log-blocks-only", action="store_true",
                    help="as --log-blocks but WITHOUT the spec's own value "
                         "sites: the image logs control flow and nothing else. "
                         "Isolates the control-flow component of the "
                         "without-PT arm")
    ap.add_argument("--log-blocks-map", metavar="FILE",
                    help="--log-blocks: write the {block id -> link-time leader "
                         "address} table as JSON.  A reconstructor that follows "
                         "control flow from the value stream instead of from PT "
                         "needs it; nothing in the PT-driven path reads it")
    ap.add_argument("--count-blocks-retry", type=int, default=2, metavar="N",
                    help="--log-blocks: when E9Patch cannot place a block site "
                         "at a block leader, move it to the next instruction "
                         "of the same block and re-run e9tool, at most N times "
                         "(default 2); blocks with no usable instruction are "
                         "listed in the --log-blocks-map file as `unlogged'")
    ap.add_argument("--call-sink", action="store_true",
                    help="--sink buffer: ABLATION (Figure 6, `w/o PT and static analysis'): every logged value "
                         "is its own clean CALL into the runtime (all caller-saved registers and the flags saved, "
                         "the value in %%rdi, `call *%%gs:568' -> ptlog_call_rec), the shape of a traditional "
                         "per-access logging call: no inline store, no batching, no liveness.  A memop logs its "
                         "ADDRESS (lea), not the loaded value.  Default off")
    ap.add_argument("--space", type=int, default=3,
                    help="min non-PTWRITE instructions between PTWRITEs")
    ap.add_argument("--sync", type=int, default=4096,
                    help="buffer sink: PTWRITE a running count every N values "
                         "(0 = no sync markers, for a run without PT)")
    ap.add_argument("--sync-carrier", choices=("ptwrite", "tnt"), default="ptwrite",
                    help="buffer sink: how a sync marker carries its count. "
                         "`ptwrite' (default) = one PTWRITE; `tnt' = the TNT bits "
                         "of a short branch loop, so the image executes NO PTWRITE "
                         "(PT-capable CPUs without PTWRITE)")
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
                         "ld-linux-x86-64.so.2: glibc decides whether it is "
                         "the interpreter or the program by comparing "
                         "AT_ENTRY with its own `_start', and E9Patch's new "
                         "e_entry breaks that test")
    ap.add_argument("--keyframe", type=int, default=None,
                    help="KEYFRAME period K for the spec's `resync' sites "
                         "(analyze.py --keyframe): a re-anchor site at a loop "
                         "back-edge header logs its registers unconditionally, "
                         "but only every K-th execution, so a PT overflow or "
                         "decoder resync inside a long-running loop costs at "
                         "most K iterations of unknown addresses instead of "
                         "the rest of the run.  Default: the K the spec "
                         "records per site.  `--keyframe 0' DROPS the resync "
                         "sites, which is how the same spec is built with and "
                         "without them.  A resync site whose EFLAGS are live "
                         "(the counter's `dec' would clobber them) is dropped")
    ap.add_argument("--dead-regs", choices=["both", "spec", "verify", "off"],
                    default="both",
                    help="where the trampoline's scratch registers come from: "
                         "`both' (default) = the spec's `dead_regs' "
                         "INTERSECTED with the rewriter's own check of the "
                         "image's bytes; `spec' = trust the analyzer; `verify' "
                         "= the rewriter's check alone (works with a spec that "
                         "has no `dead_regs'); `off' = push and pop everything")
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
    # e9tool -100 = e9patch --tactic-B0, the last-resort tactic that replaces
    # the instruction with the illegal opcode 0x27 and recovers in a SIGILL
    # handler installed by the E9Patch loader.  It is OFF by default because
    # B0 is unsound for any target that ever blocks a signal: Linux delivers a
    # synchronous fault with force_sig_info_to_task(), which RESETS the handler
    # to SIG_DFL whenever the signal is blocked in the faulting thread -- so a
    # B0 site executed with SIGILL blocked kills the process instead of
    # reaching E9Patch's handler.  glibc's pthread_create blocks ALL signals
    # around clone(2), in the parent and in the new thread until start_thread
    # restores the mask, so a rewritten libc reaches B0 sites inside that
    # window.  It cannot be fixed from outside E9Patch, and it cannot help the
    # NOT MATCHED class either (those are addresses e9tool's disassembler does
    # not consider instruction starts).
    ap.add_argument("--cfr", action="store_true",
                    help="LAYOUT: pass e9tool "
                         "-CFR, i.e. E9Patch's control-flow recovery and its "
                         "T0 tactic, which relocates a run of non-target "
                         "instructions around a site into ONE batch trampoline "
                         "placed freely (no instruction punning), so hot "
                         "trampolines stay contiguous.  Refused (falls back "
                         "to the default layout, with a note) for an image "
                         "with a .gcc_except_table: C++/unwinder landing pads "
                         "are invisible to E9Patch's target analysis.  The "
                         "site map follows the batches.  Default off")
    ap.add_argument("--full-coverage", dest="full_coverage",
                    action="store_true", default=False,
                    help="pass e9tool -100 (= e9patch --tactic-B0), which "
                         "patches the last few otherwise un-patchable sites "
                         "with an illegal instruction + SIGILL handler.  UNSAFE "
                         "for any target that blocks signals (every "
                         "pthread_create does).  Single-threaded targets only")
    ap.add_argument("--e9tool-arg", action="append", default=[],
                    help="extra argument passed straight to e9tool "
                         "(repeatable), e.g. --e9tool-arg=--option "
                         "--e9tool-arg=--tactic-T3=false")
    ap.add_argument("--e9-option", action="append", default=[],
                    help="option passed through to the e9patch BACKEND "
                         "(repeatable): `--e9-option loader-base=0x...' becomes "
                         "e9tool `--option --loader-base=0x...'")
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
                    help="GROUND TRUTH: also patch every memory-accessing "
                         "instruction of the image and make it log its "
                         "effective address and its own ip into a lossless "
                         "per-thread ring, so a run can be compared against "
                         "itself.  This is a MEASUREMENT build: it is much "
                         "slower than a normal one and it needs PTLOG_GT=1 in "
                         "the environment.")
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
                         "place a spec site.  OFF by default: a dropped oracle "
                         "probe makes every access at that address an "
                         "excluded, unverified access, which a conservative "
                         "accuracy bound charges as an error.  When on, the "
                         "drops are counted and reported in <out>.gtskip and "
                         "the site map's `gt_skip_by_reason'.")
    ap.add_argument("--no-sitemap", action="store_true")
    ap.add_argument("--keep-temps", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    if a.call_sink and (a.sink != "buffer" or a.sync != 0):
        ap.error("--call-sink needs --sink buffer and --sync 0")
    if a.sync_carrier == "tnt" and (a.sink != "buffer" or a.sync <= 0):
        ap.error("--sync-carrier tnt needs --sink buffer and --sync > 0")

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
                                   a.keyframe)
    if dropped:
        print("[rewrite] %d sites dropped:" % len(dropped))
        for addr, sid, why in dropped[:10]:
            print("   0x%x (site %s): %s" % (addr, sid, why))
        if len(dropped) > 10:
            print("   ... +%d more" % (len(dropped) - 10))

    # ---- CONTROL-FLOW LOGGING ----------------------------------------------
    # `--log-blocks' adds, at every basic-block LEADER, one `i' op: the buffer
    # sink stores a compile-time block identifier into the next 8-byte slot.
    # The value stream then interleaves "which block is running" with the
    # critical values, so control flow is recorded by INSTRUMENTATION rather
    # than by Intel PT -- the `without PT' ablation arm.
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
        if a.sink != "buffer":
            raise SystemExit("[rewrite] --log-blocks%s needs --sink buffer "
                             "(`ptwrite' has no immediate form, so a block "
                             "identifier cannot go through the PTWRITE sink)"
                             % ("-only" if a.log_blocks_only else ""))
        _ex = parse_ranges(a.exclude_ranges)
        leaders, ext, n_noblocks = block_extents(spec, _ex)
        if not leaders:
            raise SystemExit("[rewrite] --log-blocks: this spec carries no "
                             "`blocks_list' -- re-analyze the image with the "
                             "current analyzer")
        BLOCK_EXT.clear()
        BLOCK_EXT.update(ext)
        # Site ids for the block ops start above every spec site id, so nothing
        # in the site map can confuse the two.
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

    base = a.output
    site_file = base + ".sites"
    csv_file = base + ".emitted.csv"
    # The keyframe counters live in a read-write region E9Patch's loader maps
    # for us, placed just above the image so the trampolines (--mem-lb, below)
    # still start above it.
    data_base = (elf.end + PAGE - 1) & ~(PAGE - 1)
    n_resync_ops = sum(1 for ops in groups.values() for op in ops
                       if op.resync and op.kf)
    # The rewriter's own EFLAGS check is needed by the keyframe guard (its
    # `dec' clobbers the six arithmetic flags) and by the BUFFER SINK, whose
    # cursor update and sync countdown are `add'/`sub' wherever the flags are
    # dead, and `lea'/`jrcxz' where they are not.
    if n_resync_ops or a.sink == "buffer":
        n_down, kinds = verify_flags_dead(a.image, groups,
                                          report=a.output + ".flags_live")
        if n_down:
            n_read = sum(v for k, v in kinds.items() if k.startswith("read"))
            print("[rewrite] EFLAGS: %d logged value(s) the spec calls "
                  "`flags_dead' were NOT confirmed dead by the rewriter's own "
                  "check; they get no guard (a resync value is dropped, a "
                  "buffer-sink value takes the flag-free path).  %d of them "
                  "really do have live flags (an analyzer defect), %d are this "
                  "check giving up (its own conservatism).  See %s.flags_live"
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
            print("[rewrite] note: this spec carries no `dead_regs' at all, "
                  "so every trampoline pushes its scratch.  Re-analyze, or "
                  "pass --dead-regs verify to use the rewriter's own check "
                  "alone.")

    if n_resync_ops:
        n_kdrop = drop_unconfirmed_resync(groups)
        if n_kdrop:
            print("[rewrite] keyframe: %d resync value(s) dropped (their EFLAGS "
                  "is not confirmed dead, so they cannot carry a counter guard)"
                  % n_kdrop)
    n_counters = count_counters(groups)
    # A keyframe countdown in the image's data region is ONE CELL SHARED BY
    # EVERY THREAD.  On the PTWRITE sink that costs at most a keyframe interval,
    # because every value is self-locating in the packet stream.  On the BUFFER
    # sink the cv cursor is POSITIONAL, so a keyframe that fires on another
    # thread's countdown makes the reconstructor consume every later value
    # against the wrong site: keyframes on the buffer sink are for
    # single-threaded targets (or `--keyframe 0').
    if n_counters and a.ld_so:
        raise SystemExit("[rewrite] --keyframe and --ld-so are mutually "
                         "exclusive: --ld-so spends the space just above the "
                         "image on the AT_ENTRY shim and the E9Patch loader, "
                         "which is where the keyframe counters go")
    counter_base = data_base
    data_bytes = ((8 * n_counters) + PAGE - 1) & ~(PAGE - 1)
    if n_counters:
        n_res = sum(1 for ops in groups.values() for op in ops
                    if op.resync and op.kf)
        print("[rewrite] keyframes: %d counter(s) (%d KiB) at %#x -- %d resync "
              "value(s)"
              % (n_counters, (8 * n_counters) // 1024, counter_base, n_res))

    gt_skip = load_gt_skip(a.gt_skip) if a.gt_all else set()
    # Every skipped oracle observation is attributed to a reason, and the
    # reason decides whether the build may succeed (see --allow-gt-drop).
    gt_skip_reason = {addr: GT_SKIP_EXPLICIT for addr in gt_skip}
    if a.gt_all:
        # Never patch an instruction a direct branch enters in the middle
        # (glibc's `je 1f; lock; 1:' single-thread idiom).  The patch jump
        # would be entered at its second byte and the process dies.
        mid = mid_insn_branch_targets(a.image) - gt_skip
        if mid:
            gt_skip |= mid
            for addr in mid:
                gt_skip_reason[addr] = GT_SKIP_MID_INSN
            print("[rewrite] --gt-all: %d instruction(s) are entered in the "
                  "MIDDLE by a direct branch and cannot carry a patch jump; "
                  "their gt sequence is skipped" % len(mid))
    # A stale `<out>.unpatched' from an earlier run must not survive a run
    # that fails for a DIFFERENT reason: a caller's exclude-and-retry loop
    # would read it again and exclude the same sites round after round.
    for stale in (a.output + ".unpatched", a.output + ".gtskip"):
        if os.path.exists(stale):
            os.remove(stale)
    if a.gt_all:
        write_gt_skip(a.output + ".gtskip", gt_skip, gt_skip_reason)

    def emit_site_file():
        write_site_file(site_file, groups, a.sink, a.space, a.sync,
                        counter_base, n_counters, gt=a.gt_all, gtoff=GT_OFF,
                        gtstack=a.gt_stack, gt_skip=gt_skip,
                        sync_carrier=a.sync_carrier, call_sink=a.call_sink)

    emit_site_file()
    if a.gt_all:
        print("[rewrite] --gt-all: every memory-accessing instruction is "
              "patched as well; the run needs PTLOG_GT=1 (and PTLOG_GT_DIR) in "
              "its environment, and writes gt.<pid>.<tid>.bin next to the cv "
              "files")

    argv = [os.path.join(E9PATCH_DIR, "e9tool")]
    if a.cfr:
        global CFR, CFR_ORIG
        _secs = subprocess.run(["readelf", "-SW", a.image], capture_output=True,
                               text=True).stdout
        if ".gcc_except_table" in _secs:
            print("[rewrite] --cfr: NOT applied to %s (it has a "
                  ".gcc_except_table; landing pads are not recovered "
                  "control-flow targets) -- default layout" % a.image)
        elif a.ld_so:
            # --ld-so places the trampolines BELOW the image (negative
            # addresses); the E9PATCH_RELOCMAP record is not mapped onto
            # that layout -- default layout.
            print("[rewrite] --cfr: NOT applied to %s (--ld-so layout) -- "
                  "default layout" % a.image)
        else:
            argv += ["-CFR"]
            CFR, CFR_ORIG = set(), a.image
            os.environ["E9PATCH_RELOCMAP"] = os.path.abspath(a.output) + ".e9reloc"
            print("[rewrite] --cfr: e9tool -CFR (tactic T0 batches)")
    if a.full_coverage:
        argv += ["-100"]
    argv += list(a.e9tool_arg)
    ld_so_fix_base = 0
    ld_so_rt_base = 0
    ld_so_loader_base = 0
    if a.ld_so:
        # ---- Rewriting the dynamic loader ----------------------------------
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
        # A --gt-all (or buffer) build also injects the runtime ELF, which
        # the plugin maps at 0x70000000 by default.  e9patch requires
        # `--loader-base' to be at least the END of every mapping it emits, so
        # with the loader parked one page above a 1.4 MB ld.so that default is
        # fatal ("loader base address ... must not exceed maximum mapping
        # address").  Put the runtime directly above the ldfix ELF instead and
        # raise the loader above it; the hole below the loader stays one page,
        # which is what keeps the trampolines below the image base.
        rt_span = 0
        if (a.sink == "buffer" or a.gt_all) and a.rt:
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
    if (a.sink == "buffer" or a.gt_all) and a.rt:
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
                    "no-fini runtime %s, which is missing (make -C "
                    "runtime/e9plugin builds it)" % (a.image, rt))
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
      if CFR is not None and os.environ.get("E9PATCH_RELOCMAP"):
          try:
              os.remove(os.environ["E9PATCH_RELOCMAP"])
          except OSError:
              pass
      r = subprocess.run(argv, capture_output=True, text=True)
      if CFR is not None:
          _load_e9reloc(os.environ.get("E9PATCH_RELOCMAP"))
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
              # (see gt_skip_candidates) and run e9tool again.  Critical-value
              # sequences are untouched.
              attempt += 1
              new = gt_skip_candidates(a.image, np_, attempt, csv_file,
                                       groups) - gt_skip
              if new and not a.allow_gt_drop:
                  # A silent oracle hole would make every access at the dropped
                  # addresses an error under the conservative bound.  Refuse by
                  # default: the blocking SPEC sites go to <out>.unpatched,
                  # which is what analyze.py --avoid consumes, so the caller
                  # re-places the critical value instead of losing the oracle.
                  # --allow-gt-drop opts back in, loudly.
                  print("[rewrite] --gt-retry %d/%d: %d spec site(s) not "
                        "patched; placing them would mean DROPPING the ground "
                        "truth at %d instruction(s) -- refusing.\n"
                        "           Every access at a dropped address becomes "
                        "an EXCLUDED, unverified access that a conservative "
                        "bound charges as an error.\n"
                        "           Re-place the critical value instead: "
                        "static/analyze.py --avoid %s, or pass --allow-gt-drop "
                        "to accept the oracle gap, which is then counted and "
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
                  emit_site_file()
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
          if (a.log_blocks or a.log_blocks_only) and \
                  cb_attempt < a.count_blocks_retry:
              cb_attempt += 1
              n_mv, n_unp = move_block_sites(a.image, groups, np_, nm)
              if n_mv or n_unp:
                  print("[rewrite] --log-blocks retry %d/%d: %d block site(s) "
                        "could not be placed at the leader -> %d moved to the "
                        "next instruction of the block, %d block(s) left "
                        "unlogged; re-running e9tool"
                        % (cb_attempt, a.count_blocks_retry, n_mv + n_unp,
                           n_mv, n_unp))
                  if n_mv:
                      emit_site_file()
                      continue
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

    # ---- B0 / SIGILL trap audit --------------------------------------------
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
    if (a.sink == "buffer" or a.gt_all) and not a.rt:
        print("[rewrite] NOTE: sink=%s without --rt; run with "
              "LD_PRELOAD=%s" % (a.sink,
                                       os.path.join(HERE, "rt", "ptlogrt.so")))

    if not a.no_sitemap:
        site_kind = {int(x.get("id", -1)): x["kind"] for x in spec["sites"]}
        sm, problems = build_sitemap(a.output, a.image, groups, a.sink,
                                     csv_file,
                                     a.sync if a.sink == "buffer" else 0,
                                     site_kind=site_kind,
                                     allow_negative=a.ld_so or a.no_mem_lb,
                                     gt=a.gt_all)
        # A non-zero B0 count means the image is not safe to run multithreaded.
        # Recorded so a consumer (or a later audit) can tell.
        sm["b0_traps"] = n_b0
        # The buffer sink's sync marker is the per-site countdown and the
        # cursor update is the trampoline's own; recorded for consumers that
        # check the shape of the build.
        sm["sync_mode"] = "countdown"
        sm["cursor_update"] = "default"
        if a.gt_all:
            sm["gt_skip"] = sorted(gt_skip)
            # A consumer must be able to tell an unavoidable mid-instruction
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
        # about the values the reconstruction consumes.
        missing = (expected_values(groups, spec)
                   - sum(1 for e in sm["entries"] if e.get("role") != "gt"))
        if problems:
            # Every problem is a logged value the reconstructor cannot
            # interpret, i.e. a silent accuracy loss -- so the FULL list goes to
            # a file (at whole-program scale there are hundreds).
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
        # A site map that is SHORT is not a warning.  `offline/ptrecon` consumes
        # the buffer sink's value file positionally, so one omitted site shifts
        # every value after it and the reconstruction is silently wrong with
        # ZERO unknown addresses -- the worst failure mode there is.  In the
        # PTWRITE sink the effect is the same: the payloads
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

    # ---- CONTROL-FLOW LOG bookkeeping ---------------------------------------
    if a.log_blocks or a.log_blocks_only:
        if BLOCK_UNLOGGED or BLOCK_MOVE_LOST[0]:
            print("[rewrite] --log-blocks: %d block(s) left WITHOUT an "
                  "identifier (E9Patch could patch no instruction of the "
                  "block), %d spec value(s) dropped from a moved leader"
                  % (len(BLOCK_UNLOGGED), BLOCK_MOVE_LOST[0]))
        if a.log_blocks_map:
            blocks = [dict(id=b, leader=BLOCK_MOVED.get(A, A), site_addr=A,
                           moved=(A in BLOCK_MOVED))
                      for A, b in sorted(BLOCK_LOG.items(), key=lambda kv: kv[1])]
            with open(a.log_blocks_map, "w") as f:
                json.dump(dict(image=os.path.abspath(a.image),
                               output=os.path.abspath(a.output),
                               n_blocks=len(blocks),
                               unlogged=sorted(BLOCK_UNLOGGED),
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
