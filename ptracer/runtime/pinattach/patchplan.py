#!/usr/bin/env python3
"""Derive an in-memory PATCH PLAN from an E9Patch-rewritten image.

Pin (Probe mode, runtime/pinattach/ptattach.so) is the attach/detach vehicle; the
instrumentation is PTracer's own in-place trampolines -- the very bytes that
`runtime/rewrite.py' + E9Patch put into the rewritten file.  This script turns a
(original image, rewritten image) pair into everything the Pintool needs to
reproduce the *rewritten process image* inside a live, already-running process:

    patches   the byte runs that differ inside the original image's own
              mappings          -> written in place (mprotect + memcpy)
    maps      the address ranges the rewritten image adds outside the original
              image (E9Patch trampolines, the reserve region of --delta builds,
              the E9Patch loader page)
              -> mmap(MAP_FIXED_NOREPLACE) at base+va, filled from the blob
    sitemap   carried through unchanged, so `offline/ptrecon' does not know or
              care whether the trampolines were installed statically or by the
              Pintool.

This file DOES NOT MODIFY rewrite.py; it imports it as a library
(`rewrite.Elf', `rewrite.VirtualImage') and reads its outputs.

    ./patchplan.py --orig PROG --rewritten PROG.e9 [--sitemap PROG.e9.sitemap.json]
                   [--orig PROG2 --rewritten PROG2.e9 ...]
                   -o PLAN.json                     (writes PLAN.json + PLAN.blob)

How faithfully does this reproduce E9Patch?
--------------------------------------------------------------------
E9Patch's *runtime* image is completely determined by
  (a) the rewritten ELF's PT_LOAD segments, and
  (b) the `struct e9_map_s' table in the embedded "E9PATCH" config, which the
      E9Patch loader mmap()s MAP_FIXED|MAP_PRIVATE from the rewritten file
      before jumping to the real entry point (src/e9patch/e9loader_elf.cpp,
      step 4).
So a *byte-exact* reproduction only needs the final content of every virtual
page.  That makes every patching TACTIC reproduce exactly, because a tactic is a
statement about which bytes E9Patch chose to write, not about how they are
mapped:
  B1  (5-byte jmp rel32)                    exact
  B2  (punned jump)                         exact
  T1  (prefixed punned jump)                exact
  T2/T3 (successor / neighbour EVICTION)    exact -- the evicted instruction's
        own trampoline is just another trampoline region, and the evictee's
        original bytes are part of the same patched page
  physical page aliasing (the same file page mapped at many virtual addresses,
        which is how E9Patch keeps a punned jump's rel32 bytes legal)
                                            exact, but reproduced as *separate*
        anonymous pages with identical content: the aliasing is a file-size /
        page-cache optimisation, not a semantic property
What is NOT reproduced (and is reported instead):
  B0  (illegal instruction + SIGILL handler): needs the E9Patch loader's own
        signal handler and its trap table installed in the live process.  It is
        off by default (`rewrite.py --full-coverage'), and every image we ship
        has num_traps == 0.  If num_traps > 0 the plan records the trap sites as
        UNPATCHABLE unless --with-loader is given.
  the injected runtime (`rt/ptlogrt.e9rt', --sink buffer): E9Patch runs its
        init() from DT_INIT_ARRAY, which has already run in a live process.  The
        plan flags this; the PTWRITE sink needs no runtime at all.
  E9Patch's edits to the ELF header / program headers / PT_DYNAMIC: those exist
        only to make the *file* bootable; a live process is already booted.  They
        are diffed, classified as "loader plumbing" and skipped (listed in the
        plan's `skipped_deltas' so nothing is silently dropped).
"""

import argparse
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))          # runtime/
import rewrite as R                                # noqa: E402  (library use only)

PAGE = 4096


# --------------------------------------------------------------------------
# the rewritten image, mapped the way E9Patch's loader maps it
# --------------------------------------------------------------------------

def e9_maps(vi):
    """[(set, va, len, file_off, type, r, w, x, abs)] in loader order."""
    out = []
    if vi.cfg is None:
        return out
    d = vi.elf.data
    for k in (0, 1):
        n, moff = vi.cfg["num_maps"][k], vi.cfg["maps"][k]
        for i in range(n):
            o = vi.cfg_off + moff + i * 12
            addr, offset, bits = struct.unpack_from("<iII", d, o)
            out.append(dict(set=k,
                            va=addr * PAGE,
                            len=(bits & 0xfffff) * PAGE,
                            off=offset * PAGE,
                            type=(bits >> 20) & 0x3,
                            r=(bits >> 28) & 1, w=(bits >> 29) & 1,
                            x=(bits >> 30) & 1, absolute=(bits >> 31) & 1))
    return out


class Flat:
    """Flat byte image of an ELF's PT_LOADs at base 0 (memsz, zero-filled)."""

    def __init__(self, elf):
        self.segs = []                       # (va, memsz, bytes)
        for va, memsz, off, filesz, flags in elf.loads:
            b = elf.data[off:off + filesz]
            if len(b) < filesz:
                b = b + b"\0" * (filesz - len(b))
            self.segs.append((va, memsz, filesz, b, flags))

    def get(self, va, n):
        """Bytes at [va, va+n) if fully covered by ONE PT_LOAD, else None.

        A PT_LOAD is mapped in whole PAGES: everything from `filesz' to the end
        of the segment's last page exists in the process and reads as zero.  The
        E9Patch REFACTOR map is page-granular, so it routinely covers that tail
        (`gemm_large' text is 0x995 bytes and the map is the whole 0x1000 page);
        clamping to `memsz' here used to make the whole patched code page look
        like a NEW region instead of a set of in-place patches.
        """
        for sva, memsz, filesz, b, _fl in self.segs:
            end = (sva + memsz + PAGE - 1) & ~(PAGE - 1)
            if sva <= va and va + n <= end:
                o = va - sva
                chunk = b[o:o + n]
                if len(chunk) < n:
                    chunk = chunk + b"\0" * (n - len(chunk))
                return chunk
        return None

    def covers(self, va):
        return any(sva <= va < ((sva + memsz + PAGE - 1) & ~(PAGE - 1))
                   for sva, memsz, _f, _b, _fl in self.segs)

    def is_exec(self, va):
        return any(sva <= va < ((sva + memsz + PAGE - 1) & ~(PAGE - 1)) and (fl & 1)
                   for sva, memsz, _f, _b, fl in self.segs)

    @property
    def ranges(self):
        """Page-rounded [lo, hi) of every PT_LOAD, as the kernel maps them."""
        return [(va & ~(PAGE - 1), (va + memsz + PAGE - 1) & ~(PAGE - 1))
                for va, memsz, _f, _b, _fl in self.segs]


def diff_runs(a, b, base_va, maxgap=8):
    """[(va, len)] runs where a != b, merging runs separated by < maxgap."""
    n = min(len(a), len(b))
    runs = []
    i = 0
    while i < n:
        if a[i] != b[i]:
            j = i
            last = i
            while j < n:
                if a[j] != b[j]:
                    last = j
                elif j - last >= maxgap:
                    break
                j += 1
            runs.append((base_va + i, last - i + 1))
            i = last + 1
        else:
            i += 1
    return runs


# --------------------------------------------------------------------------
# plan construction for one image
# --------------------------------------------------------------------------

def build_image_plan(orig_path, rew_path, sitemap_path, blob, opt):
    orig = R.Elf(orig_path)
    vi = R.VirtualImage(rew_path)
    rew = vi.elf
    fo = Flat(orig)
    fr = Flat(rew)

    if vi.cfg is None:
        raise SystemExit("%s: no embedded E9PATCH config -- not an E9Patch "
                         "rewritten image" % rew_path)
    if orig.pie != rew.pie:
        raise SystemExit("%s: ELF type differs from the original" % rew_path)

    orig_ranges = fo.ranges
    orig_end = max(hi for _lo, hi in orig_ranges)

    def in_orig(va, ln):
        return any(lo <= va and va + ln <= hi for lo, hi in orig_ranges)

    patches = []          # byte runs to write inside the original image
    maps = []             # regions to mmap outside the original image
    skipped = []          # deltas that are E9Patch file/loader plumbing
    notes = []

    # ---- 1. the rewritten file's own PT_LOADs vs the original's --------
    # E9Patch keeps the original segments byte-identical and overlays patched
    # code with a REFACTOR map; anything else here is either loader plumbing
    # (ELF header, phdrs, PT_DYNAMIC) or a genuine in-place edit.
    for va, memsz, filesz, b, flags in fr.segs:
        ob = fo.get(va, filesz)
        if ob is None:
            # a segment the original does not have: E9Patch's loader.
            if in_orig(va, 1):
                notes.append("rewritten segment at %#x overlaps the original "
                             "image but is not covered by it" % va)
            maps.append(dict(va=va, len=(memsz + PAGE - 1) & ~(PAGE - 1),
                             content=b, type="loader",
                             prot=("r-x" if (flags & 1) else
                                   ("rw-" if (flags & 2) else "r--"))))
            continue
        for rva, rlen in diff_runs(ob, b, va):
            new = fr.get(rva, rlen)
            old = fo.get(rva, rlen)
            if fo.is_exec(rva):
                patches.append(dict(va=rva, old=old, new=new,
                                    why="in-place edit of executable bytes"))
            else:
                where = _where(orig, rva)
                lvl, why = classify_delta(where)
                skipped.append(dict(va=rva, len=rlen, section=where,
                                    old=old.hex(), new=new.hex(),
                                    level=lvl, why=why))
                if lvl == "warn":
                    notes.append("delta in %s at %#x (%d B) not reproduced: %s"
                                 % (where, rva, rlen, why))

    # ---- 2. the E9Patch loader's map table ----------------------------
    TYPE = {0: "trampoline", 1: "reserve", 2: "refactor"}
    for m in e9_maps(vi):
        if m["absolute"]:
            notes.append("absolute map at %#x -- not supported" % m["va"])
            continue
        content = _read_file_range(rew.data, m["off"], m["len"])
        prot = ("r" if m["r"] else "-") + ("w" if m["w"] else "-") + \
               ("x" if m["x"] else "-")
        if in_orig(m["va"], m["len"]) or (m["va"] < orig_end and
                                          fo.covers(m["va"])):
            # REFACTOR: a patched copy of pages the original image already has.
            # -> reproduce as byte runs written in place.
            ob = fo.get(m["va"], m["len"])
            if ob is None:
                # partially covered (the tail runs past the last PT_LOAD's
                # filesz); fall back to page-by-page.
                for p in range(0, m["len"], PAGE):
                    va = m["va"] + p
                    o1 = fo.get(va, PAGE)
                    if o1 is None:
                        maps.append(dict(va=va, len=PAGE,
                                         content=content[p:p + PAGE],
                                         type="refactor-tail", prot=prot))
                        continue
                    for rva, rlen in diff_runs(o1, content[p:p + PAGE], va):
                        patches.append(dict(va=rva, old=fo.get(rva, rlen),
                                            new=content[rva - m["va"]:
                                                        rva - m["va"] + rlen],
                                            why="E9Patch %s map" %
                                                TYPE[m["type"]]))
                continue
            for rva, rlen in diff_runs(ob, content, m["va"]):
                o = rva - m["va"]
                patches.append(dict(va=rva, old=ob[o:o + rlen],
                                    new=content[o:o + rlen],
                                    why="E9Patch %s map" % TYPE[m["type"]]))
        else:
            maps.append(dict(va=m["va"], len=m["len"], content=content,
                             type=TYPE[m["type"]], prot=prot))

    # ---- 3. sanity + the things we cannot do in a live process --------
    unpatchable = []
    if vi.cfg["num_traps"]:
        for rip, tramp in sorted(vi.traps().items()):
            unpatchable.append(dict(va=rip, tramp=tramp, tactic="B0",
                                    why="needs the E9Patch SIGILL handler "
                                        "(--full-coverage build)"))
    env = rew_path + ".ptlog.env"
    sink = "ptwrite"
    if os.path.exists(env):
        for line in open(env):
            if line.startswith("PTLOG_SINK="):
                sink = line.strip().split("=", 1)[1]
    if sink == "buffer":
        notes.append("SINK=buffer: the injected runtime rt/ptlogrt.e9rt is "
                     "started from DT_INIT_ARRAY, which has already run in a "
                     "live process; the attach path needs an explicit runtime "
                     "start-up (not implemented -- use --sink ptwrite)")

    # ---- 3b. B0 (SIGILL) sites cannot be installed in a live process ---
    # E9Patch's last-resort tactic writes an ILLEGAL instruction (0x27) plus an
    # int3 sled over the rest of the instruction and recovers in the loader's own
    # SIGILL handler, which an attached process does not have.  Writing those
    # bytes without the handler would kill the process, so the trap sites'
    # patches are REMOVED from the plan: the site is simply not instrumented
    # (its trampoline is still mapped and simply never reached).  The remedy is
    # the analyzer's `--avoid' re-solve, which moves the site somewhere a normal
    # tactic can reach -- exactly as for an unpatchable static site.
    traps = vi.traps()
    if traps:
        kept, dropped_bytes = [], 0
        tr = sorted(traps)
        for pt in patches:
            lo, hi = pt["va"], pt["va"] + len(pt["new"])
            hits = [t for t in tr if lo <= t < hi]
            if not hits:
                kept.append(pt)
                continue
            t0 = min(hits)
            # the trap sled: 0x27 at the site, then int3 (0xcc) for the rest of
            # the original instruction
            o = t0 - lo
            end = o + 1
            while end < len(pt["new"]) and pt["new"][end] == 0xcc:
                end += 1
            for a, b in ((0, o), (end, len(pt["new"]))):
                if b > a:
                    kept.append(dict(va=lo + a, old=pt["old"][a:b],
                                     new=pt["new"][a:b], why=pt["why"]))
            dropped_bytes += end - o
        patches = kept
        notes.append("B0/SIGILL tactic: %d trap site(s), %d byte(s) of trap "
                     "patch removed from the plan (see `unpatchable')"
                     % (len(traps), dropped_bytes))

    # ---- 4. serialise -------------------------------------------------
    jmaps = []
    for m in sorted(maps, key=lambda m: m["va"]):
        c = m["content"]
        if len(c) < m["len"]:
            c = c + b"\0" * (m["len"] - len(c))
        jmaps.append(dict(va=m["va"], len=m["len"], prot=m["prot"],
                          type=m["type"], blob=blob.add(c[:m["len"]])))
    patches = sorted(patches, key=lambda p: p["va"])
    regions = attribute_regions(vi, patches, jmaps)
    clusters = cluster_patches(patches)
    creg = cluster_region_sets(vi, patches, clusters, jmaps)
    jpatches = []
    for p, reg, cl in zip(patches, regions, clusters):
        jpatches.append(dict(va=p["va"], len=len(p["new"]),
                             old=blob.add(p["old"]), new=blob.add(p["new"]),
                             region=reg, cluster=cl, why=p["why"]))

    sm = None
    if sitemap_path and os.path.exists(sitemap_path):
        sm = os.path.abspath(sitemap_path)

    runtime_maps = build_runtime_maps(vi, rew, orig_end,
                                      os.path.abspath(rew_path))

    lo = min(lo for lo, _hi in orig_ranges)
    return dict(
        key=os.path.basename(orig_path),
        orig_path=os.path.abspath(orig_path),
        rewritten_path=os.path.abspath(rew_path),
        orig_size=os.path.getsize(orig_path),
        orig_sha256=_sha(orig_path),
        cluster_regions=[sorted(x) for x in creg],
        pie=orig.pie,
        link_low=lo,
        link_end=orig_end,
        sink=sink,
        sitemap=sm,
        patches=jpatches,
        maps=jmaps,
        unpatchable=unpatchable,
        skipped_deltas=skipped,
        runtime_maps=runtime_maps,
        notes=notes,
        stats=dict(n_patches=len(jpatches),
                   patch_bytes=sum(p["len"] for p in jpatches),
                   n_maps=len(jmaps),
                   map_bytes=sum(m["len"] for m in jmaps),
                   n_tramp_maps=sum(1 for m in jmaps if m["type"] == "trampoline"),
                   n_unpatchable=len(unpatchable),
                   n_clusters=len(creg),
                   n_clusters_unattributed=sum(1 for x in creg if not x),
                   n_skipped=len(skipped)),
    )


# --------------------------------------------------------------------------
# what /proc/PID/maps looks like once the plan is installed
# --------------------------------------------------------------------------

def build_runtime_maps(vi, rew, orig_end, rew_path):
    """The attached process's executable image, expressed against the REWRITTEN
    file -- i.e. exactly the map list `pt_capture2' records for a *static*
    E9Patch run, minus the E9Patch loader segment (which an attached process
    does not have).

    Why this is needed: after the attach, the bytes at the original image's own
    text addresses are the PATCHED bytes, which are NOT what is in the original
    file on disk, and the trampolines are anonymous memory.  `ptrecon' builds
    its decode image out of the sideband's file-backed mappings, so the sideband
    of an attach capture has to name the rewritten file at the offsets that hold
    those exact bytes.  `fix_sideband.py' rebases this list onto the live load
    address.  The result is byte-for-byte the image a static run decodes, so the
    two reconstructions are directly comparable.
    """
    pages = {}                       # page index -> (file_off, perms)

    def put(va, ln, off, perms):
        for k in range(0, ln, PAGE):
            pages[(va + k) // PAGE] = (off + k, perms)

    # (1) the rewritten ELF's own PT_LOADs, as the kernel maps them, excluding
    #     the segment E9Patch appends for its loader (va beyond the original
    #     image; it is also the one with p_paddr != p_vaddr).
    for va, memsz, off, filesz, flags in rew.loads:
        if va >= orig_end:
            continue
        perms = ("r" if flags & 4 else "-") + ("w" if flags & 2 else "-") + \
                ("x" if flags & 1 else "-")
        lo = va & ~(PAGE - 1)
        hi = (va + filesz + PAGE - 1) & ~(PAGE - 1)
        put(lo, hi - lo, off & ~(PAGE - 1), perms)

    # (2) the E9Patch loader's map table, in loader order (later wins).
    for m in e9_maps(vi):
        if m["absolute"]:
            continue
        perms = ("r" if m["r"] else "-") + ("w" if m["w"] else "-") + \
                ("x" if m["x"] else "-")
        put(m["va"], m["len"], m["off"], perms)

    # (3) coalesce the page map into runs of contiguous (offset, perms)
    out = []
    for pg in sorted(pages):
        off, perms = pages[pg]
        if out and out[-1]["va"] + out[-1]["len"] == pg * PAGE and \
           out[-1]["off"] + out[-1]["len"] == off and out[-1]["perms"] == perms:
            out[-1]["len"] += PAGE
        else:
            out.append(dict(va=pg * PAGE, len=PAGE, off=off, perms=perms,
                            file=rew_path))
    return out


def cluster_patches(patches, reach=15):
    """Group patches that must be installed all-or-nothing.

    E9Patch's T2/T3 tactics EVICT a neighbouring instruction: the evictor's
    punned jump overwrites the evictee's bytes, and the evictee is only correct
    because it, too, was redirected to a trampoline.  So dropping one patch of
    such a pair (because its trampoline page collided with something in the live
    process) corrupts the other -- which is exactly how a partially applied plan
    kills the target with SIGILL.  Eviction is always between neighbours, so any
    two patch runs within one maximum-length x86 instruction (15 B) of each other
    are put in the same cluster, and `ptattach.so' installs a cluster only if
    every patch in it is installable.
    """
    out = []
    cl = -1
    end = None
    for p in patches:
        if end is None or p["va"] > end + reach:
            cl += 1
        out.append(cl)
        end = p["va"] + len(p["new"])
    return out


class FastRead:
    """Page-level view of the rewritten runtime image (later mapping wins).

    `VirtualImage.read' walks every region for every read; whole-program CPython
    has 7 100 of them and the cluster scan does one read per cluster, so this
    flattens the region list into a page -> file-offset table once.
    """

    def __init__(self, vi):
        self.data = vi.elf.data
        self.pg = {}
        for va, size, off, _x in vi.regions:
            for k in range(0, size, PAGE):
                self.pg[(va + k) // PAGE] = off + k

    def read(self, va, n):
        out = bytearray()
        while len(out) < n:
            a = va + len(out)
            fo = self.pg.get(a // PAGE)
            if fo is None:
                break
            o = fo + (a % PAGE)
            take = min(n - len(out), PAGE - (a % PAGE))
            chunk = self.data[o:o + take]
            if len(chunk) < take:
                chunk = chunk + b"\0" * (take - len(chunk))
            out += chunk
        return bytes(out)


def cluster_region_sets(vi, patches, clusters, jmaps):
    """For every cluster, the SET of added regions its detours jump into.

    A single patch run can carry more than one detour (E9Patch happily writes
    two punned jumps into 18 adjacent bytes), and E9Patch's T2/T3 eviction ties
    neighbouring instructions together, so the unit that can be installed or
    skipped is the CLUSTER and the thing it depends on is a SET of trampoline
    regions -- not one region per patch.  Attributing a single region per patch
    and skipping per patch crashes the process (SIGILL/SIGSEGV).
    An EMPTY set means "could not tell": the tool then treats the cluster as
    depending on every region of the image.
    """
    fr = FastRead(vi)
    ivals = sorted((m["va"], m["va"] + m["len"], i)
                   for i, m in enumerate(jmaps))

    def find(target):
        lo, hi = 0, len(ivals) - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            a, b, i = ivals[mid]
            if target < a:
                hi = mid - 1
            elif target >= b:
                lo = mid + 1
            else:
                return i
        return None

    # A trampoline can jump into ANOTHER trampoline region (E9Patch chains them,
    # and an evictee's trampoline is reached from its evictor's), so the
    # dependency is the TRANSITIVE CLOSURE of the region graph, not just the
    # regions the original code jumps into.  Scanning only the code left CPython
    # jumping into an unmapped page -> SIGSEGV.
    dep = [set() for _ in jmaps]
    for i, m in enumerate(jmaps):
        buf = fr.read(m["va"], m["len"])
        for o in range(0, max(0, len(buf) - 4)):
            if buf[o] not in (0xe8, 0xe9):
                continue
            rel = int.from_bytes(buf[o + 1:o + 5], "little", signed=True)
            k = find(m["va"] + o + 5 + rel)
            if k is not None and k != i:
                dep[i].add(k)
    changed = True
    while changed:
        changed = False
        for i in range(len(dep)):
            new = set(dep[i])
            for k in dep[i]:
                new |= dep[k]
            new.discard(i)
            if new != dep[i]:
                dep[i] = new
                changed = True

    n = (max(clusters) + 1) if clusters else 0
    sets = [set() for _ in range(n)]
    i = 0
    while i < len(patches):
        c = clusters[i]
        j = i
        while j + 1 < len(patches) and clusters[j + 1] == c:
            j += 1
        lo = patches[i]["va"] - 6
        hi = patches[j]["va"] + len(patches[j]["new"]) + 4
        buf = fr.read(lo, hi - lo)
        for o in range(0, max(0, len(buf) - 4)):
            if buf[o] not in (0xe8, 0xe9):
                continue
            rel = int.from_bytes(buf[o + 1:o + 5], "little", signed=True)
            k = find(lo + o + 5 + rel)
            if k is not None:
                sets[c].add(k)
        i = j + 1
    for c in range(n):
        ext = set(sets[c])
        for r in sets[c]:
            ext |= dep[r]
        sets[c] = ext
    return sets


def attribute_regions(vi, patches, jmaps):
    """For each patch, which added region does its detour jump INTO?

    `ptattach.so' maps the regions with MAP_FIXED_NOREPLACE, and in a live
    process an address E9Patch picked may already be taken.  Knowing which sites
    depend on which region turns "this image is unpatchable" into "these N sites
    are unpatchable" -- and E9Patch usually aliases one physical trampoline page
    at many virtual addresses, so a single collision costs a handful of sites,
    not the image.

    Method: read the REWRITTEN runtime image around the patch and look for the
    `jmp rel32'/`call rel32' whose target lands inside an added region.  The
    window starts 6 bytes before the run because E9Patch also retargets a direct
    CALL by rewriting only its rel32 field, and it covers punned jumps because
    the displacement bytes are read from the runtime image, not from the patch.
    Returns a region index per patch, or 0xffffffff when none was found (the
    tool then treats the patch as depending on every region of the image).
    """
    ivals = sorted((m["va"], m["va"] + m["len"], i)
                   for i, m in enumerate(jmaps))

    def find(target):
        lo, hi = 0, len(ivals) - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            a, b, i = ivals[mid]
            if target < a:
                hi = mid - 1
            elif target >= b:
                lo = mid + 1
            else:
                return i
        return None

    out = []
    for p in patches:
        lo = p["va"] - 6
        n = len(p["new"]) + 11
        buf = vi.read(lo, n) or b""
        hit = None
        for o in range(0, max(0, len(buf) - 4)):
            if buf[o] not in (0xe8, 0xe9):
                continue
            rel = int.from_bytes(buf[o + 1:o + 5], "little", signed=True)
            k = find(lo + o + 5 + rel)
            if k is not None:
                hit = k
                break
        out.append(0xffffffff if hit is None else hit)
    return out


def _read_file_range(data, off, n):
    b = data[off:off + n]
    if len(b) < n:
        b = b + b"\0" * (n - len(b))
    return b


def _sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()


def _sections(elf):
    """[(name, addr, size)] from the section header table (link-time addrs)."""
    d = elf.data
    shoff = struct.unpack_from("<Q", d, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x3a)
    if not shoff or not shnum:
        return []
    so = shoff + shstrndx * shentsize
    stroff = struct.unpack_from("<Q", d, so + 0x18)[0]
    out = []
    for i in range(shnum):
        o = shoff + i * shentsize
        nameoff, _typ = struct.unpack_from("<II", d, o)
        addr = struct.unpack_from("<Q", d, o + 0x10)[0]
        size = struct.unpack_from("<Q", d, o + 0x20)[0]
        e = d.index(b"\0", stroff + nameoff)
        out.append((d[stroff + nameoff:e].decode("latin1"), addr, size))
    return out


def _where(elf, va):
    """Human-readable name of the link-time address `va' in `elf'."""
    phdr_end = elf.phoff + elf.phnum * elf.phentsize
    if va < 64:
        return "ELF header" + (" (e_entry)" if 0x18 <= va < 0x20 else "")
    if elf.phoff <= va < phdr_end:
        return "program header table [%d]" % ((va - elf.phoff) // elf.phentsize)
    for name, addr, size in _sections(elf):
        if addr and addr <= va < addr + size:
            return name
    for i, (v, m, o, f, fl) in enumerate(elf.loads):
        if v <= va < v + m:
            return "PT_LOAD[%d] flags=%d" % (i, fl)
    return "?"


# Deltas E9Patch makes to the FILE only so that the file boots; a live process
# is already booted, so they are not reproduced.  Anything not on this list is
# escalated to a plan-level warning rather than silently skipped.
_BENIGN = {
    "ELF header": "e_shoff / e_phnum bookkeeping",
    "ELF header (e_entry)": "entry redirected to the E9Patch loader; a live "
                            "process has already run its entry point",
    ".dynamic": "DT_FINI/DT_INIT_ARRAY redirection for the injected runtime; a "
                "live process has already run its initialisers",
}
_BENIGN_PREFIX = {
    "program header table": "the PT_LOAD that maps the E9Patch loader",
}


def classify_delta(name):
    if name in _BENIGN:
        return "benign", _BENIGN[name]
    for k, v in _BENIGN_PREFIX.items():
        if name.startswith(k):
            return "benign", v
    if name == ".note.gnu.property":
        return "warn", ("E9Patch clears the GNU_PROPERTY x86 feature bits "
                        "(CET IBT / shadow stack).  The kernel applies those at "
                        "execve() time, so an ALREADY RUNNING process keeps "
                        "whatever it was started with.  Harmless on a kernel that does not "
                        "enforce user-space IBT; a shadow-stack-enabled process "
                        "must be started with the feature off.")
    return "warn", "unexpected non-executable delta -- inspect before trusting "\
                   "this plan"


class Blob:
    """Append-only byte pool; identical byte strings are shared."""

    def __init__(self):
        self.buf = bytearray()
        self.index = {}

    def add(self, b):
        b = bytes(b)
        k = self.index.get(b)
        if k is not None:
            return k
        off = len(self.buf)
        self.buf += b
        self.index[b] = off
        return off


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--orig", action="append", required=True,
                    help="the ORIGINAL image (what the live process runs)")
    ap.add_argument("--rewritten", action="append", required=True,
                    help="the E9Patch-rewritten image from rewrite.py")
    ap.add_argument("--sitemap", action="append", default=None,
                    help="site map JSON (default: <rewritten>.sitemap.json)")
    ap.add_argument("-o", "--out", required=True, help="plan JSON path")
    ap.add_argument("--with-loader", action="store_true",
                    help="also map the E9Patch loader segment (needed only for "
                         "a --full-coverage/B0 build's SIGILL handler)")
    a = ap.parse_args()
    if len(a.orig) != len(a.rewritten):
        raise SystemExit("--orig and --rewritten must be given in pairs")
    sms = a.sitemap or []
    while len(sms) < len(a.orig):
        sms.append(a.rewritten[len(sms)] + ".sitemap.json")

    blob = Blob()
    images = []
    for o, r, s in zip(a.orig, a.rewritten, sms):
        p = build_image_plan(o, r, s, blob, a)
        if not a.with_loader:
            drop = [m for m in p["maps"] if m["type"] == "loader"]
            p["maps"] = [m for m in p["maps"] if m["type"] != "loader"]
            if drop:
                p["notes"].append("E9Patch loader segment(s) omitted "
                                  "(--with-loader to keep): " +
                                  ", ".join("%#x+%#x" % (m["va"], m["len"])
                                            for m in drop))
            p["stats"]["n_maps"] = len(p["maps"])
            p["stats"]["map_bytes"] = sum(m["len"] for m in p["maps"])
        images.append(p)
        st = p["stats"]
        print("[plan] %-22s patches=%-6d (%d B)  maps=%-6d (%.1f MiB)  "
              "unpatchable=%d  skipped=%d"
              % (p["key"], st["n_patches"], st["patch_bytes"], st["n_maps"],
                 st["map_bytes"] / 1048576.0, st["n_unpatchable"],
                 st["n_skipped"]), file=sys.stderr)

    blob_path = os.path.splitext(a.out)[0] + ".blob"
    with open(blob_path, "wb") as f:
        f.write(blob.buf)
    plan = dict(version=1, blob=os.path.abspath(blob_path), images=images)
    with open(a.out, "w") as f:
        json.dump(plan, f, indent=1)
    bin_path = os.path.splitext(a.out)[0] + ".pplan"
    write_binary_plan(bin_path, images, blob.buf)
    print("[plan] wrote %s (%d image%s) + %s (%.1f MiB) + %s"
          % (a.out, len(images), "" if len(images) == 1 else "s",
             blob_path, len(blob.buf) / 1048576.0, bin_path), file=sys.stderr)


# --------------------------------------------------------------------------
# the binary form the Pintool reads (it mmap()s this file; no JSON in Pin)
# --------------------------------------------------------------------------
#
#   hdr    magic[8]="PTPLAN01" u32 version u32 n_images
#          u64 img_off u64 str_off u64 blob_off u64 blob_len
#   img    u64 key_off u64 path_off u64 orig_size u64 link_low u64 link_end
#          u32 pie u32 n_patches u64 patch_off u32 n_maps u32 _pad u64 map_off
#          u8 sha[32] u32 n_clusters u64 cluster_off u64 regidx_off
#   cluster u32 first u32 count           (into the regidx array)
#   regidx  u32 region
#   patch  u64 va u64 old_off u64 new_off u32 len u32 region u32 cluster
#   map    u64 va u64 len u64 blob_off u32 prot u32 type
#
# `*_off' are file offsets; `old_off'/`new_off'/`blob_off' of a record are
# offsets into the blob AREA (blob_off + x).  prot bits: 1=r 2=w 4=x.
HDR_FMT = "<8sIIQQQQ"
IMG_FMT = "<QQQQQIIQIIQ32sIQQ"
PATCH_FMT = "<QQQIII"
MAP_FMT = "<QQQII"
MAP_TYPE = {"trampoline": 0, "reserve": 1, "refactor-tail": 2, "loader": 3}


def write_binary_plan(path, images, blobbuf):
    strtab = bytearray()
    stroff = {}

    def S(x):
        x = x.encode()
        if x in stroff:
            return stroff[x]
        o = len(strtab)
        strtab.extend(x)
        strtab.append(0)
        stroff[x] = o
        return o

    hdr_sz = struct.calcsize(HDR_FMT)
    img_sz = struct.calcsize(IMG_FMT)
    p_sz = struct.calcsize(PATCH_FMT)
    m_sz = struct.calcsize(MAP_FMT)

    # pass 1: string table + sizes
    keys = [(S(i["key"]), S(i["orig_path"])) for i in images]
    img_off = hdr_sz
    tab_off = img_off + img_sz * len(images)
    off = tab_off
    layout = []
    for i in images:
        po = off
        off += p_sz * len(i["patches"])
        mo = off
        off += m_sz * len(i["maps"])
        co = off
        off += 8 * len(i["cluster_regions"])
        ro = off
        off += 4 * sum(len(x) for x in i["cluster_regions"])
        layout.append((po, mo, co, ro))
    str_off = off
    off += len(strtab)
    off = (off + 15) & ~15
    blob_off = off

    out = bytearray()
    out += struct.pack(HDR_FMT, b"PTPLAN01", 1, len(images),
                       img_off, str_off, blob_off, len(blobbuf))
    for i, (ko, pa), (po, mo, co, ro) in zip(images, keys, layout):
        out += struct.pack(IMG_FMT, ko, pa, i["orig_size"], i["link_low"],
                           i["link_end"], 1 if i["pie"] else 0,
                           len(i["patches"]), po, len(i["maps"]), 0, mo,
                           bytes.fromhex(i["orig_sha256"]),
                           len(i["cluster_regions"]), co, ro)
    for i in images:
        for q in i["patches"]:
            out += struct.pack(PATCH_FMT, q["va"], q["old"], q["new"],
                               q["len"], q.get("region", 0xffffffff),
                               q.get("cluster", 0))
        for m in i["maps"]:
            prot = (1 if "r" in m["prot"] else 0) | (2 if "w" in m["prot"] else 0) \
                   | (4 if "x" in m["prot"] else 0)
            out += struct.pack(MAP_FMT, m["va"], m["len"], m["blob"], prot,
                               MAP_TYPE.get(m["type"], 0))
        k = 0
        for cr in i["cluster_regions"]:
            out += struct.pack("<II", k, len(cr))
            k += len(cr)
        for cr in i["cluster_regions"]:
            for r in cr:
                out += struct.pack("<I", r)
    assert len(out) == str_off, (len(out), str_off)
    out += strtab
    out += b"\0" * (blob_off - len(out))
    out += blobbuf
    with open(path, "wb") as f:
        f.write(bytes(out))


if __name__ == "__main__":
    main()
