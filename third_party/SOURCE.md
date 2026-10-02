# third_party — vendored tracer substrates and baselines

Everything the experiments need is here; nothing is downloaded at build time.  `reassemble.sh`
restores any file that had to be split (`SPLIT.sha256` lists them; the list is empty for this
directory today, the script is kept so the build calls the two directories the same way).

| directory | what | version / provenance |
|---|---|---|
| `pin-4.4/` | Intel Pin kit, the substrate of the HiFi configuration and of the `memtrace` baseline in the overhead runs | pin-external-4.4-99977-g54fbb8814-gcc-linux (Intel, MIT) |
| `pin-3.20/` | Intel Pin kit for the `libdft` baseline (libdft64 builds against Pin 3.x only) and the no-op floor studies | pin-3.20-98437-gf02b61307-gcc-linux (Intel) |
| `e9patch/` | E9Patch/E9Tool static binary rewriter, the substrate of the Fast configuration; sources plus the built `e9patch`, `e9tool`, `e9loader_*.o` and `contrib/zydis/libZydis.a` (the JIT hooks link against it). `build.sh` rebuilds it. | github.com/GJDuck/e9patch, git eda844b20eddcc712a5d47316661aa768af16fb7 (1.0.0), GPLv3 |
| `memtrace/` | the "memorytracer" baseline: a Pintool that logs every memory operand to a fill buffer. Two local changes to `memtrace.cpp`: reads are logged as well as writes (upstream logged writes only) and `open()` replaces `creat()` for Pin 3.x's CRT. `obj-intel64/memtrace.so` is built against Pin 3.20; `p4/` is the same source with the compatibility headers (`p4/compat`) and makefile for Pin 4.4, `p4/obj-intel64/memtrace.so`. | github.com/aclements/memtrace, git f284669a016b6283cb89d4e460fd27ac4a9bdf99 |
| `libdft64/` | the DFT baseline: byte-level taint tracking over every memory and register operand; unmodified; `tools/obj-intel64/track.so` built against Pin 3.20 | github.com/AngoraFuzzer/libdft64, git 20804d5bae5d8aed31a71761b1a1149e35a0da95 |
| `no_clone3/` | a seccomp shim that makes `clone3` return ENOSYS so glibc falls back to `clone`, which Pin 3.20 virtualizes; used in front of every Pin 3.20 run of a threaded target (`gcc -O2 -o no_clone3 no_clone3.c`) | local |
| `spindle-plus/` | the Spindle S-Tracer baseline made thread-safe (see `OPTIONAL_TRACERS.md`); LLVM pass sources and runtime only, not built here | local + Spindle's MTS.cpp/MTS.h (github.com/thu-pacman/Spindle, git 15c68cbfe1559e0e4c70588e43725e0916bc5af7) |
| `valgrind-3.22.0.tar.bz2` | Valgrind 3.22.0 source release, built by `run/lib/build_baselines.sh` with the patch below | sourceware.org/pub/valgrind, sha256 c811db5add2c5f729944caf47c4e7a65dcaabb9461e472b578765dd7bf6d2d4c, GPLv2 |
| `valgrind-lackey-memtrace.patch` | the patch that turns Valgrind 3.22.0's lackey into a binary memory tracer (see `OPTIONAL_TRACERS.md`) | local |

Rebuilding the Pintools:

```
cd memtrace     && make PIN_ROOT=../pin-3.20          # obj-intel64/memtrace.so
cd memtrace/p4  && make PIN_ROOT=../../pin-4.4        # p4/obj-intel64/memtrace.so
cd libdft64     && make PIN_ROOT=../pin-3.20          # tools/obj-intel64/track.so
```

Pin's kernel-version check is advisory on recent kernels; every driver passes `-ifeellucky`.

```
ad8d05780cbd77bd1fee2503c6797c78193a79f37f5dd6ec386804c5fffec323  pin-4.4/pin
d963f5b4b8ae14c7db331e3d65f54160d04e1c884efe24c0f3eb60fecabc904a  pin-3.20/pin
e5d5e309aa1ac2138b35433d72a45a32b0cc737061289aa4926513e92a0edb3d  e9patch/e9patch
cd8f02122cda2f9796a9aa988754e42cf1844956d659b9f75c072220fbbea05e  e9patch/e9tool
f2de8150a2ea2d6863e8a3873c8b46808f7acdba7e9a4474aa9a3a8df97451fb  e9patch/contrib/zydis/libZydis.a
f04633a643989dba7c6d20587865100d7fa2d56729c5862f76c6a007c6de27ce  memtrace/obj-intel64/memtrace.so
6cc6a5cf14cdfde62d03dc2cdacc7a7db40207a34f177ca019682d54568ccd15  memtrace/p4/obj-intel64/memtrace.so
745d2d8fe70569d0e4822e8aa9c80e7b7e22de9e6b7a6f5eb41ebbcebcee3e06  libdft64/tools/obj-intel64/track.so
ee4fafe0e351e4836fb0a3290636bc956f3c97a530d8ec9ab131352246e7a237  no_clone3/no_clone3
```
