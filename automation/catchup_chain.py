"""catch-up chain (motion-settle r8): who delivered each episode's FINAL picture, and when.

    python catchup_chain.py <run dir> [<run dir> ...] [--detail]

Needs the host's per-AU trace (REMOTE60_NATIVE_AU_TRACE=1: [au] + wire lines) and the harness's
episodes.csv / deliveries.csv / paints.csv. Binding, not timing: the final picture's first decoded
delivery (marker id == finalId) gives the host seq; seq -> [au] line (source, content version,
enqueue time) -> wire line (queue wait, send time). Per run:
  - who delivered the final first: real / catchup / kick / static_refresh, with T_final p50 / max
    (first decode of the final id - its paint) for each;
  - chain timing of catchup-delivered finals: paint -> AU enqueue -> wire start -> wire end -> decode;
  - duplicates: catch-up AUs carrying a content version that an earlier accepted input had already
    carried (a paid debt served again) -- must be 0;
  - catch-up AUs with no open debt before them are not separable from this log alone; the host's own
    [catchup] owed/served/accepted counters are printed from the last stats line instead.
"""

import collections
import csv
import os
import re
import statistics
import sys

KV = re.compile(r"(\w+)=(\S+)")


def load(d):
    aus, wire, counters = {}, {}, ""
    for line in open(os.path.join(d, "host.log"), encoding="utf-8", errors="replace"):
        if "[au] seq=" in line:
            kv = dict(KV.findall(line.split("[au]", 1)[1]))
            if kv.get("seq", "").isdigit():
                aus[int(kv["seq"])] = kv
        elif "] wire seq=" in line:
            kv = dict(KV.findall(line.split("wire", 1)[1]))
            if kv.get("seq", "").isdigit():
                wire[int(kv["seq"])] = kv
        elif "[catchup] owed=" in line:
            counters = line.split("[catchup]", 1)[1].strip()
    return aus, wire, counters


def main(dirs, detail):
    for d in dirs:
        aus, wire, counters = load(d)
        eps = [e for e in csv.DictReader(open(os.path.join(d, "episodes.csv"), encoding="utf-8")) if e["valid"] == "1"]
        paints = {}
        for r in csv.DictReader(open(os.path.join(d, "paints.csv"), encoding="utf-8")):
            paints[(r["scene"], r["index"], r["id"])] = int(r["paintUs"])
        firsts = {}
        for r in csv.DictReader(open(os.path.join(d, "deliveries.csv"), encoding="utf-8")):
            k = (r["scene"], r["index"], r["id"])
            if k not in firsts or int(r["atUs"]) < firsts[k][0]:
                firsts[k] = (int(r["atUs"]), int(r["seq"]) if r["seq"].lstrip("-").isdigit() else -1)
        by = collections.defaultdict(list)
        chains = []
        for e in eps:
            k = (e["scene"], e["index"], e["finalId"])
            if k not in firsts:
                by["not_delivered"].append(None)
                continue
            at, seq = firsts[k]
            a = aus.get(seq)
            src = a.get("src", "?") if a else "unbound"
            tfin = float(e["finalFirstSeenMs"])
            by[src].append(tfin)
            if src == "catchup":
                w = wire.get(seq, {})
                enq = int(a.get("enqueueUs", 0))
                ws = enq + int(w.get("queueWaitUs", 0)) if w else 0
                we = ws + int(w.get("sendDurUs", 0)) if w else 0
                paint = paints.get(k, 0)
                chains.append((e["scene"], e["index"], seq, a.get("contentVersion"), (enq - paint) / 1000 if paint else None,
                               (ws - enq) / 1000 if ws else None, (we - ws) / 1000 if we else None,
                               (at - we) / 1000 if we else None, tfin))
        # duplicates: a catch-up AU whose content version an earlier accepted input already carried
        seen_versions = {}
        dups = 0
        for seq in sorted(aus):
            a = aus[seq]
            v = a.get("contentVersion")
            if a.get("src") == "catchup" and v in seen_versions and seen_versions[v] < seq:
                dups += 1
            if v and v != "0":
                seen_versions.setdefault(v, seq)
        print(f"== {os.path.basename(d)}  episodes {len(eps)}")
        for src in ("real", "catchup", "kick", "static_refresh", "unbound", "not_delivered", "?"):
            v = by.get(src)
            if not v:
                continue
            vv = [x for x in v if x is not None and x >= 0]
            print(f"  final first by {src:<14} n {len(v):3d}  T_final p50 {statistics.median(vv) if vv else float('nan'):6.0f} "
                  f"max {max(vv) if vv else float('nan'):6.0f} ms")
        if chains:
            med = lambda i: statistics.median([c[i] for c in chains if c[i] is not None]) if any(c[i] is not None for c in chains) else float('nan')
            print(f"  catchup finals: paint->enqueue p50 {med(4):.0f}  enqueue->wire {med(5):.0f}  wire {med(6):.0f}  wire->decode {med(7):.0f} ms")
            if detail:
                for c in chains:
                    print("   ", c)
        print(f"  catch-up AUs re-carrying an already-sent content version (duplicates): {dups}")
        print(f"  host counters: {counters}")


if __name__ == "__main__":
    main([a for a in sys.argv[1:] if not a.startswith("--")], "--detail" in sys.argv)
