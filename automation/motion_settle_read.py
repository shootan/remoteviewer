"""motion-settle r4 (Codex split-metrics): readable-time and glyph-PSNR quantiles from raw deliveries.

    python motion_settle_read.py <m0runs dir> <read dir> <base prefix> <cand prefix> [<prefix> ...] [--hold=9000]

Runs are directories named <prefix>_<cell>_r<rep> (episodes.csv + deliveries.csv). <read dir> holds
read_<cell>.csv (scene,Q_read dB), the readable-quality anchors fixed BEFORE the comparison. Everything
is recomputed from deliveries.csv with ONE definition for every run (old harness builds included):

  final samples = decoded pictures of the episode's final id, quality = ink PSNR (ROI PSNR if no ink)
  T_read  = first final sample >= Q_read that every final sample keeps for 500 ms, the window ending
            before finalUs + hold; not reached = censored (+inf, kept)
  T_ref3  = the same against the shared reference - 3 dB (auxiliary, fixedRef from episodes.csv)
  ink@t   = the newest final sample decoded at or before finalUs + t (t = 0.5/1/1.5/2 s); none yet =
            missing (counted, not a number)
  T_final = finalFirstSeenMs, first reaction = firstReactionMs (episodes.csv)

Per prefix / cell / scene: T_read p50/p95/max and episodes > 2 s, the target (1.5M: p95 <= 1500 ms;
others: <= 1000 ms; every episode <= 2000 ms), ink@t p10/p50/p95, T_final and first-reaction p95 per rep.
Against the base prefix: delta ink@1 s median / p10, and per rep delta of first-reaction / T_final p95
(the responsiveness rule: <= 33 ms in every rep). Only test-valid episodes; no other filter.
"""

import csv
import glob
import math
import os
import re
import sys

POINTS = (500, 1000, 1500, 2000)


def pct(v, p):
    v = sorted(v)
    if not v:
        return float("nan")
    i = p * (len(v) - 1)
    lo = int(i)
    hi = min(len(v) - 1, lo + 1)
    a, b, f = v[lo], v[hi], i - lo
    if f == 0 or a == b:
        return a
    return float("inf") if math.isinf(b) else a + (b - a) * f  # a censored upper neighbour stays censored


def fmt(x, nd=0):
    if isinstance(x, float) and math.isinf(x):
        return ">hold"
    if isinstance(x, float) and math.isnan(x):
        return "n/a"
    return f"{x:.{nd}f}"


def hold_from(fin, level, final_us, end_us):
    for i, (at, q) in enumerate(fin):
        if q < level:
            continue
        ok = True
        for at2, q2 in fin[i + 1:]:
            if at2 > at + 500000:
                break
            if q2 < level:
                ok = False
                break
        if ok and at + 500000 <= end_us:
            return (at - final_us) / 1000.0
    return float("inf")


def episodes(run_dir, reads, hold_ms):
    eps = list(csv.DictReader(open(os.path.join(run_dir, "episodes.csv"), encoding="utf-8")))
    by = {}
    for d in csv.DictReader(open(os.path.join(run_dir, "deliveries.csv"), encoding="utf-8")):
        by.setdefault((d["scene"], d["index"], d["id"]), []).append(d)
    out = []
    for e in eps:
        if e["valid"] != "1":
            continue
        fu = int(e["finalUs"])
        rows = by.get((e["scene"], e["index"], e["finalId"]), [])
        fin = []
        for d in rows:
            roi, ink = float(d["roiPsnr"]), float(d["inkPsnr"])
            if roi < 0:
                continue
            fin.append((int(d["atUs"]), ink if ink >= 0 else roi))
        fin.sort()
        end_us = fu + hold_ms * 1000
        r = {"scene": e["scene"], "react": float(e["firstReactionMs"]), "tfinal": float(e["finalFirstSeenMs"])}
        q = reads.get(e["scene"])
        r["tread"] = hold_from(fin, q, fu, end_us) if q else float("nan")
        ref = float(e.get("fixedRef") or -1)
        r["tref3"] = hold_from(fin, ref - 3.0, fu, end_us) if ref > 0 else float("nan")
        for t in POINTS:
            seen = [qq for at, qq in fin if at <= fu + t * 1000]
            r[t] = seen[-1] if seen else None
        out.append(r)
    return out


def load(root, prefix, read_dir, hold_ms):
    runs = {}
    for d in sorted(glob.glob(os.path.join(root, f"{prefix}_*_r*"))):
        m = re.match(rf"{re.escape(prefix)}_(.+)_r(\d+)$", os.path.basename(d))
        if not m or not os.path.exists(os.path.join(d, "deliveries.csv")) or not os.path.exists(os.path.join(d, "summary.txt")):
            continue
        cell, rep = m.group(1), int(m.group(2))
        reads = {}
        rf = os.path.join(read_dir, f"read_{cell}.csv")
        if os.path.exists(rf):
            for line in open(rf, encoding="utf-8"):
                p = line.strip().split(",")
                if len(p) >= 2 and not p[0].startswith("#"):
                    reads[p[0]] = float(p[1])
        for r in episodes(d, reads, hold_ms):
            r["rep"] = rep
            runs.setdefault(cell, []).append(r)
    return runs


def summarize(rows, cell):
    s = {"n": len(rows)}
    tr = [r["tread"] for r in rows if not math.isnan(r["tread"])]
    s["tread"] = (pct(tr, 0.5), pct(tr, 0.95), max(tr) if tr else float("nan"))
    s["over2"] = sum(1 for x in tr if x > 2000)
    lim = 1500 if cell.startswith("15m") else 1000
    s["target"] = "n/a" if not tr else ("MET" if s["tread"][1] <= lim and s["over2"] == 0 else "no")
    t3 = [r["tref3"] for r in rows if not math.isnan(r["tref3"])]
    s["tref3"] = (pct(t3, 0.5), pct(t3, 0.95))
    for t in POINTS:
        v = [r[t] for r in rows if r[t] is not None]
        s[t] = (pct(v, 0.1), pct(v, 0.5), pct(v, 0.95), len(rows) - len(v))
    s["react"] = {}
    s["tfin"] = {}
    for rep in sorted({r["rep"] for r in rows}):
        rr = [r for r in rows if r["rep"] == rep]
        s["react"][rep] = pct([r["react"] for r in rr if r["react"] >= 0], 0.95)
        s["tfin"][rep] = pct([r["tfinal"] for r in rr if r["tfinal"] >= 0], 0.95)
    return s


def main(root, read_dir, prefixes, hold_ms):
    data = {p: load(root, p, read_dir, hold_ms) for p in prefixes}
    base = prefixes[0]
    cells = sorted({c for p in prefixes for c in data[p]})
    for cell in cells:
        scenes = sorted({r["scene"] for p in prefixes for r in data[p].get(cell, [])})
        print(f"== {cell}")
        for sc in scenes:
            sb = None
            for p in prefixes:
                rows = [r for r in data[p].get(cell, []) if r["scene"] == sc]
                if not rows:
                    continue
                s = summarize(rows, cell)
                if p == base:
                    sb = s
                tr = s["tread"]
                ink = " ".join(f"@{t / 1000:g}s {fmt(s[t][0], 1)}/{fmt(s[t][1], 1)}/{fmt(s[t][2], 1)}" + (f"(-{s[t][3]})" if s[t][3] else "")
                               for t in POINTS)
                line = (f"{sc:<10} {p:<12} n{s['n']:<3} Tread {fmt(tr[0])}/{fmt(tr[1])}/{fmt(tr[2])} >2s {s['over2']} {s['target']:<4} "
                        f"ref3 {fmt(s['tref3'][0])}/{fmt(s['tref3'][1])} | ink p10/p50/p95 {ink} | "
                        f"react95 {','.join(fmt(v) for v in s['react'].values())} Tfin95 {','.join(fmt(v) for v in s['tfin'].values())}")
                if sb is not None and p != base:
                    d1 = s[1000][1] - sb[1000][1]
                    d10 = s[1000][0] - sb[1000][0]
                    dr = [s["react"][k] - sb["react"][k] for k in s["react"] if k in sb["react"]]
                    df = [s["tfin"][k] - sb["tfin"][k] for k in s["tfin"] if k in sb["tfin"]]
                    ok = all(x <= 33 for x in dr + df)
                    line += (f" | d@1s p50 {d1:+.1f} p10 {d10:+.1f} dReact95 {','.join(f'{x:+.0f}' for x in dr)}"
                             f" dTfin95 {','.join(f'{x:+.0f}' for x in df)} {'resp-ok' if ok else 'RESP-REGRESS'}")
                print(line)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    hold = 9000
    for a in sys.argv[1:]:
        if a.startswith("--hold="):
            hold = int(a.split("=", 1)[1])
    main(args[0], args[1], args[2:], hold)
