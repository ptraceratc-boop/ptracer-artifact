# suites/pyperformance — whole-program CPython 3.12 + pyperformance

## Trees

| tree | content |
|---|---|
| `cpython-cg/` | the vanilla interpreter every analysis is keyed to: CPython 3.12.13 (pristine source, `./configure CFLAGS=-O2`, computed-goto dispatch, gcc 13.3.0, Ubuntu 24.04), installed tree. `bin/python3.12` Build ID db4b5fb0b5ee58e347077374af7a99d7734c7689. The stdlib `test/` package, `libpython3.12.a`, bundled pip and `__pycache__` are not included. |
| `shadow/` | the same tree with `bin/python3.12` and six extension modules (`_bisect _json _random _sha2 math zlib`) replaced by their E9Patch-rewritten images, PTWRITE sink — the six-benchmark slice (`nbody richards float go json_dumps regex_v8`) |
| `shadowb/` | the same with the software-buffer sink (the buffer runtime is injected into `python3.12`) |
| `shadowlib/` | rewritten `libc.so.6 libm.so.6 libz.so.1` (PTWRITE sink) for `shadow/`, loaded through `LD_LIBRARY_PATH` |
| `suite45/shadow.n/`, `suite45/lib.n/`, `suite45/maps.n/` | the 34-image plain build used for the 45-benchmark set: interpreter + 30 extension modules replaced, libc/libm/libz in `lib.n`, one site map per image in `maps.n` |
| `pyperf/benchmarks/` | the benchmark programs of pyperformance 1.14.0 (`data-files/benchmarks`, all 74 `bm_*` directories) |
| `pyperf/run_one.py`, `pyperf/fakepyperf.py` | the harness: runs one benchmark's `run_benchmark.py` in-process with a stand-in `pyperf` module; `PPF_LOOPS` / `PPF_WARM` control the iterations; `KTIME <s>` is printed to stderr |
| `pyperf/benches45.txt` | the 45 benchmarks that run under this interpreter and harness (the accuracy/correctness set); the overhead slice is the six named above |

Running the rewritten interpreter (paths relative to this directory):

```
LD_LIBRARY_PATH=$PWD/shadowlib shadow/bin/python3.12 pyperf/run_one.py nbody      # PTWRITE sink
PTLOG_DIR=/dev/null shadowb/bin/python3.12 pyperf/run_one.py nbody                # buffer sink
```

The rewritten trees are complete install trees (CPython finds its stdlib from `sys.prefix`),

## Images the analyses are keyed to

The specs, site maps and rewritten images are derived from these exact bytes: the vanilla
`cpython-cg/bin/python3.12`, its `lib/python3.12/lib-dynload/*.so`, and the distribution's
`libc.so.6` / `libm.so.6` / `libz.so.1` / `ld-linux-x86-64.so.2` in `../sysroot` (glibc
2.39-0ubuntu8.8, zlib 1:1.3.dfsg-3.1ubuntu2.2).  `ld-linux-x86-64.so.2` is analyzed but never
what the process actually maps in those configurations.

```
75409468da12a5e3e6b54744ecb23393eabf6ded57cd76bd66b95405766444b6  cpython-cg/bin/python3.12
a2141598c71cb65195739fe4ff1db2f66d3fb1a933b2285e8c06ca01bbf72ed1  shadow/bin/python3.12
92f82bc8e86a0e876d1913e3e557153cd3139dce5e9dc0fadffc2e8f7d238cf1  shadowb/bin/python3.12
527f642f87107440dd1f4798dade3009ed9e031f18b860d9b13acd611ddde848  shadowlib/libc.so.6
34e0779332615650a4d373f87441b7692769e8559e7bbde1c45776158d1f68fb  shadowlib/libm.so.6
679de686d49dd05aaca205bd30669d27fed6abff42729671ac3552779b3e1b33  shadowlib/libz.so.1
aaf00b78ac7a19b9389a6617e6c4d5a3c24faa7570edd7087694eaa3b4ec945f  suite45/shadow.n/bin/python3.12
a71d91c2d647a2b418af1343040c225640e68840cba8f66246732913b0607427  suite45/lib.n/libc.so.6
d72adbfef39e1bf9f2a4fe4bc3541bb27d9c9d3da305a4fbe592c5ccd7bc49c2  suite45/lib.n/libm.so.6
7dc9f0bbef5aca5e9af89ea12376bc9892c1517f3b134358e7cceaed40eb77cd  suite45/lib.n/libz.so.1
```

Every `lib-dynload/*.so` is in `../MANIFEST.sha256`.  The install prefix recorded in the
trees' `sysconfig` data is a placeholder (`/opt/cpython-cg`); the interpreter derives its real
prefix from its own location.
