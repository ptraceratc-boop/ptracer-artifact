# suites/polybench — PolyBench/C 4.2.1

| item | content |
|---|---|
| `polybench-c/` | PolyBench/C 4.2.1 sources (Ohio State University; git 3e872547cef7e5c9909422ef1e6af03cf4e56072). Prebuilt `bin*/` directories and the PDF are not included. |
| `targets/` | the 30 kernels x 3 dataset sizes the experiments run: `<kernel>_mini`, `<kernel>_small`, `<kernel>_large` (90 ELF executables) |
| `build_targets.sh` | the recipe the targets were built with (gcc 13.3.0, Ubuntu 24.04; `-O1 -fno-inline -fno-inline-functions-called-once -g -DPOLYBENCH_TIME`) |

Which size is used where: `_large` for the overhead and ablation runs (PolyBench's own kernel
timer), `_small` for the kernel-window accuracy runs against the Pin ground truth, `_mini` for
the whole-program coverage runs with rewritten libc/libm.  The overhead slice is `gemm`,
`atax`, `jacobi-2d`, `correlation`, `durbin`; the accuracy run covers all 30 kernels.

The vendored `targets/` are the images the cached specs and rewrites are keyed to.  A
rebuild with `build_targets.sh` reproduces the recipe but not the bytes (compiler build id,
paths), so a rebuilt target needs a fresh analysis.  Each target links only libc/libm from
`../sysroot`.

sha256 of the slice targets:

```
7bd2ddfd809c07e196bff11eb93807f0e21da2c4bbb57f1c3485657aee15d2bc  targets/gemm_mini
e719ea67d02f9390a033fe77607f5b6dd1e3073ea0aa3fbea236092a2e2e4b13  targets/gemm_small
6613e0cdbd1dd3dce03e572190fd6b7a222b0019daff4b679798d4b7202265bb  targets/gemm_large
cb2523ae8416ad8358c5eaa5e1bf4501a41398f94fbf6cfd803fc3df891d42aa  targets/atax_mini
ce5bdd1f5739e08959073a0812fc425640ecad962b0b2681368efc3a104de26e  targets/atax_small
6d239504930ea3916bf6f0a2883ddc6e57469ffb78766ac324a79976a099013d  targets/atax_large
991363ba69aa6dbc19865359c089f52647951d27ee60c4caa6cc4b5eb83fda31  targets/jacobi-2d_mini
bafc6e30199a5f8150d2d0ab9b29502d062e4d6f2b6a4318941727850adc368f  targets/jacobi-2d_small
1cf734c6189a235d4e575648d3812720038aea4f0877971405113915923ad822  targets/jacobi-2d_large
071fece3120c176b7fcf4bab0572ff9152d8305e021d6ae64fe08fa66a7dcdae  targets/correlation_mini
50e2190aa149ed02cc3c7c551a5eb99a56b0d9f73ac7d9ae1e270f6f2be859fb  targets/correlation_small
914a342eefb6273a1f56bc6637db6241f014925bd260485522e2db89b80c00c0  targets/correlation_large
80c318efa6c526822861d51fa901b3b674aaca6b053ddfc9bb2c8c09a0a918af  targets/durbin_mini
cb70fa51f86dd592b60012a4d4280101018856622e2fa25bbe08874e1b445725  targets/durbin_small
2e9c1ca1f37ee0a33ec942e397c15f6b2c331fa04d79f98d7f82bef81d644fdd  targets/durbin_large
```

All 90 are listed in `../MANIFEST.sha256`.
