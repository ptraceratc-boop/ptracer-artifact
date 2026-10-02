"""run_one_full.py <name> [extra args...]: superset of the artifact's run_one.py.

Runs any pyperformance 1.14.0 MANIFEST name in-process with the fake pyperf intercept
(fakepyperf_full.py).  <local:X> variants run bm_X/run_benchmark.py with the variant's
extra_opts (from bm_X/bm_<name>.toml, [tool.pyperformance] extra_opts) as argv, the way
pyperformance's worker does; <local> names use extra_opts from their own pyproject.toml.
Names not in the MANIFEST but present as a bm_<name> directory (barnes_hut, decimal_factorial,
decimal_pi, yaml, hg_startup) run with their pyproject.toml's extra_opts.
`--list` prints: name<TAB>dir<TAB>extra_opts (json) for every MANIFEST name.

Env: PPF_LOOPS / PPF_WARM (as before), PPF_FIRST_ONLY=1 (old single-bench behaviour),
PPF_BMROOT (default: /artifact/suites/pyperformance/pyperf/benchmarks).
"""
import sys, os, runpy, json, tomllib
import site as _site
# pyperformance's venv has setuptools (-> distutils-precedence.pth, the distutils shim Django 3.2 needs on 3.12).
# PYTHONPATH dirs are not site dirs, so process their .pth files the way a venv's site-packages would be.
for _d in os.environ.get("PYTHONPATH", "").split(os.pathsep):
    if _d and os.path.isdir(_d) and any(f.endswith(".pth") for f in os.listdir(_d)):
        _site.addsitedir(_d)

HERE = os.path.dirname(os.path.abspath(__file__))
BMROOT = os.environ.get("PPF_BMROOT", os.path.join(os.path.dirname(os.path.abspath(__file__)), "benchmarks"))


def manifest():
    out, sect = [], None
    with open(os.path.join(BMROOT, "MANIFEST")) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("["):
                sect = line
                continue
            if sect == "[benchmarks]":
                parts = line.split("\t")
                if parts[0] != "name":
                    out.append((parts[0], parts[1].strip()))
    return out


def _extra_opts(path):
    try:
        with open(path, "rb") as f:
            d = tomllib.load(f)
    except FileNotFoundError:
        return []
    return list(d.get("tool", {}).get("pyperformance", {}).get("extra_opts", []))


def resolve(name):
    name = name[3:] if name.startswith("bm_") else name
    meta = dict(manifest()).get(name)
    if meta is None or meta == "<local>":
        base = name
        toml = os.path.join(BMROOT, "bm_" + base, "pyproject.toml")
    elif meta.startswith("<local:"):
        base = meta[len("<local:"):-1]
        toml = os.path.join(BMROOT, "bm_" + base, "bm_%s.toml" % name)
        if not os.path.exists(toml):
            raise SystemExit("variant toml missing: " + toml)
    else:
        raise SystemExit("unsupported metafile %r for %s" % (meta, name))
    bmdir = os.path.join(BMROOT, "bm_" + base)
    if not os.path.isdir(bmdir):
        raise SystemExit("no benchmark directory for %s" % name)
    return name, bmdir, _extra_opts(toml)


def _audit_spawns():
    """PPF_AUDIT=1: report every process-creation audit event to stderr (inventory runs only)."""
    ev = {"os.fork", "os.forkpty", "os.exec", "os.posix_spawn", "os.spawn", "os.system",
          "subprocess.Popen", "pty.spawn"}

    def hook(e, a):
        if e in ev:
            try:
                sys.stderr.write("AUDIT_SPAWN pid=%d %s %r\n" % (os.getpid(), e, a[:2] if isinstance(a, tuple) else a))
            except Exception:
                pass
    sys.addaudithook(hook)


def _dump_maps_at_exit(path):
    """PPF_MAPS=<file>: at interpreter exit, write every file-backed mapping of this process."""
    import atexit
    pid = os.getpid()

    def dump():
        if os.getpid() != pid:
            return
        seen = []
        with open("/proc/self/maps") as f:
            for line in f:
                parts = line.split(None, 5)
                if len(parts) == 6 and parts[5].startswith("/"):
                    q = parts[5].strip()
                    if q not in seen:
                        seen.append(q)
        with open(path, "w") as f:
            f.write("\n".join(seen) + "\n")
    atexit.register(dump)


def main():
    if os.environ.get("PPF_AUDIT") == "1":
        _audit_spawns()
    if os.environ.get("PPF_MAPS"):
        _dump_maps_at_exit(os.environ["PPF_MAPS"])
    if sys.argv[1:2] == ["--list"]:
        for n, _ in manifest():
            n, d, e = resolve(n)
            print("%s\t%s\t%s" % (n, os.path.basename(d), json.dumps(e)))
        return
    name, bmdir, extra = resolve(sys.argv[1])
    script = os.path.join(bmdir, "run_benchmark.py")
    sys.path.insert(0, HERE)            # fakepyperf_full
    import fakepyperf_full
    sys.modules["pyperf"] = fakepyperf_full
    sys.path.insert(0, bmdir)
    os.chdir(bmdir)
    sys.argv = [script] + extra + sys.argv[2:]
    import builtins  # a real __main__ sees __builtins__ as the MODULE (bm_sphinx assigns __builtins__.open)
    runpy.run_path(script, init_globals={"__builtins__": builtins}, run_name="__main__")


if __name__ == "__main__":
    main()
