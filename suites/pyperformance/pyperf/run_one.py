"""run_one.py <bm_name>: exec a pyperformance benchmark's run_benchmark.py as __main__ with the
fake pyperf intercept. PPF_LOOPS / PPF_WARM env control iterations. Prints KTIME to stderr."""
import sys, os, runpy
BMROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)), "benchmarks")
HERE=os.path.dirname(os.path.abspath(__file__))
bm=sys.argv[1]
# benchmark dirs are named bm_<name> (e.g. bm_nbody); accept either form.
bmdir=os.path.join(BMROOT, bm if os.path.isdir(os.path.join(BMROOT,bm)) else "bm_"+bm)
sys.path.insert(0, HERE)            # fakepyperf
import fakepyperf
sys.modules["pyperf"]=fakepyperf
sys.path.insert(0, bmdir)
os.chdir(bmdir)
runpy.run_path(os.path.join(bmdir,"run_benchmark.py"), run_name="__main__")
