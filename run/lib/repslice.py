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
import argparse, csv, fcntl, hashlib, json, math, os, re, shutil, signal, socket, statistics, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ARTIFACT_ROOT") or os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.environ.get("FIG5_OUT", os.path.join(HERE, "out"))   # the driver dir may be mounted read-only
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
PYRUN = _env("FIG5_PYRUN", os.path.join(SUITES, "pyperformance", "pyperf", "run_one.py"))
# whole-process Fast: directory holding the rewritten shared libraries (libc.so.6, ...), given to the traced
# child only (never to pt_capture2 or the client) -- via --child-env on the single-core path, via `env' otherwise.
FIG5_FASTLIB = os.environ.get("FIG5_FASTLIB", "")
# Fast-PTWRITE (fast_ptw): the same layout from the mixed-sink tree (run/lib/build_ptw.sh); unset = FIG5_FASTLIB.
FIG5_FASTLIB_PTW = os.environ.get("FIG5_FASTLIB_PTW", "")


def fastlib(cfg):
    """The rewritten-library directory of a Fast configuration's traced child."""
    return (FIG5_FASTLIB_PTW or FIG5_FASTLIB) if cfg == "fast_ptw" else FIG5_FASTLIB
RUSTBIN = os.path.join(SUITES, "rust", "micro-bench", "target", "release", "micro-bench")
WTB = os.path.join(SUITES, "node", "web-tooling-benchmark")
NODE_BIN = _env("NODE", os.path.join(SUITES, "node", "bin", "node"))
WTB_JS = _env("WTB_JS", os.path.join(SUITES, "node", "wtb.js"))   # the JIT Fast sweep: run/lib/jitwp/wtb.js
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
       "cachever": "v2.35", "aopts": ""}
ANALYZE = os.path.join(TOOL, "static", "analyze.py")
# keyframe period of the JIT-code hooks in the Fast configurations (run/lib/common.sh FAST_KEYFRAME)
FAST_KEYFRAME = os.environ.get("FAST_KEYFRAME", "128")
VENVPY = _env("PYBIN", "python3")
HIFI_CFGS = ("pinhifi", "pinhifi_nopt", "pinhifi_ptw")
OV = {"core": None, "pyloops": "1", "pin": PIN, "noop": NOOP, "hifi": HIFI, "label": "",
      # extra Pintool knobs for the HiFi arms (the pinhifi_ptw sink knobs come from hifi_sink()). Empty by default.
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
    """median vanilla wall per (suite, cell) from reference CSVs (comma-separated paths)."""
    acc = {}
    for p in (path or "").split(","):
        if not p or not os.path.exists(p):
            continue
        with open(p) as f:
            for r in csv.DictReader(f):
                if (r["config"] == "vanilla" and r["rc"] == "0" and r.get("wall")
                        and (not CAP.get("label") or r.get("label") == CAP["label"])):
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


RUST_RAYON_CORES = _env("RUST_CORES", "4-7")                                  # multi-threaded Rust cells
MC_SRV_CORES = [int(c) for c in _env("MC_SRV_CORES", "4,5,6,7").split(",")]    # memcached server threads
MC_CLI_CORES = _env("MC_CLI_CORES", "8-11")                                   # the memaslap load client
JAVA_CORES = _env("JAVA_CORES", "0-3")                                           # the JVM (vanilla and baseline arms)
CLK = os.sysconf("SC_CLK_TCK")

# ---------------------------------------------------------------- the slice
POLY = os.environ.get("FIG5_POLY", "gemm atax jacobi-2d correlation durbin").split()   # full 30 via env
PYPERF = (open(os.environ["FIG5_PYPERF_LIST"]).read().split() if os.environ.get("FIG5_PYPERF_LIST")
          else ["nbody", "richards", "float", "go", "json_dumps", "regex_v8"])
RUST = os.environ.get("FIG5_RUST", "seq rayon").split()   # full Rust Stream = 6 runtimes via env
NODE = [("acorn", 10), ("babel", 10)]     # (payload, timed iterations)
NODE_WARMUP = int(os.environ.get("WTB_WARMUP") or 5)   # untimed in-process iterations before the timer
# The paper's full Web Tooling suite (18 benchmarks); cells beyond NODE are opt-in through --cells.
WTB_ALL = ("acorn babel babel-minify babylon buble chai coffeescript espree esprima jshint lebab postcss "
           "prepack prettier source-map terser typescript uglify-js").split()
JAVA_B = [("scrabble", 12, 6), ("philosophers", 8, 4)]   # (bench, iters, drop-warmup)
# Further Renaissance cells (PTJ_JAVA_BENCH=a,b,...); their iterations come from --java-pin-iters/-drop with
# PTJ_JAVA_ITERS_ALL=1 (run/lib/jitwp/sweep_jit_fast.py passes run/lib/jitwp/java_iters.txt).
JAVA_B += [(_b, 30, 20) for _b in os.environ.get("PTJ_JAVA_BENCH", "").split(",")
           if _b and _b not in ("scrabble", "philosophers")]
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
    into mutex_running.txt so other users can see who holds it and since when."""
    t = 0
    while t < wait:
        if not os.path.exists(MUTEX_RUN):
            try:
                os.rename(MUTEX_IDLE, MUTEX_RUN)
            except OSError:
                open(MUTEX_RUN, "w").close()
            # the mutex files may be owned by another uid; the directory is writable by
            # everyone who uses the protocol, so stamp the holder by replace(), not by
            # opening the file.
            tmp = os.path.join(MUTEX_ROOT, ".mutex_stamp.%d" % os.getpid())
            # A TOKEN LINE IS MANDATORY: a release only removes a hold whose `token=' line it
            # can match, so a stamp without one could only be undone by hand.
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
    release, it may already have been released and another process may have taken it.
    Unlinking then destroys THEIR stamp and lets a third process in beside them."""
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
PT_CFGS = ("vanilla_pt", "pinhifi", "fast", "pinhifi_ptw", "fast_ptw")
FAST_CFGS = ("fast", "fast_ptw")


# e9fast: the WHOLE-PROGRAM Fast mode of the JIT suites.  The runtime's native ELF images are E9Patch-rewritten
# (node; libjvm + 12 JDK libraries; libc/libm/libstdc++/libgcc_s for both), and the runtime hook (jithook.node /
# ptjava.so, tool code, not rewritten) patches trampolines into the JIT code cache; buffer sink, per-CPU Intel PT
# capture under `pt_capture2 --sideband' (the spare-TCB handoff a whole-program threaded build needs).  Built and
# staged by run/lib/jitwp/build_stage.sh; the stage directory is --jitwp-stage.  Node/Java only.
E9FAST = "e9fast"
# e9fast_ptw: the JIT suites' Fast-PTWRITE arm, the MIXED sink of the other suites' Fast-PTWRITE images: the runtime's
# main ELF image (node; libjvm.so) is rebuilt with --sink mixed from the same spec and keyframe plan
# (run/lib/jitwp/build_ptw.sh -> <jitwp>/ptw/stage): the sites of its per-site PTWRITE budget (data/ptw_sites/jit, 5 M
# values/s per benchmark from an untimed count profile) log through `ptwrite', every other ELF site and every site the
# runtime hook patches into the JIT code through the buffer, as in e9fast; the capture enables PTW packets.
E9FAST_PTW = "e9fast_ptw"
E9FAST_CFGS = (E9FAST, E9FAST_PTW)
# The Fast images' sync-marker carrier (run/lib/common.sh PT_SYNC_CARRIER; rewrite.py --sync-carrier): `tnt' (default)
# = no PTWRITE anywhere in a non-PTWRITE configuration (ELF images, the JIT hooks' PTLOG_SYNC_CARRIER), so their
# captures run with PTW packets off; only the *-PTWRITE bars (and a `ptwrite'-carrier build) enable them.
SYNC_CARRIER = os.environ.get("PT_SYNC_CARRIER", "tnt")


def ptw_flag(cfg):
    """pt_capture2's PTW switch for configuration `cfg': PTW packets only where PTWRITE executes."""
    if cfg in ("pinhifi_ptw", "fast_ptw", E9FAST_PTW) or (SYNC_CARRIER == "ptwrite" and cfg in ("fast", E9FAST)):
        return "--ptw"
    return "--no-ptw"
JITWP = {"stage": None, "manifest": None}


def jitwp_kf_n(suite):
    """Per-suite ELF keyframe-counter count: the manifest's PTLOG_KF_N_group entry (max(common, suite)), else the
    process-wide PTLOG_KF_N."""
    m = JITWP["manifest"]
    return m.get("PTLOG_KF_N_group", {}).get(suite) or m["PTLOG_KF_N"]


def jit_kf_gs():
    """PTJ_JIT_KF_GS=J (default 16384): per-thread keyframe cells reserved for the JIT code after the ELF images' own
    (PTLOG_KF_N += J; the Java agent gets kfgs=<ELF PTLOG_KF_N>)."""
    return int(os.environ.get("PTJ_JIT_KF_GS", "16384") or 0)


def e9fast_child_env(suite, cfg=None):
    """The tracee-only environment of an e9fast run (pt_capture2 --child-env, so it never applies to the capture tool).
    PTLOG_SYNC must equal the images' --sync (4096, their <image>.ptlog.env); mixed periods misalign reconstruction.
    PTLOG_SIGEXIT=0 for Java: HotSpot chains a pre-installed SIGPIPE handler and then ignores the signal, so
    the runtime's teardown handler would kill the JVM on the first EPIPE."""
    rt = os.path.join(TOOL, "runtime", "jit_toolchain", "rt")   # the runtime the images were built against
    return {"LD_PRELOAD": "%s/ptlogmt.so:%s/ptlogrt.so" % (rt, rt),
            "LD_LIBRARY_PATH": os.path.join(JITWP["ptw_stage"] if cfg == E9FAST_PTW else JITWP["stage"], "libs"),
            "PTLOG_DIR": "/dev/null", "PTLOG_KF_N": str(jitwp_kf_n(suite) + jit_kf_gs()), "PTLOG_SYNC": "4096",
            "PTLOG_SIGEXIT": "0" if suite == "java" else "1", "PTLOG_RECYCLE": "1",
            "PTLOG_SYNC_CARRIER": SYNC_CARRIER}


def MPG(cfg):
    """Fast captures poll /proc/PID/maps only while the mapping set can change (pt_capture2 --map-poll-gate);
    FIG5_MAP_POLL_GATE=0 selects the free-running poller."""
    if cfg in FAST_CFGS and os.environ.get("FIG5_MAP_POLL_GATE", "1") == "1":
        return ["--map-poll-gate"]
    return []
RT_PRELOAD = "%s:%s" % (os.path.join(TOOL, "runtime", "rt", "ptlogmt.so"),
                        os.path.join(TOOL, "runtime", "rt", "ptlogrt.so"))
# Every Fast process of a rewritten non-JIT image runs with the pthread shim preloaded (the buffer runtime is injected into the image) and under
# `pt_capture2 --sideband' (PTRACE_O_TRACECLONE: each new thread gets its own TCB at its first stop).
MT_PRELOAD = os.path.join(TOOL, "runtime", "rt", "ptlogmt.so")
FAST_DIR = _env("FAST_DIR", os.path.join(OUT, "fast"))
FAST_PTW_DIR = _env("FAST_PTW_DIR", os.path.join(OUT, "fast_ptw"))


def fast_image(suite, cell, cfg="fast"):
    """The Fast image of a cell (buffer sink: FAST_DIR; fast_ptw = the mixed-sink tree of run/lib/build_ptw.sh:
    FAST_PTW_DIR, same layout)."""
    D = FAST_PTW_DIR if cfg == "fast_ptw" else FAST_DIR
    if suite == "poly":
        return os.path.join(D, "poly", "%s_large" % cell)
    if suite == "pyperf":
        return os.path.join(D, "pyfast", "bin", "python3.12")
    if suite == "rust":
        return os.path.join(D, "rust", "micro-bench")
    if suite == "mc":
        return os.path.join(D, "mc", "memcached_sym")
    raise SystemExit("repslice: no Fast image for suite %s" % suite)


# HiFi-PTWRITE = the Pintool's MIXED sink (`-sink ptwrite -ptwbuffer LIST'): the ELF-plan sites of the cell's per-site
# PTWRITE budget (5 M values/s, the rule of the Fast-PTWRITE lists in data/ptw_sites) log through `ptwrite', the sites
# over the budget and every generated-code (JIT) site through the fill buffer.  LIST = data/ptw_sites/hifi/<suite>/
# <cell>.buf if the artifact ships one, else $OUT/hifi_ptw/<suite>/<cell>.buf, made on first use by one untimed profile
# run of the cell (pinhifi with `-sink count') and ptracer/runtime/pinjit/hifi_ptw_budget.py (rate = the run's
# values / its measured ktime).  HIFI_PTW_ALL=1: every value through `ptwrite'.
HIFI_PTW = dict(ship=os.path.join(ROOT, "data", "ptw_sites", "hifi"), gen=os.path.join(OUT, "hifi_ptw"),
                budget=os.environ.get("HIFI_PTW_BUDGET", "5e6"), all=os.environ.get("HIFI_PTW_ALL") == "1",
                count=None)     # count: the profile prefix while a profile run is in progress


def hifi_ptw_list(suite, cell):
    for d in (HIFI_PTW["ship"], HIFI_PTW["gen"]):
        f = os.path.join(d, suite, "%s.buf" % cell)
        if os.path.exists(f):
            return f
    return None


def hifi_sink(cfg, suite, cell):
    """The HiFi Pintool's sink knobs for configuration `cfg'."""
    if cfg == "pinhifi" and HIFI_PTW["count"]:
        return ["-sink", "count", "-countout", HIFI_PTW["count"]]
    if cfg != "pinhifi_ptw":
        return []
    lst = None if HIFI_PTW["all"] else hifi_ptw_list(suite, cell)
    if lst is None and not HIFI_PTW["all"]:
        log("WARNING: no HiFi-PTWRITE site list for %s/%s: every value through ptwrite" % (suite, cell))
    return ["-sink", "ptwrite"] + (["-ptwbuffer", lst] if lst else [])


def hifi_ptw_profile(s, c, a):
    """One untimed profile run (pinhifi, `-sink count') -> the cell's budgeted buffer-site list."""
    d = os.path.join(HIFI_PTW["gen"], s, c + ".count")
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    HIFI_PTW["count"] = os.path.join(d, "c")
    log("%-7s %-14s HiFi-PTWRITE site profile (untimed pinhifi, -sink count)" % (s, c))
    try:
        res = RUNNER[s]("pinhifi", c, a)
    finally:
        HIFI_PTW["count"] = None
    if res.get("rc") != 0 or not res.get("ktime"):
        log("   profile run failed (rc=%s): no list" % res.get("rc"))
        return
    out = os.path.join(HIFI_PTW["gen"], s, c + ".buf")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "ptracer", "runtime", "pinjit", "hifi_ptw_budget.py"),
                        "--budget", HIFI_PTW["budget"], out, "%s:%.6f" % (d, res["ktime"])],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    log("   " + r.stdout.strip().replace("\n", "\n   "))


def plan_for(suite, cell):
    if suite == "poly":
        return os.path.join(PLANS, "poly.%s.plan" % cell)
    if suite == "pyperf":
        return os.path.join(PLANS, os.environ.get("FIG5_PYPLAN", "cpython.plan"))
    if suite in ("rust", "mc"):
        return os.path.join(PLANS, "%s.plan" % suite)
    # node / java: JIT-generated code.  Without a plan the HiFi Pintool takes only the runtime-published JIT sites;
    # PTJ_HIFI_PLAN_<SUITE> (run/lib/node_hifi_plan.sh) adds the whole-process plan of the runtime's native images.
    return os.environ.get("PTJ_HIFI_PLAN_%s" % suite.upper()) or None


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

    The three invocations:
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
    if cfg in FAST_CFGS and suite not in ("node", "java"):
        prog_argv = [fast_image(suite, cell, cfg)] + list(prog_argv[1:])
    if cfg in HIFI_CFGS:
        plan = plan_for(suite, cell)
        tool = [NO_CLONE3, OV["pin"], "-ifeellucky"] + OV["extra"]
        if suite == "pyperf" and cell in PY_SPAWN:
            tool += ["-follow_execv"]
        tool += ["-t", OV["hifi"]]
        # A suite whose code is GENERATED has no ELF plan; the same Pintool then takes its
        # sites from the runtime bridge instead.  Nothing else about the tool changes.
        if plan:
            tool += ["-plan", plan]
        tool += ["-cvdir", "/dev/null", "-stats", "1"]
        # Node.js publishes a plan BEFORE its code first runs, so re-translating per publication is nearly free and
        # the batched hand-over (the Pintool's default, needed by HotSpot) only delays instrumentation: per request.
        if suite == "node" and "-jitbatch" not in OV["hifi_extra"]:
            tool += ["-jitbatch", "0"]
        tool += OV["hifi_extra"]
        tool += hifi_sink(cfg, suite, cell)  # HiFi-PTWRITE: the mixed sink (see HIFI_PTW)
        if JIT["miss"]:
            tool += ["-jitmiss", str(JIT["miss"])]
        tool += ["--"]
    elif cfg == "pinnoop":
        tool = [NO_CLONE3, OV["pin"], "-ifeellucky"] + OV["extra"] + ["-t", OV["noop"], "--"]
    elif cfg in BASELINE_CFGS:
        tool = baseline_prefix(cfg, os.path.join(OUT, "valgrind.%s.%s.log" % (suite, cell)))
    inner = ["taskset", "-c", cores] + (tool or []) + prog_argv
    if cfg in E9FAST_CFGS:
        # per-CPU PT on the timed cores; pt_capture2 pins the child to exactly that --cpu set, so there is no
        # `taskset' exec after `--'.  The sideband file is mappings + switch records written at exit,
        # not a trace stream: both streams are still discarded (--aux-out /dev/null, PTLOG_DIR=/dev/null).
        # FIG5_JIT_AUX_OUT=FILE keeps the PT packets (FILE.cpu<N>) for a packet count; functional checks only.
        cl = cpu_list(cores)
        percpu = []
        for c in cl:
            percpu += ["--cpu", str(c)]
        extra = ["--sideband", os.path.join(JIT["statsdir"], "sb.%s.%s.json" % (suite, cell))]
        for k, v in e9fast_child_env(suite).items():
            extra += ["--child-env", "%s=%s" % (k, v)]
        return (["taskset", "-c", HELPER_CORES, "setarch", "-R", PC,
                 "--aux-mb", str(max(128, aux_mb // max(1, len(cl)))), ptw_flag(cfg), "--no-decode",
                 "--aux-out", os.environ.get("FIG5_JIT_AUX_OUT", "/dev/null")] + percpu + extra + ["--"] + (tool or []) + prog_argv)
    if cfg in PT_CFGS:
        percpu = []
        if want_percpu(suite, cores) or (suite == "pyperf" and cell in PY_SPAWN):
            cl = cpu_list(cores)
            for c in cl:
                percpu += ["--cpu", str(c)]
            aux_mb = max(128, aux_mb // max(1, len(cl)))   # one AUX ring per event
        extra = []
        if cfg in FAST_CFGS and suite not in ("node", "java"):
            extra = ["--sideband", os.path.join(OUT, "sb.%s.%s.json" % (suite, cell)),
                     "--child-env", "LD_PRELOAD=" + MT_PRELOAD]
        if cfg in FAST_CFGS and suite in ("node", "java"):
            # native JIT mode (sideband capture); fast_ptw: trampolines execute `ptwrite',
            # no buffer runtime is preloaded.
            extra = ["--sideband", os.path.join(OUT, "sb.%s.%s.json" % (suite, cell))]
            if cfg == "fast":
                extra += ["--child-env", "LD_PRELOAD=" + RT_PRELOAD]
        if not percpu and len(cpu_list(cores)) == 1:
            # pin the traced child with --child-core, not a taskset exec after --
            inner = (tool or []) + prog_argv
            extra = ["--child-core", str(cpu_list(cores)[0])] + extra
            if cfg in FAST_CFGS and fastlib(cfg):
                extra += ["--child-env", "LD_LIBRARY_PATH=" + fastlib(cfg)]
        elif percpu and cfg in FAST_CFGS and suite not in ("node", "java"):
            # pt_capture2 pins the child to exactly the --cpu set, so no `taskset'/`env' exec chain
            # after `--' (under --sideband it would put 3 address spaces in the sideband).
            inner = (tool or []) + prog_argv
            if fastlib(cfg):
                extra += ["--child-env", "LD_LIBRARY_PATH=" + fastlib(cfg)]
        elif cfg in FAST_CFGS and fastlib(cfg):
            inner = inner[:3] + ["/usr/bin/env", "LD_LIBRARY_PATH=" + fastlib(cfg)] + inner[3:]
        return (["taskset", "-c", HELPER_CORES, "setarch", "-R", PC,
                 "--aux-mb", str(aux_mb), ptw_flag(cfg), "--no-decode",
                 "--aux-out", "/dev/null"] + percpu + extra + MPG(cfg) + ["--"] + inner)
    return ["taskset", "-c", cores, "setarch", "-R"] + (tool or []) + prog_argv


def jit_env(suite, cell, cfg, rep):
    """Environment for a run whose code is generated at run time.  Same analyzer service,
    same content-hash cache and same selected-value plans the Fast path uses (the paper's
    JIT path is shared too); only the SUBSTRATE differs -- Pin instruments, nothing in the
    application's own bytes is rewritten."""
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("PTJIT_", "JITPOC_", "PTLOG_"))}
    env.update(TMPDIR=JIT["statsdir"], PTLOG_DIR="/dev/null")
    if cfg in E9FAST_CFGS + ("fast",):
        env["PTLOG_SYNC_CARRIER"] = SYNC_CARRIER    # the JIT hooks' buffer-sink sync markers (jitpatch.h)
    if suite == "node" and cfg in E9FAST_CFGS:
        # the V8 hook in its whole-program Fast configuration: Fast location objective, buffer sink into the SAME
        # ptlog ring the rewritten ELF images use, keyframes, the engine-neutral placement rules (slide, shift,
        # flag-saving keyframe guard), asynchronous analysis with one worker per analyzer service, and the untimed
        # wait for pending plans at the warm-up/timed boundary (WTB_DRAIN_MS, read by run/lib/jitwp/wtb.js).
        env.update(PTJIT_MODE="4", PTJIT_PIN="0", PTJIT_FAST="1",
                   PTJIT_SINK="buffer",           # e9fast_ptw too: the JIT-code sites stay on the buffer
                   PTJIT_KEYFRAME=FAST_KEYFRAME, PTJIT_NOSPAWN="1", PTJIT_CACHE_VER=JIT["cachever"],
                   PTJIT_CACHE=JIT["cache"], PTJIT_SOCK=JIT["sock"] + ".0",
                   PTJIT_ADDON=JITWP["manifest"]["jithook"], PTJIT_ARENA_MB="128",
                   PTJIT_STATS=jit_statsfile(suite, cell, cfg, rep),
                   PTJIT_SLIDE="1", PTJIT_SHIFT="1", PTJIT_KF_FLAGS_SAVE="1", PTJIT_ASYNC="1",
                   PTJIT_WORKERS=str(JIT["workers"]))
        env.setdefault("WTB_DRAIN_MS", "120000")
        return env
    if suite != "node" or cfg not in HIFI_CFGS + FAST_CFGS:
        return env
    if cfg in FAST_CFGS:                    # Fast: trampolines patched into the JIT code
        env.update(PTJIT_MODE="4", PTJIT_PIN="0", PTJIT_FAST="1",
                   PTJIT_SINK="buffer" if cfg == "fast" else "ptwrite",
                   PTJIT_KEYFRAME=FAST_KEYFRAME, PTJIT_NOROOTS="1", PTJIT_NOSPAWN="1",
                   PTJIT_CACHE_VER=JIT["cachever"], PTJIT_CACHE=JIT["cache"],
                   PTJIT_SOCK=JIT["sock"] + ".0", PTJIT_ADDON=JIT["addon"],
                   PTJIT_ARENA_MB="128", PTJIT_STATS=jit_statsfile(suite, cell, cfg, rep))
        return env
    env.update(PTJIT_MODE="4", PTJIT_PIN="1", PTJIT_KEYFRAME="0", PTJIT_FAST="0",
               PTJIT_NOSPAWN="1", PTJIT_NOROOTS="1", PTJIT_CACHE_VER=JIT["cachever"],
               PTJIT_CACHE=JIT["cache"], PTJIT_SOCK=JIT["sock"] + ".0",
               PTJIT_ADDON=JIT["addon"], PTJIT_ARENA_MB="128",
               PTJIT_STATS=jit_statsfile(suite, cell, cfg, rep))
    # HiFi arms: PTJ_PASS_PTJIT names extra PTJIT_* settings to pass through (Node HiFi: PTJIT_ASYNC, PTJIT_PIN_ASYNC,
    # PTJIT_WORKERS = asynchronous analysis under the Pin bridge; see run/lib/node_hifi.sh).
    for k in filter(None, os.environ.get("PTJ_PASS_PTJIT", "").split(",")):
        if k.startswith("PTJIT_") and k in os.environ:
            env[k] = os.environ[k]
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
    stop another process from starting untimed work next to a measured row.  Every row
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


PT_SUMMARY = re.compile(r"aux_bytes=(\d+) \(drained; ring=\d+MB x\d+, peak fill ([0-9.]+)%, "
                        r"lost=(\d+) bytes, PERF_RECORD_AUX truncated=(\d+)\)")


def pt_stats(text):
    """[lost_bytes, truncated, aux_bytes, peak_fill] from pt_capture2's end-of-run line."""
    m = PT_SUMMARY.findall(text)
    if not m:
        return ["", "", "", ""]
    m = m[-1]
    return [m[2], m[3], m[0], m[1]]


def loadavg():
    """The 1-minute load average.  Recorded on EVERY timed row: taking the timing mutex does
    not evict work that was already running, so a row measured next to concurrent work
    must be self-identifying in the data and not only in prose."""
    try:
        return float(open("/proc/loadavg").read().split()[0])
    except Exception:                                            # noqa: BLE001
        return -1.0


# Every timed run starts its own session (capture tool, Pin, the tracee and all their children); when the run ends or
# is stopped, the whole session is killed and checked gone, so no tracee outlives its row on the timed cores.
SESSIONS = set()


def session_pids(sid):
    """live (non-zombie) processes of session `sid'"""
    out = []
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            with open("/proc/%s/stat" % d) as f:
                st = f.read()
        except OSError:
            continue
        rest = st[st.rindex(")") + 2:].split()
        if int(rest[3]) == sid and rest[0] != "Z":
            out.append(int(d))
    return out


def kill_session(sid, why=""):
    """SIGKILL every process of session `sid' until none is left; True when it is gone."""
    first = True
    for _ in range(600):
        pids = session_pids(sid)
        if not pids:
            SESSIONS.discard(sid)
            return True
        if first and why:
            log("   %s: killing %d process(es) of the run's session %d" % (why, len(pids), sid))
        first = False
        for q in pids:
            try:
                os.kill(q, signal.SIGKILL)
            except OSError:
                pass
        time.sleep(0.1)
    log("   WARNING: processes of session %d survive SIGKILL: %s" % (sid, session_pids(sid)))
    return False


def reap_strays():
    """before a row: no process of an earlier row may still run"""
    for sid in list(SESSIONS):
        if session_pids(sid):
            kill_session(sid, "stray tracee from an earlier row")
        else:
            SESSIONS.discard(sid)


def run(argv, cwd=None, env=None, timeout=3600):
    l0 = loadavg()
    t0 = time.monotonic()
    p = subprocess.Popen(argv, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         stdin=subprocess.DEVNULL, text=True, errors="replace", start_new_session=True)
    SESSIONS.add(p.pid)
    try:
        out, err = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        kill_session(p.pid, "stopped at its time limit")
        p.communicate()
        raise
    wall = time.monotonic() - t0
    kill_session(p.pid, "left behind by the finished run")
    return dict(rc=p.returncode, wall=wall, out=out, err=err, load0=l0, load1=loadavg())


# ------------------------------------------------------------------- cells
def cell_poly(cfg, k, args):
    tgt = os.path.join(TARGETS, "%s_large" % k)
    r = run(wrap(cfg, OV["core"] or CORE, [tgt], "poly", k), cwd=OUT, timeout=args.timeout)
    v = FLOATRE.findall(r["out"])
    r["ktime"] = float(v[-1]) if v else None
    r["ck"] = None
    return r




# benchmarks whose timed work runs in CHILD processes (functional pass, AUDIT_SPAWN > 0): per-CPU capture
# (children inherit the core pinning) and Pin -follow_execv, so the whole process TREE is traced in both modes.
PY_SPAWN = set(os.environ.get("FIG5_PYSPAWN", "2to3 concurrent_imap dask python_startup python_startup_no_site").split())


PYLOOPS = {}
if os.environ.get("FIG5_PYLOOPS"):   # per-benchmark loops calibrated on vanilla (>= 100 ms timed), the same in all arms
    for _l in open(os.environ["FIG5_PYLOOPS"]):
        _p = _l.split()
        if len(_p) >= 2 and not _l.startswith("#"):
            PYLOOPS[_p[0]] = _p[1]


def cell_pyperf(cfg, b, args):
    env = dict(os.environ, PPF_LOOPS=PYLOOPS.get(b, OV["pyloops"]), PPF_WARM="1")
    if os.environ.get("FIG5_PYLOOPS") and b not in PYLOOPS:
        raise SystemExit("repslice: no calibrated loops for %s" % b)
    # the Fast arm imports the REWRITTEN extension modules (site/extmods copies with every .so rewritten)
    if cfg == "fast_ptw" and os.environ.get("FIG5_PYPATH_FAST_PTW"):
        env["PYTHONPATH"] = os.environ["FIG5_PYPATH_FAST_PTW"]
    elif cfg in FAST_CFGS and os.environ.get("FIG5_PYPATH_FAST"):
        env["PYTHONPATH"] = os.environ["FIG5_PYPATH_FAST"]
    elif os.environ.get("FIG5_PYPATH_VAN"):             # vanilla + HiFi: the original extension modules
        env["PYTHONPATH"] = os.environ["FIG5_PYPATH_VAN"]
    r = run(wrap(cfg, OV["core"] or CORE, [PYVAN, PYRUN, b], "pyperf", b),
            env=env, timeout=args.timeout)
    m = re.findall(r"KTIME ([0-9.]+)", r["err"])
    r["ktime"] = float(m[-1]) if m else None
    r["ck"] = None
    # a Fast row whose child printed the runtime's INHERITED-%gs warning logged through another thread's
    # cursor -> invalid (rc 96), never a quiet number.
    if cfg in FAST_CFGS and "INHERITED %gs" in (r["err"] or ""):
        r["gate"] = "inherited-gs"
        if r["rc"] == 0:
            r["rc"] = 96
    return r


RUST_ARGS = {"seq":   (["sequential", "1024", "1", "3000", "2000"], CORE),
             "rayon": (["rayon", "1024", "2", "3000", "2000"], RUST_RAYON_CORES),
             # full Rust Stream: the other four runtimes, same image size/iterations/threads/cores as rayon
             "rust-ssp":    (["rust-ssp", "1024", "2", "3000", "2000"], RUST_RAYON_CORES),
             "std-threads": (["std-threads", "1024", "2", "3000", "2000"], RUST_RAYON_CORES),
             "tokio":       (["tokio", "1024", "2", "3000", "2000"], RUST_RAYON_CORES),
             "pipeliner":   (["pipeliner", "1024", "2", "3000", "2000"], RUST_RAYON_CORES),
             # the paper's traditional-tracer Rust cells (run/fig5_traditional.sh): 4000x4000 image, 100+100
             # iterations, rayon with 4 threads, everything on the one timed core, whole-process wall clock
             "seq-paper":   (["sequential", "4000", "1", "100", "100"], CORE),
             "rayon-paper": (["rayon", "4000", "4", "100", "100"], CORE)}
RUST_RESULT = {"seq": "result_sequential.txt", "rayon": "result_rayon.txt",
               "seq-paper": "result_sequential.txt", "rayon-paper": "result_rayon.txt", "rust-ssp": "result_rust-ssp.txt",
               "std-threads": "result_STDthreads.txt", "tokio": "result_tokio.txt", "pipeliner": "result_pipeliner.txt"}


def cell_rust(cfg, v, args):
    a, cores = RUST_ARGS[v]
    if OV["core"] and v in ("seq", "seq-paper", "rayon-paper"):
        cores = OV["core"]
    outf = os.path.join(OUT, RUST_RESULT[v])
    try:
        os.unlink(outf)
    except OSError:
        pass
    r = run(wrap(cfg, cores, [RUSTBIN] + a, "rust", v), cwd=OUT, timeout=args.timeout)
    m = re.findall(r"Execution time(?: [A-Za-z-]+)?: ([0-9.eE+-]+) sec", r["out"] + r["err"])
    r["ktime"] = float(m[-1]) if m else None
    if v.endswith("-paper") and r["ktime"] is not None:
        r["ktime"] = r["wall"]          # the paper's metric for these cells: whole-process wall clock
    # RustStreamBench's own gate: the result file is byte-compared with the vanilla
    # reference. The md5 travels in the row.
    try:
        with open(outf, "rb") as f:
            r["ck"] = hashlib.md5(f.read()).hexdigest()[:16]
    except OSError:
        r["ck"] = "no-result-file"
    return r


def cell_node(cfg, n, args):
    iters = int(os.environ.get("WTB_ITERS") or dict(NODE).get(n, 10))
    argv = [JITWP["manifest"]["node"] if cfg in E9FAST_CFGS else NODE_BIN]
    argv += os.environ.get("PTJ_NODE_V8FLAGS", "").split()   # fig5: same V8 flags every arm
    if cfg in E9FAST_CFGS and "--no-short-builtin-calls" not in argv:
        # V8's short builtin calls remap node's embedded builtins, and the remapped copy's `jmp rel32'
        # detours land ~2^35 bytes from their trampolines.  The JIT Fast sweep puts the flag on BOTH arms.
        argv += ["--no-short-builtin-calls"]
    env = None
    if JIT["on"] and cfg in HIFI_CFGS + FAST_CFGS + E9FAST_CFGS:
        # V8's own code-object notifications, analyzed and published to the Pintool (HiFi) or
        # patched natively into the code cache (fast, e9fast).
        argv += ["-r", os.path.join(TOOL, "runtime", "jit", "preload.js")]
        env = jit_env("node", n, cfg, getattr(args, "rep", 0))
    argv += [WTB_JS, n, str(NODE_WARMUP), str(iters)]
    r = run(wrap(cfg, OV["core"] or CORE, argv, "node", n), cwd=WTB, env=env,
            timeout=args.timeout)
    m = re.search(r'"ms":([0-9.]+)', r["out"])
    r["ktime"] = float(m.group(1)) / 1000.0 if m else None
    m = re.search(r'"analysis_calls":([0-9]+)', r["out"])
    r["acalls"] = int(m.group(1)) if m else None      # analyzer calls INSIDE the timed window
    m = re.search(r'"js_analysis_calls":([0-9]+)', r["out"])
    if m:                                             # asynchronous hook: only JS-thread analyses pause the timer
        r["acalls"] = int(m.group(1))
    if os.environ.get("PTJ_ITERLOG"):                 # the drain result and the hook's window counters
        for l in r["out"].splitlines():
            if l.startswith('{"wtb"'):
                with open(os.environ["PTJ_ITERLOG"], "a") as f:
                    f.write(json.dumps(dict(label=OV["label"], cfg=cfg, cell=n, rep=getattr(args, "rep", 0),
                                            ktime=r["ktime"], wtb=json.loads(l), load1=os.getloadavg()[0])) + "\n")
    m = re.search(r'"checksum":"([0-9a-f]+)"', r["out"])
    r["ck"] = m.group(1) if m else None
    return r


PTJ_DRAIN_JAR = os.path.join(TOOL, "runtime", "jit", "java", "ptjdrain", "ptjdrain.jar")


def java_drain_plugin(at):
    """The Java analogue of node's WTB_DRAIN_MS, used by the JIT Fast sweep only (opt-in: PTJ_DRAIN_MS > 0): an
    UNTIMED wait before the first timed iteration (op index `at' = the warm-up drop), after that op's forced GC and
    outside the measured interval, until the JVMTI agent's plan queue and analyzer lanes are empty, bounded by
    PTJ_DRAIN_MS.  The Renaissance plugin runs in EVERY arm of the sweep; the vanilla JVM has no agent, the native
    lookup fails and it returns at once, so both arms run identical iteration counts."""
    ms = int(os.environ.get("PTJ_DRAIN_MS", "0") or 0)
    if ms <= 0 or at is None or at < 0:
        return []
    return ["--plugin", PTJ_DRAIN_JAR + "!PtjDrain", "--with-arg", str(at), "--with-arg", str(ms)]


def cell_java(cfg, b, args):
    iters, drop = dict((x[0], (x[1], x[2])) for x in JAVA_B)[b]
    if cfg in HIFI_CFGS or cfg == "pinnoop" or cfg in BASELINE_CFGS or os.environ.get("PTJ_JAVA_ITERS_ALL") == "1":
        iters, drop = args.java_pin_iters, args.java_pin_drop   # the slow arms run fewer iterations
    argv = [JITWP["manifest"]["java"] if cfg in E9FAST_CFGS else JAVA]
    env = None
    if JIT["on"] and cfg in E9FAST_CFGS:
        # whole-program Fast: the rewritten JDK shadow tree + the agent's Fast objective with the whole-program
        # defaults (the agent turns them on for fast=1 with the buffer sink), buffer sink into the ptlog ring, per-thread
        # keyframe cells after the ELF images' own (kfgs), and the drain bound of the agent's own exit path.
        rep = getattr(args, "rep", 0)
        opts = ("mode=4,pin=0,ptw=1,interp=2,stubs=1,keyframe=%s,noroots=0,workers=%d,nospawn=1,"
                "cachever=%s,fast=1,relativeavoid=1,sock=%s,cache=%s,arenamb=128,drainms=120000,sink=%s,stats=%s%s"
                % (FAST_KEYFRAME, JIT["workers"], JIT["cachever"], JIT["sock"], JIT["cache"],
                   "buffer", jit_statsfile("java", b, cfg, rep), JIT["aopts"]))   # e9fast_ptw: JIT sites on the buffer
        if jit_kf_gs() > 0:
            opts += ",kfgs=%d" % jitwp_kf_n("java")
        argv += ["-agentpath:%s=%s" % (JITWP["manifest"]["ptjava"], opts)]
        env = jit_env("java", b, cfg, rep)
    elif JIT["on"] and cfg in HIFI_CFGS + FAST_CFGS:
        rep = getattr(args, "rep", 0)
        if cfg in FAST_CFGS:                # Fast: trampolines patched into the JIT code
            opts = ("mode=4,pin=0,fast=1,sink=%s,keyframe=%s,noroots=1,nospawn=1,"
                    "workers=%d,cachever=%s,sock=%s,cache=%s,arenamb=128,drainms=120000,stats=%s%s"
                    % ("buffer" if cfg == "fast" else "ptwrite", FAST_KEYFRAME,
                       JIT["workers"], JIT["cachever"], JIT["sock"], JIT["cache"],
                       jit_statsfile("java", b, cfg, rep), JIT["aopts"]))
        else:
            opts = ("mode=4,pin=1,keyframe=0,interp=1,stubs=1,workers=%d,nospawn=1,"
                    "cachever=%s,fast=0,sock=%s,cache=%s,arenamb=128,drainms=120000,stats=%s%s"
                    % (JIT["workers"], JIT["cachever"], JIT["sock"], JIT["cache"],
                       jit_statsfile("java", b, cfg, rep), JIT["aopts"]))
        argv += ["-agentpath:%s=%s" % (JIT["agent"], opts)]
        env = jit_env("java", b, cfg, rep)
    # PTJ_JAVA_XOPTS: extra JVM flags for BOTH arms (the JIT Fast sweep: -XX:UseAVX=2 and a GC log; "{cfg}" and
    # "{rep}" are expanded)
    argv += os.environ.get("PTJ_JAVA_XOPTS", "").replace("{cfg}", cfg).replace(
        "{rep}", str(getattr(args, "rep", 0))).split()
    argv += ["-jar", os.path.join(REN, "renaissance.jar"), "-r", str(iters)] + java_drain_plugin(drop) + [b]
    r = run(wrap(cfg, JAVA_CORES, argv, "java", b), cwd=REN, env=env, timeout=args.timeout)
    st = [float(m.group(2)) for m in ITER_RE.finditer(r["out"] + r["err"])]
    r["iters"] = st
    # steady state: the median of the iterations after the warm-up drop
    tail = st[drop:] if len(st) > drop else st
    r["ktime"] = statistics.median(tail) / 1000.0 if tail else None
    r["acalls"] = None
    if os.environ.get("PTJ_ITERLOG"):     # per-iteration times and the boundary drain's own report line
        pd = None
        for l in r["out"].splitlines():
            if l.startswith('{"ptjdrain"'):
                try:
                    pd = json.loads(l)["ptjdrain"]
                except (ValueError, KeyError):
                    pass
        with open(os.environ["PTJ_ITERLOG"], "a") as f:
            f.write(json.dumps(dict(label=OV["label"], cfg=cfg, cell=b, rep=getattr(args, "rep", 0), drop=drop,
                                    iters=st, drain=pd, load1=os.getloadavg()[0])) + "\n")
    if JIT["on"] and cfg in HIFI_CFGS + FAST_CFGS + E9FAST_CFGS:
        try:                                # JVMTI agent: analyzer calls over the whole process
            with open(jit_statsfile("java", b, cfg, getattr(args, "rep", 0))) as sf:
                t = sf.read(); r["acalls"] = json.loads(t[t.find("{"):]).get("analysis_calls")
        except (OSError, ValueError):
            pass
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
    """Memcached metric: WALL-CLOCK time of the fixed-op-count memslap load (server user CPU kept as srv_ucpu)."""
    port = args.mc_port + getattr(args, "rep", 0) % 7
    srvargs = ["-p", str(port), "-t", "4", "-m", "1024", "-U", "0", "-c", "1024"]
    if os.geteuid() == 0:
        srvargs += ["-u", "root"]           # memcached refuses to start as root otherwise
    cores = ",".join(str(c) for c in MC_SRV_CORES)
    # memcached is 4-threaded on 4 pinned cores, so a per-TASK Intel PT capture would trace
    # one worker out of four.  The PT configurations therefore use one PER-CPU capture per
    # server core: the capture on the first core runs the
    # server, the other three run a `cat FIFO' that is released at the end.
    tool = []
    if cfg in HIFI_CFGS:
        # OV["extra"] (-pin_memory_range ...) added: without it Pin 4.4 aborts the server with
        # "Non-fixed application mmap request overlapping with existing Pin mapping".
        tool = ([NO_CLONE3, OV["pin"], "-ifeellucky"] + OV["extra"] + ["-t", OV["hifi"],
                 "-plan", plan_for("mc", "memslap"), "-cvdir", "/dev/null"]
                + OV["hifi_extra"] + hifi_sink(cfg, "mc", "memslap") + ["--"])
    elif cfg == "pinnoop":
        # the SAME Pin kit flags as the pinhifi arm (OV["extra"] = -pin_memory_range ...), so the
        # floor differs from HiFi only by the Pintool.
        tool = [NO_CLONE3, OV["pin"], "-ifeellucky"] + OV["extra"] + ["-t", OV["noop"], "--"]
    elif cfg in BASELINE_CFGS:
        tool = baseline_prefix(cfg, os.path.join(OUT, "valgrind.mc.log"))
    mcbin = fast_image("mc", "memslap", cfg) if cfg in FAST_CFGS else MC_BIN
    inner = ["taskset", "-c", cores] + tool + [mcbin] + srvargs
    if cfg in FAST_CFGS and fastlib(cfg):
        inner = inner[:3] + ["/usr/bin/env", "LD_LIBRARY_PATH=" + fastlib(cfg)] + inner[3:]
    if cfg in PT_CFGS:
        # ONE capture with one per-task-per-CPU event per server core
        # (`--cpu 4 --cpu 5 --cpu 6 --cpu 7', one AUX ring each, aux_mb = max(128, 512 // 4) like the
        # multi-core rust cells).  The events are (pid = child, cpu = N), so every server thread is
        # traced on whichever of those cores it runs.
        pcpu = []
        for c in MC_SRV_CORES:
            pcpu += ["--cpu", str(c)]
        sb, pinner = [], inner
        if cfg in FAST_CFGS:
            sb = ["--sideband", os.path.join(OUT, "sb.mc.memslap.json"),
                  "--child-env", "LD_PRELOAD=" + MT_PRELOAD]
            if fastlib(cfg):
                sb += ["--child-env", "LD_LIBRARY_PATH=" + fastlib(cfg)]
            pinner = [mcbin] + srvargs          # pinned by pt_capture2 to the --cpu set
        elif os.environ.get("FIG5_MC_SB_ALL"):     # coverage check only (untimed)
            sb = ["--sideband", os.path.join(OUT, "sb.mc.memslap.json")]
        argv = (["taskset", "-c", HELPER_CORES, "setarch", "-R", PC,
                 "--aux-mb", os.environ.get("FIG5_MC_AUX_MB", str(max(128, 512 // len(MC_SRV_CORES)))),
                 ptw_flag(cfg), "--no-decode", "--aux-out", os.environ.get("FIG5_MC_AUX_OUT", "/dev/null")]
                + sb + MPG(cfg) + pcpu + ["--"] + pinner)
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
        if False:                            # no sidecar captures (see the argv above)
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
                               stdin=subprocess.DEVNULL, start_new_session=True)
        SESSIONS.add(srv.pid)
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
        cenv = dict(os.environ)          # memcaslap's libmemcached ships in suites/memcached/lib
        cenv["LD_LIBRARY_PATH"] = os.path.join(SUITES, "memcached", "lib") + (
            ":" + cenv["LD_LIBRARY_PATH"] if cenv.get("LD_LIBRARY_PATH") else "")
        cp = subprocess.run(["taskset", "-c", MC_CLI_CORES, MC_CLI,
                             "-s", "127.0.0.1:%d" % port, "-F", MC_CFG,
                             "-T", "4", "-c", "16", "-x", str(args.mc_ops)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, text=True, errors="replace",
                            timeout=args.timeout, env=cenv)
        res["wall"] = time.monotonic() - t0
        u1, _ = proc_times(pid)
        # the Memcached metric is WALL-CLOCK time, like every other suite: the
        # client-side elapsed time of the fixed-op-count memslap load run (t0 = after the server is ready and
        # its pid is found, i.e. server/tracer start-up EXCLUDED; t1 = memslap exit), identical for every arm.
        # Server user CPU is kept in the extra `srv_ucpu' column.
        res["ktime"] = round(res["wall"], 6)
        res["srv_ucpu"] = round(u1 - u0, 3)
        res["rc"] = cp.returncode
        res["out"] = cp.stdout[-2000:]
        m = re.search(r"Run time: ([0-9.]+)s Ops: (\d+) TPS: (\d+)", cp.stdout)
        # a load run that did not complete its fixed op count (memslap exits 0 when the server stops answering)
        # is not a timing: invalid (rc 97), never a quiet short number
        if res["rc"] == 0 and (not m or int(m.group(2)) < args.mc_ops):
            res["rc"] = 97
            res["err"] = "short-load-run: %s of %d ops" % (m.group(2) if m else "?", args.mc_ops)
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
            try:                              # let pt_capture2 finish draining/printing its summary first
                srv.wait(timeout=120)
            except Exception:                                    # noqa: BLE001
                pass
            try:
                srv.kill(); srv.wait(timeout=30)
            except Exception:                                    # noqa: BLE001
                pass
            kill_session(srv.pid, "Memcached run")
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
        try:
            res["pterr"] = open(errp, errors="replace").read()
        except OSError:
            pass
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
    ap.add_argument("--cap-label", default="",
                    help="only the vanilla rows with this label are cap references (the traditional step's own)")
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
    ap.add_argument("--jitwp-stage", default=None,
                    help="stage directory (manifest.json) of run/lib/jitwp/build_stage.sh for the `e9fast' "
                         "configuration (whole-program Fast of the JIT suites); needs --jit-dir/--jit-cache")
    ap.add_argument("--jit-agent-opts", default="",
                    help="extra comma-separated JVMTI agent options appended verbatim "
                         "(e.g. 'lanes=0').  Recorded in the row "
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
    CAP["label"] = a.cap_label
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
    if a.jitwp_stage:
        JITWP["stage"] = os.path.realpath(a.jitwp_stage)
        with open(os.path.join(JITWP["stage"], "manifest.json")) as mf:
            JITWP["manifest"] = json.load(mf)
        # e9fast_ptw's stage: <jitwp>/ptw/stage (run/lib/jitwp/build_ptw.sh), beside <jitwp>/stage
        JITWP["ptw_stage"] = os.path.join(os.path.dirname(JITWP["stage"]), "ptw", "stage")
        if os.path.exists(os.path.join(JITWP["ptw_stage"], "manifest.json")):
            with open(os.path.join(JITWP["ptw_stage"], "manifest.json")) as mf:
                JITWP["ptw"] = json.load(mf)
        elif E9FAST_PTW in cfgs:
            raise SystemExit("repslice: e9fast_ptw needs the mixed-sink stage %s (bash run/lib/jitwp/build_ptw.sh)"
                             % JITWP["ptw_stage"])
    if set(E9FAST_CFGS) & set(cfgs) and (not JITWP["manifest"] or not JIT["on"]):
        raise SystemExit("repslice: e9fast needs --jitwp-stage DIR and --jit-dir/--jit-cache")
    if "fast" in cfgs or "fast_ptw" in cfgs:  # the rewritten images discard their value stream
        os.environ["PTLOG_DIR"] = "/dev/null"
        os.environ.pop("PTLOG_GT", None)
    suites = set(a.suites.split(","))
    want = [(s, c) for s, c in CELLS if s in suites]
    if a.cells:
        keep = set(a.cells.split(","))
        want = [(s, c) for s, c in want if c in keep]
        if "node" in suites:     # the other Web Tooling cells are opt-in only (not in the default NODE set)
            want += [("node", c) for c in WTB_ALL if c in keep and ("node", c) not in want]

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
                    "mutex", "load0", "load1", "label", "ts", "tps", "gate", "cap",
                    "acalls", "pt_lost_bytes", "pt_trunc", "pt_aux_bytes", "pt_peak_fill", "topa", "iters", "inherited", "srv_ucpu"]); fh.flush()

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
            reap_strays()
            if (s, c, cf, OV["label"]) in skip:
                return
            if cf == "pinhifi_ptw" and not HIFI_PTW["all"] and hifi_ptw_list(s, c) is None:
                a.rep = 0
                hifi_ptw_profile(s, c, a)
            if r == 1 and s in ("node", "java") and cf in HIFI_CFGS + FAST_CFGS and os.environ.get("F5_WARMPROC", "1") == "1":
                # one untimed process first: it fills the JIT plan cache for this cell, so
                # the counted repetitions are not dominated by cold-cache analysis pauses
                a.rep = 0
                log("%-7s %-14s %-9s %-10s warm-up process (untimed)" % (s, c, OV["label"] or "-", cf))
                try:
                    RUNNER[s](cf, c, a)
                except subprocess.TimeoutExpired:
                    log("   warm-up process timed out")
            a.rep = r
            load_gate(a.load_gate, a.load_gate_wait)
            budget = cell_timeout(s, c, a.timeout)
            saved, a.timeout = a.timeout, budget
            cap = ""
            try:
                res = RUNNER[s](cf, c, a)
                if cf == "vanilla" and res.get("rc") == 0 and res.get("wall") and (s, c) not in CAP["wall"]:
                    CAP["wall"][(s, c)] = res["wall"]     # a cell with no reference row: budget from this run
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
                        res.get("gate") or "", cap,
                        "" if res.get("acalls") is None else res["acalls"]]
                       + pt_stats(res.get("pterr") or res.get("err") or "")
                       + [int("ToPA overflow signature" in ((res.get("err") or "") + (res.get("pterr") or ""))),
                          " ".join("%.1f" % x for x in res.get("iters") or []),
                          int("INHERITED %gs" in ((res.get("err") or "") + (res.get("pterr") or ""))),
                          "" if res.get("srv_ucpu") is None else res["srv_ucpu"]])
            if JIT["on"] and (cf in PT_CFGS or cf in E9FAST_CFGS):   # pt_capture2's own summary, for the overflow audit
                with open(os.path.join(JIT["statsdir"],
                                       "%s.%s.%s.r%s.pt.err" % (s, c, cf, r)), "w") as ef:
                    ef.write(res.get("pterr") or res.get("err") or "")
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
            if res.get("acalls") is not None:
                if s == "node":
                    log("   analyzer calls inside the timed window: %d%s" % (
                        res["acalls"], "  <-- PAUSED IN TIMER (not steady state)" if res["acalls"] else ""))
                else:
                    log("   analyzer calls over the process: %d" % res["acalls"])
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
    report(a.csv, cfgs, a.max_load, a.report_label, a.cap_from)


def report(path, cfgs, max_load=0.0, label_filter=None, ref=None):
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
    if "vanilla" not in cfgs:
        # a tracer-only run (run/fig5_traditional.sh): ratios against the uninstrumented rows of the reference
        # CSVs, never against the run's own first configuration
        base = "vanilla"
        for p in [path] + (ref or "").split(","):
            if p and os.path.exists(p):
                for r in csv.DictReader(open(p)):
                    if r["config"] == "vanilla" and r["rc"] == "0" and r["ktime"] != "":
                        by.setdefault((r["suite"], r["cell"], "vanilla"), []).append(
                            (float(r["ktime"]), float(r["wall"]) if r["wall"] else None))
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
