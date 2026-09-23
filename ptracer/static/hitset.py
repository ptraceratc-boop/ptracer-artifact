"""
Minimal critical value set (the paper's "input sets" / "relationship to the hitting set problem").

Formulation. For every memory access A the address is the root of a value graph G_A. A *valid input
set* is a set of nodes cutting every path from a leaf to the root; its values suffice to recompute
the address offline. The *critical value set* C must contain a valid input set for every access; we
minimise the logging cost of C.

Solver. Nodes shared between accesses are hash-consed, so the union of all G_A is one graph.
  1. Condense strongly connected components (loop-carried Phi cycles) into supernodes. A
     supernode is covered either by logging all of its *exit* nodes (those read from outside the
     SCC or being a root) or by covering all of its *external inputs*; both cut every leaf path.
  2. Dynamic programming: cost(S) = min(log_cost(exits(S)), sum cost(inputs(S))) memoised per SCC
     (context-free, hence sound for every root).
  3. Sharing: the greedy "prefer values already chosen" is realised by re-running the DP with the
     cost of already-chosen nodes set to 0 until the choice stabilises, then a redundancy pass
     drops any chosen node whose removal keeps every access covered.
  4. Costs: HiFi = one unit per logged value weighted by 10^loop_depth of its site; Fast = the
     same plus a per-site term so values sharing an instruction are cheaper (fewest sites).
The DP is optimal for tree-shaped graphs and near-optimal with sharing in practice.
"""
from __future__ import annotations
import sys
from nodes import Node
sys.setrecursionlimit(1_000_000)

INF = float('inf')


class SiteCost:
    """Cost of logging at an instruction = expected executions of that instruction, estimated as
    10^loop_depth of its block."""
    def __init__(self, loop_depth, mode='hifi', avoid=None):
        self.loop_depth = loop_depth        # block addr -> depth
        self.mode = mode
        self.avoid = set(avoid) if avoid else None   # instruction addresses that cannot be patched
        self._insn_block = {}
        # FAST mode ("fewest instrumentation locations", vs HiFi's "fewest unique values"): once a
        # trampoline exists at an address, logging another value there costs only the extra
        # store/ptwrite, not another detour + relocated window. `hosted` is maintained by
        # Solver.solve across its rounds; `share` is that marginal fraction of a new trampoline's cost.
        self.hosted = set()
        self.share = 0.15

    def set_insn_blocks(self, mapping):
        self._insn_block = mapping          # insn addr -> block addr

    def weight(self, addr):
        b = self._insn_block.get(addr, addr)
        return 10.0 ** self.loop_depth.get(b, 0)


def best_site(node, cost):
    """Cheapest loggable site of a node: (cost, (addr, when, reg)) or (INF, None). Sites at addresses in
    cost.avoid (the rewriter could not patch them) are unavailable, so the solver picks another cut."""
    best, bs = INF, None
    avoid = getattr(cost, 'avoid', None)
    if node.kind == 'entry':
        if node.key == 'rsp':
            return 0.0, ('entry', 'before', 'rsp')      # reconstructor tracks rsp itself
        if node.key in ('d',):
            return 0.0, None
        if node.key == 'gs':                 # the runtime cannot log the GS base
            return INF, None
        if node.key == 'fs':                 # thread-invariant TLS base: anchored once per thread, not per call
            return 0.0, ('entry', 'before', 'fs')
    if node.kind == 'const':
        return 0.0, None
    if node.kind == 'unknown':
        return INF, None
    for (addr, when, reg) in node.sites:
        if reg is None:
            continue
        # the direction flag `d` (VEX pseudo-register) and the condition-code thunk are not loggable
        # locations; `d` is 0 in every ABI-conforming program, so a cut through it costs nothing
        if reg == 'd':
            return 0.0, None
        if reg in ('cc', 'gs'):
            continue
        # an unpatchable address forbids sites that cannot move: `after` sites and memops. `before`
        # sites (function entry, block entry, pseudo-loads) are kept and moved past the offending
        # instruction by analyze.shift_sites (sound when the moved-over instructions do not write the regs)
        if avoid and addr in avoid and (when == 'after' or reg == 'memop'):
            continue
        if reg == 'memop':
            c = 1.5 * cost.weight(addr)
        else:
            c = cost.weight(addr)
        if cost.mode == 'fast' and addr in cost.hosted:
            c *= cost.share          # an existing trampoline at this address absorbs one more value
        if c < best:
            best, bs = c, (addr, when, reg)
    return best, bs


class Solver:
    def __init__(self, roots, cost, resolve):
        """roots: list of Node (addresses / rep counts). resolve: phi alias resolver."""
        self.roots = [resolve(r) for r in roots]
        self.cost = cost
        self.resolve = resolve
        self._build()

    # -------------------------------------------------------------- graph + SCC
    def _children(self, n):
        return [self.resolve(a) for a in n.args]

    def _children_scc(self, n):
        """Children used for SCC membership: the forwarded-value edge of a Load is NOT followed, so a
        loop carried through a stack slot (store in one iteration, reload in the next) is cut at the
        reload, which is always loggable."""
        if n.kind == 'load':
            return []
        return self._children(n)

    def _build(self):
        # collect nodes reachable from roots
        nodes, stack = {}, list(self.roots)
        while stack:
            n = stack.pop()
            if n.id in nodes:
                continue
            nodes[n.id] = n
            stack.extend(self._children(n))
        self.nodes = nodes
        # Tarjan SCC (iterative)
        index, low, onstack, st, sccs = {}, {}, set(), [], []
        counter = [0]
        for root in nodes.values():
            if root.id in index:
                continue
            work = [(root, iter(self._children_scc(root)))]
            index[root.id] = low[root.id] = counter[0]; counter[0] += 1
            st.append(root); onstack.add(root.id)
            while work:
                n, it = work[-1]
                advanced = False
                for c in it:
                    if c.id not in index:
                        index[c.id] = low[c.id] = counter[0]; counter[0] += 1
                        st.append(c); onstack.add(c.id)
                        work.append((c, iter(self._children_scc(c))))
                        advanced = True
                        break
                    elif c.id in onstack:
                        low[n.id] = min(low[n.id], index[c.id])
                if advanced:
                    continue
                work.pop()
                if work:
                    p = work[-1][0]
                    low[p.id] = min(low[p.id], low[n.id])
                if low[n.id] == index[n.id]:
                    comp = []
                    while True:
                        x = st.pop(); onstack.discard(x.id); comp.append(x)
                        if x is n:
                            break
                    sccs.append(comp)
        self.scc_of = {}
        self.sccs = sccs
        for i, comp in enumerate(sccs):
            for n in comp:
                self.scc_of[n.id] = i
        # exits and inputs per SCC
        roots_ids = {r.id for r in self.roots}
        self.exits = [set() for _ in sccs]
        self.inputs = [set() for _ in sccs]
        for n in nodes.values():
            si = self.scc_of[n.id]
            if n.id in roots_ids:
                self.exits[si].add(n.id)
            for c in self._children(n):
                sj = self.scc_of[c.id]
                if sj != si:
                    self.inputs[si].add(sj)
                    self.exits[sj].add(c.id)

    # -------------------------------------------------------------- DP
    def solve(self, rounds=5):
        chosen = {}          # node id -> site
        if self.cost.mode == 'fast':
            self.cost.hosted = set()         # address-level sharing is rebuilt from scratch each solve
        for _ in range(rounds):
            memo = {}
            new = {}
            lbs = {}

            def log_cost(n):
                if n.id in chosen:
                    return 0.0, chosen[n.id]
                return best_site(n, self.cost)

            def cost_scc(si):
                if si in memo:
                    return memo[si][0]
                memo[si] = (INF, None)      # re-entrance guard (condensation may cycle through Load.fwd)
                comp = self.sccs[si]
                # option A: log all exits
                la, sites = 0.0, []
                for nid in self.exits[si]:
                    n = self.nodes[nid]
                    c, s = log_cost(n)
                    if n.kind == 'const' or (n.kind == 'entry' and n.key in ('rsp', 'd', 'fs')):
                        continue
                    if c == INF:
                        la = INF
                        break
                    la += c
                    sites.append((n, s))
                # option B: cover all inputs (invalid for a leaf SCC: no inputs => nothing cut)
                lb = 0.0 if self.inputs[si] else INF
                for sj in self.inputs[si]:
                    lb += cost_scc(sj)
                    if lb == INF:
                        break
                # a load with a forwarded child: logging the load itself competes with its child
                # (handled naturally: the load node is an exit whose log cost vs input cost is compared)
                if la < lb:      # ties -> prefer inputs (leaves are shared more often)
                    memo[si] = (la, ('log', sites))
                else:
                    memo[si] = (lb, ('inputs', None))
                lbs[si] = lb
                return memo[si][0]

            for r in self.roots:
                cost_scc(self.scc_of[r.id])
            # collect choices along the chosen structure
            visited = set()

            def collect(si):
                if si in visited:
                    return
                visited.add(si)
                if si not in memo:          # an input skipped by an early INF break
                    cost_scc(si)
                c, choice = memo[si]
                if choice is None:
                    return
                if choice[0] == 'log':
                    for n, s in choice[1]:
                        if s is not None:
                            new[n.id] = s
                else:
                    for sj in self.inputs[si]:
                        collect(sj)
            for r in self.roots:
                collect(self.scc_of[r.id])
            # un-stick: a node chosen in an earlier round costs 0 now, which can keep it chosen even
            # though covering its SCC from the inputs (which may since have become free) is cheaper
            # than its real cost. Flip such SCCs to 'inputs'.
            flipped = False
            for si, (c, choice) in list(memo.items()):
                if choice is None or choice[0] != 'log' or si not in lbs:
                    continue
                real = sum(best_site(n, self.cost)[0] for n, _ in choice[1])
                if lbs[si] <= real and all(n.id in chosen for n, _ in choice[1]):
                    for n, _ in choice[1]:
                        new.pop(n.id, None)
                    flipped = True
                    vis2 = set()
                    def collect_inputs(sj):
                        if sj in vis2:
                            return
                        vis2.add(sj)
                        if sj not in memo:
                            return
                        c2, ch2 = memo[sj]
                        if ch2 is None:
                            return
                        if ch2[0] == 'log':
                            for n2, s2 in ch2[1]:
                                if s2 is not None:
                                    new[n2.id] = s2
                        else:
                            for sk in self.inputs[sj]:
                                collect_inputs(sk)
                    for sj in self.inputs[si]:
                        collect_inputs(sj)
            if set(new) == set(chosen) and not flipped:
                break
            chosen = new
            if self.cost.mode == 'fast':
                # publish the addresses that now host a trampoline, so the next round prices an extra
                # value there at `share` and the DP piles values onto existing locations
                self.cost.hosted = {st[0] for st in chosen.values()
                                    if st and isinstance(st[0], int)}
        self.chosen = chosen
        self.uncoverable = self._check_uncoverable()
        self._prune()
        return self.chosen

    def _covered(self, root, chosen):
        """Does `chosen` cut every leaf->root path? DFS from root stopping at chosen nodes."""
        stack, seen = [root], set()
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
            stack.extend(self._children(n))
        return True

    def _check_uncoverable(self):
        return [r for r in self.roots if not self._covered(r, self.chosen)]

    def _prune(self):
        """Drop redundant chosen nodes: c is redundant when every leaf path below c is already cut by
        the other chosen nodes (root-independent, so one bounded DFS per candidate)."""
        order = sorted(self.chosen, key=lambda nid: -best_site(self.nodes[nid], self.cost)[0])
        parents = {}
        for n in self.nodes.values():
            for c in self._children(n):
                parents.setdefault(c.id, []).append(n)
        root_ids = {r.id for r in self.roots}
        for nid in order:
            n = self.nodes[nid]
            # covered from above: every path from n up to a root passes through another chosen node
            stack, seen, above = list(parents.get(nid, [])), set(), True
            if nid in root_ids:
                above = False
            while stack and above:
                m = stack.pop()
                if m.id in seen:
                    continue
                seen.add(m.id)
                if m.id in self.chosen:
                    continue
                if m.id in root_ids:
                    above = False
                    break
                stack.extend(parents.get(m.id, []))
            if above:
                del self.chosen[nid]
                continue
            if n.is_leaf() and not (n.kind == 'load' and n.args):
                continue                     # a true leaf can only be covered by itself (or from above)
            trial = self.chosen
            ok = True
            stack, seen = list(self._children(n)), set()
            while stack and ok:
                m = stack.pop()
                if m.id in seen:
                    continue
                seen.add(m.id)
                if m.id in trial and m.id != nid:
                    continue
                if m.kind == 'const' or (m.kind == 'entry' and m.key in ('rsp', 'd', 'fs')):
                    continue
                if m.is_leaf() and not (m.kind == 'load' and m.args):
                    ok = False
                    break
                stack.extend(self._children(m))
            if ok:
                del self.chosen[nid]
