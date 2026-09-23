#!/usr/bin/env python3
"""The Section 5.6 inaccuracy table, from the per-cell oracle results of run/02_accuracy.sh.

    python3 run/lib/inaccuracy_table.py <cells.json> > inaccuracy.md

Input: a JSON list of {suite, cell, compared, unknown, wrong}. Per suite, unknown % and
wrong % are computed over all compared records of the suite's cells.
"""
import json
import sys

PAPER = {'PolyBench/C': '< 0.05 % (Section 5.6: mean 0.03 %, P99 0.08 %)',
         'pyperformance': '< 0.05 % (Section 5.6: mean 0.03 %, P99 0.08 %)'}


def main():
    cells = json.load(open(sys.argv[1]))
    print('| suite | cells | records compared | unknown % | wrong % | inaccuracy % | paper |')
    print('|---|---:|---:|---:|---:|---:|---|')
    for suite in ('PolyBench/C', 'pyperformance'):
        cs = [c for c in cells if c['suite'] == suite]
        if not cs:
            print('| %s | 0 | - | - | - | - | %s |' % (suite, PAPER[suite]))
            continue
        n = sum(c['compared'] for c in cs) or 1
        u = 100.0 * sum(c['unknown'] for c in cs) / n
        w = 100.0 * sum(c['wrong'] for c in cs) / n
        print('| %s | %d (%s) | %d | %.4f | %.4f | %.4f | %s |' % (
            suite, len(cs), ', '.join(c['cell'] for c in cs), n, u, w, u + w, PAPER[suite]))
    print()
    print('Fast mode (E9Patch static rewriting, software-buffer sink, complete plan), compared '
          'record by record against the ground truth logged by the same execution '
          '(`rewrite.py --gt-all`, `ptrecon --gt-in`). unknown = record not reconstructed; '
          'wrong = reconstructed with another address.')


if __name__ == '__main__':
    main()
