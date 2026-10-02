#!/usr/bin/env python3
"""six_suite_overhead.py -- Figure 5 (runtime overhead), from the measurements of run/01_overhead.sh
(PTracer's four bars) and run/fig5_traditional.sh (the traditional tracers).

    python3 figures/six_suite_overhead.py --csv <overhead.csv> [--baselines <baselines.csv>] --outdir DIR

Two panels with the paper's layout: this run (top) and the paper's published bars (bottom, from
data/paper_overhead_bars.json).  Every local bar is computed from the raw rows: per cell the median of the
repetitions, the ratio to the vanilla run of the same cell, then the geometric mean over the suite's cells.
Spindle-plus is compared against its own clang -O2 build (config spindle_vanilla).
Bars: HiFi (pinhifi), HiFi-PTWRITE (pinhifi_ptw), Fast (fast; Node.js/Java: e9fast against the vanilla rows of its
own sweep), Fast-PTWRITE (fast_ptw; Node.js/Java: e9fast_ptw, likewise), memorytracer, libdft, Valgrind, Spindle-plus.
Traditional tracers: a cell stopped at 200x counts as 200x (the paper's rule) and the suite value is capped at
200x; a suite where at least half of the attempted cells failed is "Err".  "NA" = not applicable (Spindle-plus outside PolyBench/pyperformance);
"pending" = no rows yet (the bar slot is drawn from the start and filled in as rows arrive).

Metric: the benchmark's own steady-state time; Memcached: wall-clock time of a fixed-op-count load run;
Web Tooling: time of a fixed number of iterations (= executions per second).
"""
import argparse
import csv
import json
import math
import statistics
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / 'run' / 'lib'))
import row_versions  # noqa: E402
PAPER_BARS = ROOT / 'data' / 'paper_overhead_bars.json'
SUITES = ['poly', 'mc', 'pyperf', 'rust', 'node', 'java']
NAMES = {'poly': 'PolyBench(C)', 'mc': 'Memcached(C)', 'pyperf': 'pyperformance(Python)',
         'rust': 'Rust Stream(Rust)', 'node': 'Web Tooling(Node.js)', 'java': 'Renaissance(Java)'}
BARS = [('hifi', 'HiFi', 'pinhifi'), ('hifi_ptw', 'HiFi-PTWRITE', 'pinhifi_ptw'),
        ('fast', 'Fast', 'fast'), ('fast_ptw', 'Fast-PTWRITE', 'fast_ptw'),
        ('memtrace', 'memorytracer', 'memtrace'), ('libdft', 'libdft', 'libdft'),
        ('valgrind', 'Valgrind', 'valgrind'), ('spindle', 'Spindle-plus', 'spindle')]
TRAD = {'memtrace', 'libdft', 'valgrind', 'spindle'}
PTRACER = ('hifi', 'hifi_ptw', 'fast', 'fast_ptw')
SPINDLE_SUITES = {'poly', 'pyperf'}
CAP = 200.0
COLORS = {'native': '#3d8c40', 'hifi': '#ffe21a', 'hifi_ptw': '#ff9e1b', 'fast': '#7e3f9e',
          'fast_ptw': '#2e2ee6', 'memtrace': '#e8000b', 'libdft': '#b41a1a', 'valgrind': '#f2a0a0',
          'spindle': '#e775be'}
INK, INK3 = '#1a1a1a', '#8a8a8a'


def geomean(xs):
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else None


def read_rows(paths):
    """(suite, cell, config) -> list of (ktime or None, capped, failed)."""
    by, seen = {}, set()
    for p in paths:
        if not p or not Path(p).exists():
            continue
        # rows of a configuration measured by an older version of the artifact (run/lib/row_versions.py) are ignored
        stale = row_versions.stale_configs(str(Path(p).parent)) if Path(p).name.startswith('overhead') else set()
        with open(p) as f:
            for r in csv.DictReader(f):
                lab = r.get('label') or ''
                cfg = r['config']
                if cfg in stale:
                    continue
                key = (r['suite'], r['cell'], cfg, r.get('rep'), lab)
                if key in seen:                 # a row present both in a lane's CSV and in the merged one
                    continue
                seen.add(key)
                if lab == 'trad':
                    cfg = 'trad:' + cfg         # the traditional step's own reference runs (its workload)
                if lab.startswith('wpfast_r'):
                    if lab == 'wpfast_r0':
                        continue            # untimed warm-up process
                    cfg = 'wp:' + cfg
                ok = r['rc'] == '0' and bool(r['ktime'])
                if (r.get('cap') or '') == 'na':
                    by.setdefault((r['suite'], r['cell'], cfg + ':na'), [])
                    continue                    # not applicable
                capped = (r.get('cap') or '') == 'cap'     # stopped at 200x its reference: counted as 200x
                by.setdefault((r['suite'], r['cell'], cfg), []).append(
                    (float(r['ktime']) if ok else None, capped, not ok and not capped))
    return by


def suite_value(by, s, key, cfg):
    """(value, n_ok, n_cells, status, reps)  status: ok | Err | none; reps = most repetitions of a cell."""
    base = 'spindle_vanilla' if key == 'spindle' else 'vanilla'
    if key in ('fast', 'fast_ptw') and s in ('node', 'java'):
        wcfg = 'wp:e9fast' if key == 'fast' else 'wp:e9fast_ptw'
        if any(su == s and c == wcfg for su, _, c in by):
            cfg, base = wcfg, 'wp:vanilla'
    ratios, failed, cells, reps = [], 0, 0, 0
    for (su, cell, c), v in by.items():
        if su != s or c != cfg:
            continue
        cells += 1
        good = [k for k, _, _ in v if k is not None]
        reps = max(reps, len(good))
        van = [k for k, _, _ in by.get((su, cell, base), []) if k is not None]
        if key in TRAD and key != 'spindle':   # the traditional step's own reference runs (same workload)
            van = [k for k, _, _ in by.get((su, cell, 'trad:vanilla'), []) if k is not None]
        if good and van:
            ratios.append(statistics.median(good) / statistics.median(van))
        elif any(cp for _, cp, _ in v):
            ratios.append(CAP)
        else:
            failed += 1
    if cells == 0:
        if any(su == s and c == cfg + ':na' for su, _, c in by):
            return None, 0, 0, 'NA', 0
        return None, 0, 0, 'none', 0
    if key in TRAD:
        if failed * 2 >= cells or not ratios:
            return None, len(ratios), cells, 'Err', reps
        return min(CAP, geomean([min(CAP, r) for r in ratios])), len(ratios), cells, 'ok', reps
    if not ratios:
        return None, 0, cells, 'Err', reps
    return geomean(ratios), len(ratios), cells, 'ok', reps


def load(csv_path, paper, baselines=None, extra=()):
    by = read_rows([csv_path, baselines] + list(extra))
    p = json.loads(Path(paper).read_text())['series']
    rows = []
    for s in SUITES:
        row = dict(suite=s, name=NAMES[s], ours={}, paper={}, cells={}, reps={})
        for key, _, cfg in BARS:
            row['paper'][key] = p.get(key, {}).get(s)
            if key == 'spindle' and s not in SPINDLE_SUITES:
                row['ours'][key] = 'NA'
                continue
            v, n, tot, st, nr = suite_value(by, s, key, cfg)
            if st == 'none':
                v = 'pending'
            elif st in ('Err', 'NA'):
                v = st
            row['ours'][key] = v
            row['cells'][key] = '%d/%d' % (n, tot)
            row['reps'][key] = nr
        rows.append(row)
    return rows


def _panel(ax, rows, which, title):
    import numpy as np
    n = len(BARS) + 1
    w = 0.9 / n
    x = np.arange(len(rows))
    ax.set_yscale('log')
    ax.set_ylim(0.8, 400)
    ax.set_yticks([1, 2, 5, 10, 20, 50, 100, 200], labels=['1X', '2X', '5X', '10X', '20X', '50X', '100X', '200X'])
    ax.axhline(1, color=INK3, lw=0.8, ls=':')
    ax.grid(axis='y', alpha=0.2)
    ax.spines[['top', 'right']].set_visible(False)
    for xi, r in enumerate(rows):
        x0 = x[xi] - 0.45 + w / 2
        ax.bar(x0, 1 - 0.8, bottom=0.8, width=w * 0.92, color=COLORS['native'], edgecolor='black', lw=0.5,
               label='Uninstrumented' if xi == 0 else None, zorder=3)
        for k, (key, label, _) in enumerate(BARS):
            xp = x0 + (k + 1) * w
            v = r[which].get(key)
            if isinstance(v, (int, float)):
                ax.bar(xp, v - 0.8, bottom=0.8, width=w * 0.92, color=COLORS[key], edgecolor='black', lw=0.5,
                       label=label if xi == 0 else None, zorder=3)
                ax.annotate(('%.1f' % v) if v < 10 else ('%.0f' % v), (xp, v), rotation=90, ha='center',
                            va='bottom', fontsize=5.8, color=INK, xytext=(0, 1.5), textcoords='offset points')
            else:
                ax.bar(xp, 0, bottom=0.8, width=w * 0.92, color=COLORS[key], label=label if xi == 0 else None)
                ax.annotate(str(v if v is not None else 'n/a'), (xp, 0.85), rotation=90, ha='center', va='bottom',
                            fontsize=6, color='#c00000' if v == 'Err' else INK3)
    ax.set_xticks(x, labels=[r['name'] for r in rows], fontsize=8.5)
    ax.set_xlim(-0.55, len(rows) - 0.45)
    ax.set_ylabel('Runtime overhead (times, log)', fontsize=9)
    ax.set_title(title, loc='left', fontsize=10.5)


def plot(rows, outdir):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, (a1, a2) = plt.subplots(2, 1, figsize=(13, 8.5), sharex=True)
    _panel(a1, rows, 'ours', 'Figure 5, this run (geometric mean per suite; traditional tracers capped at 200X)')
    _panel(a2, rows, 'paper', 'Figure 5, as published in the paper')
    a1.legend(ncol=9, fontsize=7.5, loc='upper left', bbox_to_anchor=(0, 1.16), frameon=False)
    fig.tight_layout()
    for ext, kw in (('png', dict(dpi=170)), ('svg', {})):   # written next to the final name, then renamed
        tmp = outdir / ('.six_suite_overhead.tmp.' + ext)
        fig.savefig(tmp, bbox_inches='tight', **kw)
        os.replace(tmp, outdir / ('six_suite_overhead.' + ext))
    plt.close(fig)


def fmt(v):
    return ('%.2fx' % v) if isinstance(v, (int, float)) else str(v)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--csv', type=Path, required=True, help='overhead.csv written by run/01_overhead.sh')
    ap.add_argument('--baselines', type=Path, default=None,
                    help='baselines.csv written by run/fig5_traditional.sh (default: next to --csv)')
    ap.add_argument('--outdir', type=Path, default=HERE)
    ap.add_argument('--paper', type=Path, default=PAPER_BARS)
    ap.add_argument('--data-only', action='store_true')
    ap.add_argument('--progress', action='store_true',
                    help='during a run: also read the per-lane CSVs next to --csv (rows not merged yet), print nothing')
    ap.add_argument('--strict', action='store_true',
                    help='exit 2 when a PTracer HiFi/Fast bar of a suite has no valid measurement')
    a = ap.parse_args()
    bl = a.baselines or a.csv.parent / 'baselines.csv'
    extra = []
    if a.progress:
        d = a.csv.parent
        extra = sorted(set(d.glob('overhead*.csv')) | set(d.glob('jhifi/*.csv')) | set(d.glob('baselines*.csv'))
                       - {d / 'overhead.stale.csv'})
        sys.stdout = open(os.devnull, 'w')
    rows = load(a.csv, a.paper, bl, extra)
    a.outdir.mkdir(parents=True, exist_ok=True)
    (a.outdir / 'six_suite_overhead.json').write_text(json.dumps(dict(rows=rows), indent=2) + '\n')
    print('%-22s' % 'suite' + ''.join('%14s' % b[1] for b in BARS))
    for r in rows:
        print('%-22s' % r['name'] + ''.join('%14s' % fmt(r['ours'][b[0]]) for b in BARS))
        print('%-22s' % '  cells/reps' + ''.join('%14s' % (('%s r%d' % (r['cells'][b[0]], r['reps'][b[0]]))
                                                         if b[0] in r['cells'] else '') for b in BARS))
        print('%-22s' % '  paper' + ''.join('%14s' % fmt(r['paper'][b[0]]) for b in BARS))
    bad = [(r['name'], k) for r in rows for k in ('hifi', 'fast')
           if r['ours'][k] in ('Err',) or (r['ours'][k] == 'pending' and a.strict)]
    if bad and a.strict:
        print('ERROR: no valid measurement for ' + ', '.join('%s/%s' % b for b in bad))
        sys.exit(2)
    if not a.data_only:
        plot(rows, a.outdir)
        print('Wrote six_suite_overhead.png (+ .svg, .json) to %s' % a.outdir)

if __name__ == '__main__':
    main()
