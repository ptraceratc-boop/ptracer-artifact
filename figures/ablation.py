#!/usr/bin/env python3
"""ablation.py -- Figure 6 (ablation of the new techniques), from the measurements of
run/03_ablation.sh.

    python3 figures/ablation.py --data <ablation.json> --outdir DIR

The input holds raw per-repetition timings only; every median and ratio is computed here.

  * bar 1  PTracer (Intel PT + static analysis)                  -- reference
  * bar 2  PTracer w/o PT (control flow instrumented instead)   -- bar2->bar1 = PT's contribution
  * bar 3  PTracer w/o PT and w/o static (every access logged)  -- bar3->bar2 = static's contribution

A bar that was not measured for a suite is simply absent.
"""
import argparse, json, math, os, statistics

HERE = os.path.dirname(os.path.realpath(__file__))
ROOT = os.path.dirname(HERE)

# categorical palette (light mode) + inks.
PAL = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100",
       "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
INK, INK2, INK3 = "#1a1a1a", "#4a4a4a", "#8a8a8a"
THRESH = "#e34948"
# one colour per bar, in bar order.
BAR_COLOR = {"bar1": PAL[2], "bar2": PAL[3], "bar3": PAL[7]}


def median(xs):
    return statistics.median(xs)


def compute(data):
    """Turn raw reps into {suite: {bar_key: {ratio, median_s, n, ...}}} with baselines."""
    bars = data["bars"]
    out = {"bars": bars, "suites": []}
    for suite in data["suites"]:
        reps = suite["reps"]
        base_med = median(reps[suite["baseline"]])
        row = {"key": suite["key"], "name": suite["name"], "cell": suite["cell"],
               "metric": suite["metric"], "baseline_median_s": base_med, "values": {}}
        for bar in bars:
            k = bar["key"]
            if k in reps:
                m = median(reps[k])
                row["values"][k] = {"ratio": m / base_med, "median_s": m, "n": len(reps[k])}
            elif k == "bar3" and "bar3_unmeasured" in suite:
                row["values"][k] = {"ratio": None, "unmeasured": suite["bar3_unmeasured"]}
            else:
                row["values"][k] = {"ratio": None}
        if "bar3_same_channel" in suite:
            row["bar3_same_channel"] = suite["bar3_same_channel"]
        out["suites"].append(row)
    return out


def plot(data, model, outdir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    bars = model["bars"]
    suites = model["suites"]
    nb = len(bars)
    x = np.arange(len(suites))
    width = 0.8 / nb

    fig, ax = plt.subplots(figsize=(8.4, 5.0))
    ax.set_yscale("log")
    ax.set_ylim(0.9, 90)
    ax.set_yticks([1, 2, 3, 5, 10, 20, 50], labels=["1", "2", "3", "5", "10", "20", "50"])
    ax.axhline(1, color=INK3, lw=0.9)
    ax.grid(axis="y", alpha=0.22)
    ax.spines[["top", "right"]].set_visible(False)

    for bi, bar in enumerate(bars):
        k = bar["key"]
        offs = (bi - (nb - 1) / 2) * width
        for si, srow in enumerate(suites):
            v = srow["values"].get(k, {})
            xp = x[si] + offs
            r = v.get("ratio")
            if r is not None:
                ax.bar(xp, r - 1, bottom=1, width=width * 0.92,
                       color=BAR_COLOR[k], edgecolor=INK, linewidth=0.5,
                       label=bar["label"] if si == 0 else None, zorder=3)
                ax.annotate(f"{r:.2f}x", (xp, r), rotation=90, ha="center", va="bottom",
                            fontsize=8, xytext=(0, 2), textcoords="offset points", color=INK)

    ax.set_xticks(x, labels=[f"{s['name']}\n({s['cell']})" for s in suites], fontsize=9)
    ax.set_xlim(-0.6, len(suites) - 0.4)
    ax.set_ylabel("Runtime / uninstrumented  (log scale)", fontsize=10)
    ax.set_title("Ablation: isolating Intel PT and static analysis "
                 "(Fast mode, no PTWRITE)", fontsize=11, loc="left")
    ax.legend(fontsize=8.5, loc="upper left", framealpha=0.95)

    fig.text(0.012, -0.02,
             "bar2->bar1 = Intel PT's contribution; bar3->bar2 = the static analysis's contribution. "
             "Fast mode, software-buffer sink, median of the repetitions in the input.",
             fontsize=6.6, color=INK2, va="top")

    fig.tight_layout(rect=(0, 0.06, 1, 1))
    fig.savefig(os.path.join(outdir, "ablation.png"), dpi=180, bbox_inches="tight")
    fig.savefig(os.path.join(outdir, "ablation.svg"), bbox_inches="tight")
    plt.close(fig)

    # ---- per-bar PNGs on identical axes ------------
    # Paper's qualitative expectation per bar, drawn as a labelled reference line where it
    # is a single ratio; bar1 is the reference (no line).
    for bi, bar in enumerate(bars):
        k = bar["key"]
        fig, ax = plt.subplots(figsize=(6.2, 3.4))
        ax.set_yscale("log"); ax.set_ylim(0.9, 90)
        ax.set_yticks([1, 2, 3, 5, 10, 20, 50], labels=["1", "2", "3", "5", "10", "20", "50"])
        ax.axhline(1, color=INK3, lw=0.9); ax.grid(axis="y", alpha=0.22)
        ax.spines[["top", "right"]].set_visible(False)
        for si, srow in enumerate(suites):
            v = srow["values"].get(k, {}); r = v.get("ratio")
            if r is not None:
                ax.bar(si, r - 1, bottom=1, width=0.55, color=BAR_COLOR[k],
                       edgecolor=INK, linewidth=0.5, zorder=3)
                ax.annotate(f"{r:.2f}x", (si, r), ha="center", va="bottom", fontsize=9,
                            xytext=(0, 2), textcoords="offset points")
                # paper's "~2x the PTracer bar" expectation for bar 2, per suite.
                if k == "bar2":
                    b1 = srow["values"]["bar1"]["ratio"]
                    ax.plot([si - 0.3, si + 0.3], [2 * b1, 2 * b1], color=THRESH, lw=1.3, ls="--",
                            zorder=4, label="paper: ~2x PTracer" if si == 0 else None)
        ax.set_xticks(range(len(suites)), labels=[s["name"] for s in suites], fontsize=9)
        ax.set_xlim(-0.6, len(suites) - 0.4)
        ax.set_ylabel("Runtime / uninstrumented", fontsize=9)
        ax.set_title(f"{bar['label']} ({bar['sublabel']})", fontsize=10, loc="left")
        if k == "bar2":
            ax.legend(fontsize=8, loc="upper left")
        fig.tight_layout()
        fig.savefig(os.path.join(outdir, f"ablation_{k}.png"), dpi=180)
        plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--outdir", default=HERE)
    ap.add_argument("--data", required=True)
    ap.add_argument("--data-only", action="store_true")
    args = ap.parse_args()
    data = json.load(open(args.data))
    model = compute(data)
    os.makedirs(args.outdir, exist_ok=True)
    # write the computed model beside the figure (medians/ratios, no hand-typed numbers).
    with open(os.path.join(args.outdir, "ablation_computed.json"), "w") as f:
        json.dump(model, f, indent=2)
    for s in model["suites"]:
        cells = " ".join(f"{b['key']}={s['values'][b['key']].get('ratio')}" for b in model["bars"])
        print(f"{s['name']}: baseline={s['baseline_median_s']:.6f}s  {cells}")
    if not args.data_only:
        plot(data, model, args.outdir)
        print(f"Wrote ablation.png (+ .svg, per-bar PNGs) to {args.outdir}")


if __name__ == "__main__":
    main()
