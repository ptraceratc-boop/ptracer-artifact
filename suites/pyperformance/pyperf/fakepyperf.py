"""Fake `pyperf` module: intercept a pyperformance benchmark's workload and run it directly
with a warmup (so Pin JIT/startup is excluded) + an internal KTIME timer, instead of pyperf's
fork/calibrate machinery. Prints `KTIME <sec>` for the timed region to stderr."""
import sys, time, argparse, os

LOOPS = int(os.environ.get("PPF_LOOPS", "1"))
WARM  = int(os.environ.get("PPF_WARM", "1"))
VALIDATE_RESULT = os.environ.get("PPF_VALIDATE_RESULT", "0") == "1"


def result_evidence(name, func, value):
    """Digest a real final result AFTER timing; never digest inputs as outputs.

    This is a program-output check, not a memory-trace completeness check. The
    last result is retained when LOOPS > 1. Unsupported benchmarks say so; a
    nonempty 'unavailable' line must not be mistaken for successful validation.
    """
    import hashlib
    import json
    if name == "nbody" and "BODIES" in func.__globals__:
        result = func.__globals__["BODIES"]
        scope = "final mutated planetary state"
    elif name == "float" and all(hasattr(value, k) for k in ("x", "y", "z")):
        result = [value.x, value.y, value.z]
        scope = "last returned maximized Point"
    elif name == "richards":
        if value is not True:
            raise ValueError("richards failed its built-in packet/hold-count checks")
        result = value
        scope = "built-in packet/hold-count checks"
    elif name == "go" and type(value) is int:
        result = value
        scope = "last returned move"
    else:
        return {"status": "unavailable", "benchmark": name,
                "reason": "no supported observable result; not a correctness pass"}
    payload = json.dumps(result, sort_keys=True, separators=(",", ":"), allow_nan=False)
    return {"status": "observed", "benchmark": name, "scope": scope,
            "sha256": hashlib.sha256(payload.encode()).hexdigest()}

def perf_counter():
    return time.perf_counter()

class _FakeArgs:
    def __init__(self, ns): self.__dict__.update(vars(ns))
    def __getattr__(self, k): return None     # tolerate unknown args

class Runner:
    def __init__(self, *a, **k):
        self.argparser = argparse.ArgumentParser()
        self.metadata = {}
        self.values = []
        self.args = None
        # pyperf adds these; benchmarks may read them
        try:
            self.argparser.add_argument("--worker", action="store_true")
            self.argparser.add_argument("-l", "--loops", type=int, default=0)
        except Exception: pass
    def parse_args(self, *a, **k):
        try: ns, _ = self.argparser.parse_known_args([])
        except SystemExit: ns = argparse.Namespace()
        self.args = _FakeArgs(ns)
        return self.args
    def _finish(self, el, evidence=None):
        if evidence is not None:
            import json
            print("RESULT " + json.dumps(evidence, sort_keys=True), flush=True)
        sys.stderr.write("KTIME %.6f\n" % el); sys.stderr.flush()
        sys.exit(0)
    def bench_time_func(self, name, func, *args, **kw):
        # func(loops, *args) runs an inner loop of `loops` and returns elapsed seconds
        func(WARM, *args)                         # warmup
        el = func(LOOPS, *args)                    # timed (func measures internally)
        evidence = result_evidence(name, func, None) if VALIDATE_RESULT else None
        self._finish(el, evidence)
    def bench_func(self, name, func, *args, **kw):
        for _ in range(WARM): func(*args)          # warmup
        if VALIDATE_RESULT:
            value = None
            t = perf_counter()
            for _ in range(LOOPS): value = func(*args)
            elapsed = perf_counter() - t
            self._finish(elapsed, result_evidence(name, func, value))
        else:
            t = perf_counter()
            for _ in range(LOOPS): func(*args)
            self._finish(perf_counter() - t)
    def bench_async_func(self, name, func, *args, **kw):
        import asyncio
        async def _w(n):
            for _ in range(n): await func(*args)
        asyncio.run(_w(WARM)); t = perf_counter(); asyncio.run(_w(LOOPS)); self._finish(perf_counter()-t)
    def bench_command(self, name, cmd):
        sys.stderr.write("KTIME SKIP_bench_command\n"); sys.exit(2)
    def timeit(self, name, stmt=None, setup=None, **kw):
        sys.stderr.write("KTIME SKIP_timeit\n"); sys.exit(2)

# pyperf module-level helpers some benchmarks import
def add_metadata(*a, **k): pass
class _Benchmark: pass
