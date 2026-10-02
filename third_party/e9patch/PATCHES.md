# Local changes to E9Patch 1.0.0

Two source files differ from upstream (both inert unless requested), and `contrib/zydis/Makefile` is added to build
`libZydis.a` from the bundled Zydis sources:

* `src/e9patch/e9CFR.cpp` -- control-flow-recovery (`-OCFR`, used by `rewrite.py --cfr`) target fixes: every
  `endbr64`, IRELATIVE relocations in `.rela.dyn` and `.rela.plt`, `STT_GNU_IFUNC`, and every defined FUNC/IFUNC
  of `.dynsym` and `.symtab` are treated as jump targets.  Only active under `-OCFR`.
* `src/e9patch/e9trampoline.cpp` -- `E9PATCH_RELOCMAP=FILE` writes a relocation record (unset: no effect).

Non-CFR rewrites are byte-identical to stock E9Patch.  `ptracer/build.sh` rebuilds `e9patch` whenever these
sources are newer than the binary.
