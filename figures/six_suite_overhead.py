#!/usr/bin/env python3
"""six_suite_overhead.py -- Figure 5 (runtime overhead), from the measurements of
run/01_overhead.sh.

    python3 figures/six_suite_overhead.py --csv <overhead.csv> --outdir DIR

PTracer's HiFi (Intel Pin 4.4 JIT + the HiFi Pintool, software-buffer sink) and Fast (E9Patch
static rewriting, software-buffer sink) slowdown on each suite, beside the paper's own HiFi and
Fast bars (data/paper_overhead_bars.json: the heights of the published figure). Every local bar
is computed here from the raw rows of the CSV: per cell the median of the repetitions, the
ratio to the vanilla run, then the geometric mean over the suite's cells. A configuration that
was not measured for a suite has no bar.

Metric: the benchmark's own steady-state time, except Memcached (server user CPU).
"""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PAPER_BARS = ROOT / 'data' / 'paper_overhead_bars.json'

SUITES = ['poly', 'mc', 'pyperf', 'rust', 'node', 'java']
NAMES = {'poly': 'PolyBench/C', 'mc': 'Memcached', 'pyperf': 'pyperformance',
         'rust': 'Rust Stream', 'node': 'Node.js', 'java': 'Java'}
METRIC = {'mc': 'server user CPU'}
CONFIGS = {'hifi_ours': 'pinhifi', 'fast_ours': 'fast'}
BASE = 'vanilla'

COLORS = {'hifi_ours': '#d8720c', 'hifi_paper': '#eda100',
          'fast_ours': '#2a5db0', 'fast_paper': '#7aa8dd'}
INK, INK2, INK3 = '#1a1a1a', '#4a4a4a', '#8a8a8a'


def geomean(xs):
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else None


def load(csv_path, paper):
    by = {}
    with open(csv_path) as f:
        for r in csv.DictReader(f):
            if r['rc'] != '0' or not r['ktime']:
                continue
            by.setdefault((r['suite'], r['cell'], r['config']), []).append(float(r['ktime']))
    p = json.loads(Path(paper).read_text())['series']
    rows, cells = [], []
    for s in SUITES:
        row = dict(suite=s, name=NAMES[s], metric=METRIC.get(s, 'steady-state time'),
                   hifi_paper=p['hifi'].get(s), fast_paper=p['fast'].get(s))
        for key, cfg in CONFIGS.items():
            ratios = []
            for (su, cell, c), v in by.items():
                if su != s or c != cfg or (su, cell, BASE) not in by:
                    continue
                r = statistics.median(v) / statistics.median(by[(su, cell, BASE)])
                ratios.append(r)
                cells.append(dict(suite=s, cell=cell, config=cfg, n=len(v), ratio=r))
            row[key] = geomean(ratios)
            row[key + '_cells'] = len(ratios)
        rows.append(row)
    return rows, cells


def plot(rows, outdir):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    keys = [('hifi_ours', 'PTracer HiFi'), ('hifi_paper', 'paper HiFi'),
            ('fast_ours', 'PTracer Fast'), ('fast_paper', 'paper Fast')]
    x = np.arange(len(rows))
    w = 0.8 / len(keys)
    fig, ax = plt.subplots(figsize=(11, 5))
    ax.set_yscale('log')
    ax.set_ylim(0.9, 22)
    ax.set_yticks([1, 2, 3, 5, 10, 20], labels=['1', '2', '3', '5', '10', '20'])
    ax.axhline(1, color=INK3, lw=0.9)
    ax.grid(axis='y', alpha=0.22)
    ax.spines[['top', 'right']].set_visible(False)
    for i, (k, label) in enumerate(keys):
        off = (i - (len(keys) - 1) / 2) * w
        hatched = k.endswith('paper')
        for xi, r in enumerate(rows):
            v = r.get(k)
            if v is None:
                continue
            xp = x[xi] + off
            ax.bar(xp, v - 1, bottom=1, width=w * 0.92,
                   color='none' if hatched else COLORS[k],
                   edgecolor=COLORS[k], linewidth=1.1, hatch='///' if hatched else None,
                   label=label if xi == 0 else None, zorder=3)
            ax.annotate(f'{v:.2f}', (xp, v), rotation=90, ha='center', va='bottom',
                        fontsize=6.5, color=INK, xytext=(0, 2), textcoords='offset points')
    ax.set_xticks(x, labels=[f"{r['name']}\n({r['metric']})" for r in rows], fontsize=8)
    ax.set_xlim(-0.6, len(rows) - 0.4)
    ax.set_ylabel('Runtime / uninstrumented  (log scale)', fontsize=10)
    ax.set_title('Runtime overhead: PTracer vs. the paper (filled = this run, hatched = paper)',
                 loc='left', fontsize=12)
    ax.legend(ncol=4, fontsize=8.5, loc='upper left')
    fig.text(0.012, -0.01,
             'HiFi = Intel Pin 4.4 JIT with the HiFi Pintool; Fast = E9Patch static rewriting; '
             'both with the software-buffer sink under a complete Intel PT capture. '
             'Geometric mean over the suite\'s cells of the median of the repetitions.',
             fontsize=6.6, color=INK2, va='top')
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    fig.savefig(outdir / 'six_suite_overhead.png', dpi=180, bbox_inches='tight')
    fig.savefig(outdir / 'six_suite_overhead.svg', bbox_inches='tight')
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--csv', type=Path, required=True, help='the CSV written by run/lib/repslice.py')
    ap.add_argument('--outdir', type=Path, default=HERE)
    ap.add_argument('--paper', type=Path, default=PAPER_BARS)
    ap.add_argument('--data-only', action='store_true')
    a = ap.parse_args()
    rows, cells = load(a.csv, a.paper)
    a.outdir.mkdir(parents=True, exist_ok=True)
    (a.outdir / 'six_suite_overhead.json').write_text(
        json.dumps(dict(rows=rows, cells=cells), indent=2) + '\n')
    for r in rows:
        print('%-14s HiFi %s (%d cells)   Fast %s (%d cells)' % (
            r['name'], 'n/a' if r['hifi_ours'] is None else '%.2fx' % r['hifi_ours'],
            r['hifi_ours_cells'],
            'n/a' if r['fast_ours'] is None else '%.2fx' % r['fast_ours'], r['fast_ours_cells']))
    if not a.data_only:
        plot(rows, a.outdir)
        print('Wrote six_suite_overhead.png (+ .svg, .json) to %s' % a.outdir)


if __name__ == '__main__':
    main()
