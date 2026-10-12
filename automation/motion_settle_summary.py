"""motion-settle r2: summarise --motion-settle runs per configuration, cell and scene (r2 D definitions).

    python motion_settle_summary.py <m0runs dir> <prefix> [<prefix> ...]

Runs are directories named <prefix>_<cell>_r<rep>. Per prefix / cell / scene:
  - test-valid episodes (fixture/source/event binding) -- the only thing that removes an episode;
  - against the SHARED long-still reference (episodes.csv fixedRef): reached, censored (did not reach
    reference - 1 dB held 500 ms by the end of the hold = a product failure, kept as +inf), and the
    time-to-reference median / p95 / max, printed as ">hold" when the quantile is censored;
  - T_final (final picture first decoded), glyph PSNR at 0.5 / 1 / 3 s (absolute, no reference choice),
    first reaction p95 and the first-second gap p95 / max of the onset, distinct pictures in it.
With several prefixes, a side-by-side line per cell and scene. Targets (Codex r1 s6) are not applied
here; the numbers are printed as measured.
"""

import csv
import glob
import math
import os
import re
import statistics
import sys


def quant(v, p, hold):
    if not v:
        return "n/a"
    v = sorted(v)
    i = max(0, math.ceil(p * len(v)) - 1)
    x = v[min(i, len(v) - 1)]
    return f">{hold}" if x == float("inf") else f"{x:.0f}"


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
        f = os.path.join(d, "episodes.csv")
        if not m or not os.path.exists(f):
            continue
        rows = list(csv.DictReader(open(f, encoding="utf-8")))
        runs.setdefault(m.group(1), []).extend(rows)
    return runs


def stats(rows, hold):
    valid = [r for r in rows if r["valid"] == "1"]
    out = {"n": len(rows), "valid": len(valid)}
    have_ref = [r for r in valid if float(r.get("fixedRef", "-1") or -1) > 0]
    t = [float(r["sharpFixedMs"]) if r.get("reached") == "1" else float("inf") for r in have_ref]
    out["reached"] = sum(1 for r in have_ref if r.get("reached") == "1")
    out["censored"] = len(have_ref) - out["reached"]
    out["sharp"] = (quant(t, 0.5, hold), quant(t, 0.95, hold), quant(t, 1.0, hold))
    tf = [float(r["finalFirstSeenMs"]) for r in valid if float(r["finalFirstSeenMs"]) >= 0]
    out["tfinal"] = (pct(tf, 0.5), pct(tf, 0.95))
    for k in ("ink500", "ink1000", "ink3000"):
        v = [float(r[k]) for r in valid if k in r and float(r[k]) >= 0]
        out[k] = pct(v, 0.5)
    react = [float(r["firstReactionMs"]) for r in valid if float(r["firstReactionMs"]) >= 0]
    gap = [float(r["maxGap1sMs"]) for r in valid if float(r["maxGap1sMs"]) >= 0]
    out["react95"] = pct(react, 0.95)
    out["gap95"] = pct(gap, 0.95)
    out["gapmax"] = max(gap) if gap else float("nan")
    out["f1"] = pct([float(r["first1s"]) for r in valid], 0.5)
    out["invalid"] = sorted({r["why"] for r in rows if r["valid"] != "1"})
    return out


def main(root, prefixes, hold=9000):
    table = {}
    for p in prefixes:
        for cell, rows in load(root, p).items():
            for sc in sorted({r["scene"] for r in rows}):
                table.setdefault((cell, sc), {})[p] = stats([r for r in rows if r["scene"] == sc], hold)
    for (cell, sc), per in sorted(table.items()):
        print(f"{cell} {sc}")
        for p in prefixes:
            if p not in per:
                continue
            s = per[p]
            print(f"   {p:<10} valid {s['valid']:>3}/{s['n']:<3} reached {s['reached']:>3} censored {s['censored']:>3} | "
                  f"toRef med {s['sharp'][0]:>6} p95 {s['sharp'][1]:>6} max {s['sharp'][2]:>6} | Tfinal med {s['tfinal'][0]:5.0f} "
                  f"p95 {s['tfinal'][1]:5.0f} | ink@0.5/1/3s {s['ink500']:5.1f}/{s['ink1000']:5.1f}/{s['ink3000']:5.1f} | "
                  f"react95 {s['react95']:5.0f} gap95 {s['gap95']:5.0f} gapmax {s['gapmax']:5.0f} f1s {s['f1']:4.0f}"
                  + (f" | TEST-INVALID: {'; '.join(s['invalid'])}" if s['invalid'] else ""))


if __name__ == "__main__":
    args = sys.argv[1:]
    hold = 9000
    if args and args[0].startswith("--hold="):
        hold = int(args[0].split("=", 1)[1])
        args = args[1:]
    main(args[0], args[1:], hold)
