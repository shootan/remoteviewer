"""motion-settle r5: first reaction by GOP phase -- where the onset fell relative to the nearest key AU.

    python motion_settle_phase.py <run dir> [<run dir> ...]

Needs host.log with REMOTE60_NATIVE_AU_TRACE=1 ([au] + wire lines; enqueueUs / queueWaitUs / sendDurUs are
qpc microseconds, the clock of episodes.csv onsetUs). Per episode, the last key AU enqueued before the
onset paint + 1 s and the onset are compared:
  onset_frame   the key AU is the onset's own first picture path (enqueued after the paint, first AU after it)
  on_wire       the key was still being sent when the onset was painted
  queued        the key was enqueued but not yet on the wire at the paint
  just_after    the key finished sending 0-300 ms before the paint
  far           otherwise (no key within 300 ms before, none in the onset path)
First reaction (episodes.csv) p50 / p95 / max and count per phase and run; key source (real / settle /
gop_drain / static_refresh ...) per phase.
"""

import csv
import math
import os
import re
import sys

KV = re.compile(r"(\w+)=(\S+)")


def pct(v, p):
    v = sorted(v)
    if not v:
        return float("nan")
    i = p * (len(v) - 1)
    lo = int(i)
    hi = min(len(v) - 1, lo + 1)
    return v[lo] + (v[hi] - v[lo]) * (i - lo)


def load(d):
    keys, aus = [], []
    wire = {}
    for line in open(os.path.join(d, "host.log"), encoding="utf-8", errors="replace"):
        if "[au] seq=" in line:
            kv = dict(KV.findall(line.split("[au]", 1)[1]))
            if kv.get("seq", "").isdigit() and kv.get("enqueueUs", "0").isdigit():
                rec = (int(kv["enqueueUs"]), int(kv["seq"]), kv.get("src", "?"), int(kv.get("bytes", 0)), kv.get("key") == "1")
                aus.append(rec)
                if rec[4]:
                    keys.append(rec)
        elif "] wire seq=" in line:
            kv = dict(KV.findall(line.split("wire", 1)[1]))
            if kv.get("seq", "").isdigit():
                wire[int(kv["seq"])] = (int(kv.get("queueWaitUs", 0)), int(kv.get("sendDurUs", 0)))
    aus.sort()
    keys.sort()
    return aus, keys, wire


def classify(on_us, aus, keys, wire):
    first_after = next((a for a in aus if a[0] >= on_us), None)
    if first_after and first_after[4]:
        return "onset_frame", first_after
    before = [k for k in keys if k[0] <= on_us]
    if not before:
        return "far", None
    k = before[-1]
    qw, sd = wire.get(k[1], (0, 0))
    start, end = k[0] + qw, k[0] + qw + sd
    if on_us < start:
        return "queued", k
    if on_us < end:
        return "on_wire", k
    if on_us - end <= 300000:
        return "just_after", k
    return "far", k


def main(dirs):
    for d in dirs:
        aus, keys, wire = load(d)
        rows = [e for e in csv.DictReader(open(os.path.join(d, "episodes.csv"), encoding="utf-8")) if e["valid"] == "1"]
        by = {}
        for e in rows:
            ph, k = classify(int(e["onsetUs"]), aus, keys, wire)
            by.setdefault(ph, []).append((float(e["firstReactionMs"]), k[2] if k else "-", e["scene"]))
        print(f"== {os.path.basename(d)}  keys {len(keys)}  episodes {len(rows)}")
        for ph in ("onset_frame", "queued", "on_wire", "just_after", "far"):
            v = by.get(ph, [])
            if not v:
                continue
            r = [x[0] for x in v if x[0] >= 0]
            srcs = {}
            for x in v:
                srcs[x[1]] = srcs.get(x[1], 0) + 1
            print(f"  {ph:<12} n {len(v):3d}  react p50 {pct(r, .5):6.0f} p95 {pct(r, .95):6.0f} max {max(r) if r else float('nan'):6.0f}  key src {srcs}")


if __name__ == "__main__":
    main(sys.argv[1:])
