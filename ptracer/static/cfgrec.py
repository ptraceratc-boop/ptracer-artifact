"""
CFG recovery for ONE function (linear sweep, or recursive descent for JIT code objects, over the
function's address range) plus loop nesting depth per block.

FuncCFG fields:
  entry, blocks {addr: pyvex IRSB}, preds/succs {addr: set(addr)}, call_return_sites {block: ret_addr},
  loop_depth {addr: int}, orphans (blocks not reachable from entry: region / unresolved indirect
  targets), coverage (fraction of [start,end) bytes covered by lifted blocks), unresolved
  (blocks ending in an indirect jump with no in-range successor).
"""
from __future__ import annotations
import logging
import networkx as nx

for n in ('angr', 'cle', 'pyvex', 'claripy'):
    logging.getLogger(n).setLevel('CRITICAL')


def irsb_next(irsb):
    """(kind, value): ('const', addr) for a known successor, ('indirect', None) otherwise. Handles the
    unoptimised VEX form `PUT(rip)=X; t = GET(rip); NEXT: t` by looking at the last PUT(rip)."""
    import archinfo
    RIP = archinfo.ArchAMD64().registers['rip'][0]
    nxt = irsb.next
    if nxt.tag == 'Iex_Const':
        return 'const', nxt.con.value
    if nxt.tag == 'Iex_RdTmp':
        last = None
        for st in irsb.statements:
            if st.tag == 'Ist_Put' and st.offset == RIP:
                last = st.data
            if st.tag == 'Ist_WrTmp' and st.tmp == nxt.tmp:
                if st.data.tag == 'Iex_Get' and st.data.offset == RIP and last is not None:
                    return ('const', last.con.value) if last.tag == 'Iex_Const' else ('indirect', None)
                return 'indirect', None
    return 'indirect', None


class FuncCFG:
    def __init__(self, name, start, end):
        self.name, self.start, self.end = name, start, end
        self.entry = start
        self.blocks = {}
        self.preds, self.succs = {}, {}
        self.call_return_sites = {}
        self.loop_depth = {}
        self.orphans = set()
        self.coverage = 0.0
        self.unresolved = set()
        self.plt_targets = {}    # block -> callee name (direct calls)


def recover(proj, name, start, end, method='linear', roots=(), fill=False, data_from=None):
    """Recover the CFG of [start,end). method='linear' (default): linear sweep from `start` following
    direct jumps (fast, deterministic); 'descent': recursive descent from `start` (only bytes
    reachable through direct control flow are decoded -- for JIT code objects, which embed jump
    tables and constant pools inline)."""
    if method == 'descent':
        return recover_descent(proj, name, start, end, roots=roots, fill=fill, data_from=data_from)
    return recover_linear(proj, name, start, end)


def recover_descent(proj, name, start, end, roots=(), fill=False, data_from=None):
    """Recursive-descent CFG: lift blocks only at addresses reached by fallthrough, direct jumps,
    conditional exits and call returns from `start` (and from `roots`: extra entry points the
    caller knows, e.g. HotSpot's jvmtiAddrLocationMap). Unreached bytes (inline data) are never
    decoded. Jump tables are added later by resolve_jump_tables (targets must then be inside a
    lifted block or start a new descent from the target).

    fill=True (JIT code objects entered in the middle by OSR/deopt so that the descent from
    `start` covers a few bytes only): after the descent, restart it at the first uncovered byte
    that passes a self-synchronisation check (`_sync_restart`), repeat until the object (clipped
    at `data_from`, the first byte of inline data) is covered. The restart addresses are recorded
    in f.restart_roots so that the runtime patcher can seed ITS sweep with the same roots and
    decode the same instruction boundaries."""
    f = FuncCFG(name, start, end)
    f.undecodable = []
    f.restart_roots = []
    f.restart_evidence = []
    blocks = {}
    work = [start] + [r for r in roots if start <= r < end and r != start]
    _descend(proj, f, blocks, work, start, end)
    if fill:
        limit = data_from if data_from and start < data_from <= end else end
        f.restart_rejected = []
        tried = set(f.undecodable)
        for _ in range(256):
            hit = _sync_restart(proj, blocks, tried, start, limit)
            if hit is None:
                break
            c, kind, nins = hit
            before = dict(blocks)
            if _descend(proj, f, blocks, [c], start, end, strict=True):
                f.restart_roots.append(c)
                f.restart_evidence.append((c, kind, nins))
            else:
                # the restart's phase reached a known block at a non-boundary -> it was misaligned;
                # drop everything it added and never try this byte again
                blocks.clear(); blocks.update(before)
                f.restart_rejected.append(c)
                tried.add(c)
    _descent_finish(proj, f, blocks, start, end)
    return f


def _insn_boundary(irsb, addr):
    return any(st.tag == 'Ist_IMark' and st.addr == addr for st in irsb.statements)


def _descend(proj, f, blocks, work, start, end, strict=False):
    """Recursive descent from `work`. strict=True (restart fill): a target that falls strictly inside a
    known block at a NON-instruction-boundary proves the seed was decoded out of phase -> return False."""
    while work:
        addr = work.pop()
        if addr in blocks or not (start <= addr < end):
            continue
        # split an existing block if addr falls inside it
        hit = next((a for a, b in blocks.items() if a < addr < a + b.size), None)
        if hit is not None:
            if strict and not _insn_boundary(blocks[hit], addr):
                return False
            try:
                blocks[hit] = proj.factory.block(hit, size=addr - hit, opt_level=0).vex
                blocks[addr] = proj.factory.block(addr, size=end - addr, opt_level=0).vex
            except Exception:
                continue
        else:
            try:
                irsb = proj.factory.block(addr, size=end - addr, opt_level=0).vex
            except Exception:
                irsb = None
            if irsb is None or irsb.size == 0:
                f.undecodable.append(addr)
                continue
            blocks[addr] = irsb
        irsb = blocks[addr]
        nxt_addr = addr + irsb.size
        for st in irsb.statements:
            if st.tag == 'Ist_Exit' and st.jumpkind == 'Ijk_Boring':
                work.append(st.dst.value)
        nk, nv = irsb_next(irsb)
        jk = irsb.jumpkind
        if jk == 'Ijk_Boring' and nk == 'const':
            work.append(nv)
        elif jk == 'Ijk_Call' or jk in ('Ijk_Sys_syscall', 'Ijk_Sys_int128', 'Ijk_Yield'):
            work.append(nxt_addr)
    return True


_PAD = {'nop', 'int3', 'hlt', 'ud2'}
_TERM = {'ret', 'retq', 'int3', 'ud2', 'hlt'}
MIN_RESTART_INSNS = 6       # a restart with no other evidence must decode at least this many instructions
PHASE_INSNS = 8             # an alternative phase that decodes this many instructions without converging kills it


def _lin(proj, c, lim, n):
    """Linear capstone decode from c: list of (addr, size, mnemonic, op_str, direct_target|None) until an
    invalid instruction, a terminator, or lim. Returns (list, how) with how in {'invalid','term','end'}."""
    out = []
    p = c
    while p < lim and len(out) < n:
        try:
            ins = proj.factory.block(p, size=min(15, lim - p)).capstone.insns
        except Exception:
            ins = []
        if not ins:
            return out, 'invalid'
        i = ins[0]
        tgt = None
        if (i.mnemonic.startswith('j') or i.mnemonic.startswith('call') or i.mnemonic.startswith('loop')) and i.op_str.startswith('0x'):
            try:
                tgt = int(i.op_str, 16)
            except ValueError:
                tgt = None
        out.append((p, i.size, i.mnemonic, i.op_str, tgt))
        p += i.size
        if i.mnemonic in _TERM or i.mnemonic == 'jmp':
            return out, 'term'
    return out, 'end'


def _phase_bounds(proj, c, lim, n=64):
    """Instruction-boundary set of the linear decode from c (None if it decodes < 4 instructions)."""
    chain, how = _lin(proj, c, lim, n)
    if len(chain) < 4:
        return None
    return {a for a, _, _, _, _ in chain} | {chain[-1][0] + chain[-1][1]}


def _sync_restart(proj, blocks, tried, start, limit, max_chain=64):
    """Where to restart the descent inside an uncovered gap of [start,limit) (the restart fill):
    a restart must be PHASE-ANCHORED, never found by stepping bytes.
      1. The first byte of a gap (after nop/int3 padding) is anchored by its predecessor: gaps lie between
         known blocks, so a known instruction ends exactly there. If its linear decode is valid (no invalid
         opcode, never straddling a known block start) it is accepted as is -- a 1-instruction `jmp` to a
         not-yet-known target included.
      2. Otherwise the gap head holds data or a mis-phased region. The only trustworthy address is the
         CONVERGENCE POINT m: the first address on which EVERY phase c..c+15 that decodes at least 4 valid
         instructions agrees (x86 self-synchronises; before m no phase is trustworthy, at m all are). Restart
         at m when its own decode is valid; else give the gap up.
    Bytes examined are added to `tried`; a gap whose head is in `tried` is skipped. None = nothing left."""
    spans = sorted((a, a + b.size) for a, b in blocks.items())
    starts = set(blocks)
    pos = start
    gaps = []
    for a, e in spans:
        if pos < a:
            gaps.append((pos, a))
        pos = max(pos, e)
    if pos < limit:
        gaps.append((pos, limit))

    def valid_chain(c, g1):
        chain, how = _lin(proj, c, g1, max_chain)
        if not chain or how == 'invalid':
            return None
        if any(a < b < a + sz for a, sz, _, _, _ in chain for b in starts):
            return None
        return chain

    for g0, g1 in gaps:
        if g0 in tried:
            continue
        tried.add(g0)
        c = g0
        # skip padding at the head of the gap (real instructions of the true phase)
        chain, how = _lin(proj, c, g1, 8)
        k = 0
        while k < len(chain) and chain[k][2] in _PAD:
            k += 1
        if chain and k == len(chain):
            c = chain[-1][0] + chain[-1][1]
            if c >= g1:
                continue
        elif k:
            c = chain[k][0]
        if c in tried and c != g0:
            continue
        tried.add(c)
        # rule 1: anchored head
        if valid_chain(c, g1) is not None:
            return (c, 'anchored', len(valid_chain(c, g1)))
        # rule 2: convergence point of all valid phases
        sets = []
        for kk in range(0, 16):
            if c + kk >= g1:
                break
            B = _phase_bounds(proj, c + kk, g1)
            if B is not None:
                sets.append((c + kk, B))
        if len(sets) >= 2:
            common = set.intersection(*[B for _, B in sets])
            common = sorted(m for m in common if m > c + 15 and m < g1)
            for m in common:
                if m in tried:
                    continue
                tried.add(m)
                ch = valid_chain(m, g1)
                if ch is not None:
                    for b in range(c, m):
                        tried.add(b)
                    return (m, 'converged', len(ch))
                break
        # give this gap up
        for b in range(c, min(c + 16, g1)):
            tried.add(b)
    return None


def _descent_finish(proj, f, blocks, start, end):
    # a block lifted with size=end-addr may run past a later-discovered block start: trim
    for a in sorted(blocks):
        irsb = blocks[a]
        later = [b for b in blocks if a < b < a + irsb.size]
        if later:
            try:
                blocks[a] = proj.factory.block(a, size=min(later) - a, opt_level=0).vex
            except Exception:
                pass
    f.blocks = blocks
    f.descent = True
    f.insn_addrs = set()
    for a, irsb in blocks.items():
        for st in irsb.statements:
            if st.tag == 'Ist_IMark':
                f.insn_addrs.add(st.addr)
    f.preds = {a: set() for a in blocks}
    f.succs = {a: set() for a in blocks}

    def edge(a, b):
        if b in f.blocks:
            f.succs[a].add(b)
            f.preds[b].add(a)
    for a in sorted(blocks):
        irsb = blocks[a]
        nxt_addr = a + irsb.size
        for st in irsb.statements:
            if st.tag == 'Ist_Exit':
                t = st.dst.value
                if start <= t < end:
                    edge(a, t)
        jk = irsb.jumpkind
        nk, nv = irsb_next(irsb)
        if jk == 'Ijk_Boring':
            if nk == 'const':
                if start <= nv < end:
                    edge(a, nv)
            else:
                f.unresolved.add(a)
        elif jk == 'Ijk_Call':
            if nk == 'const':
                f.plt_targets[a] = hex(nv)
            if nxt_addr < end:
                f.call_return_sites[a] = nxt_addr
                edge(a, nxt_addr)
        elif jk in ('Ijk_Sys_syscall', 'Ijk_Sys_int128', 'Ijk_Yield'):
            if nxt_addr < end:
                edge(a, nxt_addr)
    _finish(proj, f)
    return f


def recover_linear(proj, name, start, end):
    f = FuncCFG(name, start, end)
    f.undecodable = []
    # pass 1: linear sweep -> instruction boundaries + block starts (function start, jump targets,
    # fallthrough after every block terminator)
    blocks = {}
    addr = start
    targets = set()
    while addr < end:
        try:
            irsb = proj.factory.block(addr, size=end - addr, opt_level=0).vex
        except Exception:
            irsb = None
        if irsb is None or irsb.size == 0 or irsb.jumpkind == 'Ijk_NoDecode' and irsb.size == 0:
            # undecodable (e.g. AVX-512 EVEX); skip one instruction using capstone, else one byte
            n = 1
            try:
                cs = proj.factory.block(addr, size=min(16, end - addr)).capstone
                if cs.insns:
                    n = cs.insns[0].size
            except Exception:
                pass
            f.undecodable.append(addr)
            addr += n
            continue
        blocks[addr] = irsb
        for st in irsb.statements:
            if st.tag == 'Ist_Exit' and st.jumpkind in ('Ijk_Boring',):
                t = st.dst.value
                if start <= t < end:
                    targets.add(t)
        nk, nv = irsb_next(irsb)
        if nk == 'const' and irsb.jumpkind == 'Ijk_Boring':
            if start <= nv < end:
                targets.add(nv)
        addr += irsb.size
    # pass 2: split blocks at jump targets that fall inside a block
    for t in sorted(targets):
        if t in blocks:
            continue
        for a in sorted(blocks):
            irsb = blocks[a]
            if a < t < a + irsb.size:
                try:
                    blocks[a] = proj.factory.block(a, size=t - a, opt_level=0).vex
                    blocks[t] = proj.factory.block(t, size=a + irsb.size - t, opt_level=0).vex
                except Exception:
                    pass
                break
    f.blocks = blocks
    f.insn_addrs = set()
    for a, irsb in blocks.items():
        for st in irsb.statements:
            if st.tag == 'Ist_IMark':
                f.insn_addrs.add(st.addr)
    f.preds = {a: set() for a in blocks}
    f.succs = {a: set() for a in blocks}

    def edge(a, b):
        if b in f.blocks:
            f.succs[a].add(b)
            f.preds[b].add(a)

    for a in sorted(blocks):
        irsb = blocks[a]
        nxt_addr = a + irsb.size
        for st in irsb.statements:
            if st.tag == 'Ist_Exit':
                t = st.dst.value
                if start <= t < end:
                    edge(a, t)
        jk = irsb.jumpkind
        nk, nv = irsb_next(irsb)
        if jk == 'Ijk_Boring':
            if nk == 'const':
                if start <= nv < end:
                    edge(a, nv)
                # else: tail jump out of the function
            else:
                f.unresolved.add(a)
        elif jk == 'Ijk_Call':
            if nk == 'const':
                f.plt_targets[a] = hex(nv)
            if nxt_addr < end:
                f.call_return_sites[a] = nxt_addr
                edge(a, nxt_addr)
        elif jk in ('Ijk_Sys_syscall', 'Ijk_Sys_int128', 'Ijk_Yield', 'Ijk_SigTRAP', 'Ijk_NoDecode', 'Ijk_MapFail', 'Ijk_EmWarn', 'Ijk_InvalICache', 'Ijk_EmFail'):
            if nxt_addr < end and jk not in ('Ijk_NoDecode',):
                edge(a, nxt_addr)
        # Ijk_Ret: no successor
    _finish(proj, f)
    return f


def _finish(proj, f):
    start, end = f.start, f.end
    seen, stack = set(), [start] if start in f.blocks else []
    while stack:
        a = stack.pop()
        if a in seen:
            continue
        seen.add(a)
        stack.extend(f.succs[a])
    f.orphans = set(f.blocks) - seen
    covered = sum(b.size for b in f.blocks.values())
    f.coverage = min(1.0, covered / max(1, end - start))
    for a in list(f.orphans):
        irsb = f.blocks[a]
        if not any(st.tag not in ('Ist_IMark', 'Ist_NoOp', 'Ist_AbiHint') for st in irsb.statements) and not f.succs[a] or _is_padding(proj, a, f.blocks[a]):
            f.orphans.discard(a)
            del f.blocks[a]
            for p in f.preds.pop(a, ()):
                f.succs[p].discard(a)
            for sx in f.succs.pop(a, ()):
                f.preds[sx].discard(a)
    f.loop_depth = loop_depths(f)


def loop_depths(f):
    """Nesting depth of natural loops via recursive SCC decomposition (Bourdoncle-style)."""
    G = nx.DiGraph()
    G.add_nodes_from(f.blocks)
    for a, ss in f.succs.items():
        for s in ss:
            G.add_edge(a, s)
    depth = {a: 0 for a in f.blocks}

    def rec(sub, d):
        for comp in nx.strongly_connected_components(sub):
            if len(comp) == 1 and not sub.has_edge(next(iter(comp)), next(iter(comp))):
                continue
            for a in comp:
                depth[a] = max(depth[a], d + 1)
            inner = sub.subgraph(comp).copy()
            # remove loop entry nodes (nodes with a predecessor outside the SCC)
            entries = [a for a in comp if any(p not in comp for p in sub.predecessors(a))]
            if not entries:
                entries = [min(comp)]
            inner.remove_nodes_from(entries)
            if inner.number_of_nodes():
                rec(inner, d + 1)
    rec(G, 0)
    return depth


def _is_padding(proj, addr, irsb):
    """True if the block consists only of nop-like instructions (multi-byte nops, cs/data16 nops)."""
    try:
        blk = proj.factory.block(addr, size=irsb.size)
        return all(ins.mnemonic.endswith('nop') or ins.mnemonic in ('nop', 'nopw', 'nopl', 'xchg') and 'ax' in ins.op_str
                   for ins in blk.capstone.insns) and len(blk.capstone.insns) > 0
    except Exception:
        return False


# ----------------------------------------------------------------------------- jump tables
def _phi_single_const(df, phi, f, limit=4096):
    """The one constant a phi web can carry if its only other leaves are BlockIn values of orphan
    blocks (code reached through still-unresolved indirect jumps); else None."""
    seen, stack, const = set(), [phi], None
    while stack:
        x = df.resolve(stack.pop())
        if x.id in seen:
            continue
        seen.add(x.id)
        if len(seen) > limit:
            return None
        if x.kind == 'phi':
            stack.extend(x.args)
        elif x.kind == 'const':
            if const is not None and const.key != x.key:
                return None
            const = x
        elif not (x.kind == 'blockin' and x.key[0] in f.orphans):
            return None
    return const


def resolve_jump_tables(proj, f, df, max_entries=4096):
    """Resolve indirect jumps angr could not (e.g. CPython's computed-goto dispatch
    `jmp *opcode_targets[op]`) using the dataflow's symbolic target expression:
      (A) target = Load(table + idx*8)                      -> absolute pointer table
      (B) target = base + sext(Load(table + idx*4))          -> gcc relative jump table
    Entries are read from the loaded (relocated) image until one falls outside the function.
    Returns the number of new edges; splits blocks so every target starts a block."""
    from nodes import walk
    new_edges = 0
    lo, hi = f.start, f.end
    for b in sorted(f.unresolved):
        br = df.results.get(b)
        if br is None or br.next_node is None:
            continue
        n = df.resolve(br.next_node)
        table = None; esz = 8; base = None; load = None
        if n.kind == 'load':
            load = n
        elif n.kind == 'op' and n.key == 'Add64' and len(n.args) == 2:
            a0, a1 = df.resolve(n.args[0]), df.resolve(n.args[1])
            if a0.kind == 'const':
                a0, a1 = a1, a0
            if a1.kind == 'phi' and a0.kind != 'phi':
                # relative table whose base register (`lea table(%rip)' before a loop) is a loop phi
                # whose other operands are BlockIn values of orphan blocks (the table's own unresolved
                # targets): one constant + only orphan BlockIns = that constant (optimistic fixpoint;
                # the targets read with it are still bounds- and boundary-checked below).
                c = _phi_single_const(df, a1, f)
                if c is not None:
                    a1 = c
            if a1.kind == 'const':
                base = a1.key
                x = a0
                while x.kind == 'op' and x.key in ('32Sto64', '32Uto64', '64to32', '16Sto64', '8Sto64', '8Uto64', '16Uto64'):
                    x = df.resolve(x.args[0])
                if x.kind == 'load':
                    load = x; esz = x.meta['size']
        if load is None:
            continue
        acc = next((a for a in br.accesses if a.addr == load.meta['addr'] and a.seq == load.key[2]), None)
        if acc is None:
            continue
        # find the table constant in the address expression; the table base may also have been
        # spilled to a stack slot and reloaded (orphan blocks cannot forward it), so look at what
        # the function stores into that slot
        table = _find_table_const(proj, df, acc.node)
        if table is None:
            continue
        targets = []
        for i in range(max_entries):
            try:
                v = proj.loader.memory.unpack_word(table + i * esz, size=esz)
            except Exception:
                break
            if base is not None:
                if v >= 1 << (esz * 8 - 1):
                    v -= 1 << (esz * 8)
                t = (base + v) & 0xFFFFFFFFFFFFFFFF
            else:
                t = v
            if not _in_text(proj, t):
                break
            if lo <= t < hi:
                # only instruction boundaries of the linear sweep: a table read past its true end
                # yields in-range garbage that would split blocks mid-instruction
                if hasattr(f, 'insn_addrs') and t not in f.insn_addrs and not getattr(f, 'descent', False):
                    break
                targets.append(t)
        if not targets:
            continue
        for t in set(targets):
            if t not in f.blocks and not _split_block(proj, f, t):
                if getattr(f, 'descent', False):
                    # descent mode: a table target may start bytes not yet decoded
                    try:
                        irsb = proj.factory.block(t, size=f.end - t, opt_level=0).vex
                    except Exception:
                        continue
                    later = [b for b in f.blocks if t < b < t + irsb.size]
                    if later:
                        irsb = proj.factory.block(t, size=min(later) - t, opt_level=0).vex
                    f.blocks[t] = irsb
                    f.preds.setdefault(t, set()); f.succs.setdefault(t, set())
                    for st in irsb.statements:
                        if st.tag == 'Ist_IMark':
                            f.insn_addrs.add(st.addr)
                    nk, nv = irsb_next(irsb)
                    if irsb.jumpkind == 'Ijk_Boring' and nk == 'const' and f.start <= nv < f.end and nv in f.blocks:
                        f.succs[t].add(nv); f.preds[nv].add(t)
                    for st in irsb.statements:
                        if st.tag == 'Ist_Exit' and f.start <= st.dst.value < f.end and st.dst.value in f.blocks:
                            f.succs[t].add(st.dst.value); f.preds[st.dst.value].add(t)
                else:
                    continue
            f.succs[b].add(t)
            f.preds.setdefault(t, set()).add(b)
            new_edges += 1
        f.unresolved.discard(b)
    if new_edges:
        _recompute_reach(f)
    return new_edges


def _in_data(proj, addr):
    mo = proj.loader.main_object
    if not (mo.min_addr <= addr < mo.max_addr):
        return False
    sec = mo.find_section_containing(addr)
    if sec is None:
        return not getattr(mo, 'sections', None) and True   # raw blob: everything is readable data too
    return not sec.is_executable


def _split_block(proj, f, t):
    """Make `t` a block start by splitting the block containing it."""
    for a in sorted(f.blocks):
        irsb = f.blocks[a]
        if a < t < a + irsb.size:
            try:
                head = proj.factory.block(a, size=t - a, opt_level=0).vex
                tail = proj.factory.block(t, size=a + irsb.size - t, opt_level=0).vex
            except Exception:
                return False
            f.blocks[a] = head
            f.blocks[t] = tail
            f.succs[t] = f.succs[a]
            f.succs[a] = {t}
            f.preds[t] = {a}
            for sx in f.succs[t]:
                f.preds[sx].discard(a); f.preds[sx].add(t)
            if a in f.call_return_sites:
                f.call_return_sites[t] = f.call_return_sites.pop(a)
            if a in f.unresolved:
                f.unresolved.discard(a); f.unresolved.add(t)
            return True
    return False


def _recompute_reach(f):
    seen, stack = set(), [f.entry] if f.entry in f.blocks else []
    while stack:
        a = stack.pop()
        if a in seen:
            continue
        seen.add(a)
        stack.extend(f.succs[a])
    f.orphans = set(f.blocks) - seen
    f.loop_depth = loop_depths(f)


def _in_text(proj, addr):
    mo = proj.loader.main_object
    if not (mo.min_addr <= addr < mo.max_addr):
        return False
    sec = mo.find_section_containing(addr)
    if sec is None:
        return not getattr(mo, 'sections', None) and True   # raw blob
    return sec.is_executable


def _find_table_const(proj, df, addr_node, depth=0):
    from nodes import walk
    for x in walk(df.resolve(addr_node)):
        if x.kind == 'const' and _in_data(proj, x.key):
            return x.key
    if depth > 1:
        return None
    # loads of stack slots in the expression: what does the function store there?
    for x in walk(df.resolve(addr_node)):
        if x.kind == 'load':
            br = df.results.get(next((b for b, r in df.results.items() if x.meta['addr'] in r.insns), None))
            if br is None:
                continue
            acc = next((a for a in br.accesses if a.addr == x.meta['addr'] and a.seq == x.key[2]), None)
            if acc is None:
                continue
            an = df.resolve(acc.node)
            fo = df.frame_offset(an)
            if fo is None:
                # rsp unresolved here: match the raw rsp displacement against raw rsp-relative
                # stores anywhere in the function (candidate only; table entries are validated)
                from dataflow import raw_rsp_disp
                k = raw_rsp_disp(an)
                if k is not None:
                    for r in df.results.values():
                        v = r.raw_rsp_writes.get((k, x.meta['size']))
                        if v is not None:
                            v = df.resolve(v)
                            if v.kind == 'const' and _in_data(proj, v.key):
                                return v.key
                continue
            for r in df.results.values():
                v = r.slot_writes.get((fo, x.meta['size']))
                if v is not None:
                    v = df.resolve(v)
                    if v.kind == 'const' and _in_data(proj, v.key):
                        return v.key
    return None

