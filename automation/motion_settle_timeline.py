"""motion-settle r1: absolute quality after the final paint, at fixed times, per run.

    python motion_settle_timeline.py <run dir> [<run dir> ...]

For every episode: the glyph-pixel PSNR of the newest decoded final picture at t = 0.25, 0.5, 1, 2, 3,
5, 8 s after the final paint (from deliveries.csv), and the slice QP of the newest AU the host emitted
by then (host.log [au] lines). Then the per-scene medians. Absolute figures, no reference picture
choice involved -- the companion of time-to-sharp, whose reference moves with the hold length.
"""

import csv
import os
import re
import statistics
import sys

TIMES = [0.25, 0.5, 1, 2, 3, 5, 8]


def main(dirs):
    for d in dirs:
        eps = list(csv.DictReader(open(os.path.join(d, "episodes.csv"), encoding="utf-8")))
        dl = list(csv.DictReader(open(os.path.join(d, "deliveries.csv"), encoding="utf-8")))
        aus = []
        for line in open(os.path.join(d, "host.log"), encoding="utf-8", errors="replace"):
            if "[au] seq" not in line:
                continue
            kv = dict(re.findall(r"(\w+)=(\S+)", line))
            aus.append((int(kv["enqueueUs"]), int(kv.get("sliceQp", "-1"))))
        per_scene = {}
        for e in eps:
            f = int(e["finalUs"])
            pts = sorted((int(x["atUs"]) - f, float(x["inkPsnr"])) for x in dl
                         if x["scene"] == e["scene"] and x["index"] == e["index"] and x["id"] == e["finalId"])
            row = []
            for t in TIMES:
                ps = [p for (tt, p) in pts if tt <= t * 1e6]
                qs = [q for (tt, q) in aus if f <= tt <= f + t * 1e6]
                row.append((ps[-1] if ps else float("nan"), qs[-1] if qs else -1))
            per_scene.setdefault(e["scene"], []).append(row)
        name = os.path.basename(d.rstrip("/\\"))
        print(f"{name}: median ink PSNR dB (slice QP) after the final paint")
        print("  scene      " + " ".join(f"{t:>11}s" for t in TIMES))
        for sc, rows in sorted(per_scene.items()):
            cells = []
            for i in range(len(TIMES)):
                ink = [r[i][0] for r in rows if r[i][0] == r[i][0]]
                qp = [r[i][1] for r in rows if r[i][1] >= 0]
                cells.append(f"{statistics.median(ink):5.1f}({statistics.median(qp):4.1f})" if ink and qp else
                             (f"{statistics.median(ink):5.1f}(  - )" if ink else "    -       "))
            print(f"  {sc:<10} " + " ".join(cells))


if __name__ == "__main__":
    main(sys.argv[1:])
