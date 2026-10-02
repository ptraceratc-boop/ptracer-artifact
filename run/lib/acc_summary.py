#!/usr/bin/env python3
"""acc_summary.py CELLS.jsonl [--md OUT.md] [--csv OUT.csv] [--png OUT.png]: the Section 5.6 table from run/lib/acc_cell.sh.

Per cell, G = the main thread's ground-truth records (N = |G|), R = the reconstruction restricted to the instructions
that carry a ground-truth probe.  `ptrecon --gt-in' pairs G and R monotonically: a paired record is identical (cost 0)
or unknown / wrong (a substitution, cost 1); an unpaired ground-truth record (gt_only, before the anchor, after the
end) is a deletion, an unpaired reconstructed record (recon_only) an insertion.  The cost of any alignment is >= the
optimal one, so
    D_ub = unknown + wrong + gt_only + recon_only + gt_before_anchor + gt_tail (+ 2 x records re-paired after a loss)
is >= Levenshtein(G, R), and inaccuracy = D_ub / N is an upper bound on the paper's Levenshtein distance / |ground
truth|.  |len(G) - len(R)| / N is the lower bound.  An unknown address counts as an error.
"""
import argparse
import collections
import json
import math

PAPER = {"mean": 0.03, "p99": 0.08}       # Section 5.6, inaccuracy in % (Fast, PolyBench + pyperformance)
MODE = {"tnt": "Fast", "ptw": "Fast-PTWRITE"}
SUITE = {"poly": "PolyBench/C", "py": "pyperformance", "all": "all"}


def pct(xs, q):
    xs = sorted(xs)
    if not xs:
        return float("nan")
    k = (len(xs) - 1) * q
    f, c = math.floor(k), math.ceil(k)
    return xs[f] + (xs[c] - xs[f]) * (k - f)


def stats(r):
    g = lambda k: int(r.get(k) or 0)
    n = g("gt_records")
    if not n or r.get("records_compared") is None:
        return None
    paired = g("identical") + g("unknown") + g("wrong")
    d = (g("unknown") + g("wrong") + g("gt_only") + g("recon_only") + g("gt_before_anchor") + g("gt_tail")
         + 2 * g("gt_rewound_after_loss"))
    return dict(n=n, d=d, inacc=100.0 * d / n, lb=100.0 * abs(n - paired - g("recon_only")) / n,
                unk=100.0 * g("unknown") / n, wrong=g("wrong"), wrong_pct=100.0 * g("wrong") / n,
                unpaired=100.0 * (g("gt_only") + g("gt_before_anchor") + g("gt_tail") + g("recon_only")) / n,
                cover=100.0 * n / max(1, n + g("excluded")),
                in_ovf=100.0 * (g("unknown_in_overflow") + g("wrong_in_overflow")) / n,
                ovf=g("recon_pt_overflows"), capped=g("gt_bytes") >= 4294967296 - 64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows")
    ap.add_argument("--md")
    ap.add_argument("--csv")
    ap.add_argument("--png")
    a = ap.parse_args()
    rows = {}
    for line in open(a.rows):
        if line.strip():
            r = json.loads(line)
            rows[(r["mode"], r["suite"], r["cell"])] = r          # the last row of a cell wins
    agg, fails = collections.defaultdict(list), []
    csv = ["mode,suite,cell,gt_records,D_ub,inaccuracy_pct,lower_bound_pct,unknown_pct,wrong,unpaired_pct,"
           "oracle_coverage_pct,in_pt_overflow_pct,pt_overflows,gt_capped"]
    for (m, s, c), r in sorted(rows.items()):
        st = stats(r)
        if st is None:
            fails.append("%s %s %s: capture rc %s, reconstruction rc %s %s" % (
                MODE.get(m, m), s, c, r.get("capture_rc"), r.get("recon_rc"), r.get("error_tail") or ""))
            continue
        agg[(m, s)].append(st)
        csv.append("%s,%s,%s,%d,%d,%.6f,%.6f,%.6f,%d,%.6f,%.2f,%.6f,%d,%d" % (
            MODE.get(m, m), s, c, st["n"], st["d"], st["inacc"], st["lb"], st["unk"], st["wrong"], st["unpaired"],
            st["cover"], st["in_ovf"], st["ovf"], st["capped"]))
    out = ["Section 5.6: inaccuracy (%%) = upper bound on Levenshtein(ground truth, reconstruction) / |ground truth|, "
           "per benchmark; paper: mean %.2f %%, P99 %.2f %%." % (PAPER["mean"], PAPER["p99"]), "",
           "| configuration | suite | cells | mean % | median % | P99 % | max % | unknown % (mean) | wrong records "
           "(cells with any) | unpaired % (mean) | ground-truth coverage % (mean) | cells with PT overflow |",
           "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    series = {}
    for m in ("tnt", "ptw"):
        for s in ("poly", "py", "all"):
            xs = [x for (mm, ss), v in agg.items() if mm == m and (s == "all" or ss == s) for x in v]
            if not xs:
                continue
            i = [x["inacc"] for x in xs]
            if s == "all":
                series[m] = i
            out.append("| %s | %s | %d | %.4f | %.4f | %.4f | %.4f | %.4f | %d (%d) | %.4f | %.1f | %d |" % (
                MODE[m], "**all**" if s == "all" else SUITE[s], len(xs), sum(i) / len(i), pct(i, 0.5), pct(i, 0.99),
                max(i), sum(x["unk"] for x in xs) / len(xs), sum(x["wrong"] for x in xs),
                sum(1 for x in xs if x["wrong"]), sum(x["unpaired"] for x in xs) / len(xs),
                sum(x["cover"] for x in xs) / len(xs), sum(1 for x in xs if x["ovf"])))
    for m, i in series.items():
        out.append("")
        out.append("%s vs paper: mean %.4f %% (paper %.2f %%), P99 %.4f %% (paper %.2f %%)" % (
            MODE[m], sum(i) / len(i), PAPER["mean"], pct(i, 0.99), PAPER["p99"]))
    if fails:
        out += ["", "Cells without a comparison (%d):" % len(fails)] + ["- " + f for f in fails]
    txt = "\n".join(out)
    print(txt)
    if a.md:
        open(a.md, "w").write(txt + "\n")
    if a.csv:
        open(a.csv, "w").write("\n".join(csv) + "\n")
    if a.png and series:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(6.4, 3.6))
        color = {"tnt": "#2a78d6", "ptw": "#eb6834"}
        lo = min(v for i in series.values() for v in i if v > 0) if any(v > 0 for i in series.values() for v in i) else 1e-4
        for m, i in series.items():
            xs = sorted(max(v, lo / 2) for v in i)
            ys = [100.0 * (k + 1) / len(xs) for k in range(len(xs))]
            ax.step(xs, ys, where="post", color=color[m], lw=2, label="%s (%d benchmarks)" % (MODE[m], len(xs)))
        for k, ls in (("mean", "--"), ("p99", ":")):
            ax.axvline(PAPER[k], color="#5f5e5a", lw=1, ls=ls)
            ax.text(PAPER[k], 30, " paper %s %.2f %%" % ("mean" if k == "mean" else "P99", PAPER[k]),
                    rotation=90, va="bottom", ha="right", fontsize=8, color="#5f5e5a", bbox=dict(fc="white", ec="none", pad=1))
        ax.set_xscale("log")
        ax.set_xlabel("inaccuracy per benchmark (%, upper bound; log scale; 0 drawn at the left edge)")
        ax.set_ylabel("benchmarks (cumulative %)")
        ax.set_ylim(0, 102)
        ax.grid(True, which="major", color="#e5e4df", lw=0.6)
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)
        ax.legend(frameon=False, fontsize=8, loc="upper left")
        ax.set_title("Section 5.6: reconstruction inaccuracy (PolyBench/C + pyperformance)", fontsize=9)
        fig.tight_layout()
        fig.savefig(a.png, dpi=150)


if __name__ == "__main__":
    main()
