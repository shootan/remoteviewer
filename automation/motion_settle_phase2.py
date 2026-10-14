"""motion-settle r7: first reaction stratified by the encoder's GOP phase at the onset, bound by marker/seq.

    python motion_settle_phase2.py <run dir> [<run dir> ...] [--batch=N]

Needs host.log with REMOTE60_NATIVE_AU_TRACE=1 ([au] lines with seq / key / ordinal / bytes / src /
enqueueUs, wire lines with queueWaitUs / sendDurUs) and deliveries.csv / paints.csv / episodes.csv.

Binding (not timing): the episode's first reaction is the first decoded picture with a marker id >= the
onset id (deliveries.csv, the harness's own definition); its host seq is joined to its [au] line.
Phase at the onset, from that AU's accepted-input ordinal and the key AUs before it (the GOP period =
the most common distance between consecutive non-forced keys in the run):
  onset_idr     the onset picture's own AU is a key
  idr_on_wire   a key AU enqueued before the onset paint was still queued / sending at the paint
  near          the next GOP boundary was within --batch + 3 inputs of the onset AU's ordinal
                (a refinement batch of N plus the safety margin and the surfacing input)
  far           otherwise
Per run and phase: episode count (the natural exposure of that phase), first reaction p50 / max,
T_final p50, the onset AU's bytes p50. Episodes whose first picture or AU is missing are counted as
unbound, never dropped silently.
"""

import collections
import csv
import os
import re
import statistics
import sys

KV = re.compile(r"(\w+)=(\S+)")


def load_host(d):
    aus, wire = {}, {}
    for line in open(os.path.join(d, "host.log"), encoding="utf-8", errors="replace"):
        if "[au] seq=" in line:
            kv = dict(KV.findall(line.split("[au]", 1)[1]))
            if kv.get("seq", "").isdigit():
                aus[int(kv["seq"])] = kv
        elif "] wire seq=" in line:
            kv = dict(KV.findall(line.split("wire", 1)[1]))
            if kv.get("seq", "").isdigit():
                wire[int(kv["seq"])] = kv
    return aus, wire


def gop_period(aus):
    keys = sorted(int(a["ordinal"]) for a in aus.values()
                  if a.get("key") == "1" and a.get("forced") == "0" and a.get("ordinal", "0").isdigit())
    diffs = collections.Counter(b - a for a, b in zip(keys, keys[1:]) if b > a)
    return diffs.most_common(1)[0][0] if diffs else 0


def classify(e, aus, wire, keys_sorted, period, batch, first):
    at, seq = first
    a = aus.get(seq)
    if a is None or not a.get("ordinal", "").isdigit():
        return "unbound", None
    if a.get("key") == "1":
        return "onset_idr", a
    on_us = int(e["onsetUs"])
    for k in reversed(keys_sorted):
        ks = int(k["seq"])
        if int(k.get("enqueueUs", "0")) > on_us:
            continue
        w = wire.get(ks, {})
        end = int(k.get("enqueueUs", "0")) + int(w.get("queueWaitUs", 0)) + int(w.get("sendDurUs", 0))
        if end > on_us:
            return "idr_on_wire", a
        break
    if period:
        ordn = int(a["ordinal"])
        last = max((int(k["ordinal"]) for k in keys_sorted if int(k["ordinal"]) <= ordn), default=0)
        if last and period - (ordn - last) <= batch + 3:
            return "near", a
    return "far", a


def main(dirs, batch):
    for d in dirs:
        aus, wire = load_host(d)
        period = gop_period(aus)
        keys_sorted = sorted((a for a in aus.values() if a.get("key") == "1" and a.get("ordinal", "").isdigit()),
                             key=lambda k: int(k["seq"]))
        eps = [e for e in csv.DictReader(open(os.path.join(d, "episodes.csv"), encoding="utf-8")) if e["valid"] == "1"]
        firsts = {}
        for r in csv.DictReader(open(os.path.join(d, "deliveries.csv"), encoding="utf-8")):
            firsts.setdefault((r["scene"], r["index"]), []).append((int(r["atUs"]), int(r["id"]), int(r["seq"]) if r["seq"].lstrip("-").isdigit() else -1))
        by = collections.defaultdict(list)
        for e in eps:
            rows = sorted(x for x in firsts.get((e["scene"], e["index"]), []) if x[1] >= int(e["onsetId"]) and x[2] >= 0)
            if not rows:
                by["unbound"].append((e, None))
                continue
            ph, a = classify(e, aus, wire, keys_sorted, period, batch, (rows[0][0], rows[0][2]))
            by[ph].append((e, a))
        print(f"== {os.path.basename(d)}  GOP period {period} inputs, batch {batch}, episodes {len(eps)}")
        for ph in ("onset_idr", "idr_on_wire", "near", "far", "unbound"):
            v = by.get(ph, [])
            if not v:
                continue
            react = [float(e["firstReactionMs"]) for e, _ in v if float(e["firstReactionMs"]) >= 0]
            tfin = [float(e["finalFirstSeenMs"]) for e, _ in v if float(e["finalFirstSeenMs"]) >= 0]
            byt = [int(a.get("bytes", 0)) for _, a in v if a]
            med = lambda x: statistics.median(x) if x else float("nan")
            print(f"  {ph:<12} n {len(v):3d}  react p50 {med(react):6.0f} max {max(react) if react else float('nan'):6.0f}"
                  f"  Tfinal p50 {med(tfin):6.0f}  onset AU bytes p50 {med(byt):7.0f}")


if __name__ == "__main__":
    batch = 4
    for a in sys.argv[1:]:
        if a.startswith("--batch="):
            batch = int(a.split("=", 1)[1])
    main([a for a in sys.argv[1:] if not a.startswith("--")], batch)
