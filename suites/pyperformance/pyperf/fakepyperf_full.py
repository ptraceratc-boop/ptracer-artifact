"""fakepyperf_full: superset of the artifact's fakepyperf.py (pyperformance 1.14.0, full MANIFEST).

Timing semantics are those of fakepyperf.py: each bench_* call runs PPF_WARM untimed warm-up
iterations, then PPF_LOOPS timed iterations; the timed region excludes interpreter start-up,
imports and data set-up.  Differences, all of them additive:

* argv: Runner.parse_args() parses sys.argv[1:] (run_one_full.py puts the MANIFEST variant's
  extra_opts there, as pyperformance does) instead of [], so <local:X> variants select their
  workload.  With no extra args the parse is identical to the old harness's.
* multi-benchmark scripts (genshi, sympy, xml_etree, pprint, base64, deepcopy, logging, ...):
  every bench_* call the script makes is run, as pyperformance does; the printed
  `KTIME <s>` is the SUM of the timed regions, each one also printed as `SUBTIME <name> <s>`.
  PPF_FIRST_ONLY=1 restores the old behaviour (exit after the first bench_* call).
* bench_command (2to3, python_startup[_no_site]): runs the command as a child process
  (subprocess.run), PPF_WARM untimed + PPF_LOOPS timed runs, wall-clock of the child runs.
  These benchmarks therefore SPAWN a subprocess by construction.
* timeit(): implemented with timeit.Timer (no benchmark in 1.14.0 uses it).
* module-level python_implementation(), python_has_jit(), perf_counter, add_metadata.
"""
import sys, time, argparse, os, atexit

LOOPS = int(os.environ.get("PPF_LOOPS", "1"))
WARM = int(os.environ.get("PPF_WARM", "1"))
VALIDATE_RESULT = os.environ.get("PPF_VALIDATE_RESULT", "0") == "1"
FIRST_ONLY = os.environ.get("PPF_FIRST_ONLY", "0") == "1"

try:  # identical result digests to the artifact harness when it is importable
    sys.path.insert(1, os.environ.get("PPF_ARTIFACT_HARNESS", os.path.dirname(os.path.abspath(__file__))))
    from fakepyperf import result_evidence  # noqa: E402
except Exception:  # pragma: no cover
    def result_evidence(name, func, value):
        return {"status": "unavailable", "benchmark": name,
                "reason": "artifact fakepyperf not importable"}
finally:
    del sys.path[1]


def perf_counter():
    return time.perf_counter()


def python_implementation():
    return sys.implementation.name


def python_has_jit():
    return False


def add_metadata(*a, **k):
    pass


class _Benchmark:
    pass


class _FakeArgs:
    def __init__(self, ns): self.__dict__.update(vars(ns))
    def __getattr__(self, k): return None     # tolerate unknown args


_TIMES = []          # (name, seconds) for every bench_* call
_REPORTED = [False]


def _report():
    if _REPORTED[0] or not _TIMES:
        return
    _REPORTED[0] = True
    for n, el in _TIMES:
        sys.stderr.write("SUBTIME %s %.6f\n" % (n, el))
    sys.stderr.write("NSUB %d\n" % len(_TIMES))
    sys.stderr.write("KTIME %.6f\n" % sum(el for _, el in _TIMES))
    sys.stderr.flush()


atexit.register(_report)


class Runner:
    def __init__(self, values=None, processes=None, loops=0, min_time=0.1, metadata=None,
                 show_name=True, program_args=None, add_cmdline_args=None,
                 _argparser=None, warmups=1, **kw):
        self.argparser = argparse.ArgumentParser()
        self.metadata = dict(metadata or {})
        self.values = []
        self.args = None
        self._add_cmdline_args = add_cmdline_args
        ap = self.argparser
        # pyperf's own options, so a benchmark that reads them (or a user that passes them)
        # does not break; values are ignored by this harness.
        for flags, kw2 in ((("--worker",), dict(action="store_true")),
                           (("-l", "--loops"), dict(type=int, default=0)),
                           (("-p", "--processes"), dict(type=int)),
                           (("-n", "--values"), dict(type=int)),
                           (("-w", "--warmups"), dict(type=int)),
                           (("--fast",), dict(action="store_true")),
                           (("--rigorous",), dict(action="store_true")),
                           (("--debug-single-value",), dict(action="store_true")),
                           (("-o", "--output"), dict()),
                           (("--append",), dict()),
                           (("--inherit-environ",), dict()),
                           (("--affinity",), dict()),
                           (("-v", "--verbose"), dict(action="store_true")),
                           (("-q", "--quiet"), dict(action="store_true"))):
            try:
                ap.add_argument(*flags, **kw2)
            except Exception:
                pass

    def parse_args(self, args=None):
        if self.args is not None:
            return self.args
        argv = sys.argv[1:] if args is None else list(args)
        try:
            ns, rest = self.argparser.parse_known_args(argv)
        except SystemExit:
            ns = argparse.Namespace()
        self.args = _FakeArgs(ns)
        return self.args

    def _ensure_args(self):
        if self.args is None:
            self.parse_args()

    def _finish(self, name, el, evidence=None):
        if evidence is not None:
            import json
            print("RESULT " + json.dumps(evidence, sort_keys=True), flush=True)
        _TIMES.append((name, el))
        if FIRST_ONLY:
            _report()
            sys.exit(0)

    def bench_time_func(self, name, func, *args, **kw):
        self._ensure_args()
        func(WARM, *args)                          # warmup
        el = func(LOOPS, *args)                    # timed (func measures internally)
        evidence = result_evidence(name, func, None) if VALIDATE_RESULT else None
        self._finish(name, el, evidence)

    def bench_func(self, name, func, *args, **kw):
        self._ensure_args()
        for _ in range(WARM): func(*args)          # warmup
        if VALIDATE_RESULT:
            value = None
            t = perf_counter()
            for _ in range(LOOPS): value = func(*args)
            elapsed = perf_counter() - t
            self._finish(name, elapsed, result_evidence(name, func, value))
        else:
            t = perf_counter()
            for _ in range(LOOPS): func(*args)
            self._finish(name, perf_counter() - t)

    def bench_async_func(self, name, func, *args, **kw):
        import asyncio
        self._ensure_args()

        async def _w(n):
            for _ in range(n): await func(*args)
        asyncio.run(_w(WARM)); t = perf_counter(); asyncio.run(_w(LOOPS))
        self._finish(name, perf_counter() - t)

    def bench_command(self, name, command):
        import subprocess
        self._ensure_args()
        for _ in range(WARM):
            subprocess.run(command, check=True)
        t = perf_counter()
        for _ in range(LOOPS):
            subprocess.run(command, check=True)
        self._finish(name, perf_counter() - t)

    def timeit(self, name, stmt=None, setup="pass", teardown="pass", inner_loops=None,
               duplicate=None, metadata=None, globals=None):
        import timeit as _timeit
        self._ensure_args()
        timer = _timeit.Timer(stmt or "pass", setup, globals=globals)
        timer.timeit(WARM)
        self._finish(name, timer.timeit(LOOPS))
