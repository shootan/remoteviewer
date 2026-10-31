"""Summarize a traffic_*.csv from sample_traffic.ps1 per app (GNLink vs OSLink).

A second is credited to the app whose host process moved more I/O bytes in that second, and only
when the NIC sent at least --min-mbps. Output: per app, seconds, mean/p50/p95/max send Mbps, and
contiguous segments, so the reader can see which stretch was which app.
"""
import argparse
import csv
import statistics
import sys


def pct(values, q):
    if not values:
        return 0.0
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(round(q * (len(s) - 1)))))
    return s[k]


def num(x):
    try:
        return int(x)
    except (TypeError, ValueError):
        return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--min-mbps", type=float, default=0.3)
    ap.add_argument("--target-mbps", type=float, default=6.0)
    ap.add_argument("--from", dest="t_from", default="")
    ap.add_argument("--to", dest="t_to", default="")
    a = ap.parse_args()

    rows = list(csv.DictReader(open(a.csv, encoding="ascii")))
    per = {"GNLink": [], "OSLink": []}
    segs = []
    for r in rows:
        ts = r["ts"]
        if a.t_from and ts < a.t_from:
            continue
        if a.t_to and ts > a.t_to:
            continue
        ms = max(1, num(r["ms"]))
        tx = num(r["nicTxB"]) * 8 / ms / 1000.0  # Mbps
        gn = num(r["gnWriteB"]) + num(r["gnOtherB"])
        os_ = num(r["osWriteB"]) + num(r["osOtherB"])
        if tx < a.min_mbps or max(gn, os_) == 0:
            app = None
        else:
            app = "GNLink" if gn >= os_ else "OSLink"
        if app:
            per[app].append(tx)
        if segs and segs[-1][0] == app:
            segs[-1][2] = ts
            segs[-1][3] += 1
        else:
            segs.append([app, ts, ts, 1])

    print(f"file={a.csv} rows={len(rows)} window={a.t_from or 'start'}..{a.t_to or 'end'}")
    print(f"{'app':7} {'sec':>5} {'mean':>6} {'p50':>6} {'p95':>6} {'max':>6} {'>tgt%':>6}  (send Mbps, target {a.target_mbps})")
    for app, v in per.items():
        if not v:
            print(f"{app:7} {0:>5}")
            continue
        over = 100.0 * sum(1 for x in v if x > a.target_mbps) / len(v)
        print(f"{app:7} {len(v):>5} {statistics.mean(v):6.2f} {pct(v, .5):6.2f} {pct(v, .95):6.2f} {max(v):6.2f} {over:6.1f}")
    print("segments (app, from, to, seconds) of 5s or more:")
    for app, t0, t1, n in segs:
        if n >= 5:
            print(f"  {app or 'idle':7} {t0}..{t1} {n}s")


if __name__ == "__main__":
    sys.exit(main())
