# PTracer artifact

PTracer is a binary-level memory tracer that cuts tracing overhead by an order of magnitude.
A static analysis finds the few values that must be logged, lightweight instrumentation logs
only those while Intel PT records control flow, and an offline pass reconstructs the complete
memory trace.

## Update (9/23)

This repo currently contains part of the full artifact. Over the next three weeks we will keep
adding experiments in an append-only way (you will not need to rerun what you already ran),
answer issues, fix bugs, and provide machines to reviewers whose machine does not meet the
requirements.

Reproducible now — entry point `run/experiments_v1.sh`: part of Figure 5 (runtime overhead, omitting traditional tracers,
temporarily omitting Fast mode for Java and Node.js due to a bug we are fixing), Figure 6
(ablation of the new techniques), Section 5.6 (inaccuracy).

## Requirements

* Hardware: bare-metal x86-64 (no VM), **Intel CPU with Intel PT and PTWRITE** (Ice Lake /
  Gracemont or newer), >= 8 cores, 32 GB RAM, 100 GB free disk, >= 1 GB/s sequential disk write.
* Software: Linux (tested on kernels 6.8-7.0) and Docker. Root is not needed.
* Run `bash envcheck/check_env.sh` on the host: it checks all of the above, measures disk and
  DRAM bandwidth, and says what to expect on your machine. If it fails, contact us for a machine.
* You may use coding AI agents (Claude Code, Codex, Cursor, ...) to inspect and run this artifact;
  we built it with Claude Code. Be aware of their inaccuracy: we have been seeing even Fable 5.1 and GPT 6 Astra
  make strange mistakes in this project.

## Contact us for a machine or questions

Please feel free to create GitHub issues if you need us to provide a machine or have questions.
We cannot guarantee that we are able to provide high-end AWS machines to all reviewers,
but at least there is a desktop in our lab that can run almost all experiments with worse performance.

## Set-up and run

```sh
git clone <this repository> ptracer-artifact && cd ptracer-artifact
bash envcheck/check_env.sh                 # host check            [1 human-min, seconds]
bash docker/build.sh                       # build the image       [1 human-min, 20-40 compute-min]
bash docker/run.sh run/experiments_v1.sh   # all experiments       [10 human-min, 4-8 compute-h]
```

Results appear in `out/experiments_v1/` on the host — three things to read:

| file | paper |
|---|---|
| `six_suite_overhead.png` | Figure 5 (runtime overhead) |
| `ablation.png` | Figure 6 (ablation of the new techniques) |
| `inaccuracy.md` | Section 5.6 (inaccuracy table) |

The script prints a time estimate first, skips finished experiments and resumes interrupted ones;
`--dry-run` shows the plan. Everything is measured on your machine; the only shipped numbers are
the paper's bars for comparison. A user in the `docker` group can run everything (the container is
privileged, so Intel PT works without changing the host); `QUIET=1` additionally locks the CPU
frequency for the run, which needs `sudo`.

## Claims and experiments

| claim | script | time |
|---|---|---|
| C1 (Figure 5): PTracer traces all six suites at a fraction of traditional tracers' overhead, in HiFi and Fast mode | `run/01_overhead.sh` | 3-5 compute-h |
| C2 (Section 5.6): the reconstructed trace is complete to < 0.1 % on PolyBench/C and pyperformance | `run/02_accuracy.sh` | 1-3 compute-h, tens of GB of temporary traces |
| C3 (Figure 6): Intel PT and the static analysis each remove a distinct part of the overhead | `run/03_ablation.sh` | ~30 compute-min |
| traditional tracers (Pin memtrace, libdft, Valgrind, Spindle) | `run/optional_baselines.sh` | hours; optional, no figure depends on it |

One configuration per mode, used everywhere:

* **HiFi**: the program runs under Intel Pin's JIT, which inserts the logging.
* **Fast**: the program's binary is rewritten ahead of time with small trampolines that do the
  logging; no Pin at run time. For Node.js and Java the trampolines are patched into the
  JIT-compiled code at run time; no Pin.

We are working on a further optimization for Java and Node.js that is very promising but needs some more time.

Shipped versions are never changed; later updates add `run/experiments_v2.sh` and so on.

## Layout and licence

```
envcheck/    host environment check
docker/      Dockerfile, build.sh, run.sh
run/         experiments_v1.sh (entry point), 01..03 per experiment, optional_baselines.sh
ptracer/     static analyzer, runtime (binary rewriter, Pin tools), offline reconstructor
third_party/ Pin 3.20, Pin 4.4, the binary rewriter, baseline tracers (see third_party/SOURCE.md)
suites/      the six benchmark suites (see suites/*/SOURCE.md)
data/        experiment inputs: HiFi plans and the paper's Figure 5 bars
figures/     figure generators
```

PTracer's own code is MIT-licensed (`LICENSE`); third-party components keep their own licences
in their directories.
