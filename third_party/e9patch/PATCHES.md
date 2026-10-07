# Local changes to E9Patch 1.0.0

These source files differ from upstream (all inert unless requested), and `contrib/zydis/Makefile` is added to build
`libZydis.a` from the bundled Zydis sources:

* `src/e9patch/e9CFR.cpp` -- control-flow-recovery (`-OCFR`, used by `rewrite.py --cfr`) target fixes: every
  `endbr64`, IRELATIVE relocations in `.rela.dyn` and `.rela.plt`, `STT_GNU_IFUNC`, and every defined FUNC/IFUNC
  of `.dynsym` and `.symtab` are treated as jump targets.  Only active under `-OCFR`.
* `src/e9patch/e9trampoline.cpp` -- `E9PATCH_RELOCMAP=FILE` writes a relocation record (unset: no effect).
* `src/e9patch/e9tactics.cpp`, `e9patch.cpp`, `e9patch.h` -- `--tactic-T3-realloc` (default off; `rewrite.py --gt-all`
  turns it on for the accuracy twins only): when T3 placed its punned jump inside a neighbour but could not then evict
  that neighbour, retry the jump in each sub-window of its range where the most significant free rel32 byte differs
  (that byte is the high byte of the evicting jump, so the allocator's one choice can leave the evictee no window).

Non-CFR rewrites are byte-identical to stock E9Patch.  `ptracer/build.sh` rebuilds `e9patch` whenever these
sources are newer than the binary.
