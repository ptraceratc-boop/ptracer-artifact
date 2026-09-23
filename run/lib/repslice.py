#!/usr/bin/env python3
"""repslice.py -- the HiFi overhead driver for the six-suite slice, with per-suite geomeans.

It times the vendored suite binaries with the benchmark's own steady-state timer, median of
REPS, configurations ALTERNATED inside every repetition, a machine-wide PT lock held once,
and (optionally) a machine-quiet mutex. Both the benchmark's own timer (`ktime`) and the
whole-process wall time are recorded.

Configurations
  vanilla     un-instrumented, no tracer
  vanilla_pt  the same under a complete Intel PT capture (the substrate floor)
  pinnoop     Pin JIT with a Pintool that registers NO instrumentation -- the Pin JIT floor
  pinhifi     Pin JIT with the HiFi Pintool (fill-buffer logging of the critical values) = HiFi
  memtrace    the paper's `memorytracer' baseline (Pin fill-buffer over every operand)
  libdft      the paper's byte-level taint-tracking baseline (Pintool, Pin 3.20 kit)
  valgrind    the paper's Valgrind baseline

Pin 4.4 is the default kit for HiFi; the three baseline rows (used by run/optional_baselines.sh)
obey the paper's 200x slowdown cap via --cap-x 200 --cap-from CSV.
"""
import argparse, csv, fcntl, hashlib, json, math, os, re, signal, socket, statistics, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "out")
# Every path is relative to the artifact root; environment variables override each one so a
# reviewer can point at kits/suites installed elsewhere.
def _env(name, default):
    return os.environ.get(name, default)

TP = os.path.join(ROOT, "third_party")
SUITES = os.path.join(ROOT, "suites")
TOOL = os.path.join(ROOT, "ptracer")

# Pin 4.4 is the default HiFi kit; Pin 3.20 is only for the libdft baseline.
PIN_ROOT = _env("PIN_ROOT", os.path.join(TP, "pin-4.4"))
PIN = os.path.join(PIN_ROOT, "pin")
PIN320 = _env("PIN320_ROOT", os.path.join(TP, "pin-3.20"))
# The HiFi Pintool and its no-op floor are built by ptracer/build.sh for the 4.4 kit.
NOOP = _env("NOOP_TOOL", os.path.join(TOOL, "runtime", "pinjit", "obj-intel64", "nooptool.so"))
HIFI = _env("HIFI_TOOL", os.path.join(TOOL, "runtime", "pinjit", "obj-intel64", "hifitool.so"))
# The paper's traditional-tracer baselines (used only by run/optional_baselines.sh):
# memorytracer (Pin fill-buffer over every operand), libdft (byte-level taint), Valgrind.
# Each writes its bulk trace to a fixed filename in the cwd, which the driver points at
# /dev/null so only instrumentation cost is measured.
MEMTRACE = _env("MEMTRACE_TOOL", os.path.join(TP, "memtrace", "p4", "obj-intel64", "memtrace.so"))
LIBDFT = _env("LIBDFT_TOOL", os.path.join(TP, "libdft64", "tools", "obj-intel64", "track.so"))
VALGRIND = _env("VALGRIND", "valgrind")
TRACE_SINK = {"memtrace": "memtrace.log", "valgrind": "lackey_memtrace.log"}
BASELINE_CFGS = ("memtrace", "libdft", "valgrind")
PC = os.path.join(TOOL, "offline", "pt_capture2")
PLANS = _env("PLANS_DIR", os.path.join(ROOT, "data", "plans"))
HELPER_CORES = _env("HELPER_CORES", "12-15")
NO_CLONE3 = os.path.join(TP, "no_clone3", "no_clone3")
LOCK = _env("PT_LOCK", os.path.join(ROOT, ".pt_pmu.lock"))

TARGETS = os.path.join(SUITES, "polybench", "targets")
PYVAN = os.path.join(SUITES, "pyperformance", "cpython-cg", "bin", "python3.12")
PYRUN = os.path.join(SUITES, "pyperformance", "pyperf", "run_one.py")
RUSTBIN = os.path.join(SUITES, "rust", "micro-bench", "target", "release", "micro-bench")
WTB = os.path.join(SUITES, "node", "web-tooling-benchmark")
NODE_BIN = _env("NODE", os.path.join(SUITES, "node", "bin", "node"))
WTB_JS = os.path.join(SUITES, "node", "wtb.js")
JAVA = os.path.join(SUITES, "java", "jdk17", "bin", "java")
REN = os.path.join(SUITES, "java", "renaissance")
MC_BIN = os.path.join(SUITES, "memcached", "bin", "memcached_sym")
MC_CLI = _env("MEMCASLAP", os.path.join(SUITES, "memcached", "bin", "memcaslap"))
MC_CFG = os.path.join(SUITES, "memcached", "mcslap.cfg")

CORE = _env("TIMED_CORE", "5")     # a single P-core for the single-threaded cells
# HiFi is one definition for every suite: the same Pintool, the same fill-buffer emitter.
# For a target that generates its code at run time (V8, HotSpot) the site list arrives from
# the runtime's own code-object notifications, published to the Pintool; these knobs only say
# where that runtime-side hook, its analyzer service and its cache live.
JIT = {"on": False, "addon": None, "agent": None, "cache": None, "sock": None,
       "workers": 4, "acores": ["16", "17", "18", "19"], "statsdir": None, "miss": 0,
       "cachever": "v2.34", "aopts": ""}
ANALYZE = os.path.join(TOOL, "static", "analyze.py")
VENVPY = _env("PYBIN", "python3")
HIFI_CFGS = ("pinhifi", "pinhifi_nopt")
OV = {"core": None, "pyloops": "1", "pin": PIN, "noop": NOOP, "hifi": HIFI, "label": "",
      # `-sink ptwrite' turns every logged value into a callout executing one `ptwrite'
      # (the paper's HiFi-PTWRITE arm; Pin's JIT cannot inline PTWRITEs). Empty by default.
      "hifi_extra": [],
      "memtrace": MEMTRACE, "libdft": LIBDFT, "libdft_pin": PIN320, "valgrind": VALGRIND,
      # Pin kit flags applied identically to every Pin arm. Pin 4.4 needs a separate
      # allocation range or a JVM run can abort on a Pin/application mapping collision; the
      # memory-conflict assertions stay on.
      "extra": []}


# --- the paper's 200x slowdown cap ------------------------------------------------------
# "Slowdown is capped at 200x and reported conservatively at that cap when a combination
# will not finish."  Operationally: a run is given `cap_x' times the cell's NATIVE WALL
# time (plus a fixed allowance so that process start-up under a 100x tracer is never what
# trips the cap); if it does not finish in that budget it is killed and the cell is
# reported as ">=200x (capped)", never as a measured number.  CAP["wall"] is filled from
# the vanilla rows of a reference CSV so the budget is this machine's own native time.
CAP = {"x": 0.0, "wall": {}, "slack": 120.0, "ceil": 0.0}


def cap_load(path):
    """median vanilla wall per (suite, cell) from a reference CSV."""
    if not path or not os.path.exists(path):
        return
    acc = {}
    with open(path) as f:
        for r in csv.DictReader(f):
            if r["config"] == "vanilla" and r["rc"] == "0" and r.get("wall"):
                acc.setdefault((r["suite"], r["cell"]), []).append(float(r["wall"]))
    for k, v in acc.items():
        CAP["wall"][k] = statistics.median(v)
    log("cap: native walls for %d cells from %s" % (len(CAP["wall"]), path))


def cell_timeout(suite, cell, default):
    """Seconds this run is allowed before the 200x cap fires."""
    if CAP["x"] <= 0:
        return default
    w = CAP["wall"].get((suite, cell))
    if w is None:
        return default
    t = CAP["x"] * w + CAP["slack"]
    if CAP["ceil"] > 0:
        t = min(t, CAP["ceil"])
    return int(min(t, default))


RUST_RAYON_CORES = "4-7"
MC_SRV_CORES = [4, 5, 6, 7]
MC_CLI_CORES = "8-11"
JAVA_CORES = "0-3"
CLK = os.sysconf("SC_CLK_TCK")

# ---------------------------------------------------------------- the slice
POLY = ["gemm", "atax", "jacobi-2d", "correlation", "durbin"]
PYPERF = ["nbody", "richards", "float", "go", "json_dumps", "regex_v8"]
RUST = ["seq", "rayon"]
NODE = [("acorn", 10), ("babel", 10)]
JAVA_B = [("scrabble", 12, 6), ("philosophers", 8, 4)]   # (bench, iters, drop-warmup)
CELLS = ([("poly", k) for k in POLY] + [("pyperf", b) for b in PYPERF] +
         [("mc", "memslap")] + [("rust", v) for v in RUST] +
         [("node", n) for n, _ in NODE] + [("java", b) for b, _, _ in JAVA_B])

FLOATRE = re.compile(r"^\s*([0-9]+\.[0-9]+)\s*$", re.M)
ITER_RE = re.compile(r"iteration (\d+) completed \(([0-9.]+) ms\)")


def log(m):
    print("[repslice] " + m, flush=True)


class MachineLock(object):
    """The machine-wide lock every other driver takes around a measured block."""
    def __enter__(self):
        self.fd = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o666)
        fcntl.flock(self.fd, fcntl.LOCK_EX)
        return self
    def __exit__(self, *e):
        fcntl.flock(self.fd, fcntl.LOCK_UN); os.close(self.fd); return False


SINKS = []
MUTEX_HELD = [False]
MUTEX_TOKEN = [None]
MUTEX_ROOT = os.environ.get("MUTEX_ROOT", ROOT)
MUTEX_IDLE = os.path.join(MUTEX_ROOT, "mutex_idle.txt")
MUTEX_RUN = os.path.join(MUTEX_ROOT, "mutex_running.txt")


def mutex_take(wait, label="pinjit repslice"):
    """Timing-mutex protocol: wait out the current holder, then stamp holder/pid/time
    into mutex_running.txt so the next agent can see who holds it and since when."""
    t = 0
    while t < wait:
        if not os.path.exists(MUTEX_RUN):
            try:
                os.rename(MUTEX_IDLE, MUTEX_RUN)
            except OSError:
                open(MUTEX_RUN, "w").close()
            # the mutex files may be owned by another uid (they were created by a
            # different session); the directory is writable by everyone who uses the
            # protocol, so stamp the holder by replace(), not by opening the file.
            tmp = os.path.join(MUTEX_ROOT, ".mutex_stamp.%d" % os.getpid())
            # A TOKEN LINE IS MANDATORY.  eval/mutex.sh's mutex_release refuses to release a
            # hold whose `token=' line it cannot match, so a stamp without one can only be
            # undone by hand -- and a hand-edit is exactly how this driver's agent destroyed
            # never destroy another session's stamp: it may already have released and
            MUTEX_TOKEN[0] = "%d-%d" % (os.getpid(), int(time.time() * 1000) % 100000)
            with open(tmp, "w") as f:
                f.write("holder=%s\npid=%d\ntaken=%s\ntoken=%s\n"
                        % (label, os.getpid(),
                           time.strftime("%Y-%m-%dT%H:%M:%S%z"), MUTEX_TOKEN[0]))
            try:
                os.replace(tmp, MUTEX_RUN)
            except OSError:
                pass
            MUTEX_HELD[0] = True
            log("mutex taken by %s" % label)
            return
        time.sleep(15); t += 15
    try:
        holder = open(MUTEX_RUN).read().strip().replace("\n", " ")
    except OSError:
        holder = "?"
    log("mutex still held after %ds (%s) -- rows will be marked mutex=shared" % (wait, holder))


def mutex_give():
    """Release, but ONLY if the running file still carries our own token.

    Never unlink a holder stamp that is not ours: between this driver exiting and the
    release, it may already have been released and another
    session may have taken it.  Unlinking then destroys THEIR stamp and lets a third
    session in beside them -- two sessions measuring at once, with nothing in either
    another session may have taken the lock in the gap."""
    if not MUTEX_HELD[0]:
        return
    MUTEX_HELD[0] = False
    try:
        cur = open(MUTEX_RUN).read()
    except OSError:
        log("mutex: running file already gone -- someone released for us, leaving it alone")
        return
    if MUTEX_TOKEN[0] and ("token=%s" % MUTEX_TOKEN[0]) not in cur.split("\n"):
        log("mutex: running file is NOT ours any more (%s) -- refusing to release"
            % cur.splitlines()[:1])
        return
    tmp = os.path.join(MUTEX_ROOT, ".mutex_stamp.%d" % os.getpid())
    with open(tmp, "w") as f:
        f.write("holder=UNKNOWN\npid=\nnote=idle token; released by repslice.py\n")
    try:
        os.replace(tmp, MUTEX_IDLE)
        os.unlink(MUTEX_RUN)
        log("mutex released")
    except OSError as e:                                         # noqa: BLE001
        log("mutex release failed: %s" % e)


# ------------------------------------------------------------- pin prefixes
#  vanilla        the program alone
#  vanilla_pt     the program under a complete Intel PT capture (the substrate floor)
#  pinnoop        Pin 3.20 JIT, a Pintool with NO instrumentation  -- the Pin JIT FLOOR
#  pinhifi_nopt   Pin 3.20 JIT + the HiFi Pintool, Intel PT OFF    -- the logging alone
#  pinhifi        Pin 3.20 JIT + the HiFi Pintool + a complete Intel PT capture = HiFi
#  memtrace       Pin 3.20 memorytracer (the Task-1 whole-trace baseline)
#  fast           the E9Patch-rewritten image (buffer sink) + a complete Intel PT capture = Fast
PT_CFGS = ("vanilla_pt", "pinhifi", "fast")
RT_PRELOAD = "%s:%s" % (os.path.join(TOOL, "runtime", "rt", "ptlogmt.so"),
                        os.path.join(TOOL, "runtime", "rt", "ptlogrt.so"))
FAST_DIR = _env("FAST_DIR", os.path.join(OUT, "fast"))


def fast_image(suite, cell):
    """The Fast image of a cell: the vanilla image rewritten by run/lib/build_fast.sh."""
    if suite == "poly":
        return os.path.join(FAST_DIR, "poly", "%s_large" % cell)
    if suite == "pyperf":
        return os.path.join(FAST_DIR, "pyfast", "bin", "python3.12")
    if suite == "rust":
        return os.path.join(FAST_DIR, "rust", "micro-bench")
    if suite == "mc":
        return os.path.join(FAST_DIR, "mc", "memcached_sym")
    raise SystemExit("repslice: no Fast image for suite %s" % suite)


def plan_for(suite, cell):
    if suite == "poly":
        return os.path.join(PLANS, "poly.%s.plan" % cell)
    if suite == "pyperf":
        return os.path.join(PLANS, "cpython.plan")
    if suite in ("rust", "mc"):
        return os.path.join(PLANS, "%s.plan" % suite)
    return None                       # node / java: JIT-generated code, no plan (see note)


def cpu_list(cores):
    """"0-3" / "5" / "4,6" -> [0,1,2,3] / [5] / [4,6]."""
    out = []
    for part in str(cores).split(","):
        if "-" in part:
            a, b = part.split("-")
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


# Cells whose PT capture must be PER-CPU, one intel_pt event per core the timed process may
# run on.  WHY: without `--cpu' pt_capture2 opens a per-TASK event with inherit OFF, so only
# the thread it exec'd is traced.  That is complete for a single-threaded cell and NOT complete
# for a JVM, whose compiler and GC threads are the point.  `--cpu' is repeatable and pins the
# child to exactly that core set. Node is named explicitly because its libuv workers share the
# JS thread's core, so a per-task event is incomplete there too. Any cell pinned to more than
# one core needs this by construction.
PERCPU_SUITES = ("java", "node")


def want_percpu(suite, cores):
    return suite in PERCPU_SUITES or len(cpu_list(cores)) > 1


def baseline_prefix(cfg, diag=None):
    """argv prefix for one of the paper's three traditional-tracer baselines, or None.

    The same three invocations Task 1 used (`experiments/tracer-issues/harness/harness.py`
    `build_argv`), so the rows are directly comparable with that study's:
      memtrace  -> pin -ifeellucky -t memtrace.so --   (memorytracer)
      libdft    -> pin -ifeellucky -t track.so    --   (byte-level taint tracking)
      valgrind  -> valgrind --tool=lackey --log-file=...
    `no_clone3` forces glibc's clone() fallback so Pin 3.x does not SIGSEGV on a threaded
    or JIT target; it is a no-op for a single-threaded one, and Valgrind does not need it.
    """
    if cfg == "memtrace":
        return [NO_CLONE3, OV["pin"], "-ifeellucky"] + OV["extra"] + ["-t", OV["memtrace"], "--"]
    if cfg == "libdft":
        return [NO_CLONE3, os.path.join(OV["libdft_pin"], "pin") if os.path.isdir(OV["libdft_pin"])
                else OV["libdft_pin"], "-ifeellucky", "-t", OV["libdft"], "--"]
    if cfg == "valgrind":
        return [OV["valgrind"], "--tool=lackey",
                "--log-file=" + (diag or os.path.join(OUT, "valgrind.diag.log"))]
    return None


def trace_sinks(cwds):
    """Point every baseline tracer's fixed trace filename at /dev/null in each cwd a timed
    run uses.  Returns the list of links created, for removal at the end of the sweep."""
    made = []
    for d in cwds:
        if not d or not os.path.isdir(d):
            continue
        for name in TRACE_SINK.values():
            p = os.path.join(d, name)
            try:
                if os.path.islink(p):
                    if os.readlink(p) == "/dev/null":
                        continue
                    os.unlink(p)
                elif os.path.exists(p):
                    os.unlink(p)
                os.symlink("/dev/null", p)
                made.append(p)
            except OSError as e:                                 # noqa: BLE001
                log("trace sink %s: %s" % (p, e))
    return made


def wrap(cfg, cores, prog_argv, suite, cell, aux_mb=512):
    """Full argv for one configuration.  The timed process is always pinned to `cores`;
    pt_capture2 runs on the helper cores so the capture never shares the timed core."""
    tool = None
    if cfg == "fast" and suite not in ("node", "java"):
        prog_argv = [fast_image(suite, cell)] + list(prog_argv[1:])
    if cfg in HIFI_CFGS:
        plan = plan_for(suite, cell)
        tool = [NO_CLONE3, OV["pin"], "-ifeellucky"] + OV["extra"] + ["-t", OV["hifi"]]
        # A suite whose code is GENERATED has no ELF plan; the same Pintool then takes its
        # sites from the runtime bridge instead.  Nothing else about the tool changes.
        if plan:
            tool += ["-plan", plan]
        tool += ["-cvdir", "/dev/null", "-stats", "1"] + OV["hifi_extra"]
        if JIT["miss"]:
            tool += ["-jitmiss", str(JIT["miss"])]
        tool += ["--"]
    elif cfg == "pinnoop":
        tool = [NO_CLONE3, OV["pin"], "-ifeellucky"] + OV["extra"] + ["-t", OV["noop"], "--"]
    elif cfg in BASELINE_CFGS:
        tool = baseline_prefix(cfg, os.path.join(OUT, "valgrind.%s.%s.log" % (suite, cell)))
    inner = ["taskset", "-c", cores] + (tool or []) + prog_argv
    if cfg in PT_CFGS:
        percpu = []
        if want_percpu(suite, cores):
            cl = cpu_list(cores)
            for c in cl:
                percpu += ["--cpu", str(c)]
            aux_mb = max(128, aux_mb // max(1, len(cl)))   # one AUX ring per event
        extra = []
        if cfg == "fast" and suite in ("node", "java"):
            # native JIT mode: the runtime hook patches the compiled code itself (no Pin);
            # the buffer-sink runtime is preloaded into the child only, and the capture runs
            # in sideband (ptrace) mode so every JIT thread gets its ring at its first stop.
            extra = ["--sideband", os.path.join(OUT, "sb.%s.%s.json" % (suite, cell)),
                     "--child-env", "LD_PRELOAD=" + RT_PRELOAD]
        return (["taskset", "-c", HELPER_CORES, "setarch", "-R", PC,
                 "--aux-mb", str(aux_mb), "--ptw", "--no-decode",
                 "--aux-out", "/dev/null"] + percpu + extra + ["--"] + inner)
    return ["taskset", "-c", cores, "setarch", "-R"] + (tool or []) + prog_argv


def jit_env(suite, cell, cfg, rep):
    """Environment for a run whose code is generated at run time.  Same analyzer service,
    same content-hash cache and same selected-value plans the Fast path uses (the paper's
    JIT path is shared too); only the SUBSTRATE differs -- Pin instruments, nothing in the
    application's own bytes is rewritten."""
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("PTJIT_", "JITPOC_", "PTLOG_"))}
    env.update(TMPDIR=JIT["statsdir"], PTLOG_DIR="/dev/null")
    if suite != "node" or cfg not in HIFI_CFGS + ("fast",):
        return env
    if cfg == "fast":                       # Fast: native detours in the JIT code, no Pin
        env.update(PTJIT_MODE="4", PTJIT_PIN="0", PTJIT_FAST="1", PTJIT_SINK="buffer",
                   PTJIT_KEYFRAME="1024", PTJIT_NOROOTS="1", PTJIT_NOSPAWN="1",
                   PTJIT_CACHE_VER=JIT["cachever"], PTJIT_CACHE=JIT["cache"],
                   PTJIT_SOCK=JIT["sock"] + ".0", PTJIT_ADDON=JIT["addon"],
                   PTJIT_ARENA_MB="128", PTJIT_STATS=jit_statsfile(suite, cell, cfg, rep))
        return env
    env.update(PTJIT_MODE="4", PTJIT_PIN="1", PTJIT_KEYFRAME="0", PTJIT_FAST="0",
               PTJIT_NOSPAWN="1", PTJIT_NOROOTS="1", PTJIT_CACHE_VER=JIT["cachever"],
               PTJIT_CACHE=JIT["cache"], PTJIT_SOCK=JIT["sock"] + ".0",
               PTJIT_ADDON=JIT["addon"], PTJIT_ARENA_MB="128",
               PTJIT_STATS=jit_statsfile(suite, cell, cfg, rep))
    return env


def jit_statsfile(suite, cell, cfg, rep):
    return os.path.join(JIT["statsdir"], "%s.%s.%s.r%s.json" % (suite, cell, cfg, rep))


class Analyzers(object):
    """The analyzer services the runtime hook calls when it meets a new code object.  They
    are pinned OFF every timed core and off the PT helper cores, and they are the same
    service the Fast substrate uses."""
    def __init__(self, n, cores, sockbase, logdir):
        self.n, self.cores, self.sock, self.logdir = n, cores, sockbase, logdir
        self.procs, self.logs = [], []

    def __enter__(self):
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(("PTJIT_", "JITPOC_", "PTLOG_"))}
        env.update(TMPDIR=self.logdir, PTLOG_DIR="/dev/null")
        for i in range(self.n):
            path = "%s.%d" % (self.sock, i)
            if os.path.exists(path):
                os.unlink(path)
            fh = open(os.path.join(self.logdir, "analyzer-%d.log" % i), "w")
            self.logs.append(fh)
            p = subprocess.Popen(["taskset", "-c", self.cores[i % len(self.cores)],
                                  VENVPY, ANALYZE, "--serve", path],
                                 stdout=fh, stderr=fh, env=env, start_new_session=True)
            self.procs.append(p)
            t0 = time.monotonic()
            while not os.path.exists(path):
                if p.poll() is not None or time.monotonic() - t0 > 60:
                    self.__exit__()
                    raise SystemExit("repslice: analyzer %d did not start" % i)
                time.sleep(0.1)
        log("%d analyzer services up on cores %s" % (self.n, ",".join(self.cores)))
        return self

    def __exit__(self, *e):
        for p in self.procs:
            try:
                p.kill(); p.wait(timeout=30)
            except Exception:                                    # noqa: BLE001
                pass
        for fh in self.logs:
            fh.close()
        self.procs, self.logs = [], []
        return False


def load_gate(limit, wait):
    """Hold a timed run until the machine is actually quiet.

    Taking the timing mutex does NOT evict work that is already running, and it does not
    stop a sibling session from starting an untimed reconstruction next to a measured row
    (the timing mutex does not evict work already running).  Every row
    already records the load, so a contended row is self-identifying -- this gate makes it
    not happen in the first place.  `limit' <= 0 disables the gate.
    """
    if limit <= 0:
        return
    t0 = time.monotonic()
    first = loadavg()
    while loadavg() > limit:
        if time.monotonic() - t0 > wait:
            log("load gate: still %.2f after %ds, proceeding (the row records the load)"
                % (loadavg(), int(wait)))
            return
        time.sleep(10)
    if time.monotonic() - t0 > 10:
        log("load gate: waited %ds for load %.2f -> %.2f"
            % (int(time.monotonic() - t0), first, loadavg()))


def loadavg():
    """The 1-minute load average.  Recorded on EVERY timed row: taking the timing mutex does
    not evict work that was already running, so a row measured next to concurrent work
    must be self-identifying in the data and not only in prose."""
    try:
        return float(open("/proc/loadavg").read().split()[0])
    except Exception:                                            # noqa: BLE001
        return -1.0


def run(argv, cwd=None, env=None, timeout=3600):
    l0 = loadavg()
    t0 = time.monotonic()
    p = subprocess.run(argv, cwd=cwd, env=env, timeout=timeout,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       stdin=subprocess.DEVNULL, text=True, errors="replace")
    return dict(rc=p.returncode, wall=time.monotonic() - t0, out=p.stdout, err=p.stderr,
                load0=l0, load1=loadavg())


# ------------------------------------------------------------------- cells
def cell_poly(cfg, k, args):
    tgt = os.path.join(TARGETS, "%s_large" % k)
    r = run(wrap(cfg, OV["core"] or CORE, [tgt], "poly", k), cwd=OUT, timeout=args.timeout)
    v = FLOATRE.findall(r["out"])
    r["ktime"] = float(v[-1]) if v else None
    r["ck"] = None
    return r




def cell_pyperf(cfg, b, args):
    env = dict(os.environ, PPF_LOOPS=OV["pyloops"], PPF_WARM="1")
    r = run(wrap(cfg, OV["core"] or CORE, [PYVAN, PYRUN, b], "pyperf", b),
            env=env, timeout=args.timeout)
    m = re.findall(r"KTIME ([0-9.]+)", r["err"])
    r["ktime"] = float(m[-1]) if m else None
    r["ck"] = None
    return r


RUST_ARGS = {"seq":   (["sequential", "1024", "1", "3000", "2000"], CORE),
             "rayon": (["rayon", "1024", "2", "3000", "2000"], RUST_RAYON_CORES)}


def cell_rust(cfg, v, args):
    a, cores = RUST_ARGS[v]
    if OV["core"] and v == "seq":
        cores = OV["core"]
    outf = os.path.join(OUT, "result_%s.txt"
                        % ("sequential" if v == "seq" else "rayon"))
    try:
        os.unlink(outf)
    except OSError:
        pass
    r = run(wrap(cfg, cores, [RUSTBIN] + a, "rust", v), cwd=OUT, timeout=args.timeout)
    m = re.findall(r"Execution time(?: Rayon)?: ([0-9.eE+-]+) sec", r["out"] + r["err"])
    r["ktime"] = float(m[-1]) if m else None
    # RustStreamBench's own gate: the result file is byte-compared with the vanilla
    # reference. The md5 travels in the row.
    try:
        with open(outf, "rb") as f:
            r["ck"] = hashlib.md5(f.read()).hexdigest()[:16]
    except OSError:
        r["ck"] = "no-result-file"
    return r


def cell_node(cfg, n, args):
    iters = dict(NODE)[n]
    argv = [NODE_BIN]
    env = None
    if JIT["on"] and cfg in HIFI_CFGS + ("fast",):
        # V8's own code-object notifications, analyzed and published to the Pintool.
        argv += ["-r", os.path.join(TOOL, "runtime", "jit", "preload.js")]
        env = jit_env("node", n, cfg, getattr(args, "rep", 0))
    argv += [WTB_JS, n, str(iters)]
    r = run(wrap(cfg, OV["core"] or CORE, argv, "node", n), cwd=WTB, env=env,
            timeout=args.timeout)
    m = re.search(r'"ms":([0-9.]+)', r["out"])
    r["ktime"] = float(m.group(1)) / 1000.0 if m else None
    m = re.search(r'"checksum":"([0-9a-f]+)"', r["out"])
    r["ck"] = m.group(1) if m else None
    return r


def cell_java(cfg, b, args):
    iters, drop = dict((x[0], (x[1], x[2])) for x in JAVA_B)[b]
    if cfg != "vanilla":
        iters, drop = args.java_pin_iters, args.java_pin_drop
    argv = [JAVA]
    env = None
    if JIT["on"] and cfg in HIFI_CFGS + ("fast",):
        rep = getattr(args, "rep", 0)
        if cfg == "fast":                   # Fast: native detours in the JIT code, no Pin
            opts = ("mode=4,pin=0,fast=1,sink=buffer,keyframe=1024,noroots=1,nospawn=1,"
                    "workers=%d,cachever=%s,sock=%s,cache=%s,arenamb=128,drainms=120000,stats=%s%s"
                    % (JIT["workers"], JIT["cachever"], JIT["sock"], JIT["cache"],
                       jit_statsfile("java", b, cfg, rep), JIT["aopts"]))
        else:
            opts = ("mode=4,pin=1,keyframe=0,interp=1,stubs=1,workers=%d,nospawn=1,"
                    "cachever=%s,fast=0,sock=%s,cache=%s,arenamb=128,drainms=120000,stats=%s%s"
                    % (JIT["workers"], JIT["cachever"], JIT["sock"], JIT["cache"],
                       jit_statsfile("java", b, cfg, rep), JIT["aopts"]))
        argv += ["-agentpath:%s=%s" % (JIT["agent"], opts)]
        env = jit_env("java", b, cfg, rep)
    argv += ["-jar", os.path.join(REN, "renaissance.jar"), "-r", str(iters), b]
    r = run(wrap(cfg, JAVA_CORES, argv, "java", b), cwd=REN, env=env, timeout=args.timeout)
    st = [float(m.group(2)) for m in ITER_RE.finditer(r["out"] + r["err"])]
    r["iters"] = st
    # steady state: the median of the iterations after the warm-up drop
    tail = st[drop:] if len(st) > drop else st
    r["ktime"] = statistics.median(tail) / 1000.0 if tail else None
    r["ck"] = None
    return r


# ---------------------------------------------------------------- memcached
def proc_times(pid):
    with open("/proc/%d/stat" % pid) as f:
        d = f.read()
    rest = d[d.rfind(")") + 2:].split()
    return int(rest[11]) / CLK, int(rest[12]) / CLK


def find_server(port):
    for e in os.listdir("/proc"):
        if not e.isdigit():
            continue
        try:
            argvb = open("/proc/%s/cmdline" % e, "rb").read().split(b"\0")
            comm = open("/proc/%s/comm" % e).read().strip()
        except OSError:
            continue
        if comm.startswith("memcached") and b"-p" in argvb and str(port).encode() in argvb:
            return int(e)
    return None


def wait_ready(port, timeout, proc=None):
    """Wait for the server to answer `version'.  `proc' (when given) is the server process:
    a tracer that refuses to run it at all -- Valgrind cannot satisfy memcached's
    setrlimit(RLIMIT_NOFILE), so the server exits within a second -- is then detected
    immediately instead of burning the whole 900 s start-up budget."""
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        if proc is not None and proc.poll() is not None:
            return False
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=1)
            s.sendall(b"version\r\n"); ok = s.recv(64).startswith(b"VERSION"); s.close()
            if ok:
                return True
        except OSError:
            time.sleep(0.2)
    return False


def mc_roundtrip(port):
    """The Memcached suite's own correctness gate: a set/get/delete round trip against the
    server that was just measured.  Returns "ok" or a short failure description."""
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=30)
        s.settimeout(30)
        val = b"ptracer-baseline-gate"
        s.sendall(b"set gatekey 0 0 %d\r\n%s\r\n" % (len(val), val))
        if not s.recv(64).startswith(b"STORED"):
            s.close(); return "set-failed"
        s.sendall(b"get gatekey\r\n")
        buf = b""
        while b"END\r\n" not in buf:
            d = s.recv(4096)
            if not d:
                break
            buf += d
        if val not in buf:
            s.close(); return "get-mismatch"
        s.sendall(b"delete gatekey\r\n")
        if not s.recv(64).startswith(b"DELETED"):
            s.close(); return "delete-failed"
        s.close()
        return "ok"
    except OSError as e:                                         # noqa: BLE001
        return "gate-error:%s" % e


def cell_mc(cfg, _n, args):
    """The paper's memcached metric: SERVER user CPU time for a fixed op count."""
    port = args.mc_port + getattr(args, "rep", 0) % 7
    srvargs = ["-p", str(port), "-t", "4", "-m", "1024", "-U", "0", "-c", "1024"]
    cores = ",".join(str(c) for c in MC_SRV_CORES)
    # memcached is 4-threaded on 4 pinned cores, so a per-TASK Intel PT capture would trace
    # one worker out of four.  The PT configurations therefore use one PER-CPU capture per
    # server core: the capture on the first core runs the
    # server, the other three run a `cat FIFO' that is released at the end.
    tool = []
    if cfg in ("pinhifi", "pinhifi_nopt"):
        tool = ([NO_CLONE3, OV["pin"], "-ifeellucky", "-t", OV["hifi"],
                 "-plan", plan_for("mc", "memslap"), "-cvdir", "/dev/null"]
                + OV["hifi_extra"] + ["--"])
    elif cfg == "pinnoop":
        tool = [NO_CLONE3, OV["pin"], "-ifeellucky", "-t", OV["noop"], "--"]
    elif cfg in BASELINE_CFGS:
        tool = baseline_prefix(cfg, os.path.join(OUT, "valgrind.mc.log"))
    mcbin = fast_image("mc", "memslap") if cfg == "fast" else MC_BIN
    inner = ["taskset", "-c", cores] + tool + [mcbin] + srvargs
    if cfg in PT_CFGS:
        argv = (["taskset", "-c", HELPER_CORES, "setarch", "-R", PC, "--aux-mb", "512",
                 "--ptw", "--no-decode", "--aux-out", "/dev/null",
                 "--cpu", str(MC_SRV_CORES[0]), "--"] + inner)
    else:
        argv = ["taskset", "-c", cores, "setarch", "-R"] + tool + [mcbin] + srvargs
    env = dict(os.environ, PIN_ROOT=PIN_ROOT)
    errp = os.path.join(OUT, "mc.%s.err" % cfg)
    serr = open(errp, "w+")
    res = dict(rc=1, wall=None, out="", err="", ktime=None, ck=None,
               load0=loadavg(), load1=-1.0)
    srv = None
    sidecars, fifos = [], []
    try:
        if cfg in PT_CFGS:
            for c in MC_SRV_CORES[1:]:
                f = os.path.join(OUT, "fifo.%d" % c)
                if os.path.exists(f):
                    os.unlink(f)
                os.mkfifo(f)
                fifos.append(f)
                sidecars.append(subprocess.Popen(
                    ["taskset", "-c", HELPER_CORES, "setarch", "-R", PC, "--aux-mb", "512",
                     "--ptw", "--no-decode", "--aux-out", "/dev/null", "--cpu", str(c),
                     "--", "/bin/cat", f], cwd=OUT, env=env,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            time.sleep(0.5)
        srv = subprocess.Popen(argv, cwd=OUT, env=env, stdout=serr, stderr=serr,
                               stdin=subprocess.DEVNULL)
        if not wait_ready(port, 900 if cfg != "vanilla" else 120, srv):
            res["err"] = ("server-exited-rc=%s" % srv.poll()) if srv.poll() is not None \
                else "server-not-ready"
            try:
                serr.seek(0); res["err"] += " | " + serr.read()[-400:]
            except OSError:
                pass
            return res
        pid = find_server(port)
        if pid is None:
            res["err"] = "server-pid-not-found"; return res
        u0, _ = proc_times(pid)
        t0 = time.monotonic()
        cp = subprocess.run(["taskset", "-c", MC_CLI_CORES, MC_CLI,
                             "-s", "127.0.0.1:%d" % port, "-F", MC_CFG,
                             "-T", "4", "-c", "16", "-x", str(args.mc_ops)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, text=True, errors="replace",
                            timeout=args.timeout)
        res["wall"] = time.monotonic() - t0
        u1, _ = proc_times(pid)
        res["ktime"] = round(u1 - u0, 3)          # server user CPU seconds
        res["rc"] = cp.returncode
        res["out"] = cp.stdout[-2000:]
        m = re.search(r"Run time: ([0-9.]+)s Ops: (\d+) TPS: (\d+)", cp.stdout)
        # Our Memcached table prints BOTH the paper's server user-CPU metric and the
        # steady-state throughput, so both travel with every row.
        res["tps"] = int(m.group(3)) if m else None
        res["ck"] = ("ops=%s,tps=%s" % (m.group(2), m.group(3))) if m else None
        res["gate"] = mc_roundtrip(port)          # set/get/delete correctness gate
        res["load1"] = loadavg()
    except Exception as e:                                       # noqa: BLE001
        res["err"] = "%s: %s" % (type(e).__name__, e)
    finally:
        pid = find_server(port)
        if pid:
            for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGKILL):
                try:
                    os.kill(pid, sig)
                except OSError:
                    break
                for _ in range(200):
                    if not os.path.exists("/proc/%d" % pid):
                        break
                    time.sleep(0.05)
                if not os.path.exists("/proc/%d" % pid):
                    break
        if srv is not None:
            try:
                srv.kill(); srv.wait(timeout=30)
            except Exception:                                    # noqa: BLE001
                pass
        for f in fifos:                       # release the sidecar captures
            try:
                with open(f, "w") as fh:
                    fh.write("x\n")
            except OSError:
                pass
        for sc in sidecars:
            try:
                sc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                sc.kill()
        for f in fifos:
            if os.path.exists(f):
                os.unlink(f)
        serr.close()
    return res


RUNNER = {"poly": cell_poly, "pyperf": cell_pyperf, "rust": cell_rust,
          "node": cell_node, "java": cell_java, "mc": cell_mc}


# -------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--configs", default="vanilla,pinnoop")
    ap.add_argument("--suites", default="poly,pyperf,rust,node,java,mc")
    ap.add_argument("--cells", default="")
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--java-reps", type=int, default=3)
    ap.add_argument("--timeout", type=int, default=5400)
    ap.add_argument("--csv", default=os.path.join(OUT, "repslice.csv"))
    ap.add_argument("--mutex-wait", type=int, default=1800)
    ap.add_argument("--mc-ops", type=int, default=1000000)
    ap.add_argument("--mc-port", type=int, default=11311)
    ap.add_argument("--java-pin-iters", type=int, default=6)
    ap.add_argument("--java-pin-drop", type=int, default=3)
    ap.add_argument("--core", default=None,
                    help="override the timed core for the single-threaded cells "
                         "(0-7 are P-cores, 8-19 E-cores on this Arrow Lake part)")
    ap.add_argument("--pyloops", default="1",
                    help="PPF_LOOPS for the pyperformance cells: how many times the "
                         "benchmark's own steady-state region is repeated inside the timer. "
                         "Raising it amortises Pin's one-off translation cost.")
    ap.add_argument("--pin-root", default=PIN_ROOT,
                    help="Pin kit to run under (a different version rebuilds nothing here; "
                         "pass --noop-tool built against the same kit)")
    ap.add_argument("--pin-extra", default="",
                    help="whitespace-separated Pin kit flags added to every Pin arm. The "
                         "adopted default HiFi invocation is '-pin_memory_range "
                         "0x1000000000:0x1400000000 -enforce_pin_range_allocations 1 "
                         "-xyzzy -inter_trace_liveness 1' (the trailing liveness knob is a "
                         "completeness-neutral Pin-engine optimization the tool never reads; "
                         "kept on every Pin arm so the HiFi/floor ratio stays fair).")
    ap.add_argument("--memtrace-tool", default=MEMTRACE,
                    help="memorytracer Pintool; pass the build made against --pin-root "
                         "so the memorytracer row shares ONE Pin kit with the HiFi rows")
    ap.add_argument("--libdft-tool", default=LIBDFT)
    ap.add_argument("--libdft-pin-root", default=PIN_ROOT,
                    help="Pin kit for libdft (libdft64 only builds against Pin 3.x)")
    ap.add_argument("--valgrind", default=VALGRIND)
    ap.add_argument("--cap-x", type=float, default=0.0,
                    help="the paper's slowdown cap (200): a run gets cap_x * the cell's "
                         "native wall time (+ --cap-slack) and is otherwise killed and "
                         "reported conservatively AT the cap")
    ap.add_argument("--cap-from", default="",
                    help="CSV whose vanilla rows give this machine's native wall per cell")
    ap.add_argument("--cap-slack", type=float, default=120.0)
    ap.add_argument("--cap-ceiling", type=float, default=0.0,
                    help="absolute per-run ceiling in seconds (0 = none); a run stopped by "
                         "the ceiling BEFORE its 200x budget is marked 'stopped', not 'cap'")
    ap.add_argument("--load-gate", type=float, default=0.0,
                    help="hold each timed run until the 1-minute load average is at or "
                         "below this (0 = off).  The mutex does not evict work that was "
                         "already running, so gate on BOTH the lock and the load")
    ap.add_argument("--load-gate-wait", type=float, default=3600.0,
                    help="give up waiting after this many seconds and take the row anyway "
                         "(its load columns still say it was contended)")
    ap.add_argument("--stop-after-fail", action="store_true",
                    help="after a cap or a non-zero exit on rep 1, skip the remaining reps "
                         "of that (cell, config): the cell is already reported at the cap "
                         "or as Err, and the remaining reps would only burn the budget")
    ap.add_argument("--noop-tool", default=NOOP)
    ap.add_argument("--hifi-tool", default=HIFI)
    ap.add_argument("--hifi-extra", default="",
                    help="extra Pintool knobs for the HiFi arms only, e.g. '-sink ptwrite'")
    ap.add_argument("--label", default="",
                    help="a tag written into every row, naming this arm of the sweep")
    ap.add_argument("--mutex-label", default="repslice",
                    help="who to stamp into the mutex_running file, so a sibling "
                         "reading mutex_holder can see who holds it and for what")
    ap.add_argument("--mutex-inherit", action="store_true",
                    help="the caller already holds the timing mutex: do not take or release "
                         "it, and record the rows as mutex=held")
    ap.add_argument("--interleave-cells", action="store_true",
                    help="alternate the CELLS inside every repetition as well as the configs "
                         "and arms; use it when the result is the contrast BETWEEN cells")
    ap.add_argument("--no-lock", action="store_true")
    ap.add_argument("--report-only", action="store_true")
    ap.add_argument("--report-label", default=None,
                    help="report only rows carrying this --label")
    ap.add_argument("--jit-addon", default=None,
                    help="V8 hook addon (runtime/jit/jithook.cc build) for the node cells")
    ap.add_argument("--jit-agent", default=None,
                    help="JVMTI agent (runtime/jit/java/jvmtiagent.cc build) for the java cells")
    ap.add_argument("--jit-cache", default=None, help="content-hash analysis cache directory")
    ap.add_argument("--jit-dir", default=None,
                    help="scratch directory for analyzer sockets, logs and per-run stats")
    ap.add_argument("--jit-workers", type=int, default=4)
    ap.add_argument("--jit-agent-opts", default="",
                    help="extra comma-separated JVMTI agent options appended verbatim "
                         "(e.g. 'lanes=0' -- the D-J14 A/B control).  Recorded in the row "
                         "label by the caller, never silently defaulted.")
    ap.add_argument("--jit-cores", default="16,17,18,19",
                    help="cores for the analyzer services: never a timed or PT-helper core")
    ap.add_argument("--jit-miss", type=int, default=0,
                    help="hifitool -jitmiss: 1 counts code objects Pin had already translated "
                         "when their plan arrived, 2 also counts generated-code block "
                         "executions with and without a plan.  DIAGNOSTIC ONLY -- 2 inserts a "
                         "counter into every generated-code block, so never time with it")
    ap.add_argument("--max-load", type=float, default=0.0,
                    help="report only rows whose load average stayed below this "
                         "(0 = no filter); a contended row is thus self-identifying")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    OV["core"] = a.core
    OV["pyloops"] = a.pyloops
    OV["pin"] = os.path.join(a.pin_root, "pin")
    OV["noop"] = a.noop_tool
    OV["hifi"] = a.hifi_tool
    OV["hifi_extra"] = a.hifi_extra.split()
    OV["label"] = a.label
    OV["extra"] = a.pin_extra.split()
    OV["memtrace"] = a.memtrace_tool
    OV["libdft"] = a.libdft_tool
    OV["libdft_pin"] = a.libdft_pin_root
    OV["valgrind"] = a.valgrind
    CAP["x"] = a.cap_x
    CAP["slack"] = a.cap_slack
    CAP["ceil"] = a.cap_ceiling
    cap_load(a.cap_from)
    SINKS.extend(trace_sinks([OUT, WTB, REN, os.getcwd()]))
    if a.jit_dir:
        JIT.update(on=True, addon=a.jit_addon, agent=a.jit_agent,
                   cache=os.path.abspath(a.jit_cache), statsdir=os.path.abspath(a.jit_dir),
                   sock=os.path.join(os.path.abspath(a.jit_dir), "a.sock"),
                   workers=a.jit_workers, acores=a.jit_cores.split(","), miss=a.jit_miss,
                   aopts=("," + a.jit_agent_opts.strip(",")) if a.jit_agent_opts else "")
        os.makedirs(JIT["statsdir"], exist_ok=True)
        os.makedirs(JIT["cache"], exist_ok=True)
        if len(JIT["sock"]) + 4 >= 108:
            raise SystemExit("repslice: analyzer socket path too long")
    cfgs = [c for c in a.configs.split(",") if c]
    if "fast" in cfgs:                      # the rewritten images discard their value stream
        os.environ["PTLOG_DIR"] = "/dev/null"
        os.environ.pop("PTLOG_GT", None)
    suites = set(a.suites.split(","))
    want = [(s, c) for s, c in CELLS if s in suites]
    if a.cells:
        keep = set(a.cells.split(","))
        want = [(s, c) for s, c in want if c in keep]

    new = not os.path.exists(a.csv)
    done = set()
    if not new:
        with open(a.csv) as f:
            for row in csv.DictReader(f):
                done.add((row["suite"], row["cell"], row["config"], row["rep"],
                          row.get("label", "")))
    fh = open(a.csv, "a", newline="")
    w = csv.writer(fh)
    if new:
        w.writerow(["suite", "cell", "config", "rep", "ktime", "wall", "rc", "ck",
                    "mutex", "load0", "load1", "label", "ts", "tps", "gate", "cap"]); fh.flush()

    arms = [None]
    services = Analyzers(JIT["workers"], JIT["acores"], JIT["sock"], JIT["statsdir"]) \
        if (JIT["on"] and not a.report_only) else None
    if services:
        services.__enter__()
    try:
      if not a.report_only:
        if a.mutex_inherit:
            MUTEX_HELD[0] = True
            log("mutex inherited from the caller")
        else:
            mutex_take(a.mutex_wait, a.mutex_label)
        skip = set()

        def one(s, c, r, cf):
            """One timed run, appended to the CSV.  OV/arm is already set by the caller."""
            if (s, c, cf, str(r), OV["label"]) in done:
                return
            if (s, c, cf, OV["label"]) in skip:
                return
            a.rep = r
            load_gate(a.load_gate, a.load_gate_wait)
            budget = cell_timeout(s, c, a.timeout)
            saved, a.timeout = a.timeout, budget
            cap = ""
            try:
                res = RUNNER[s](cf, c, a)
            except subprocess.TimeoutExpired:
                # The run did not finish inside cap_x * native wall: the paper's rule is to
                # stop it and report the cell conservatively AT the cap.
                cap = "cap" if (CAP["x"] > 0 and budget < saved) else "timeout"
                res = dict(rc=-9, wall=None, ktime=None, ck=None, out="", err="timeout",
                           load0=loadavg(), load1=loadavg())
            finally:
                a.timeout = saved
            if cap == "cap" and CAP["ceil"] > 0 and budget >= CAP["ceil"]:
                cap = "stopped"       # the absolute ceiling fired before the 200x budget
            # memtrace on the Pin 4.4 kit cannot CREATE its trace file (compat shim, see
            # tracers/memtrace/p4/compat/fcntl.h): if the /dev/null sink is missing the tool
            # returns 1 from main() before PIN_StartProgram, so the benchmark never runs.
            # Fail the row loudly rather than let a sink-less run look like a fast one.
            if cf == "memtrace" and "failed to open" in (res.get("err") or ""):
                res["rc"], res["ktime"], cap = 90, None, "no-trace-sink"
            w.writerow([s, c, cf, r,
                        "" if res["ktime"] is None else "%.6f" % res["ktime"],
                        "" if res.get("wall") is None else "%.3f" % res["wall"],
                        res["rc"], res.get("ck") or "",
                        "held" if MUTEX_HELD[0] else "shared",
                        "%.2f" % res.get("load0", -1.0),
                        "%.2f" % res.get("load1", -1.0),
                        OV["label"], int(time.time()),
                        res.get("tps") if res.get("tps") is not None else "",
                        res.get("gate") or "", cap])
            fh.flush()
            if a.stop_after_fail and (cap or res["rc"] != 0 or res["ktime"] is None):
                skip.add((s, c, cf, OV["label"]))
                log("   -> %s/%s/%s: %s on rep%d, remaining reps skipped"
                    % (s, c, cf, cap or ("rc=%s" % res["rc"]), r))
            if JIT["on"] and cf in HIFI_CFGS:
                # the Pintool's own end-of-run counters (values logged, JIT objects and
                # sites taken, first-execution misses) are evidence, not decoration
                with open(os.path.join(JIT["statsdir"],
                                       "%s.%s.%s.r%s.err" % (s, c, cf, r)), "w") as ef:
                    ef.write(res.get("err") or "")
            log("%-7s %-14s %-9s %-10s rep%d  ktime=%s wall=%s rc=%s load=%.1f/%.1f"
                % (s, c, OV["label"] or "-", cf, r, res["ktime"],
                   None if res.get("wall") is None else round(res["wall"], 2),
                   res["rc"], res.get("load0", -1), res.get("load1", -1)))
            if res["rc"] != 0 and res.get("err"):
                log("   err: " + str(res["err"])[-300:])

        try:
            if a.interleave_cells:
                # The CELLS are alternated too when the result is the contrast between
                # two cells, so one repetition runs every cell x arm x config.
                lk = MachineLock() if not a.no_lock else None
                if lk:
                    lk.__enter__()
                try:
                    for r in range(1, a.reps + 1):
                        for s, c in want:
                            for x in arms:
                                for cf in cfgs:
                                    one(s, c, r, cf)
                finally:
                    if lk:
                        lk.__exit__()
            else:
              for s, c in want:
                nrep = a.java_reps if s == "java" else a.reps
                todo = [(r, cf, x) for r in range(1, nrep + 1) for x in arms
                        for cf in cfgs
                        if (s, c, cf, str(r), x or a.label) not in done]
                if not todo:
                    log("%s/%s complete" % (s, c)); continue
                lk = MachineLock() if not a.no_lock else None
                if lk:
                    lk.__enter__()
                try:
                    for r in range(1, nrep + 1):
                      for x in arms:                          # arms ALTERNATED inside the rep
                        for cf in cfgs:                       # configs ALTERNATED inside the arm
                            one(s, c, r, cf)
                finally:
                    if lk:
                        lk.__exit__()
        finally:
            if not a.mutex_inherit:
                mutex_give()
    finally:
        if services:
            services.__exit__()
        for lnk in SINKS:                   # remove the /dev/null trace sinks
            try:
                if os.path.islink(lnk):
                    os.unlink(lnk)
            except OSError:
                pass
    fh.close()
    report(a.csv, cfgs, a.max_load, a.report_label)


def report(path, cfgs, max_load=0.0, label_filter=None):
    rows = list(csv.DictReader(open(path)))
    by = {}
    drop = 0
    for r in rows:
        if r["ktime"] == "" or r["rc"] != "0":
            continue
        if max_load > 0 and r.get("load0") and r.get("load1"):
            if max(float(r["load0"]), float(r["load1"])) > max_load:
                drop += 1
                continue
        if label_filter is not None and r.get("label", "") != label_filter:
            continue
        by.setdefault((r["suite"], r["cell"], r["config"]), []).append(
            (float(r["ktime"]), float(r["wall"]) if r["wall"] else None))
    base = cfgs[0]
    if max_load > 0:
        print("[repslice] load filter %.1f: %d contended rows excluded" % (max_load, drop))
    print("\n| suite | cell | " + " | ".join("%s ktime | %s x | %s wall x" % (c, c, c)
                                             for c in cfgs) + " |")
    geo = {c: {} for c in cfgs}
    geow = {c: {} for c in cfgs}
    for s, c in CELLS:
        if (s, c, base) not in by:
            continue
        bk = statistics.median(x[0] for x in by[(s, c, base)])
        bw = statistics.median(x[1] for x in by[(s, c, base)] if x[1] is not None) \
            if any(x[1] is not None for x in by[(s, c, base)]) else None
        line = "| %s | %s |" % (s, c)
        for cf in cfgs:
            if (s, c, cf) not in by:
                line += " - | - | - |"; continue
            k = statistics.median(x[0] for x in by[(s, c, cf)])
            ws = [x[1] for x in by[(s, c, cf)] if x[1] is not None]
            wm = statistics.median(ws) if ws else None
            rx = k / bk if bk else float("nan")
            wx = (wm / bw) if (wm and bw) else float("nan")
            line += " %.6f | %.3fx | %s |" % (k, rx, "-" if wx != wx else "%.3fx" % wx)
            geo[cf].setdefault(s, []).append(rx)
            if wx == wx:
                geow[cf].setdefault(s, []).append(wx)
        print(line)
    print("\n| suite | " + " | ".join("%s geomean(ktime) | %s geomean(wall)" % (c, c)
                                      for c in cfgs) + " |")
    for s in ["poly", "pyperf", "mc", "rust", "node", "java"]:
        line = "| %s |" % s
        for cf in cfgs:
            v = geo[cf].get(s); vw = geow[cf].get(s)
            g = math.exp(sum(math.log(x) for x in v) / len(v)) if v else None
            gw = math.exp(sum(math.log(x) for x in vw) / len(vw)) if vw else None
            line += " %s | %s |" % ("-" if g is None else "%.3fx" % g,
                                    "-" if gw is None else "%.3fx" % gw)
        print(line)


if __name__ == "__main__":
    main()
