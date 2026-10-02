# PTracer artifact

PTracer is a binary-level memory tracer that cuts tracing overhead by an order of magnitude.
A static analysis finds the few values that must be logged, lightweight instrumentation logs
only those while Intel PT records control flow, and an offline pass reconstructs the complete
memory trace.

## Status (Updated Oct 1)

**We apologize for the late update! We have been fixing issues caused by compatibility, speeding up experiments, new techniques, bugs, and AI messing up code. If you ran experiments by Oct 1, please re-run them with the latest code. Sorry again for the trouble!**

* **Part 1: Overhead Figure** (the most important figure in our paper) -- execute `run/fig5.sh` for PTracer's bars,
  and then if you have time, execute `run/fig5_traditional.sh` for the traditional tracers.
* **Part 2: Inaccuracy** (inaccuracy of PTracer's most inaccurate modes) -- execute 
  `run/02_accuracy.sh`.
* Part 3 and so on: Figure 6 (ablation), the offline reconstruction figure, DeathStarBench and the other experiments: **not ready yet**.

## Requirements

* Hardware: bare-metal x86-64 machine (not VM), **Intel CPU with Intel PT** (Ice Lake / Gracemont or newer).
  Intel PT required; PTWRITE only for the *-PTWRITE bars (4th-gen Xeon Scalable / 12th-gen Core P-cores or newer); every
  other configuration executes no PTWRITE. >= 32 physical cores for the default lane layout (fewer work with `TIMED_LANES`/`HELPER_LANES`, slower), 64 GB RAM, >= 60 GB free disk (see below), >= 1 GB/s sequential disk write. We can provide instances of this machine, and please contact us if you need one: AWS c7i.metal-24xl (4th-gen Xeon Scalable, 48 cores / 96 vCPUs, 192 GB RAM, Ubuntu 24.04, Docker).
* Software: Linux (tested on kernels 6.8-7.0) and Docker. Root is not needed, except one `sudo` for
  `kernel.yama.ptrace_scope` (see below).
* Please run `bash envcheck/check_env.sh` on the host: it checks all of the above, measures disk and
  DRAM bandwidth, and says what to expect on your machine. If it fails, contact us for a machine.
* You may use coding AI agents (Claude Code, Codex, Cursor, ...) to inspect and run this artifact;
  we built it with Claude Code. Be aware of their inaccuracy: we have been seeing even Fable 5.1 and GPT 6 Astra
  make lots of mistakes in this project.

## Contact us for a machine or questions

Please feel free to create GitHub issues if you need us to provide a machine or have questions.
We cannot guarantee that we are able to provide high-end AWS machines to all reviewers,
but at least there is a workstation in our lab that can run almost all experiments with worse performance.


## Part 1 and Part 2 (Ready Oct 1)

### How to run
```sh
# Part 1: Overhead Figure
git clone <this repository> ptracer-artifact && cd ptracer-artifact
bash envcheck/check_env.sh                          # host check               [1 human-min, seconds]
bash docker/build.sh                                # build the image          [1 human-min, ~1 h]
bash docker/run.sh run/fig5.sh --dry-run            # the plan, nothing run    [1 human-min, seconds]
bash docker/run.sh run/fig5.sh                      # Figure 5, PTracer bars   [5 human-min, ~1-1.5 days from scratch]
bash docker/run.sh run/fig5_traditional.sh          # Figure 5, other tracers  [5 human-min, ~4-5 h, <= ~11 h]
# Part 2: Inaccuracy
bash docker/run.sh run/02_accuracy.sh --dry-run     # Section 5.6, the plan    [1 human-min, seconds]
bash docker/run.sh run/02_accuracy.sh               # Section 5.6, inaccuracy  [5 human-min, ~5 h on 8 lanes]
```

We realize that the whole experiment is very slow, so we reduce the repetition number of experiments on some time-consuming benchmarks.

`docker/run.sh run/fig5.sh` asks for `sudo` once: the Java HiFi bars run the JVM under Pin, which must attach to the
child processes the JVM starts, so it sets `kernel.yama.ptrace_scope=0` for the run and restores the previous value
on exit (equivalently, run `sudo sysctl -w kernel.yama.ptrace_scope=0` yourself first; `SKIP_JAVA_HIFI=1` skips
those two bars). Run the commands in a `tmux`/`screen` session; an interrupted run is resumed by running the same
command again.

Quick functional check (kick the tires): `QUICK=1 bash docker/run.sh run/fig5.sh` and then
`QUICK=1 bash docker/run.sh run/fig5_traditional.sh` run every benchmark of every suite and every bar once, with
minimal iteration counts, short JIT drains and a 60 s limit per traditional-tracer run (a run stopped there shows as
"Err", never as 200x), into `out/quick/` (~15 h in all, image builds included; Java HiFi takes ~10 of them). QUICK numbers only show that everything runs; they are not for comparison with the paper.

### What to expect

Below is a table of approximate time per suite. (a) = all PTracer
columns of the suite (Uninstrumented, HiFi, HiFi-PTWRITE, Fast, Fast-PTWRITE); (b) = all traditional-tracer columns (`run/fig5_traditional.sh`), with an upper bound: every run of every tracer stopped at its 200x limit, same as paper. 

| suite | (a) PTracer columns | (b) traditional tracers, at most |
|---|---|---|
| PolyBench/C | ~1 h | ~10 h |
| pyperformance | ~2 h | ~10 h |
| Rust Stream | < 1 h | < 1 h |
| Memcached | < 1 h | < 1 h |
| Node.js (Web Tooling) | ~6 h | < 1 h |
| Java (Renaissance) | ~19 h | ~10 h |

While a step runs, `out/experiments_v1/six_suite_overhead.png` (QUICK: `out/quick/six_suite_overhead.png`) is redrawn
whenever new results arrive (at most once a minute): every bar slot of the final figure is there from the start,
labelled "pending" until its rows exist, then drawn from the rows measured so far.

If you have run experiments before Oct 1, please discard their results and cache if any: our fixes of PTracer
make previous cache files unusable. You can simply run `git restore .` to discard everything, or `git clone` the repo again to a different dir.

Disk: >= 60 GB free. The peak is about 30 GB. 

Every script prints its plan first (`--dry-run` shows it without running), skips finished work and resumes
an interrupted run from the rows already measured, so it can be restarted at any time. Run the steps one after the
other (they time on the same cores). A user in the `docker` group can run everything (the container is privileged,
so Intel PT works without changing the host); `QUIET=1` additionally locks the CPU frequency for the run (needs
`sudo`).

Results appear in `out/experiments_v1/` on the host:

| file | paper |
|---|---|
| `six_suite_overhead.png` | Figure 5 (top panel: this run; bottom panel: the paper's bars) |
| `six_suite_overhead.json`, `overhead.csv`, `baselines.csv` | the per-suite numbers and the raw rows behind them |

The figure is redrawn at the end of each step, so `run/fig5.sh` alone already gives the PTracer bars with the
traditional-tracer bars shown as "pending".

## Claims and experiments

| claim | script |
|---|---|
| C1 (Figure 5): PTracer traces all six suites at a fraction of traditional tracers' overhead, in HiFi and Fast mode, with and without PTWRITE; Fast mode is faster than HiFi mode | `run/fig5.sh` |
| C1 (Figure 5), reference points: memorytracer, libdft, Valgrind, Spindle-plus | `run/fig5_traditional.sh`  |
| C2 (Section 5.6): the reconstructed memory trace is nearly exact (paper: mean 0.03 %, P99 0.08 % inaccuracy) | `run/02_accuracy.sh` |
| C3 (Figure 6, ablation), offline reconstruction, DeathStarBench | not ready yet | |


## Layout and licence

```
envcheck/    host environment check
docker/      Dockerfile, build.sh, run.sh
run/         fig5.sh + fig5_traditional.sh (Figure 5), 02_accuracy.sh (Section 5.6); the others are not ready yet
ptracer/     static analyzer, runtime (binary rewriter, Pin tools), offline reconstructor
third_party/ Pin 3.20, Pin 4.4, the binary rewriter, baseline tracers (see third_party/SOURCE.md)
suites/      the six benchmark suites (see suites/*/SOURCE.md)
data/        experiment inputs: HiFi plans, Fast-PTWRITE site lists, seed avoid lists (data/avoid), the paper's Figure 5 bars
figures/     figure generators
```

PTracer's own code is MIT-licensed (`LICENSE`); third-party components keep their own licences
in their directories.
