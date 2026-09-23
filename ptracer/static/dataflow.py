"""
Dataflow: builds the value graph (nodes.py) of one function from its VEX blocks and records
every memory access with its address expression.

Method (the paper's dependency-graph construction, generalised):
  * Each basic block is interpreted ONCE, symbolically, over the value graph. Reads of a
    location (register / stack-frame slot) that the block has not defined yet become a Phi node
    of that block (lazy SSA construction); Phi operands are resolved after all blocks are done,
    so no fixpoint iteration is needed and loops are handled exactly (a loop-carried value is a
    Phi whose operands include a value computed from itself).
  * Leaves: function-entry registers, loaded values, pseudo-loads after calls (caller-saved
    registers), block-entry registers of blocks with no predecessors (region / unresolved
    indirect targets), opaque values (flag-dependent, helper calls, unsupported ops).
  * Stack frame slots addressed as entry-rsp + constant are tracked as locations, so spills and
    reloads are forwarded (a reload becomes recomputable from the stored value); if the frame
    address escapes (is taken), calls kill the slots.
  * Every Load/Store/CAS/... yields an Access(addr_node); `rep` string instructions also require
    the iteration count (rcx) since Intel PT does not record their iterations.
  * Loggability: whenever a value is written to a full 64-bit GP register (or an xmm half) the
    node gets a site (insn_addr, 'after', reg); Phi(block, reg) gets (block, 'before', reg);
    Entry/Pseudo/BlockIn likewise. hitset.py turns this into the critical value set.
"""
from __future__ import annotations
import pyvex, archinfo
from nodes import Graph, Node, walk

ARCH = archinfo.ArchAMD64()
GP = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15']
CALLER_SAVED = ['rax', 'rcx', 'rdx', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11'] + [f'xmm{i}{h}' for i in range(16) for h in 'lh']
SYSCALL_CLOBBER = ['rax', 'rcx', 'r11']

# offset -> (reg, sub_offset) tables
_GP_OFF = {ARCH.registers[r][0]: r for r in GP}
_XMM_BASE = ARCH.registers['xmm0'][0]
_FS = ARCH.registers['fs_const'][0]
_GS = ARCH.registers['gs_const'][0]
_D = ARCH.registers['d'][0]
_CC = {ARCH.registers[r][0] for r in ('cc_op', 'cc_dep1', 'cc_dep2', 'cc_ndep')}
_RIP = ARCH.registers['rip'][0]


def loggable_loc(loc):
    """Locations the runtime can log: 64-bit GP registers, xmm halves, the TLS base (fs)."""
    return loc in GP or loc.startswith('xmm') or loc == 'fs'


def loc_of(offset, size):
    """Map a VEX guest offset/size to (location, sub_offset_in_bytes, is_full). Location names:
    GP 64-bit names, 'xmmNl'/'xmmNh' (64-bit halves), 'fs', 'gs', 'd', 'cc', None (ignored)."""
    for base, r in _GP_OFF.items():
        if base <= offset < base + 8:
            return r, offset - base, (offset == base and size == 8)
    if _XMM_BASE <= offset < _XMM_BASE + 16 * 32:
        i, rem = divmod(offset - _XMM_BASE, 32)
        if rem < 16:
            half = 'l' if rem < 8 else 'h'
            return f'xmm{i}{half}', rem % 8, (rem % 8 == 0 and size == 8)
        return None, 0, False      # ymm high half: ignored
    if offset == _FS:
        return 'fs', 0, True
    if offset == _GS:
        return 'gs', 0, True
    if offset == _D:
        return 'd', 0, True
    if offset in _CC:
        return 'cc', 0, True
    return None, 0, False


_TY_BITS = {'Ity_I1': 1, 'Ity_I8': 8, 'Ity_I16': 16, 'Ity_I32': 32, 'Ity_I64': 64, 'Ity_I128': 128,
            'Ity_F32': 32, 'Ity_F64': 64, 'Ity_F128': 128, 'Ity_V128': 128, 'Ity_V256': 256, 'Ity_D64': 64}


def ty_bits(ty):
    return _TY_BITS.get(ty, 64)


class Access:
    __slots__ = ('addr', 'seq', 'kind', 'size', 'node', 'block', 'cond')

    def __init__(self, addr, seq, kind, size, node, block, cond=False):
        self.addr, self.seq, self.kind, self.size, self.node, self.block, self.cond = addr, seq, kind, size, node, block, cond

    def __repr__(self):
        return f"Access({hex(self.addr)}#{self.seq} {self.kind}{self.size} {self.node!r})"


class BlockResult:
    __slots__ = ('addr', 'out', 'slot_writes', 'slot_kill_all', 'reads', 'accesses', 'ends_call', 'insns', 'next_node', 'raw_rsp_writes', 'insn_flags', 'insn_regs')

    def __init__(self, addr):
        self.addr = addr
        self.out = {}            # loc -> node (locations defined in this block)
        self.slot_writes = {}    # (fo,size) -> node
        self.slot_kill_all = False
        self.reads = set()       # locs read before defined (=> phi requests)
        self.accesses = []
        self.ends_call = False
        self.insns = []          # instruction addresses
        self.next_node = None    # value node of an indirect jump target
        self.raw_rsp_writes = {} # (disp, size) -> value for stores at rsp_in + disp when rsp_in is unresolved
        self.insn_flags = {}     # insn addr -> (reads_flags, writes_flags) for flag liveness at sites
        self.insn_regs = {}      # insn addr -> (GP regs read before written, GP regs fully written)


class FunctionDataflow:
    def __init__(self, cfg, graph=None, memop_check=None):
        """cfg: cfgrec.FuncCFG (blocks: addr->irsb, preds, succs, entry, call_return_sites, loop_depth).
        memop_check(addr) -> bool: does the instruction have an explicit memory operand the runtime can
        re-read (a `memop` site)? None = assume yes."""
        self.cfg = cfg
        self.memop_check = memop_check
        self.g = graph or Graph()
        self.results = {}
        self.accesses = []
        self.frame_escapes = False
        # Per-offset frame escape: the constant frame offsets whose
        # address is taken (a pointer into the frame handed to a register or memory, hence possibly
        # to a callee). A callee can only reach the object each such pointer names and objects grow
        # UP in address, so a slot strictly below every escaped offset is provably not written by
        # any callee -- its value survives a call and the reconstructor's stack shadow serves the
        # reload. `escape_floor` = min escaped offset; slots at offset < floor are not killed by a
        # call. This refines the whole-function `frame_escapes` boolean, which killed ALL slots.
        self.escaped_offsets = set()
        self._escape_unknown = False   # an rsp-derived pointer with a non-constant offset escaped:
                                       # the callee could reach any slot, so the floor is disabled.
        self._floor_ready = False
        self._phi_memo = {}
        self.rep_counts = []     # (insn_addr, node)

    # ------------------------------------------------------------------ helpers
    def frame_offset(self, n):
        """If n == Entry(rsp) + c (or Entry(rsp)), return c else None."""
        if n.kind == 'entry' and n.key == 'rsp':
            return 0
        if n.kind == 'op' and n.key == 'Add64' and len(n.args) == 2:
            a, b = n.args
            if a.kind == 'entry' and a.key == 'rsp' and b.kind == 'const':
                return b.key if b.key < (1 << 63) else b.key - (1 << 64)
        return None

    def _rsp_derived(self, n, cap=128):
        """True if `n` is built from Entry(rsp) (a pointer into this frame). Bounded so it stays cheap
        even on large value graphs; a cap hit is treated as derived (conservative)."""
        seen, stack, k = set(), [n], 0
        while stack:
            m = stack.pop()
            if m.id in seen:
                continue
            seen.add(m.id)
            k += 1
            if k > cap:
                return True
            if m.kind == 'entry' and m.key == 'rsp':
                return True
            stack.extend(m.args)
        return False

    def _note_escape(self, v):
        """A value that is a pointer into this frame is being handed out (to a register or memory,
        hence possibly to a callee). Record the constant offset; if the offset is not constant but
        the pointer is still frame-derived, disable the per-slot floor (kill all on calls)."""
        fo = self.frame_offset(v)
        if fo is not None:
            self.frame_escapes = True
            self.escaped_offsets.add(fo)
        elif self._rsp_derived(v):
            # frame-derived but the offset is not constant: only relevant when the per-slot floor
            # would otherwise engage (a constant escape also present). Do NOT set frame_escapes here,
            # so a function with no constant escape keeps its original (unchanged) behaviour.
            self._escape_unknown = True

    def run(self):
        """Interpret every block once (phis stay unresolved until finalize()). Then, because the
        stack pointer at a block entry is almost always entry_rsp + constant, resolve the rsp phis
        and re-interpret blocks with a constant rsp offset seeded, so that frame-slot stores and
        reloads are forwarded across blocks (the paper's stack-spill handling)."""
        self.rsp_seed = {}
        for addr in sorted(self.cfg.blocks):
            self.results[addr] = self._run_block(addr, self.cfg.blocks[addr])
        self.finalize()
        seeds = {}
        for addr in self.cfg.blocks:
            if addr == self.cfg.entry:
                continue
            v = self.resolve(self._in_value(addr, 'rsp'))
            if self.frame_offset(v) is not None:
                seeds[addr] = v
        if seeds:
            self.rsp_seed = seeds
            self.g = Graph()            # rebuild the graph from scratch with the seeds
            self.results, self._phi_memo, self.rep_counts = {}, {}, []
            self.frame_escapes = False
            # keep escaped_offsets from pass 1 so the floor is complete from the first block of the
            # seeded pass (pass 2 re-adds the same offsets); only now is per-slot forwarding safe.
            self._floor_ready = True
            for addr in sorted(self.cfg.blocks):
                self.results[addr] = self._run_block(addr, self.cfg.blocks[addr])
            self.finalize()
        return self

    def _out_value(self, baddr, loc):
        """Value of `loc` at the END of block baddr (creating phis in that block on demand)."""
        key = (baddr, loc)
        if key in self._phi_memo:
            return self._phi_memo[key]
        br = self.results[baddr]
        if isinstance(loc, tuple):   # frame slot
            if loc in br.slot_writes:
                v = br.slot_writes[loc]
            elif self._call_kills(br, loc) or self._slot_overlaps(br, loc):
                v = self.g.unknown
            else:
                v = self._in_value(baddr, loc)
        else:
            if loc in br.out:
                v = br.out[loc]
            else:
                v = self._in_value(baddr, loc)
        self._phi_memo[key] = v
        return v

    def _call_kills(self, br, loc):
        """Does a call in block `br` kill the frame slot `loc` = (offset, size)?  A hard kill
        (conditional/atomic store to a frame slot with a possibly-unknown address) always kills.
        A plain `call` kills only slots the callee could reach: offsets >= the lowest escaped
        frame offset (escaped objects grow upward in address).  When the floor is not yet known
        (pass 1, or a function with no seeded pass) fall back to the original full kill."""
        ka = br.slot_kill_all
        if ka is True:
            return True
        if ka == 'call':
            if not self._floor_ready or self._escape_unknown:
                return True
            if not self.escaped_offsets:
                return False
            return loc[0] >= min(self.escaped_offsets)
        return False

    def _slot_overlaps(self, br, loc):
        fo, size = loc
        for (f2, s2) in br.slot_writes:
            if f2 < fo + size and fo < f2 + s2 and (f2, s2) != loc:
                return True
        return False

    def _in_value(self, baddr, loc):
        """Value of loc at the START of block baddr."""
        if baddr == self.cfg.entry:
            if isinstance(loc, tuple):
                return self.g.unknown     # caller's frame / uninitialised
            n = self.g.entry(loc)
            if not n.sites and loggable_loc(loc):
                n.sites.append((self.cfg.entry, 'before', loc))
            return n
        preds = self.cfg.preds.get(baddr, ())
        if not preds:
            if isinstance(loc, tuple):
                return self.g.unknown
            n = self.g.blockin(baddr, loc)
            if loggable_loc(loc):
                n.sites.append((baddr, 'before', loc))
            return n
        phi = self.g.phi(baddr, loc)
        if not isinstance(loc, tuple) and not phi.sites and loggable_loc(loc):
            phi.sites.append((baddr, 'before', loc))
        return phi

    # ------------------------------------------------------------------ block interpretation
    def _run_block(self, baddr, irsb):
        g = self.g
        br = BlockResult(baddr)
        state = {}        # loc -> node (defined in this block)
        slots = {}        # (fo,size) -> node
        tmps = {}
        loadcache = {}    # (address node id, size) -> Load node, cleared at every store / call
        cur = [baddr, 0]  # current insn addr, seq counter within insn
        last_rip = [None] # value of the last non-constant PUT(rip) (indirect jump target)
        fl = [False, False]   # this instruction reads / writes the arithmetic flags
        rr = [set(), set()]   # GP registers this instruction reads (before writing) / fully writes
        flagsop = g.opaque(baddr, None, 'flags')   # placeholder node for "the current flags"

        def uses_flags(v):
            return any(x.kind == 'opaque' and x.key[2] == 'flags' for x in walk(v))
        depth = self.cfg.loop_depth.get(baddr, 0)

        seed = self.rsp_seed.get(baddr)
        if seed is not None:
            state['rsp'] = self.g.op('Add64', [self.g.entry('rsp'), self.g.const(self.frame_offset(seed))]) if self.frame_offset(seed) else self.g.entry('rsp')
            if not self.g.entry('rsp').sites:
                self.g.entry('rsp').sites.append((self.cfg.entry, 'before', 'rsp'))

        def read(loc):
            if loc in GP and loc not in rr[1]:
                rr[0].add(loc)
            if loc in state:
                return state[loc]
            if loc == 'cc':
                return flagsop
            br.reads.add(loc)
            return self._in_value(baddr, loc)

        def read_slot(fo, size):
            k = (fo, size)
            if k in slots:
                return slots[k]
            for (f2, s2) in slots:      # partially overlapping earlier write in this block
                if f2 < fo + size and fo < f2 + s2:
                    return g.unknown
            if self._call_kills(br, (fo, size)):
                return g.unknown
            br.reads.add(k)
            return self._in_value(baddr, k)

        def write_slot(fo, size, v):
            for k2 in [k for k in slots if k[0] < fo + size and fo < k[0] + k[1]]:
                del slots[k2]
            slots[(fo, size)] = v
            br.slot_writes[(fo, size)] = v

        def put(loc, sub, full, v, bits):
            if loc is None or loc == 'cc':
                return
            if loc in GP and full:
                rr[1].add(loc)
            if full:
                state[loc] = v
            else:
                old = read(loc)
                state[loc] = g.op(f'merge{bits}@{sub}', [old, v])
            if loggable_loc(loc):
                state[loc].sites.append((cur[0], 'after', loc))
            if loc not in ('rsp', 'rbp'):
                self._note_escape(v)
            br.out[loc] = state[loc]

        def ev(e):
            t = e.tag
            if t == 'Iex_RdTmp':
                return tmps.get(e.tmp, g.unknown)
            if t == 'Iex_Const':
                return g.const(e.con.value, ty_bits(e.con.type))
            if t == 'Iex_Get':
                loc, sub, full = loc_of(e.offset, ty_bits(e.ty) // 8)
                if loc is None:
                    return g.opaque(cur[0], None, f'get{e.offset}')
                v = read(loc)
                if not full:
                    v = g.op(f'sub{ty_bits(e.ty)}@{sub}', [v], bits=ty_bits(e.ty))
                return v
            if t == 'Iex_Load':
                a = ev(e.addr)
                size = ty_bits(e.ty) // 8
                cur[1] += 1
                fo = self.frame_offset(a)
                fwd = read_slot(fo, size) if fo is not None else None
                if fwd is None or fwd.kind == 'unknown':
                    # same address loaded earlier in this block with no store/call since: the
                    # value is the same (forward it, so logging it again is optional)
                    prev = loadcache.get((a.id, size))
                    if prev is not None:
                        fwd = prev
                n = g.load(cur[0], size, cur[1], fwd if (fwd is not None and fwd.kind != 'unknown') else None)
                if fo is None:
                    loadcache[(a.id, size)] = n
                if self.memop_check is None or self.memop_check(cur[0]):
                    n.sites.append((cur[0], 'before', 'memop'))
                acc = Access(cur[0], cur[1], 'L', size, a, baddr)
                br.accesses.append(acc)
                return n
            if t in ('Iex_Binop', 'Iex_Unop', 'Iex_Triop', 'Iex_Qop'):
                name = e.op[4:] if e.op.startswith('Iop_') else e.op
                args = [ev(a) for a in e.args]
                bits = _result_bits(name, args)
                if any(a.kind == 'opaque' and a.key[1] is None for a in args):
                    # flag/opaque-dependent arithmetic: keep as op; loggable when PUT
                    pass
                return g.op(name, args, bits=bits)
            if t == 'Iex_ITE':
                c, a, b = ev(e.cond), ev(e.iftrue), ev(e.iffalse)
                if a is b:
                    return a
                if c.kind == 'const':
                    return a if c.key else b
                return g.op('ITE', [c, a, b], bits=a.bits)
            if t == 'Iex_CCall':
                if 'calculate_condition' in e.callee.name or 'rflags' in e.callee.name:
                    fl[0] = True
                    for a_ in e.args:
                        ev(a_)
                return g.opaque(cur[0], None, 'ccall:' + e.callee.name)
            if t == 'Iex_GetI':
                return g.opaque(cur[0], None, 'geti')
            return g.opaque(cur[0], None, 'expr:' + t)

        for st in irsb.statements:
            tag = st.tag
            if tag == 'Ist_IMark':
                if br.insns:
                    br.insn_flags[br.insns[-1]] = (fl[0], fl[1])
                    br.insn_regs[br.insns[-1]] = (set(rr[0]), set(rr[1]))
                fl[0] = fl[1] = False
                rr[0].clear(); rr[1].clear()
                cur[0], cur[1] = st.addr, 0
                br.insns.append(st.addr)
            elif tag == 'Ist_WrTmp':
                tmps[st.tmp] = ev(st.data)
            elif tag == 'Ist_Put':
                if st.offset == _RIP:
                    last_rip[0] = ev(st.data) if st.data.tag != 'Iex_Const' else None
                    continue
                v = ev(st.data)
                bits = ty_bits(st.data.result_type(irsb.tyenv)) if hasattr(st.data, 'result_type') else 64
                loc, sub, full = loc_of(st.offset, bits // 8)
                if loc == 'cc':
                    if st.offset == ARCH.registers['cc_op'][0]:
                        fl[1] = True
                    if uses_flags(v):          # e.g. shift by a non-constant count keeps the old flags
                        fl[0] = True
                    continue
                if uses_flags(v):
                    fl[0] = True
                put(loc, sub, full, v, bits)
            elif tag == 'Ist_Store':
                loadcache.clear()
                a = ev(st.addr)
                v = ev(st.data)
                if uses_flags(v):
                    fl[0] = True
                size = ty_bits(st.data.result_type(irsb.tyenv)) // 8
                cur[1] += 1
                br.accesses.append(Access(cur[0], cur[1], 'S', size, a, baddr))
                fo = self.frame_offset(a)
                if fo is not None:
                    write_slot(fo, size, v)
                else:
                    k = raw_rsp_disp(a)
                    if k is not None:
                        br.raw_rsp_writes[(k, size)] = v
                self._note_escape(v)
            elif tag == 'Ist_StoreG':
                loadcache.clear()
                a = ev(st.addr)
                v = ev(st.data)
                size = ty_bits(st.data.result_type(irsb.tyenv)) // 8
                cur[1] += 1
                br.accesses.append(Access(cur[0], cur[1], 'S', size, a, baddr, cond=True))
                fo = self.frame_offset(a)
                if fo is not None:
                    br.slot_kill_all = True
            elif tag == 'Ist_LoadG':
                a = ev(st.addr)
                size = ty_bits(_loadg_type(st.cvt)) // 8
                cur[1] += 1
                br.accesses.append(Access(cur[0], cur[1], 'L', size, a, baddr, cond=True))
                n = g.load(cur[0], size, cur[1])
                tmps[st.dst] = g.op('LoadG', [n, ev(st.alt)])
            elif tag == 'Ist_CAS':
                loadcache.clear()
                a = ev(st.addr)
                size = ty_bits(st.dataLo.result_type(irsb.tyenv)) // 8
                cur[1] += 1
                br.accesses.append(Access(cur[0], cur[1], 'RMW', size * (2 if st.dataHi is not None else 1), a, baddr))
                n = g.load(cur[0], size, cur[1])
                tmps[st.oldLo] = n
                if st.oldHi is not None and st.oldHi != 0xFFFFFFFF:
                    cur[1] += 1
                    tmps[st.oldHi] = g.load(cur[0], size, cur[1])
                fo = self.frame_offset(a)
                if fo is not None:
                    br.slot_kill_all = True
            elif tag == 'Ist_LLSC':
                a = ev(st.addr)
                cur[1] += 1
                if st.storedata is None:
                    size = ty_bits(st.result_type(irsb.tyenv)) // 8 if hasattr(st, 'result_type') else 8
                    br.accesses.append(Access(cur[0], cur[1], 'L', size, a, baddr))
                    tmps[st.result] = g.load(cur[0], size, cur[1])
                else:
                    br.accesses.append(Access(cur[0], cur[1], 'S', 8, a, baddr))
                    tmps[st.result] = g.opaque(cur[0], None, 'llsc')
            elif tag == 'Ist_Dirty':
                loadcache.clear()
                if st.tmp is not None and st.tmp != 0xFFFFFFFF:
                    tmps[st.tmp] = g.opaque(cur[0], None, 'dirty:' + st.cee.name)
                if getattr(st, 'mFx', 'Ifx_None') not in (None, 'Ifx_None') and st.mAddr is not None:
                    a = ev(st.mAddr)
                    cur[1] += 1
                    k = {'Ifx_Read': 'L', 'Ifx_Write': 'S', 'Ifx_Modify': 'RMW'}.get(st.mFx, 'RMW')
                    br.accesses.append(Access(cur[0], cur[1], k, st.mSize, a, baddr))
                    fo = self.frame_offset(a)
                    if fo is not None and k != 'L':
                        br.slot_kill_all = True
            elif tag == 'Ist_PutI':
                pass
            elif tag == 'Ist_Exit':
                if uses_flags(ev(st.guard)):
                    fl[0] = True
            elif tag in ('Ist_NoOp', 'Ist_AbiHint', 'Ist_MBE'):
                pass

        if br.insns:
            br.insn_flags[br.insns[-1]] = (fl[0], fl[1])
            br.insn_regs[br.insns[-1]] = (set(rr[0]), set(rr[1]))
        # ---- block exit: keep the target expression of indirect jumps (jump-table resolution)
        jk = irsb.jumpkind
        br.next_node = None
        if irsb.next is not None and irsb.next.tag != 'Iex_Const':
            # unoptimised VEX: `PUT(rip)=X; t=GET(rip); NEXT: t` -> the target is the last PUT(rip)
            br.next_node = last_rip[0] if last_rip[0] is not None else ev(irsb.next)
        last_insn = br.insns[-1] if br.insns else baddr
        if jk == 'Ijk_Call':
            br.ends_call = True
            ret = self.cfg.call_return_sites.get(baddr)
            if ret is not None:
                # callee returns: rsp restored to pre-call value (VEX pushed the return address)
                if 'rsp' in state:
                    state['rsp'] = g.op('Add64', [state['rsp'], g.const(8)])
                    br.out['rsp'] = state['rsp']
                for r in CALLER_SAVED:
                    n = g.pseudo(ret, r)
                    n.sites.append((ret, 'before', r))
                    state[r] = n
                    br.out[r] = n
                # a callee may write escaped frame slots; finalize() clears this if the frame
                # address never escapes in this function
                br.slot_kill_all = 'call'
        elif jk == 'Ijk_Sys_syscall':
            for r in SYSCALL_CLOBBER:
                n = g.pseudo(last_insn + 2, r)
                n.sites.append((last_insn + 2, 'before', r))
                state[r] = n
                br.out[r] = n
        # rep-string self loop: iteration count required
        from cfgrec import irsb_next
        nk, nv = irsb_next(irsb)
        if nk == 'const' and nv == last_insn and jk == 'Ijk_Boring':
            n = self._rep_count_node(baddr, br, tmps, irsb)
            if n is not None:
                self.rep_counts.append((last_insn, n))
        return br

    def _rep_count_node(self, baddr, br, tmps, irsb):
        # find "t = GET(rcx)" in the block: the first read of rcx
        for st in irsb.statements:
            if st.tag == 'Ist_WrTmp' and st.data.tag == 'Iex_Get':
                loc, _, full = loc_of(st.data.offset, 8)
                if loc == 'rcx' and full:
                    return tmps.get(st.tmp)
        return None

    def finalize(self):
        """Second pass decisions that need whole-function facts (frame escape)."""
        if not self.frame_escapes:
            for br in self.results.values():
                if br.slot_kill_all == 'call':
                    br.slot_kill_all = False
        self._phi_memo.clear()
        # (re)resolve phis now that slot kills are settled; _out_value may create new phis, which
        # land on g.pending_phis (worklist)
        g = self.g
        for n in g.phis:
            n.set_args([])
        g.pending_phis = list(g.phis)
        while g.pending_phis:
            phi = g.pending_phis.pop()
            if phi.args:
                continue
            baddr, loc = phi.key
            phi.set_args([self._out_value(p, loc) for p in self.cfg.preds.get(baddr, ())])
        # collect accesses
        self.accesses = [a for br in self.results.values() for a in br.accesses]
        self.alias = trivial_phi_aliases(self.g)
        return self

    def resolve(self, n):
        while n.kind == 'phi' and n in self.alias:
            n = self.alias[n]
        return n


def _result_bits(name, args):
    import re
    m = re.search(r'(\d+)$', name)
    if name.startswith('Cmp') or name.endswith('to1'):
        return 1
    if m:
        return int(m.group(1))
    return args[0].bits if args else 64


def _loadg_type(cvt):
    return {'ILGop_Ident64': 'Ity_I64', 'ILGop_Ident32': 'Ity_I32', 'ILGop_16Uto32': 'Ity_I16', 'ILGop_16Sto32': 'Ity_I16',
            'ILGop_8Uto32': 'Ity_I8', 'ILGop_8Sto32': 'Ity_I8', 'ILGop_IdentV128': 'Ity_V128'}.get(cvt, 'Ity_I64')


def trivial_phi_aliases(g):
    """Redundant-phi elimination (Braun et al. 2013, SCC variant): consider the graph of phis whose
    operands are other phis; process its SCCs innermost-first; if all operands of an SCC that come
    from outside the SCC resolve to ONE value v, every phi in the SCC is an alias of v (a phi with no
    outside operand is Unknown). This catches loop-carried "same value" phis such as rsp."""
    alias = {}
    phis = g.phis
    if not phis:
        return alias
    import networkx as nx

    def res(n):
        while n.kind == 'phi' and n in alias:
            n = alias[n]
        return n

    G = nx.DiGraph()
    G.add_nodes_from(p.id for p in phis)
    byid = {p.id: p for p in phis}
    for p in phis:
        for a in p.args:
            if a.kind == 'phi':
                G.add_edge(p.id, a.id)
    cond = nx.condensation(G)
    for ci in reversed(list(nx.topological_sort(cond))):      # innermost (sink) SCCs first
        members = cond.nodes[ci]['members']
        # For an all-rsp SCC we must gather EVERY outside operand: the loop-carried rsp of a
        # computed-goto interpreter forms one huge SCC whose outside operands are many distinct
        # BlockIn(rsp) nodes (from unresolved-jump orphan regions) plus the single real frame-depth
        # value entry_rsp+c; an early break after two operands would never see the real value and
        # hide every [rsp+disp] spill/reload from the frame-slot forwarding. Only cheap for all-rsp
        # SCCs, so gate on that.
        mem_all_rsp = all(byid[pid].key[1] == 'rsp' for pid in members)
        outside = set()
        stop = False
        for pid in members:
            for a in byid[pid].args:
                r = res(a)
                if r.kind == 'phi' and r.id in members:
                    continue
                outside.add(r)
                if len(outside) > 1 and not mem_all_rsp:
                    stop = True
                    break
            if stop:
                break
        if len(outside) > 1 and all(byid[pid].key[1] == 'rsp' for pid in members):
            # ASSUMPTION: code regions we could not connect to the
            # entry (unresolved indirect jump targets) run at the same frame depth as the connected
            # code, so BlockIn(rsp) operands do not change the stack pointer value.
            rest = {o for o in outside if not (o.kind == 'blockin' and o.key[1] == 'rsp')}
            if len(rest) == 1:
                outside = rest
        if len(outside) == 1:
            v = next(iter(outside))
            for pid in members:
                alias[byid[pid]] = v
        elif len(outside) == 0 and any(byid[pid].args for pid in members):
            # a cycle of phis with no definition in view (an orphan loop region): the value enters
            # from code we could not connect -> log it at the region's first block (a BlockIn leaf)
            first = min(members, key=lambda pid: byid[pid].key[0])
            block, loc = byid[first].key
            if isinstance(loc, tuple):
                v = g.unknown
            else:
                v = g.blockin(block, loc)
                if not v.sites:
                    v.sites.append((block, 'before', loc))
            for pid in members:
                alias[byid[pid]] = v
    return alias


def raw_rsp_disp(n):
    """If n == X + k where X is an unresolved rsp value (phi/blockin of rsp), return k (signed)."""
    if n.kind in ('phi', 'blockin') and n.key[1] == 'rsp':
        return 0
    if n.kind == 'op' and n.key == 'Add64' and len(n.args) == 2 and n.args[1].kind == 'const':
        b = n.args[0]
        if b.kind in ('phi', 'blockin') and b.key[1] == 'rsp':
            k = n.args[1].key
            return k if k < (1 << 63) else k - (1 << 64)
    return None


def flags_liveness(cfg, results):
    """Backward liveness of the arithmetic flags. Returns a function dead(addr, when) -> True when a
    trampoline placed before (`when`='before') or after (`when`='after') the instruction at `addr`
    may clobber the flags (the next use along every path is a write). Conservative: unknown
    successors (indirect jumps, returns into callers) count as live; a call/syscall kills them."""
    live_in = {b: False for b in cfg.blocks}
    # block summary: first flag event when scanning forward
    first = {}
    for b, br in results.items():
        ev = None
        for a in br.insns:
            r, w = br.insn_flags.get(a, (False, False))
            if r:
                ev = 'read'; break
            if w:
                ev = 'write'; break
        first[b] = ev
    changed = True
    while changed:
        changed = False
        for b in cfg.blocks:
            br = results[b]
            jk = cfg.blocks[b].jumpkind
            if first[b] == 'read':
                v = True
            elif first[b] == 'write':
                v = False
            else:
                if jk in ('Ijk_Call', 'Ijk_Sys_syscall'):
                    v = False            # ABI: flags are not preserved across calls
                elif jk == 'Ijk_Ret' or not cfg.succs.get(b):
                    v = jk != 'Ijk_Ret' # flags are dead at a return; live if we do not know the successor
                else:
                    v = any(live_in[s2] for s2 in cfg.succs[b])
            if v != live_in[b]:
                live_in[b] = v; changed = True
    insn_block = {a: b for b, br in results.items() for a in br.insns}

    def dead(addr, when):
        b = insn_block.get(addr)
        if b is None:
            return False
        br = results[b]
        insns = br.insns
        i = insns.index(addr)
        start = i if when == 'before' else i + 1
        for a in insns[start:]:
            r, w = br.insn_flags.get(a, (False, False))
            if r:
                return False
            if w:
                return True
        jk = cfg.blocks[b].jumpkind
        if jk in ('Ijk_Call', 'Ijk_Sys_syscall'):
            return True
        if jk == 'Ijk_Ret':
            return True
        if not cfg.succs.get(b):
            return False
        return not any(live_in[s2] for s2 in cfg.succs[b])
    return dead


CALLER_SAVED_GP = ['rax', 'rcx', 'rdx', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11']


def reg_liveness(cfg, results):
    """Backward liveness of the 64-bit GP registers (excluding rsp, always live). Returns
    dead(addr, when) -> sorted list of registers a trampoline may clobber at that point. Conservative:
    unknown successors (indirect jumps, returns) -> all live except the caller-saved registers at a
    return (rax is the return value: live); at a call every register is live before it (arguments),
    and the caller-saved ones except rax are dead right after it (clobbered by the callee)."""
    ALL = set(GP) - {'rsp'}
    gen, kill = {}, {}
    for b, br in results.items():
        g, k = set(), set()
        for a in br.insns:
            r, w = br.insn_regs.get(a, (set(), set()))
            g |= (r - k)
            k |= w
        gen[b], kill[b] = g, k
    live_in = {b: set() for b in cfg.blocks}
    changed = True
    while changed:
        changed = False
        for b in cfg.blocks:
            jk = cfg.blocks[b].jumpkind
            if jk == 'Ijk_Ret':
                out = {'rax', 'rbx', 'rbp', 'r12', 'r13', 'r14', 'r15'}   # callee-saved must be intact, rax returned
            elif jk == 'Ijk_Call' or jk == 'Ijk_Sys_syscall':
                out = ALL                                            # arguments: everything may be read
            elif not cfg.succs.get(b):
                out = ALL
            else:
                out = set()
                for s2 in cfg.succs[b]:
                    out |= live_in[s2]
            new = gen[b] | (out - kill[b])
            if new != live_in[b]:
                live_in[b] = new; changed = True

    def dead(addr, when):
        b = next((bb for bb, br in results.items() if addr in br.insns), None)
        if b is None:
            return []
        br = results[b]
        insns = br.insns
        i = insns.index(addr)
        start = i if when == 'before' else i + 1
        jk = cfg.blocks[b].jumpkind
        if jk == 'Ijk_Ret':
            live = {'rax', 'rbx', 'rbp', 'r12', 'r13', 'r14', 'r15'}
        elif jk in ('Ijk_Call', 'Ijk_Sys_syscall') or not cfg.succs.get(b):
            live = set(ALL)
        else:
            live = set()
            for s2 in cfg.succs[b]:
                live |= live_in[s2]
        # walk backwards from the block end to the point
        for a in reversed(insns[start:]):
            r, w = br.insn_regs.get(a, (set(), set()))
            live = (live - w) | r
        return sorted(ALL - live)
    return dead
