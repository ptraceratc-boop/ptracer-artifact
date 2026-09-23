# Optional baselines: Valgrind and Spindle-plus

These two traditional-tracer baselines are used only by `run/optional_baselines.sh`.  They are
slow, their results are not needed for the three figures, and they are not vendored as binaries.
The exact steps that produced the measured builds follow; both install under `third_party/`.

## Valgrind 3.22.0 with the lackey memory-tracer patch

Upstream lackey's `--trace-mem` prints text per access and ran at ~270x; the measured baseline
is lackey patched into a lightweight binary memory tracer (reads + writes, fixed 16-byte records,
bulk-written; basic/detailed counts and SB/instruction tracing forced off).  The patch is
`valgrind-lackey-memtrace.patch`.

```
cd third_party
curl -LO https://sourceware.org/pub/valgrind/valgrind-3.22.0.tar.bz2
echo "c811db5add2c5f729944caf47c4e7a65dcaabb9461e472b578765dd7bf6d2d4c  valgrind-3.22.0.tar.bz2" | sha256sum -c
tar xjf valgrind-3.22.0.tar.bz2
cd valgrind-3.22.0
patch -p1 < ../valgrind-lackey-memtrace.patch
./configure --prefix="$PWD/../valgrind-build"
make -j"$(nproc)" && make install
```

Result: `third_party/valgrind-build/bin/valgrind`.  Invocation used by the drivers:

```
valgrind --tool=lackey --log-file=<diag.log> <program> [args]
```

The trace goes to `lackey_memtrace.log` in the working directory, which the drivers point at
`/dev/null` (instrumentation cost only, no disk).  Valgrind cannot start memcached (it refuses
the server's rlimit setup, exit 71); that cell is reported as missing.

## Spindle-plus (S-Tracer with a thread-safe runtime)

Spindle (github.com/thu-pacman/Spindle) ships its static analysis (`MTS.cpp`) and the
S-Detector tool but not S-Tracer.  `spindle-plus/pass/STracer.cpp` re-implements S-Tracer's
runtime path on top of Spindle's unmodified analysis as an LLVM 18 pass plugin, and
`spindle-plus/stracer_lib_mt.c` is the runtime with a per-thread buffer so that multithreaded
targets (memcached) can be instrumented.  It is a compile-time tracer: the target is rebuilt
from source through LLVM bitcode.

Requirements: `clang-18`, `llvm-18` (`llvm-config-18`, `opt-18`, `llvm-link-18`).

```
cd third_party/spindle-plus/pass && ./build.sh          # -> libSTracerPlusPass.so
```

Instrumenting a program (what the driver does for each PolyBench kernel and for memcached):

```
clang -O2 -emit-llvm -c <each TU>.c -o <TU>.bc            # same flags as the vanilla build
llvm-link-18 *.bc -o whole.bc
clang -O2 whole.bc -o prog.vanilla                          # same pipeline, no pass
opt-18 -load-pass-plugin=libSTracerPlusPass.so -passes=stracerplus whole.bc -o instr.bc
clang -O2 instr.bc ../stracer_lib_mt.c -lm -pthread -o prog.straced
STRACE_OUT=/dev/null ./prog.straced ...                     # trace discarded, as for every baseline
```

Overhead is `prog.straced` against `prog.vanilla` (both from the same bitcode pipeline), so
the number isolates the instrumentation.  For memcached the 27 translation units the configure
step selected (`suites/memcached/memcached-1.6.21`, EXTSTORE on) are compiled with
`-O2 -DHAVE_CONFIG_H -pthread -fno-omit-frame-pointer` and linked with `-levent -lpthread -lm`.
Spindle-plus covers PolyBench and memcached only: it needs the target's source through clang,
which rules out the interpreter, Rust and JIT suites.
