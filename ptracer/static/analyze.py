#!/usr/bin/env python3
"""
PTracer static analysis: an ELF image -> spec v2 JSON.

  analyze.py IMAGE -o IMAGE.spec.json [--func NAME ...] [--jobs N] [--cache DIR] [--mode hifi|fast]

Per function (in parallel worker processes, results cached by a content hash of the function's
bytes): recover the local CFG (cfgrec.py), build the value graph and access list (dataflow.py),
solve the minimal critical value set (hitset.py), and translate chosen nodes into logging sites.
Function boundaries come from the ELF symbol tables, extended by direct-call / relocation /
IFUNC discovery for stripped images.

`--serve` runs the same pipeline as a warm service for JIT code objects (see serve()).
"""
from __future__ import annotations
import argparse, hashlib, json, os, sys, time, traceback
from concurrent.futures import ProcessPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logging
for n in ('angr', 'cle', 'pyvex', 'claripy'):
    logging.getLogger(n).setLevel('CRITICAL')

REG_SPEC_NAME = {'fs': 'fs_base', 'gs': 'gs_base'}
LOGGABLE = set(['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15', 'fs_base'] + ['xmm%d' % i for i in range(16)])
UNLOGGABLE = []   # (addr, when, reg) skipped by sites_from_choice; reported in the summary
ANALYZER_VERSION = 'v2.37'   # reported in the spec (`analyzer_version`)
# The per-function cache key carries CACHE_VERSION: bump it whenever a change alters the cached
# per-function output. KEYFRAME_RULE is appended to the key only for keyframed runs (`--keyframe K>0`)
# and is bumped when the keyframe/resync logged-set rule changes.
CACHE_VERSION = 'v2.38'
KEYFRAME_RULE = 'kf3'


def add_successor_sites(proj, df, roots, avoid):
    """Offer after(A) at before(B), only when B immediately follows A in one block.

    These are the same architectural boundary: no instruction or consumer is
    crossed. The solver, rather than a post-hoc relocation, chooses the new site;
    liveness is therefore recomputed there. Restrict this to forbidden original
    sites so ordinary placement and costs are unchanged. Never cross a control
    transfer, repeat instruction, block boundary or undecoded byte gap.
    """
    if not avoid:
        return
    successors = {a: b for br in df.results.values() for a, b in zip(br.insns, br.insns[1:])}
    safe = {}
    pending, seen = list(roots), set()
    while pending:
        node = df.resolve(pending.pop())
        if node.id in seen:
            continue
        seen.add(node.id)
        pending.extend(node.args)
        for addr, when, reg in list(node.sites):
            if when != 'after' or addr not in avoid or reg in (None, 'memop'):
                continue
            nxt = successors.get(addr)
            if nxt is None or nxt in avoid:
                continue
            if addr not in safe:
                try:
                    ins = proj.factory.block(addr, num_inst=1).capstone.insns[0]
                    # Capstone's generic jump/call/ret/int/iret groups are 1..5.
                    safe[addr] = (addr + ins.size == nxt and not set(ins.groups) & {1, 2, 3, 4, 5}
                                  and not ins.mnemonic.startswith(('rep', 'loop', 'sys')))
                except Exception:
                    safe[addr] = False
            candidate = (nxt, 'before', reg)
            if safe[addr] and candidate not in node.sites:
                node.sites.append(candidate)


def sites_from_choice(chosen, nodes, orphans, insn_block):
    """chosen: node id -> (addr, when, reg). Returns list of site dicts (unsorted, ids unassigned)."""
    regs_at = {}       # (addr, when) -> [regs]
    loads = []         # (addr, reg, size)
    memops = []        # (addr, size)
    for nid, (addr, when, reg) in chosen.items():
        n = nodes[nid]
        if addr == 'entry':
            continue
        if reg == 'memop':
            memops.append((addr, n.meta['size'] if n.meta else 8))
            continue
        r = reg
        if r.startswith('xmm') and r[-1] in 'lh':
            r = r[:-1]
        r = REG_SPEC_NAME.get(r, r)
        if r not in LOGGABLE:            # defensive: never emit a pseudo-register the runtime cannot log
            UNLOGGABLE.append((addr, when, r))
            continue
        if when == 'after' and n.kind == 'load':
            loads.append((addr, r, n.meta['size'] if n.meta else 8))
        else:
            regs_at.setdefault((addr, when), []).append(r)
    sites = []
    for (addr, when), regs in regs_at.items():
        regs = sorted(set(regs))
        sites.append({'addr': addr, 'when': when, 'kind': 'reg', 'regs': regs,
                      'orphan': insn_block.get(addr, addr) in orphans})
    seen = set()
    for addr, r, size in loads:
        if (addr, r) in seen:
            continue
        seen.add((addr, r))
        sites.append({'addr': addr, 'when': 'after', 'kind': 'load', 'reg': r, 'size': size,
                      'orphan': insn_block.get(addr, addr) in orphans})
    for addr, size in memops:
        sites.append({'addr': addr, 'when': 'before', 'kind': 'memop', 'size': size,
                      'orphan': insn_block.get(addr, addr) in orphans})
    return sites


def resync_sites(f, df, S, sites, K):
    """Bounded-loss re-anchoring after a PT overflow / decoder resync: at the back-edge targets (loop
    headers) of a function's OUTERMOST loops add a `reg` site marked resync=True that the runtime logs
    only every K-th execution (a keyframe: `dec counter; jnz skip`). Loss after a resync is bounded by
    K iterations.

    Registers listed:
    * live-in GP registers of the loop that lie on some address's dependency graph;
    * the GP registers LIVE ACROSS the loop -- carried unchanged from a definition before the loop
      to a use after it (e.g. an allocation result in `%rdi` that `free()` reads after a long loop):
      a chunk that restarts at a keyframe (parallel replay, or after a lost segment) never learns
      such a value unless the keyframe re-logs it;
    * `rsp`, so the tracked stack pointer is re-anchored too.

    across = used_after(region) - written_in(region)

    `used_after` is a backward liveness with ABI call semantics: a `call` READS the SysV
    integer-argument registers (rdi,rsi,rdx,rcx,r8,r9) and CLOBBERS the caller-saved ones; a `ret`
    ends the function (out = {}); an unknown indirect target keeps out = ALL. The conservative
    clobber-liveness (`reg_liveness`, out = ALL at a call) is deliberately NOT reused here: it would
    mark every register live at the call and log all 16.
    `- written_in(region)` is the soundness clause: a register the loop rewrites has a different
    value at the header than at the exit, so re-anchoring it to the header value would be WRONG.
    Missing a genuinely carried register only costs an `unknown`, never a wrong address."""
    import networkx as nx
    from dataflow import GP, CALLER_SAVED_GP
    ALL = set(GP) - {'rsp'}       # rsp is tracked/re-anchored separately
    ARG = {'rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9'}          # SysV integer argument registers
    SYSARG = ARG | {'rax', 'r10'}                            # + syscall number / 4th syscall arg
    CSAVE = set(CALLER_SAVED_GP)                             # clobbered across a call
    # ABI-aware backward liveness ("value used after this point"): la_in[b] = registers whose value on
    # entry to block b is read on some path before being overwritten, with calls modelled per the ABI.
    la_gen, la_kill = {}, {}
    for b, br in df.results.items():
        g, k = set(), set()
        for a in br.insns:
            r, w = br.insn_regs.get(a, (set(), set()))
            g |= (r - k); k |= w
        jk = f.blocks[b].jumpkind if b in f.blocks else None
        if br.ends_call or jk == 'Ijk_Call':
            g |= (ARG - k); k |= CSAVE            # the call reads args, clobbers caller-saved
        elif jk == 'Ijk_Sys_syscall':
            g |= (SYSARG - k); k |= {'rax', 'rcx', 'r11'}
        la_gen[b], la_kill[b] = g, k
    la_in = {b: set() for b in f.blocks}
    changed = True
    while changed:
        changed = False
        for b in f.blocks:
            br = df.results.get(b)
            jk = f.blocks[b].jumpkind
            if jk == 'Ijk_Ret':
                out = set()                        # leaving the function; the caller re-anchors its own
            elif f.succs.get(b):
                out = set()
                for s2 in f.succs[b]:
                    out |= la_in.get(s2, set())
            elif (br and br.ends_call) or jk in ('Ijk_Call', 'Ijk_Sys_syscall'):
                out = set()                        # NORETURN call (e.g. __assert_fail): nothing after it
            else:
                out = set(ALL)                     # unknown indirect target: conservative
            new = la_gen.get(b, set()) | (out - la_kill.get(b, set()))
            if new != la_in[b]:
                la_in[b] = new; changed = True
    G = nx.DiGraph(); G.add_nodes_from(f.blocks)
    for a, ss in f.succs.items():
        for b in ss:
            G.add_edge(a, b)
    # back edges by DFS from the entry and from every orphan root (a block with no predecessors:
    # OSR/deopt-entered JIT regions found by the restart fill have loops the entry never reaches)
    headers = set()
    roots = ([f.entry] if f.entry in f.blocks else []) + sorted(b for b in f.blocks if b != f.entry and not f.preds.get(b))
    color = {}
    for root in roots:
        if color.get(root, 0):
            continue
        stack = [(root, iter(sorted(f.succs.get(root, ()))))]
        color[root] = 1
        while stack:
            n, it = stack[-1]
            for m in it:
                if color.get(m, 0) == 1:
                    headers.add(m)
                elif color.get(m, 0) == 0:
                    color[m] = 1
                    stack.append((m, iter(sorted(f.succs.get(m, ())))))
                    break
            else:
                color[n] = 2
                stack.pop()
    have = {(st['addr'], st['when']): st for st in sites if st['kind'] == 'reg'}
    out = []
    for comp in nx.strongly_connected_components(G):
        if len(comp) == 1 and not G.has_edge(next(iter(comp)), next(iter(comp))):
            continue
        if min(f.loop_depth.get(b, 0) for b in comp) != 1:
            continue                      # outermost loops only
        reads = set()
        for b in comp:
            reads |= {loc for loc in df.results[b].reads if loc in GP}
        used_after = set()
        for u in comp:
            for w in f.succs.get(u, ()):
                if w not in comp and w in f.blocks:
                    used_after |= la_in.get(w, set())
        written_in = set()
        for b in comp:
            br = df.results[b]
            for a in br.insns:
                written_in |= br.insn_regs.get(a, (set(), set()))[1]
            if br.ends_call or (b in f.blocks and f.blocks[b].jumpkind == 'Ijk_Call'):
                written_in |= CSAVE            # an in-loop call clobbers caller-saved
        across = (used_after - written_in) & ALL   # carried unchanged across the loop, read after it
        # the outermost header(s) only: an inner-loop header would pay the counter on every
        # inner iteration, for no bounded-loss benefit.
        for b in sorted(h for h in headers & comp if f.loop_depth.get(h, 0) == 1):
            regs = []
            for r in sorted(reads):
                v = df.resolve(df._in_value(b, r))
                if v.kind in ('const', 'unknown') or v.id not in S.nodes:
                    continue              # not on any address's dependency graph
                regs.append(r)
            # live-across registers, restricted to those carrying a KNOWN value at the header: an
            # ABI-conservative arg register holding caller-saved garbage resolves to `unknown` and is
            # dropped. (S.nodes is NOT required: the use may be in a callee.)
            for r in sorted(across):
                v = df.resolve(df._in_value(b, r))
                if v.kind not in ('const', 'unknown'):
                    regs.append(r)
            regs.append('rsp')            # the stack pointer is tracked, not logged: re-anchor it too
            regs = sorted(set(regs))
            ex = have.get((b, 'before'))
            if ex is not None:
                regs = sorted(set(regs) - set(ex['regs']))
            if regs:
                out.append({'addr': b, 'when': 'before', 'kind': 'reg', 'regs': regs, 'orphan': b in f.orphans,
                            'resync': True, 'keyframe': K, 'note': 'resync'})
    return out


def move_off_orphan_padding(proj, df, sites):
    """An orphan block (reached only through an unresolved indirect jump) that the linear sweep
    started at alignment padding is entered at its first real instruction, so a `before' site on
    the padding never executes and the block's live-in values are never logged.  Move such a site
    to the first non-nop instruction of its block (nops change no register: same logged values)."""
    def is_nop(ins):
        return ins.mnemonic.endswith('nop') or (ins.mnemonic == 'xchg' and ins.op_str == 'ax, ax')
    insn_blk = {}
    for b, br in df.results.items():
        for i in br.insns:
            insn_blk[i] = b
    taken = {(st['addr'], st['when']) for st in sites}
    for st in sites:
        if not st.get('orphan') or st['when'] != 'before' or st['kind'] != 'reg':
            continue
        b = insn_blk.get(st['addr'])
        if b is None or b != st['addr']:
            continue
        try:
            insns = proj.factory.block(b, size=f_size(df, b)).capstone.insns
        except Exception:
            continue
        if not insns or not is_nop(insns[0]):
            continue
        own = set(df.results[b].insns)
        t = next((ins.address for ins in insns if ins.address in own and not is_nop(ins)), None)
        if t is None or (t, 'before') in taken:
            continue
        taken.discard((st['addr'], 'before')); taken.add((t, 'before'))
        st['moved_off_padding'] = st['addr']; st['addr'] = t
    return sites


def f_size(df, b):
    br = df.results[b]
    last = max(br.insns)
    return last - b + 16


def coalesce_fast(proj, df, sites):
    """Fast mode (one instrumentation location per basic block): merge a block's `load` sites into a
    single `reg` site.

    SOUNDNESS IN TIME: the reconstructor consumes logged values POSITIONALLY, in execution order, so a
    value logged at instruction p is only available to accesses at or after p. A merged site must
    therefore sit strictly after the load that defines the register and at or before the FIRST
    instruction that READS it. Loads whose feasible windows do not intersect are not merged together
    (they keep their own sites)."""
    import dataflow
    by_block = {}
    for st in sites:
        if st['kind'] == 'load':
            b = next((b for b, br in df.results.items() if st['addr'] in br.insns), None)
            if b is not None:
                by_block.setdefault(b, []).append(st)
    out = [st for st in sites if st['kind'] != 'load']
    for b, lst in by_block.items():
        insns = df.results[b].insns
        last = insns[-1]
        writes, reads = {}, {}
        for a in insns:
            try:
                irsb = proj.factory.block(a, num_inst=1, opt_level=0).vex
            except Exception:
                writes[a] = reads[a] = None
                continue
            w, r = set(), set()
            for x in irsb.statements:
                if x.tag == 'Ist_Put':
                    loc, _, _ = dataflow.loc_of(x.offset, 8)
                    if loc:
                        w.add(loc[:-1] if loc.startswith('xmm') else loc)
            try:
                for e in irsb.expressions:
                    if e.tag == 'Iex_Get':
                        loc, _, _ = dataflow.loc_of(e.offset, 8)
                        if loc:
                            r.add(loc[:-1] if loc.startswith('xmm') else loc)
            except Exception:
                r = None
            writes[a], reads[a] = w, r
        # feasible placement window per candidate load: (lo, hi) as indices into `insns`
        cands = []
        for st in lst:
            if st['addr'] == last or len(insns) < 2:
                out.append(st); continue
            i = insns.index(st['addr'])
            hi = len(insns) - 1                      # default: the block's last instruction
            bad = False
            for k in range(i + 1, len(insns)):
                a = insns[k]
                if reads[a] is None or writes[a] is None:
                    bad = True; break                # unliftable: do not move this site
                if st['reg'] in reads[a]:
                    hi = k; break                    # first consumer -- the value must exist by here
                if st['reg'] in writes[a]:
                    hi = k; break                    # overwritten before any read: log before that point
            if bad or hi <= i:
                out.append(st); continue
            cands.append((i + 1, hi, st))
        # group candidates whose windows intersect; one merged site per group at the latest feasible point
        cands.sort(key=lambda t: t[1])
        gi = 0
        while gi < len(cands):
            lo, hi, st0 = cands[gi]
            grp = [st0]; g_lo, g_hi = lo, hi
            gj = gi + 1
            while gj < len(cands):
                lo2, hi2, st2 = cands[gj]
                if max(g_lo, lo2) <= min(g_hi, hi2):
                    g_lo, g_hi = max(g_lo, lo2), min(g_hi, hi2)
                    grp.append(st2); gj += 1
                else:
                    break
            addr = insns[g_hi]
            regs = sorted({x['reg'] for x in grp})
            ex = next((st for st in out if st['kind'] == 'reg' and st['when'] == 'before' and st['addr'] == addr), None)
            if ex is not None:
                ex['regs'] = sorted(set(ex['regs']) | set(regs))
            else:
                out.append({'addr': addr, 'when': 'before', 'kind': 'reg', 'regs': regs,
                            'orphan': grp[0]['orphan'], 'note': 'fast-coalesced'})
            gi = gj
    return out


# --------------------------------------------------------------------------
# `--all-memops`: the ablation's "without PT and without static analysis" arm.
# --------------------------------------------------------------------------
# Instructions that are NOT a single plain data access, mirroring the rewriter's
# own rule for an instruction it can patch as a memory operand
# (`gtBadMnemonic`/`gtForm`/`getMem`, runtime/e9plugin/ptlog.cpp).  `movsd`/`cmpsd`
# are deliberately absent: the SSE scalar instructions share those mnemonics with
# the string ones, and the string forms are caught by the two-memory-operand rule.
_AM_NO_ACCESS = ('lea', 'nop', 'prefetch', 'clflush', 'clwb', 'clzero')
_AM_STRING = ('stos', 'lods', 'scas', 'xlat')
_AM_STRING_EXACT = {'cmpsb', 'cmpsw', 'cmpsq', 'movsb', 'movsw', 'movsq',
                    'insb', 'insw', 'insd', 'outsb', 'outsw', 'outsd'}
_AM_BULK = ('xsave', 'xrstor', 'fxsave', 'fxrstor')
_AM_STACK_EXACT = {'push', 'pushf', 'pushfd', 'pushfq', 'pusha', 'pushad',
                   'pop', 'popf', 'popfd', 'popfq', 'popa', 'popad',
                   'leave', 'enter'}
_AM_GP64 = set(['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi'] + ['r%d' % i for i in range(8, 16)])


def _memop_form(ins):
    """(size, None) if the runtime can log this instruction's memory operand as a
    `memop` site, else (None, reason).

    The rule is the rewriter's, not a new one: exactly one EXPLICIT memory operand,
    a GP base and index, no `%gs` override (the buffer sink owns %gs), and not a
    stack / string / bulk-save instruction (several accesses, or an implicit one the
    trampoline cannot re-address).  See `gtForm` in runtime/e9plugin/ptlog.cpp.
    """
    import capstone
    mn = ins.mnemonic.lower()
    if mn.startswith('rep'):                       # `rep stosq`, `repne scasb`
        return None, 'string'
    head = mn.split()[0] if ' ' in mn else mn
    if head.startswith(_AM_NO_ACCESS):
        return None, 'no-access'                   # memory OPERAND, no memory ACCESS
    if head.startswith(_AM_STRING) or head in _AM_STRING_EXACT:
        return None, 'string'
    if head.startswith(_AM_BULK):
        return None, 'bulk-save-restore'
    if head in _AM_STACK_EXACT:
        return None, 'stack'
    g = set(ins.groups)
    if g & {capstone.x86.X86_GRP_CALL, capstone.x86.X86_GRP_RET,
            capstone.x86.X86_GRP_INT, capstone.x86.X86_GRP_IRET}:
        return None, 'stack'
    mems = [op for op in ins.operands if op.type == capstone.x86.X86_OP_MEM]
    if not mems:
        return None, 'implicit-memory'             # an access with no operand to re-encode
    if len(mems) > 1:
        return None, 'two-memory-operands'         # order matters; two records, one site
    m = mems[0].mem
    raw = getattr(ins, 'insn', ins)
    seg = raw.reg_name(m.segment) if m.segment else None
    if seg == 'gs':
        return None, 'gs-segment'                  # the buffer sink's own segment
    base = raw.reg_name(m.base) if m.base else None
    index = raw.reg_name(m.index) if m.index else None
    if base == 'rip':
        if index:
            return None, 'rip-with-index'
    elif base is not None and base not in _AM_GP64:
        return None, 'non-gp-base'
    if index is not None and base != 'rip' and index not in _AM_GP64:
        return None, 'non-gp-index'                # e.g. a gather's vector index
    return mems[0].size, None


def _am_implicit_regs(ins, why):
    """The address register(s) of a stack or string access `_memop_form' cannot re-address, else None."""
    if why == 'stack':
        return ['rsp']
    if why == 'string':
        mn = ins.mnemonic.lower().split()[-1]
        if mn.startswith(('movs', 'cmps')):
            return ['rdi', 'rsi']
        if mn.startswith(('lods', 'outs')):
            return ['rsi']
        if mn.startswith('xlat'):
            return ['rbx']
        return ['rdi']                             # stos, scas, ins
    return None


def all_memop_sites(proj, df, insn_block, orphans, avoid):
    """One `memop` site per memory-accessing instruction; no hitting set, no reg sites.

    "Memory-accessing" is VEX's verdict -- the very access list the hitting set is
    normally built from -- and "loggable as a memop" is `_memop_form` above.  An
    instruction that accesses memory and fails that test is SKIPPED WITH A REASON and
    counted (never silently dropped).  Returns (sites, stats).
    """
    sites, skipped, implicit = [], {}, {}
    clamped = 0
    accessing = sorted({a.addr for a in df.accesses})
    for addr in accessing:
        if addr in avoid:
            skipped['unpatchable-avoid'] = skipped.get('unpatchable-avoid', 0) + 1
            continue
        try:
            ins = proj.factory.block(addr, num_inst=1).capstone.insns[0]
        except Exception:
            skipped['undecodable'] = skipped.get('undecodable', 0) + 1
            continue
        size, why = _memop_form(ins)
        if size is None:
            regs = _am_implicit_regs(ins, why)
            if regs:
                # An access with no single re-addressable operand (push/pop/call/ret/leave/enter, a string
                # instruction): log the ADDRESS REGISTER(S) the access goes through instead -- %rsp before a
                # stack access, %rsi/%rdi before a string one -- one record per register per execution.
                sites.append({'addr': addr, 'when': 'before', 'kind': 'reg', 'regs': regs,
                              'orphan': insn_block.get(addr, addr) in orphans})
                implicit[why] = implicit.get(why, 0) + 1
                continue
            skipped[why] = skipped.get(why, 0) + 1
            continue
        if size not in (1, 2, 4, 8):
            # The runtime logs 1/2/4/8 bytes of the operand (ptlog.cpp `emitLoadMem`).
            # A wider operand (a 16-byte SSE load, an 80-bit x87 one) still gets its
            # site and its one logged value -- the first 8 bytes of the same operand.
            size = 8
            clamped += 1
        sites.append({'addr': addr, 'when': 'before', 'kind': 'memop', 'size': size,
                      'orphan': insn_block.get(addr, addr) in orphans})
    return sites, {'accessing_insns': len(accessing), 'sites': len(sites),
                   'size_clamped': clamped, 'skipped': skipped,
                   'skipped_total': sum(skipped.values()), 'implicit': implicit}


def analyze_function(binary, name, start, end, mode='hifi', keyframe=0, avoid=None, roots=(), data_from=None, all_memops=False):
    """Worker: full pipeline for one function. Returns a JSON-able dict."""
    import angr, cfgrec, dataflow, hitset
    t0 = time.time()
    proj = _project(binary)
    mo = proj.loader.main_object
    delta = mo.mapped_base - mo.linked_base       # angr rebases PIE images; spec uses link-time vaddrs
    raw = binary in _RAW
    f = cfgrec.recover(proj, name, start + delta, end + delta, method='descent' if raw else 'linear',
                       roots=[r + delta for r in (roots or ())], fill=raw,
                       data_from=(data_from + delta) if (raw and data_from) else None)
    if not f.blocks:
        return {'name': name, 'start': start, 'end': end, 'error': 'no blocks', 'sites': []}
    def memop_check(addr, _cache={}):
        """explicit memory operand present (capstone) and not a string/implicit-memory instruction
        (VEX models e.g. `bt reg,reg` with a scratch-memory load that has no x86 memory operand)"""
        r = _cache.get(addr)
        if r is None:
            try:
                ins = proj.factory.block(addr, num_inst=1).capstone.insns[0]
                r = any(op.type == 3 for op in ins.operands) and not ins.mnemonic.startswith(('rep', 'movs', 'stos', 'lods', 'cmps', 'scas', 'push', 'pop', 'call', 'ret', 'leave', 'enter', 'xlat', 'fld', 'fst', 'fist', 'fild', 'fbld', 'fbst', 'fxsave', 'fxrstor', 'xsave', 'xrstor', 'cmpxchg16b', 'cmpxchg8b', 'bt'))
            except Exception:
                r = False
            _cache[addr] = r
        return r
    df = dataflow.FunctionDataflow(f, memop_check=memop_check).run()
    tables = 0
    for _ in range(4):          # recovered edges improve the dataflow, which resolves more tables
        if not f.unresolved:
            break
        n = cfgrec.resolve_jump_tables(proj, f, df)
        if not n:
            break
        tables += n
        df = dataflow.FunctionDataflow(f, memop_check=memop_check).run()
    insn_block = {i: b for b, br in df.results.items() for i in br.insns}
    if all_memops:
        # ablation arm: emit every memory-accessing instruction as a site and minimise nothing --
        # no solver, no keyframes, no resync sites, no reg/load sites, no rsp anchors (main() skips those too).
        sites, am = all_memop_sites(proj, df, insn_block, f.orphans,
                                    {a + delta for a in (avoid or ())})
        dead = dataflow.flags_liveness(f, df.results)
        rdead = dataflow.reg_liveness(f, df.results)
        for st in sites:
            st['flags_dead'] = bool(dead(st['addr'], st['when']))
            st['dead_regs'] = rdead(st['addr'], st['when'])[:6]
            st['addr'] -= delta
        return {
            'name': name, 'start': start, 'end': end, 'mode': mode, 'all_memops': am,
            'blocks': len(f.blocks), 'orphans': len(f.orphans),
            'unresolved': sorted(a - delta for a in f.unresolved),
            'coverage': round(f.coverage, 4), 'blocks_list': sorted(a - delta for a in f.blocks),
            'accesses': len(df.accesses), 'frame_escapes': df.frame_escapes,
            'jump_table_edges': tables,
            'undecodable': [hex(a - delta) for a in getattr(f, 'undecodable', [])],
            'sites': sites, 'has_calls': bool(f.call_return_sites),
            'n_values': len(sites),
            'time': round(time.time() - t0, 3),
        }
    cost = hitset.SiteCost(f.loop_depth, mode=mode, avoid=[a + delta for a in (avoid or ())])
    cost.set_insn_blocks(insn_block)
    rdead = dataflow.reg_liveness(f, df.results)
    roots = [a.node for a in df.accesses] + [n for _, n in df.rep_counts]
    add_successor_sites(proj, df, roots, {a + delta for a in (avoid or ())})
    S = hitset.Solver(roots, cost, df.resolve)
    chosen = _solve_big_stack(S)
    sites = sites_from_choice(chosen, S.nodes, f.orphans, insn_block)
    if mode == 'fast':
        sites = coalesce_fast(proj, df, sites)
    if keyframe:
        sites += resync_sites(f, df, S, sites, keyframe)
    sites = move_off_orphan_padding(proj, df, sites)
    dead = dataflow.flags_liveness(f, df.results)
    for st in sites:
        st['flags_dead'] = bool(dead(st['addr'], st['when']))
        st['dead_regs'] = rdead(st['addr'], st['when'])[:6]     # scratch registers the trampoline may use
    for st in sites:
        st['addr'] -= delta
        if 'moved_off_padding' in st: st['moved_off_padding'] -= delta
    unc = S.uncoverable
    unc_ids = {n.id for n in unc}
    # per direct call site: which argument registers are computable offline from this function's
    # chosen set at the call
    callsites = {}
    for baddr, br in df.results.items():
        if br.ends_call and baddr in f.plt_targets:
            covered = []
            for reg in ('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9'):
                v = df.resolve(br.out.get(reg) or df._in_value(baddr, reg))
                if S._covered(v, chosen) if v.id in S.nodes else _covered_outside(S, v, chosen):
                    covered.append(reg)
            tgt = f.plt_targets[baddr]
            try:
                tgt = hex(int(tgt, 16) - delta)
            except ValueError:
                pass
            callsites[hex(br.insns[-1] - delta)] = {'target': tgt, 'covered': covered}
    unc_sites = sorted({a.addr for a in df.accesses if df.resolve(a.node).id in unc_ids})
    return {
        'name': name, 'start': start, 'end': end, 'mode': mode,
        'blocks': len(f.blocks), 'orphans': len(f.orphans), 'unresolved': sorted(a - delta for a in f.unresolved),
        'coverage': round(f.coverage, 4), 'blocks_list': sorted(a - delta for a in f.blocks),
        'restart_roots': [a - delta for a in getattr(f, 'restart_roots', [])],
        'restart_evidence': [[a - delta, k, n] for a, k, n in getattr(f, 'restart_evidence', [])],
        'restart_rejected': [a - delta for a in getattr(f, 'restart_rejected', [])],
        'accesses': len(df.accesses), 'frame_escapes': df.frame_escapes,
        'rep_counts': [hex(a - delta) for a, _ in df.rep_counts],
        'jump_table_edges': tables,
        'undecodable': [hex(a - delta) for a in getattr(f, 'undecodable', [])],
        'sites': sites, 'uncoverable_accesses': [hex(a - delta) for a in unc_sites],
        'callsites': callsites,
        'has_calls': bool(f.call_return_sites),
        'n_values': sum(len(s.get('regs', [])) or 1 for s in sites),
        'drop_schedule': None, 'budget_totals': None,     # always null (spec v2 layout)
        'time': round(time.time() - t0, 3),
    }


_PROJ = {}


def _covered_outside(S, v, chosen):
    """Coverage check for a value that is not part of any access graph (e.g. a call argument)."""
    stack, seen = [v], set()
    while stack:
        n = stack.pop()
        if n.id in seen:
            continue
        seen.add(n.id)
        if n.id in chosen:
            continue
        if n.kind == 'const' or (n.kind == 'entry' and n.key in ('rsp', 'd', 'fs')):
            continue
        if n.is_leaf() and not (n.kind == 'load' and n.args):
            return False
        stack.extend(S.resolve(a) for a in n.args)
    return True


def _solve_big_stack(S):
    """The DP recurses along the SCC condensation; run it on a thread with a large stack."""
    import threading
    out = {}
    def go():
        out['r'] = S.solve()
    threading.stack_size(1 << 29)
    t = threading.Thread(target=go)
    t.start(); t.join()
    threading.stack_size(0)
    return out['r']


_RAW = {}     # binary path -> base address for raw code blobs (JIT code objects)


_RAW_MAX_CACHED = 8      # raw blobs (JIT code objects) are one-shot: keep only a few projects alive


def _project(binary):
    import angr
    p = _PROJ.get(binary)
    if p is None:
        if binary in _RAW:
            p = angr.Project(binary, auto_load_libs=False,
                             main_opts={'backend': 'blob', 'arch': 'amd64', 'base_addr': _RAW[binary], 'entry_point': _RAW[binary]})
            raw_cached = [k for k in _PROJ if k in _RAW]
            while len(raw_cached) >= _RAW_MAX_CACHED:
                old = raw_cached.pop(0)
                _PROJ.pop(old, None)
                try:
                    os.unlink(old)
                except OSError:
                    pass
                _RAW.pop(old, None)
        else:
            p = angr.Project(binary, auto_load_libs=False)
        _PROJ[binary] = p
    return p


def analyze_raw(data, base, name='jit', mode='hifi', ranges=None, avoid=None, keyframe=0, roots=(), data_from=None):
    """Analyze a raw code blob (a JIT code object): `data` bytes mapped at `base`. `ranges` optionally
    lists (name, start, end) functions inside the blob; default = the whole blob is one function.
    Returns the same per-function dicts as analyze_function (addresses absolute)."""
    import tempfile, hashlib
    h = hashlib.sha256(data).hexdigest()[:16]
    path = os.path.join(tempfile.gettempdir(), f'ptracer_raw_{h}_{base:x}.bin')
    if not os.path.exists(path):
        with open(path, 'wb') as fh:
            fh.write(data)
    _RAW[path] = base
    out = []
    for nm, s, e in (ranges or [(name, base, base + len(data))]):
        out.append(analyze_function(path, nm, s, e, mode, keyframe, avoid, roots, data_from))
    return out


DISCOVERY_VERSION = 'd1'   # bump when _discover_functions changes what it finds


def list_functions(binary, cache=None):
    """(name, start, end) from ELF symbols (FUNC with size>0), de-duplicated and clipped to
    executable sections, plus the entries _discover_functions finds for a stripped image.

    `cache` (a directory, the same one as --cache) memoises the discovery pass, keyed on the image
    bytes and on the symbol-derived function list it starts from. Discovery re-disassembles every
    byte of code for several fix-point rounds and is single-threaded, so on a large image it
    dominates every re-analysis round of a `--avoid` loop although its result never changes."""
    from elftools.elf.elffile import ELFFile
    funcs = {}
    with open(binary, 'rb') as fh:
        elf = ELFFile(fh)
        exec_ranges = [(s['sh_addr'], s['sh_addr'] + s['sh_size']) for s in elf.iter_sections()
                       if s['sh_flags'] & 0x4 and s['sh_size'] > 0]
        symsecs = [elf.get_section_by_name(n) for n in ('.symtab', '.dynsym')]
        symsecs = [x for x in symsecs if x is not None]
        # stripped image: use the distro's detached debug file (libc6-dbg etc.) matched by build-id
        if elf.get_section_by_name('.symtab') is None:
            dbg = debug_file_for(elf)
            if dbg is not None:
                delf = ELFFile(open(dbg, 'rb'))
                if delf.get_section_by_name('.symtab') is not None:
                    symsecs.insert(0, delf.get_section_by_name('.symtab'))
        for sec in symsecs:
            zero = []
            for sym in sec.iter_symbols():
                if sym['st_info']['type'] != 'STT_FUNC':
                    continue
                a, sz = sym['st_value'], sym['st_size']
                if not any(lo <= a < hi for lo, hi in exec_ranges):
                    continue
                if sz == 0:
                    zero.append((sym.name, a))
                    continue
                if a not in funcs or len(sym.name) < len(funcs[a][0]):
                    funcs[a] = (sym.name, a, a + sz)
            # size-less functions (crt stubs such as _init/_fini/register_tm_clones): extend to
            # the next symbol or the end of their section
            starts = sorted(funcs)
            import bisect
            for name, a in zero:
                if a in funcs:
                    continue
                sec_end = next(hi for lo, hi in exec_ranges if lo <= a < hi)
                i = bisect.bisect_right(starts, a)
                nxt = starts[i] if i < len(starts) else sec_end
                e = min(nxt, sec_end)
                if e > a:
                    funcs[a] = (name, a, e)
    # A stripped image keeps only .dynsym, so its STATIC functions have no symbol at all. Recover
    # them from direct call/tail-jump targets, relocation addends and IFUNC resolvers of the
    # functions we do know, iterating to a fixpoint.
    disc = None
    dpath = None
    if cache:
        # keyed on the file bytes AND the symbol-derived starting set (a detached debug file can
        # change the latter for the same image), so a hit can only ever serve this exact input
        fsha = hashlib.sha256(open(binary, 'rb').read()).hexdigest()
        ssha = hashlib.sha256(repr(sorted(funcs.items())).encode()).hexdigest()[:16]
        dpath = os.path.join(cache, f'discover.{fsha}.{ssha}.{DISCOVERY_VERSION}.json')
        if os.path.exists(dpath):
            try:
                disc = {int(k): tuple(v) for k, v in json.load(open(dpath)).items()}
            except Exception:
                disc = None
    if disc is None:
        disc = _discover_functions(binary, funcs, exec_ranges)
        if dpath:
            os.makedirs(cache, exist_ok=True)
            tmp = f'{dpath}.{os.getpid()}.tmp'
            with open(tmp, 'w') as fh:
                json.dump({str(k): list(v) for k, v in disc.items()}, fh)
            os.replace(tmp, dpath)
    funcs.update(disc)
    out = sorted(funcs.values(), key=lambda t: t[1])
    # clip overlapping symbols (aliases with different sizes)
    clipped = []
    for i, (n, s, e) in enumerate(out):
        if i + 1 < len(out) and out[i + 1][1] < e:
            e = out[i + 1][1]
        if e > s:
            clipped.append((n, s, e))
    return clipped


def _discover_functions(binary, funcs, exec_ranges, rounds=8):
    """Direct call / tail-jump targets that lie in an executable section but inside no known function are
    function entries the symbol table did not name (static functions of a stripped image). Returns
    {addr: (name, start, end)} for the new ones; extents run to the next known or discovered start."""
    import capstone
    from elftools.elf.elffile import ELFFile
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    with open(binary, 'rb') as fh:
        elf = ELFFile(fh)
        segs = [(sg['p_vaddr'], sg['p_vaddr'] + sg['p_filesz'], sg['p_offset'])
                for sg in elf.iter_segments() if sg['p_type'] == 'PT_LOAD' and sg['p_filesz']]
        data = open(binary, 'rb').read()

    def read(a, n):
        for lo, hi, off in segs:
            if lo <= a and a + n <= hi:
                return data[off + (a - lo): off + (a - lo) + n]
        return b''

    import bisect as _bisect
    _cov = {}

    def covered(a, table):
        # same answer as any(st <= a < en for every function): a prefix maximum of the extents' ends over the
        # starts sorted, rebuilt only when `table' changes (once per round); a linear scan per query is too slow on
        # large binaries
        key = id(table), len(table)
        if _cov.get('key') != key:
            iv = sorted((st, en) for _, st, en in table.values())
            mx, m = [], None
            for _st, en in iv:
                m = en if m is None or en > m else m
                mx.append(m)
            _cov.update(key=key, starts=[st for st, _ in iv], mx=mx)
        i = _bisect.bisect_right(_cov['starts'], a) - 1
        return i >= 0 and _cov['mx'][i] > a

    # RELOCATION SEEDS: a function reached only through a table of function pointers (vtables, ops
    # structs) is never the target of a direct call, so the call-target scan below cannot find it.
    # Its address appears instead as the addend of an `R_X86_64_RELATIVE` (or the value of an
    # absolute) relocation. Seed discovery with every relocation target in an executable section.
    reloc_seeds = set()
    ifunc_resolvers = set()
    try:
        from elftools.elf.relocation import RelocationSection
        with open(binary, 'rb') as fh:
            elf2 = ELFFile(fh)
            for sec in elf2.iter_sections():
                if not isinstance(sec, RelocationSection):
                    continue
                for r in sec.iter_relocations():
                    a = r['r_addend'] if r.is_RELA() else 0
                    if a and any(lo <= a < hi for lo, hi in exec_ranges):
                        reloc_seeds.add(a)          # the addend is the target; r_offset is the slot
                        if r['r_info_type'] == 37:  # R_X86_64_IRELATIVE: the addend is a RESOLVER
                            ifunc_resolvers.add(a)
    except Exception:
        pass
    # IFUNC: glibc selects `memmove`/`memcpy`/`strlen`/... at load time through an IRELATIVE relocation
    # whose addend is a RESOLVER; the implementation it returns is computed at run time, so it is
    # neither a direct-call target nor a relocation addend. The resolvers name their candidates with
    # `lea reg,[rip+disp]`, so scan each resolver.
    if ifunc_resolvers:
        md_i = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        md_i.detail = True
        for rv in sorted(ifunc_resolvers):
            body = read(rv, 512)
            if not body:
                continue
            for ins in md_i.disasm(body, rv):
                if ins.mnemonic == 'lea' and len(ins.operands) == 2:
                    m = ins.operands[1].mem
                    if m.base == capstone.x86.X86_REG_RIP and m.index == 0:
                        t = ins.address + ins.size + m.disp
                        if any(lo <= t < hi for lo, hi in exec_ranges):
                            reloc_seeds.add(t)
            # NB: no `break` at the first `ret` -- a resolver has several return paths, one per CPU
            # feature, and the later ones name the very implementations we are looking for.

    found = {}
    for _ in range(rounds):
        table = dict(funcs); table.update(found)
        targets = set(t for t in reloc_seeds if not covered(t, table))
        for _n, st, en in list(table.values()):
            if en - st > 1 << 20:
                continue
            body = read(st, en - st)
            if not body:
                continue
            for ins in md.disasm(body, st):
                # `lea reg,[rip+disp]` into an executable section is a FUNCTION POINTER taken in code --
                # the computed twin of a relocation-held pointer (glibc's IFUNC dispatch selects its
                # implementations this way).
                if ins.mnemonic == 'lea' and len(ins.operands) == 2:
                    m = ins.operands[1].mem
                    if m.base == capstone.x86.X86_REG_RIP and m.index == 0:
                        t = ins.address + ins.size + m.disp
                        if any(lo <= t < hi for lo, hi in exec_ranges) and not covered(t, table):
                            targets.add(t)
                    continue
                if ins.mnemonic not in ('call', 'jmp') or not ins.op_str.startswith('0x'):
                    continue
                try:
                    t = int(ins.op_str, 16)
                except ValueError:
                    continue
                if ins.mnemonic == 'jmp' and st <= t < en:
                    continue                      # intra-function jump, not a tail call
                if any(lo <= t < hi for lo, hi in exec_ranges) and not covered(t, table):
                    targets.add(t)
        targets = {t for t in targets if t not in found}
        if not targets:
            break
        bounds = sorted({st for _, st, _ in table.values()} | targets |
                        {hi for _, hi in exec_ranges})
        import bisect
        for t in sorted(targets):
            i = bisect.bisect_right(bounds, t)
            end = bounds[i] if i < len(bounds) else None
            sec_end = next((hi for lo, hi in exec_ranges if lo <= t < hi), None)
            if sec_end is None:
                continue
            end = min(end or sec_end, sec_end)
            if end > t:
                found[t] = ('sub_%x' % t, t, end)
    return found


def debug_file_for(elf):
    """/usr/lib/debug/.build-id/xx/yyyy.debug for the image's NT_GNU_BUILD_ID, if installed."""
    for sec in elf.iter_sections():
        if sec.name == '.note.gnu.build-id':
            for note in sec.iter_notes():
                if note['n_type'] == 'NT_GNU_BUILD_ID':
                    bid = note['n_desc']
                    p = f'/usr/lib/debug/.build-id/{bid[:2]}/{bid[2:]}.debug'
                    return p if os.path.exists(p) else None
    return None


def func_hash(binary, start, end):
    from elftools.elf.elffile import ELFFile
    with open(binary, 'rb') as fh:
        elf = ELFFile(fh)
        for seg in elf.iter_segments():
            if seg['p_type'] == 'PT_LOAD' and seg['p_vaddr'] <= start < seg['p_vaddr'] + seg['p_filesz']:
                off = start - seg['p_vaddr'] + seg['p_offset']
                fh.seek(off)
                data = fh.read(end - start)
                return hashlib.sha256(data).hexdigest()
    return None


def serve(sock_path=None):
    """Warm analyzer service. Requests are JSON lines: {"hex": <code bytes hex>, "base": <int>, "name": str,
    "mode": "hifi"|"fast", "ranges": [[name,start,end],...], "keyframe": K, "avoid": [addr,...],
    "roots": [addr,...], "data_from": addr} -> one JSON line reply {"ok": true, "sites": [...],
    "functions": [...]} with the spec sites (absolute addresses) and the per-function reports (which
    carry "restart_roots"), or {"ok": false, "error": ..., "trace": ...}. Over stdin/stdout by
    default, or a unix socket.

    "keyframe": K>0 adds resync keyframe sites at outermost-loop back-edge headers (JIT code is
    entered by OSR at a loop header, so the entry anchor never executes; the keyframe re-anchors).
    "roots": extra entry points (HotSpot's jvmtiAddrLocationMap). "data_from": absolute address of
    the object's first inline-data byte (clips the restart fill). The patcher must seed its own
    sweep with the reply's "restart_roots" to decode the same instruction boundaries."""
    import json as _json
    def handle(line):
        try:
            req = _json.loads(line)
            data = bytes.fromhex(req['hex'])
            res = analyze_raw(data, int(req['base']), req.get('name', 'jit'), req.get('mode', 'hifi'), req.get('ranges'),
                              req.get('avoid'), int(req.get('keyframe', 0) or 0),
                              [int(r) for r in (req.get('roots') or [])],
                              int(req['data_from']) if req.get('data_from') else None)
            for k in [k for k in list(_PROJ) if k in _RAW]:   # one-shot: free the project and the temp file
                _PROJ.pop(k, None); _RAW.pop(k, None)
                try:
                    os.unlink(k)
                except OSError:
                    pass
            import gc; gc.collect()
            sites = [dict(st, func=r.get('name')) for r in res for st in r['sites']]
            for i, st in enumerate(sites):
                st['id'] = i
            return _json.dumps({'ok': True, 'sites': sites, 'functions': [{k: v for k, v in r.items() if k != 'sites'} for r in res]})
        except Exception as exn:
            return _json.dumps({'ok': False, 'error': repr(exn), 'trace': traceback.format_exc()[-600:]})
    if sock_path:
        import socket
        if os.path.exists(sock_path):
            os.unlink(sock_path)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(sock_path); srv.listen(4)
        while True:
            conn, _ = srv.accept()
            # a client that disconnects mid-reply must not kill the service
            try:
                with conn, conn.makefile('rwb') as fh:
                    for line in fh:
                        fh.write((handle(line.decode()) + '\n').encode()); fh.flush()
            except (BrokenPipeError, ConnectionResetError, OSError) as exn:
                print(f'[serve] client dropped: {exn!r}', file=sys.stderr)
                continue
    else:
        for line in sys.stdin:
            if line.strip():
                sys.stdout.write(handle(line) + '\n'); sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary', nargs='?')
    ap.add_argument('-o', '--out')
    ap.add_argument('--raw', default=None, help='analyze a raw code blob file (JIT code) mapped at --base')
    ap.add_argument('--base', type=lambda x: int(x, 0), default=0)
    ap.add_argument('--serve', nargs='?', const='-', default=None, help='analyzer service: JSON lines on stdin/stdout or on a unix socket path')
    ap.add_argument('--func', action='append', help='only these functions (name)')
    ap.add_argument('--jobs', type=int, default=max(1, os.cpu_count() - 4))
    ap.add_argument('--cache', default=None, help='directory for the per-function result cache')
    ap.add_argument('--mode', default='hifi', choices=['hifi', 'fast'])
    ap.add_argument('--max-bytes', type=int, default=2_000_000, help='skip functions larger than this')
    ap.add_argument('--anchor', action='append', help='functions at whose entry rsp is logged (main always)')
    ap.add_argument('--anchor-all', action='store_true', help='rsp anchor at the entry of every function')
    ap.add_argument('--keyframe', type=int, default=0, help='K>0: add resync keyframe sites at outermost-loop back-edge targets (logged every K-th execution)')
    ap.add_argument('--avoid', default=None, help='hex addresses (comma list or file) the rewriter could not patch: move/drop sites there')
    ap.add_argument('--all-memops', action='store_true',
                    help="ablation arm: emit EVERY memory-accessing instruction as a `memop` site and skip "
                         "the hitting-set minimisation entirely -- no reg/load sites, no keyframes, no resync "
                         "sites, no rsp anchors, no solver. The spec stays a valid spec v2, so `rewrite.py "
                         "--sink buffer` builds it unchanged and `PTLOG_DIR=/dev/null` discards its values: "
                         "that is 'PTracer without PT and without static analysis' as an instrumentation-cost "
                         "measurement")
    ap.add_argument('-v', action='store_true')
    args = ap.parse_args()
    if args.all_memops:
        # Everything the flag bypasses must be refused, not silently ignored.
        bad = [n for n, v in (('--mode fast', args.mode != 'hifi'), ('--keyframe', args.keyframe),
                              ('--anchor', args.anchor),
                              ('--anchor-all', args.anchor_all), ('--raw', args.raw),
                              ('--serve', args.serve)) if v]
        if bad:
            sys.exit('[analyze] --all-memops minimises nothing and logs no registers, so it is '
                     'incompatible with: ' + ', '.join(bad))
    if args.serve:
        serve(None if args.serve == '-' else args.serve); return
    if args.raw:
        data = open(args.raw, 'rb').read()
        res = analyze_raw(data, args.base, os.path.basename(args.raw), args.mode)
        sites = [dict(st, func=r.get('name')) for r in res for st in r['sites']]
        for i, st in enumerate(sites):
            st['id'] = i
        spec = {'version': 2, 'image': os.path.abspath(args.raw), 'raw_base': args.base, 'pie': False, 'mode': args.mode,
                'sites': sites, 'functions': [{k: v for k, v in r.items() if k != 'sites'} for r in res]}
        json.dump(spec, open(args.out, 'w'), indent=1); print(f"[analyze] raw blob: {len(sites)} sites", file=sys.stderr); return
    if not args.binary or not args.out:
        ap.error('binary and -o are required')
    binary = os.path.abspath(args.binary)
    funcs = list_functions(binary, cache=args.cache)
    if args.func:
        want = set(args.func)
        funcs = [f for f in funcs if f[0] in want]
        missing = want - {f[0] for f in funcs}
        if missing:
            sys.exit(f'functions not found: {missing}')
    from elftools.elf.elffile import ELFFile
    with open(binary, 'rb') as fh:
        elf = ELFFile(fh)
        pie = elf['e_type'] == 'ET_DYN'
    sha = hashlib.sha256(open(binary, 'rb').read()).hexdigest()
    avoid = set()
    if args.avoid:
        # `#' comments are stripped so the rewriter's own `<image>.unpatched' file --
        # which is what rewrite.py tells the user to pass here -- can be fed in unedited.
        if os.path.exists(args.avoid):
            text = ' '.join(ln.split('#', 1)[0] for ln in open(args.avoid))
            avoid = {int(a, 16) for a in text.replace(',', ' ').split()}
        else:
            avoid = {int(a, 16) for a in args.avoid.split(',')}
    # The per-function cache key carries only the avoid addresses INSIDE that function: they are
    # all analyze_function() ever sees, so a function untouched by a new --avoid entry keeps its
    # entry across the rounds of a rewrite --avoid loop.
    def avoid_key_for(s, e):
        fa = sorted(a for a in avoid if s <= a < e)
        return hashlib.sha256(','.join(map(hex, fa)).encode()).hexdigest()[:8] if fa else 'a0'
    results, todo, hashes = {}, [], {}
    n_alias_cache = 0     # cache hits refused because they belong to a byte-identical twin elsewhere
    if args.cache:
        os.makedirs(args.cache, exist_ok=True)
    for name, s, e in funcs:
        if e - s > args.max_bytes:
            results[s] = {'name': name, 'start': s, 'end': e, 'error': 'too large', 'sites': []}
            continue
        h = func_hash(binary, s, e)
        hashes[s] = h
        cpath = os.path.join(args.cache, f'{h}.{args.mode}.k{args.keyframe}{("." + KEYFRAME_RULE) if args.keyframe else ""}.{avoid_key_for(s, e)}{".am2" if args.all_memops else ""}.{CACHE_VERSION}.json') if args.cache and h else None
        if cpath and os.path.exists(cpath):
            r = json.load(open(cpath))
            # The cache key is the function's BYTES, so a byte-identical function at ANOTHER address
            # hits this entry -- and every ABSOLUTE field in it (`blocks_list`, `unresolved`,
            # `rep_counts`, `undecodable`, `uncoverable_accesses`, `callsites`, `restart_*`) then
            # names the other copy's addresses; only `sites` is stored function-relative. A function's
            # entry block is its lowest block, so `blocks_list[0] != start` detects the case exactly:
            # re-analyze, and do NOT rewrite the entry (the twin that owns it keeps it).
            bl = r.get('blocks_list')
            if not bl or bl[0] == s:
                r.update({'name': name, 'start': s, 'end': e, 'cached': True})
                # sites are stored relative to the function start in the cache
                for st in r['sites']:
                    st['addr'] += s
                results[s] = r
                continue
            n_alias_cache += 1
            cpath = None
        todo.append((name, s, e, cpath))
    t0 = time.time()
    print(f'[analyze] {binary}: {len(funcs)} functions, {len(todo)} to analyze, {len(results)} cached', file=sys.stderr)
    if n_alias_cache:
        print(f'[analyze] {n_alias_cache} cache entr(ies) refused: byte-identical function at another '
              f'address; re-analyzed', file=sys.stderr)
    if args.jobs <= 1 or len(todo) <= 1:
        for name, s, e, cpath in todo:
            results[s] = _run_one(binary, name, s, e, args.mode, cpath, args.keyframe, sorted(a for a in avoid if s <= a < e), args.all_memops)
    else:
        with ProcessPoolExecutor(max_workers=args.jobs) as ex:
            futs = {ex.submit(_run_one, binary, name, s, e, args.mode, cpath, args.keyframe, sorted(a for a in avoid if s <= a < e), args.all_memops): s for name, s, e, cpath in todo}
            done = 0
            for fut in as_completed(futs):
                s = futs[fut]
                try:
                    results[s] = fut.result()
                except Exception as exn:
                    results[s] = {'start': s, 'error': repr(exn), 'sites': []}
                done += 1
                if args.v and done % 50 == 0:
                    print(f'  {done}/{len(todo)} ({time.time() - t0:.0f}s)', file=sys.stderr)
    # assemble spec
    sites, functions = [], []
    for s in sorted(results):
        r = results[s]
        for st in r['sites']:
            st = dict(st)
            st['func'] = r.get('name')
            sites.append(st)
        functions.append({k: r[k] for k in r if k != 'sites'})
    # rsp anchors: the reconstructor derives every later rsp from a logged value (push/pop/call/ret
    # deltas are constants), but an un-liftable instruction or a PT overflow loses it. Policy:
    # add rsp to the entry `reg` site of NON-LEAF functions that already have one (leaf helpers are
    # the hottest functions; re-anchoring there is a large share of all logged values), and
    # create an rsp-only entry site for main and the --anchor functions (--anchor-all: every function).
    anchors = set(args.anchor or []) | {'main'}
    # the first `before' reg site at (function, entry address) -- an index, not a scan per function (a scan is
    # O(functions x sites))
    entry_site = {}
    for st in sites:
        if st['kind'] == 'reg' and st['when'] == 'before':
            entry_site.setdefault((st['func'], st['addr']), st)
    for r in ([] if args.all_memops else results.values()):
        if 'error' in r or not r.get('name'):
            continue
        ent = entry_site.get((r['name'], r['start']))
        if ent is not None:
            if 'rsp' not in ent['regs'] and (r.get('has_calls') or args.anchor_all or r['name'] in anchors):
                ent['regs'] = sorted(set(ent['regs']) | {'rsp'})
            if r['name'] in anchors and 'fs_base' not in ent['regs']:
                ent['regs'] = sorted(set(ent['regs']) | {'fs_base'})
        elif r['name'] in anchors or args.anchor_all:
            regs = ['rsp', 'fs_base'] if r['name'] in anchors else ['rsp']
            sites.append({'addr': r['start'], 'when': 'before', 'kind': 'reg', 'regs': regs, 'orphan': False, 'func': r['name'], 'note': 'anchor', 'flags_dead': False})
    am_summary = None
    if args.all_memops:
        # Two functions may alias the same code (the same address analyzed twice); one
        # address may carry only ONE memop site or the runtime would log it twice.
        seen, uniq, dup = set(), [], 0
        for st in sites:
            if st['addr'] in seen:
                dup += 1
                continue
            seen.add(st['addr'])
            uniq.append(st)
        sites = uniq
        skipped, implicit, n_acc, n_clamp = {}, {}, 0, 0
        for fr in functions:
            am = fr.get('all_memops')
            if not am:
                continue
            n_acc += am['accessing_insns']
            n_clamp += am['size_clamped']
            for k, v in am['skipped'].items():
                skipped[k] = skipped.get(k, 0) + v
            for k, v in am.get('implicit', {}).items():
                implicit[k] = implicit.get(k, 0) + v
        am_summary = {'accessing_instructions': n_acc, 'sites': len(sites),
                      'duplicate_addresses': dup, 'size_clamped': n_clamp,
                      'skipped_total': sum(skipped.values()), 'skipped': skipped,
                      'implicit_address_sites': implicit}
        print('[analyze] --all-memops: implicit stack/string accesses logged by address register: %s'
              % (', '.join('%s=%d' % kv for kv in sorted(implicit.items())) or 'none'), file=sys.stderr)
        print('[analyze] --all-memops: %d memory-accessing instruction(s), %d memop site(s), '
              '%d skipped (%s), %d operand(s) wider than 8 bytes logged 8 bytes wide, '
              '%d duplicate address(es) from aliased functions'
              % (n_acc, len(sites), am_summary['skipped_total'],
                 ', '.join('%s=%d' % kv for kv in sorted(skipped.items())) or 'none',
                 n_clamp, dup), file=sys.stderr)
    if avoid:
        sites = shift_sites(binary, sites, avoid)     # anchors/resync sites the re-solve could not move
    dropped = [st for st in sites if st.get('dropped')]
    sites = [st for st in sites if not st.get('dropped')]
    sites.sort(key=lambda st: (st['addr'], 0 if st['when'] == 'before' else 1, st['kind']))
    for i, st in enumerate(sites):
        st['id'] = i
    spec = {'version': 2, 'analyzer_version': ANALYZER_VERSION,
            'image': binary, 'sha256': sha, 'pie': pie, 'mode': args.mode,
            'sites': sites, 'functions': functions, 'dropped_sites': dropped,
            'summary': {'functions': len(functions), 'analyzed': sum(1 for f in functions if 'error' not in f),
                        'errors': sum(1 for f in functions if 'error' in f),
                        'discovered_functions': sum(1 for f in functions if str(f.get('name', '')).startswith('sub_')),
                        'sites': len(sites), 'values': sum(len(s.get('regs', [])) or 1 for s in sites),
                        'uncoverable_accesses': sum(len(f.get('uncoverable_accesses', [])) for f in functions),
                        'time_s': round(time.time() - t0, 1), 'fast': None}}
    if am_summary is not None:      # only in --all-memops mode: an ordinary spec is unchanged, byte for byte
        spec['summary']['all_memops'] = am_summary
    spec['summary']['unloggable_skipped'] = len(UNLOGGABLE)
    if UNLOGGABLE:
        print(f"[analyze] WARNING: {len(UNLOGGABLE)} chosen value(s) on non-loggable locations skipped, e.g. "
              f"{UNLOGGABLE[:3]}", file=sys.stderr)
    with open(args.out, 'w') as fh:
        json.dump(spec, fh, indent=1)
    print(f"[analyze] done: {spec['summary']}", file=sys.stderr)
    if functions and spec['summary']['analyzed'] == 0:
        # a spec with zero analyzed functions (wrong interpreter, no angr, ...) is not a result
        first = next((f.get('error') for f in functions if 'error' in f), '?')
        sys.exit(f"[analyze] ERROR: every function failed ({spec['summary']['errors']}); first error: {first}")


def shift_over_nops(proj, delta, site, avoid):
    """Move only across decoded NOPs, never across a read/write/control transfer.

    Orphan does not prove unreachable: binary CFG recovery is incomplete. A
    forbidden one-byte NOP can safely hand its incoming values to its immediate
    successor. All original paths through the NOP reach that successor with the
    same state. Extra predecessors merely add observations. Liveness belongs to
    the old location, so use the conservative save/restore emitter at the new one.
    """
    if site['kind'] != 'reg' or site['when'] != 'before':
        return None
    addr = site['addr']
    for _ in range(8):
        try:
            insns = proj.factory.block(addr + delta, num_inst=1).capstone.insns
            if len(insns) != 1 or insns[0].mnemonic != 'nop' or insns[0].size <= 0:
                return None
            addr += insns[0].size
        except Exception:
            return None
        if addr not in avoid:
            return dict(site, addr=addr, moved_from=site['addr'],
                        move_reason='verified-nop-prefix', flags_dead=False, dead_regs=[])
    return None


def shift_sites(binary, sites, avoid):
    """Sites at addresses the rewriter cannot patch: a `reg`/before site is moved forward past
    instructions that do not write any of its registers (rsp excepted: the logged rsp value is simply
    the value at the new point); other sites are dropped with a warning (reported as unpatchable)."""
    import pyvex, dataflow
    proj = _project(binary)
    mo = proj.loader.main_object
    delta = mo.mapped_base - mo.linked_base
    out = []
    for st in sites:
        if st['addr'] not in avoid:
            out.append(st)
            continue
        nop_shift = shift_over_nops(proj, delta, st, avoid)
        if nop_shift is not None:
            print(f"[analyze] moved NOP-entry site {hex(st['addr'])} -> {hex(nop_shift['addr'])} ({st.get('func')})", file=sys.stderr)
            out.append(nop_shift)
            continue
        if st.get('orphan'):
            # an unreachable block the rewriter cannot patch is almost always data decoded as code
            # (jump tables inside .text): drop it, do not try to move it byte by byte
            out.append(dict(st, dropped='unpatchable-orphan'))
            continue
        if not (st['kind'] == 'reg' and st['when'] == 'before'):
            print(f"[analyze] WARNING: dropping unpatchable {st['kind']} site at {hex(st['addr'])} ({st.get('func')})", file=sys.stderr)
            st = dict(st, dropped='unpatchable')
            out.append(st)
            continue
        addr = st['addr']
        regs = set(st['regs'])
        moved = False
        for _ in range(8):
            try:
                irsb = proj.factory.block(addr + delta, num_inst=1, opt_level=0).vex
            except Exception:
                break
            written = set()
            for x in irsb.statements:
                if x.tag == 'Ist_Put':
                    loc, _, _ = dataflow.loc_of(x.offset, 8)
                    if loc:
                        written.add(loc)
            written = {w[:-1] if w.startswith('xmm') and w[-1] in 'lh' else w for w in written}
            if irsb.jumpkind != 'Ijk_Boring' or (written & (regs - {'rsp'})):
                break
            addr += irsb.size
            if addr not in avoid:
                moved = True
                break
        if moved:
            print(f"[analyze] moved reg site {hex(st['addr'])} -> {hex(addr)} ({st.get('func')})", file=sys.stderr)
            out.append(dict(st, addr=addr, moved_from=st['addr']))
        else:
            print(f"[analyze] WARNING: cannot move site at {hex(st['addr'])} ({st.get('func')}); dropped", file=sys.stderr)
            out.append(dict(st, dropped='unpatchable'))
    return out


def _run_one(binary, name, s, e, mode, cpath, keyframe=0, avoid=None, all_memops=False):
    try:
        r = analyze_function(binary, name, s, e, mode, keyframe, avoid, all_memops=all_memops)
    except Exception as exn:
        r = {'name': name, 'start': s, 'end': e, 'error': repr(exn), 'trace': traceback.format_exc()[-800:], 'sites': []}
    if cpath and 'error' not in r:
        c = dict(r)
        c['sites'] = [dict(st, addr=st['addr'] - s) for st in r['sites']]
        for k in ('name', 'start', 'end'):
            c.pop(k, None)
        tmp = f'{cpath}.{os.getpid()}.tmp'      # identical functions share a hash: avoid tmp collisions
        with open(tmp, 'w') as fh:
            json.dump(c, fh)
        os.replace(tmp, cpath)
    return r


if __name__ == '__main__':
    main()
