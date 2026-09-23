"""
Value graph (the paper's dependency graph).

Every runtime value the analysis reasons about is a hash-consed node of a directed graph:

  Leaf kinds (loggable at a definite site, or unloggable):
    Entry(reg)                     value of `reg` at function entry (a function parameter / inherited value)
    Load(addr, size, ...)          the value loaded by the instruction at `addr` (has an optional
                                   `fwd` child = the value known to be in that memory slot, i.e.
                                   store-to-load forwarding on the stack frame; then logging it is
                                   optional)
    Pseudo(addr, reg)              value of `reg` right after a call returns to `addr` (paper's
                                   "pseudo-load": the callee may have modified the register)
    BlockIn(block, reg)            value of `reg` at the entry of a block with no predecessors
                                   (region mode / unresolved indirect jump target)
    Opaque(addr, reg, why)         a value we cannot recompute offline (cmov/flag-dependent, helper
                                   call, unsupported IR op, syscall result...) that is materialised in
                                   `reg` right after the instruction at `addr` -> loggable there
    Unknown                        a value that is neither recomputable nor loggable (e.g. the
                                   contents of an untracked stack slot). Anything depending on it
                                   must be logged downstream or is unreconstructable.
    Const(value)

  Interior nodes:
    Op(name, args)                 VEX-level arithmetic; recomputable offline from its args
    Phi(block, loc, args)          join of the values reaching `loc` (register or frame slot) at the
                                   entry of `block` (one operand per predecessor)

A node is *loggable* at a site if its value sits in a full 64-bit GP register (or xmm) at a known
instruction boundary; `Node.sites` lists such (addr, when, reg) triples (filled by the dataflow).
The hitting-set solver (hitset.py) picks, for every memory access, a *cut* of loggable nodes that
separates all leaves from the address (the paper's "input set"), minimising the logging cost.
"""
from __future__ import annotations
import itertools

_counter = itertools.count()


class Node:
    __slots__ = ("kind", "key", "args", "id", "sites", "bits", "_hash", "meta")

    def __init__(self, kind, key, args=(), bits=64, meta=None):
        self.kind = kind          # 'entry','load','pseudo','blockin','opaque','unknown','const','op','phi'
        self.key = key            # kind-specific identity tuple
        self.args = tuple(args)   # children (Phi: filled in later, mutable via set_args)
        self.id = next(_counter)
        self.sites = []           # [(addr, when('before'|'after'), reg)] places where the value is in a register
        self.bits = bits
        self.meta = meta          # free-form (e.g. load size, insn addr)
        self._hash = hash((kind, key, tuple(a.id for a in self.args) if kind != 'phi' else self.id))

    def set_args(self, args):
        assert self.kind == 'phi'
        self.args = tuple(args)

    def __hash__(self):
        return self._hash

    def __eq__(self, o):
        return self is o

    def is_leaf(self):
        return self.kind in ('entry', 'load', 'pseudo', 'blockin', 'opaque', 'unknown', 'const')

    def __repr__(self):
        if self.kind == 'const':
            return hex(self.key)
        if self.kind == 'op':
            return f"{self.key}({','.join(map(repr, self.args))})"
        if self.kind == 'phi':
            return f"phi{self.id}@{hex(self.key[0])}:{self.key[1]}"
        return f"{self.kind}{self.key}"


class Graph:
    """Hash-consing factory: structurally identical Op/Const/leaf nodes are the same object."""

    def __init__(self):
        self._tab = {}
        self.unknown = Node('unknown', ('unknown',))
        self.pending_phis = []      # phis whose operands are not yet filled in
        self.phis = []

    def _get(self, kind, key, args=(), bits=64, meta=None):
        k = (kind, key, tuple(a.id for a in args))
        n = self._tab.get(k)
        if n is None:
            n = Node(kind, key, args, bits, meta)
            self._tab[k] = n
        return n

    def const(self, v, bits=64):
        if isinstance(v, float):
            import struct
            v = struct.unpack('<Q', struct.pack('<d', v))[0] if bits == 64 else struct.unpack('<I', struct.pack('<f', v))[0]
        v &= (1 << bits) - 1 if bits < 64 else 0xFFFFFFFFFFFFFFFF
        return self._get('const', v, bits=bits)

    def entry(self, reg):
        return self._get('entry', reg)

    def load(self, addr, size, seq, fwd=None):
        # seq distinguishes multiple loads in one instruction (rep movs etc.)
        return self._get('load', (addr, size, seq), args=(fwd,) if fwd is not None else (), bits=size * 8,
                         meta={'addr': addr, 'size': size})

    def pseudo(self, addr, reg):
        return self._get('pseudo', (addr, reg))

    def blockin(self, block, reg):
        return self._get('blockin', (block, reg))

    def opaque(self, addr, reg, why):
        return self._get('opaque', (addr, reg, why))

    def phi(self, block, loc):
        # Phis are unique per (block, location); not hash-consed on args (args set later)
        k = ('phi', (block, loc), ())
        n = self._tab.get(k)
        if n is None:
            n = Node('phi', (block, loc))
            self._tab[k] = n
            self.pending_phis.append(n)
            self.phis.append(n)
        return n

    def op(self, name, args, bits=64):
        args = tuple(args)
        # constant folding for the common address arithmetic (keeps graphs small)
        if all(a.kind == 'const' for a in args):
            v = fold(name, [a.key for a in args], [a.bits for a in args], bits)
            if v is not None:
                return self.const(v, bits)
        if any(a.kind == 'unknown' for a in args):
            return self.unknown
        # normalise 64-bit add/sub with constants into Add64(x, c) and fold chains
        if name == 'Sub64' and len(args) == 2 and args[1].kind == 'const':
            name, args = 'Add64', (args[0], self.const(-args[1].key, 64))
        if name == 'Add64' and len(args) == 2:
            a, b = args
            if a.kind == 'const' and b.kind != 'const':
                a, b = b, a
            if b.kind == 'const':
                if b.key == 0:
                    return a
                if a.kind == 'op' and a.key == 'Add64' and a.args[1].kind == 'const':
                    return self.op('Add64', [a.args[0], self.const(a.args[1].key + b.key, 64)])
            args = (a, b)
        if name in ('Or64', 'Xor64') and len(args) == 2 and args[1].kind == 'const' and args[1].key == 0:
            return args[0]
        return self._get('op', name, args, bits=bits)


MASK64 = 0xFFFFFFFFFFFFFFFF


def fold(name, vals, argbits, bits):
    m = (1 << bits) - 1
    try:
        if name.startswith('Add'):
            return (vals[0] + vals[1]) & m
        if name.startswith('Sub'):
            return (vals[0] - vals[1]) & m
        if name.startswith('Mul'):
            return (vals[0] * vals[1]) & m
        if name.startswith('And'):
            return vals[0] & vals[1]
        if name.startswith('Or'):
            return vals[0] | vals[1]
        if name.startswith('Xor'):
            return vals[0] ^ vals[1]
        if name.startswith('Shl'):
            return (vals[0] << (vals[1] & 63)) & m
        if name.startswith('Shr'):
            return (vals[0] & m) >> (vals[1] & 63)
        if name.startswith('Sar'):
            ab = argbits[0]
            v = vals[0] & ((1 << ab) - 1)
            if v >> (ab - 1):
                v -= 1 << ab
            return (v >> (vals[1] & 63)) & m
        if name in ('32Uto64', '16Uto64', '8Uto64', '8Uto32', '16Uto32', '8Uto16', '1Uto64', '1Uto8', '32to8', '32to16', '64to32', '64to16', '64to8', '64to1', '16to8', '32to1'):
            return vals[0] & m
        if name in ('32Sto64', '16Sto64', '8Sto64', '8Sto32', '16Sto32', '8Sto16'):
            ab = argbits[0]
            v = vals[0] & ((1 << ab) - 1)
            if v >> (ab - 1):
                v -= 1 << ab
            return v & m
        if name == '64HIto32':
            return (vals[0] >> 32) & m
        if name == '32HLto64':
            return ((vals[0] << 32) | (vals[1] & 0xFFFFFFFF)) & m
        if name.startswith('Not'):
            return (~vals[0]) & m
        if name.startswith('CmpEQ'):
            return 1 if vals[0] == vals[1] else 0
        if name.startswith('CmpNE'):
            return 1 if vals[0] != vals[1] else 0
        if name.startswith('CmpLT') and name.endswith('U'):
            return 1 if vals[0] < vals[1] else 0
        if name.startswith('CmpLE') and name.endswith('U'):
            return 1 if vals[0] <= vals[1] else 0
    except Exception:
        return None
    return None


def walk(root):
    seen, stack = set(), [root]
    while stack:
        n = stack.pop()
        if n.id in seen:
            continue
        seen.add(n.id)
        yield n
        stack.extend(n.args)
