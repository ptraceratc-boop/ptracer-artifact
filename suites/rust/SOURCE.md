# suites/rust — RustStreamBench micro-bench

| item | content |
|---|---|
| `micro-bench/src`, `Cargo.toml`, `Cargo.lock` | the micro-bench application of RustStreamBench (GMAP, PUCRS; github.com/GMAP/RustStreamBench, git f40c95a4eed71e2594773bad39dcc27315cae92c) |
| `libs/rust-ssp` | the in-tree dependency `Cargo.toml` points at |
| `micro-bench/target/release/micro-bench` | the binary the experiments run: `cargo build --release`, rustc ac68faa20c58cbccd01ee7208bf3b6e93a7d7f96 (1.87.0), Ubuntu 24.04 |
| `README.md`, `LICENSE.md` | upstream (MIT) |

Cells and their exact invocations (run with `micro-bench/` as the working directory; the
program writes `result_sequential.txt` / `result_rayon.txt` there, which the drivers compare
byte-for-byte against the vanilla run):

| cell | argv | threads |
|---|---|---|
| `seq` | `micro-bench sequential 1024 1 3000 2000` | 1 |
| `rayon` | `micro-bench rayon 1024 2 3000 2000` | 4 rayon workers + main |

Whole-program rewrite images: `micro-bench` + `../sysroot/lib/x86_64-linux-gnu/libc.so.6` +
`libgcc_s.so.1`.

```
0686f7d4be9e6c9f9f8942c8aa54e598d8c2fafdd8069f934a2fea50ef2341a4  micro-bench/target/release/micro-bench
```

A `cargo build --release` with another toolchain produces a different image and needs a fresh
analysis; the vendored binary is the one the cached specs are keyed to.
