"""motion-settle r3: decompose the onset's worst first-second gap into where the time went.

    python motion_settle_gap.py <run dir> [scene prefix] [--first]

--first (r4): decompose the FIRST REACTION instead -- the first decoded picture newer than the pre-onset
one, waited for from the onset paint.

For every episode (optionally only scenes starting with the prefix): the decoded picture that ended the
longest wait in the first second after the onset paint, and its path, joined by host seq:
  wait_from  = max(previous delivery, paint of the first newer picture)  (as the harness defines the gap)
  paint->enq = capture + encode, including pictures the async MFT still held   (host [au] enqueueUs)
  enq->wire  = queued behind earlier AUs (e.g. a refinement AU's tail)        (wire line queueWaitUs)
  wire       = its own send time                                              (wire line sendDurUs)
  wire->seen = network + reassembly + decode + publish to the FrameBuffer    (sampler atUs)
and what was on the wire just before it (the previous seq's source / bytes / slice QP, whether it was
still sending when this AU was enqueued). Needs the run's host.log with REMOTE60_NATIVE_AU_TRACE=1
([au] + wire lines) and deliveries.csv / paints.csv. Reads only.
"""

import csv
import os
import re
import sys

KV = re.compile(r"(\w+)=(\S+)")


def main(d, prefix="", first=False):
    eps = [e for e in csv.DictReader(open(os.path.join(d, "episodes.csv"), encoding="utf-8")) if e["scene"].startswith(prefix)]
    dl = list(csv.DictReader(open(os.path.join(d, "deliveries.csv"), encoding="utf-8")))
    paints = {}
    for r in csv.DictReader(open(os.path.join(d, "paints.csv"), encoding="utf-8")):
        paints[(r["scene"], r["index"], int(r["id"]))] = int(r["paintUs"])
    au, wire = {}, {}
    for line in open(os.path.join(d, "host.log"), encoding="utf-8", errors="replace"):
        if "[au] seq=" in line:
            kv = dict(KV.findall(line.split("[au]", 1)[1]))
            if kv.get("seq", "").isdigit():
                au[int(kv["seq"])] = kv
        elif "] wire seq=" in line:
            kv = dict(KV.findall(line.split("wire", 1)[1]))
            if kv.get("seq", "").isdigit():
                wire[int(kv["seq"])] = kv
    for e in eps:
        key = (e["scene"], e["index"])
        onset, final, onUs = int(e["onsetId"]), int(e["finalId"]), int(e["onsetUs"])
        rows = sorted((int(x["atUs"]), int(x["id"]), int(x["seq"])) for x in dl
                      if (x["scene"], x["index"]) == key and onset <= int(x["id"]) <= final)
        seen, shown, prev, worst = set(), onset - 1, onUs, None
        for at, i, seq in rows:
            if i in seen or at > onUs + 1_000_000:
                continue
            seen.add(i)
            if i > shown:
                start = max(prev, paints.get((e["scene"], e["index"], shown + 1), 0))
                g = at - start
                if worst is None or g > worst[0]:
                    worst = (g, at, i, seq, start)
                shown, prev = i, at
        if first:
            fr = next(((at, i, seq) for at, i, seq in rows if i >= onset), None)
            worst = (fr[0] - onUs, fr[0], fr[1], fr[2], onUs) if fr else None
        if not worst:
            continue
        g, at, i, seq, start = worst
        a, w = au.get(seq, {}), wire.get(seq, {})
        paintUs = paints.get((e["scene"], e["index"], i), 0)
        enq = int(a.get("enqueueUs", 0))
        qwait = int(w.get("queueWaitUs", -1))
        sdur = int(w.get("sendDurUs", -1))
        wire_start = enq + qwait if qwait >= 0 else 0
        wire_end = wire_start + sdur if sdur >= 0 else 0
        pa = au.get(seq - 1, {})
        pw = wire.get(seq - 1, {})
        p_end = (int(pa.get("enqueueUs", 0)) + int(pw.get("queueWaitUs", 0)) + int(pw.get("sendDurUs", 0))) if pa and pw else 0
        busy = p_end > enq if p_end else False
        ms = lambda v: f"{v / 1000:6.0f}" if v else "     ?"
        print(f"{e['scene']:<10} #{e['index']:<2} gap {g / 1000:4.0f}ms id {i} seq {seq} {a.get('src', '?'):<7} {a.get('bytes', '?'):>6}B qp {a.get('sliceQp', '?'):>2} | "
              f"paint->enq {ms(enq - paintUs if enq and paintUs else 0)} enq->wire {ms(qwait if qwait >= 0 else 0)} wire {ms(sdur if sdur >= 0 else 0)} "
              f"wire->seen {ms(at - wire_end if wire_end else 0)} | before: seq {seq - 1} {pa.get('src', '?')} {pa.get('bytes', '?')}B qp {pa.get('sliceQp', '?')} "
              f"{'still on wire at enqueue' if busy else ''}")


if __name__ == "__main__":
    pos = [a for a in sys.argv[1:] if not a.startswith("--")]
    main(pos[0], pos[1] if len(pos) > 1 else "", "--first" in sys.argv)
