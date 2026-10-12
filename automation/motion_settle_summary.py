"""motion-settle r1: summarise a --motion-settle matrix (episodes.csv of every run) per cell and scene.

    python motion_settle_summary.py <m0runs dir> <prefix> [<prefix> ...]

Runs are directories named <prefix>_<cell>_r<rep>. For each prefix, cell and scene: valid episodes,
time-to-sharp median / p95 / max, first reaction p95, the longest first-second gap p95 / max, distinct
pictures in the first second against the same cell's sustained long-scroll rate, and the per-rep
medians (so a drift between repetitions shows). With two prefixes, a paired line per cell and scene.
Targets (Codex r1 s6) are printed beside the numbers, never used to change them.
"""

import csv
import glob
import os
import re
import statistics
import sys


def pct(v, p):
    if not v:
        return float("nan")
    v = sorted(v)
    i = p * (len(v) - 1)
    lo = int(i)
    hi = min(len(v) - 1, lo + 1)
    return v[lo] + (v[hi] - v[lo]) * (i - lo)


def load(root, prefix):
    runs = {}
    for d in sorted(glob.glob(os.path.join(root, f"{prefix}_*_r*"))):
        m = re.match(rf"{re.escape(prefix)}_(.+)_r(\d+)$", os.path.basename(d))
        if not m or not os.path.exists(os.path.join(d, "episodes.csv")):
            continue
        cell, rep = m.group(1), int(m.group(2))
        rows = list(csv.DictReader(open(os.path.join(d, "episodes.csv"), encoding="utf-8")))
        runs.setdefault(cell, {})[rep] = rows
    return runs


def cell_stats(reps):
    out = {}
    sustained = [float(r["sustainedFps"]) for rows in reps.values() for r in rows
                 if r["scene"] == "scrolllong" and float(r["sustainedFps"]) > 0]
    sus = statistics.median(sustained) if sustained else float("nan")
    scenes = sorted({r["scene"] for rows in reps.values() for r in rows})
    for sc in scenes:
        rows = [r for rows in reps.values() for r in rows if r["scene"] == sc]
        valid = [r for r in rows if r["valid"] == "1"]
        sharp = [float(r["timeToSharpMs"]) for r in valid]
        react = [float(r["firstReactionMs"]) for r in rows if float(r["firstReactionMs"]) >= 0]
        gap = [float(r["maxGap1sMs"]) for r in rows if float(r["maxGap1sMs"]) >= 0]
        f1 = [float(r["first1s"]) for r in rows]
        per_rep = []
        for rep, rr in sorted(reps.items()):
            s = [float(r["timeToSharpMs"]) for r in rr if r["scene"] == sc and r["valid"] == "1"]
            per_rep.append(f"r{rep}:{statistics.median(s):.0f}" if s else f"r{rep}:-")
        out[sc] = {
            "n": len(rows), "valid": len(valid),
            "sharp_med": pct(sharp, 0.5), "sharp_p95": pct(sharp, 0.95), "sharp_max": max(sharp) if sharp else float("nan"),
            "react_p95": pct(react, 0.95), "gap_p95": pct(gap, 0.95), "gap_max": max(gap) if gap else float("nan"),
            "f1_med": pct(f1, 0.5), "sustained": sus,
            "onset_ratio": (pct(f1, 0.5) / sus) if sus and sus == sus and sus > 0 else float("nan"),
            "ref_med": pct([float(r["refPsnr"]) for r in valid], 0.5),
            "per_rep": " ".join(per_rep),
            "invalid": sorted({r["why"] for r in rows if r["valid"] != "1"}),
        }
    return out


def main(root, prefixes):
    stats = {}
    for p in prefixes:
        stats[p] = {cell: cell_stats(reps) for cell, reps in load(root, p).items()}
        for cell, sc in sorted(stats[p].items()):
            for scene, s in sc.items():
                print(f"{p:<6} {cell:<6} {scene:<10} valid {s['valid']:>2}/{s['n']:<2} sharp med {s['sharp_med']:6.0f} p95 {s['sharp_p95']:6.0f} "
                      f"max {s['sharp_max']:6.0f} | react p95 {s['react_p95']:5.0f} gap1s p95 {s['gap_p95']:5.0f} max {s['gap_max']:5.0f} "
                      f"| first1s med {s['f1_med']:4.0f} / sustained {s['sustained']:4.1f} = {s['onset_ratio']:.2f} | ref {s['ref_med']:.1f} "
                      f"| per-rep sharp med {s['per_rep']}" + (f" | INVALID: {'; '.join(s['invalid'])}" if s['invalid'] else ""))
    if len(prefixes) == 2:
        a, b = prefixes
        print(f"\npaired {a} -> {b} (time-to-sharp median / p95 / max ms; react p95; gap1s p95)")
        for cell in sorted(set(stats[a]) & set(stats[b])):
            for scene in sorted(set(stats[a][cell]) & set(stats[b][cell])):
                x, y = stats[a][cell][scene], stats[b][cell][scene]
                cut = (1 - y["sharp_med"] / x["sharp_med"]) * 100 if x["sharp_med"] > 0 else float("nan")
                print(f"  {cell:<6} {scene:<10} sharp {x['sharp_med']:5.0f}/{x['sharp_p95']:5.0f}/{x['sharp_max']:5.0f} -> "
                      f"{y['sharp_med']:5.0f}/{y['sharp_p95']:5.0f}/{y['sharp_max']:5.0f} (median -{cut:.0f}%) | react {x['react_p95']:.0f}->{y['react_p95']:.0f}"
                      f" | gap {x['gap_p95']:.0f}->{y['gap_p95']:.0f} | onset ratio {x['onset_ratio']:.2f}->{y['onset_ratio']:.2f}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
