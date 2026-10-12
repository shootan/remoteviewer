"""motion-settle r1 M0: join a --motion-settle run's episodes.csv with the host's per-AU trace.

For every episode: the AUs whose capture stamp falls in [onset - 50 ms, end], split into the action
(onset .. final paint) and the settle (after the final paint): count, bytes, sources (real / kick /
static_refresh / settle), keys, and the QP when the encoder reported one. The host and the harness
run on one machine, so the AU captureUs and the episode's onsetUs/finalUs are the same QPC clock.

    python motion_settle_join.py <run dir> [<run dir> ...]

Writes <run dir>/au_join.csv and prints one line per episode. Reads only; changes nothing.
"""

import csv
import os
import re
import sys

AU_RE = re.compile(r"\[native-video-host\]\[au\] (.*)$")
KV_RE = re.compile(r"(\w+)=(\S+)")


def read_aus(path):
    aus = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = AU_RE.search(line)
            if not m:
                continue
            kv = dict(KV_RE.findall(m.group(1)))
            try:
                aus.append({
                    "seq": int(kv["seq"]),
                    "key": int(kv["key"]),
                    "src": kv["src"],
                    "qp": int(kv["qp"]),
                    "bytes": int(kv["bytes"]),
                    "captureUs": int(kv["captureUs"]),
                    "enqueueUs": int(kv["enqueueUs"]),
                    "enqueued": int(kv["enqueued"]),
                    "forced": int(kv["forced"]),
                })
            except (KeyError, ValueError):
                continue
    return aus


def summarise(aus):
    by_src = {}
    for a in aus:
        s = by_src.setdefault(a["src"], [0, 0])
        s[0] += 1
        s[1] += a["bytes"]
    qps = [a["qp"] for a in aus if a["qp"] >= 0]
    return {
        "n": len(aus),
        "bytes": sum(a["bytes"] for a in aus),
        "keys": sum(a["key"] for a in aus),
        "maxBytes": max((a["bytes"] for a in aus), default=0),
        "src": " ".join(f"{k}:{v[0]}/{v[1]}B" for k, v in sorted(by_src.items())),
        "qp": (f"{min(qps)}-{max(qps)}" if qps else "unknown"),
    }


def main(dirs):
    for d in dirs:
        eps = list(csv.DictReader(open(os.path.join(d, "episodes.csv"), encoding="utf-8")))
        aus = read_aus(os.path.join(d, "host.log"))
        out = open(os.path.join(d, "au_join.csv"), "w", newline="", encoding="utf-8")
        w = csv.writer(out)
        w.writerow(["scene", "index", "timeToSharpMs", "refPsnr", "worstPsnr", "firstReactionMs", "maxGap1sMs",
                    "actN", "actBytes", "actMaxBytes", "actKeys", "actSrc", "settleN", "settleBytes", "settleKeys",
                    "settleSrc", "settleQp", "settleFirstMs", "settleLastMs"])
        for e in eps:
            onset = int(e["onsetUs"])
            final = int(e["finalUs"])
            end = final + 7_000_000
            act = [a for a in aus if onset - 50_000 <= a["captureUs"] < final]
            st = [a for a in aus if final <= a["captureUs"] <= end]
            sa, ss = summarise(act), summarise(st)
            first = (st[0]["enqueueUs"] - final) // 1000 if st else -1
            last = (st[-1]["enqueueUs"] - final) // 1000 if st else -1
            w.writerow([e["scene"], e["index"], e["timeToSharpMs"], e["refPsnr"], e["worstPsnr"], e["firstReactionMs"],
                        e["maxGap1sMs"], sa["n"], sa["bytes"], sa["maxBytes"], sa["keys"], sa["src"], ss["n"],
                        ss["bytes"], ss["keys"], ss["src"], ss["qp"], first, last])
            print(f"{os.path.basename(d.rstrip('/'))} {e['scene']:<10} #{e['index']:>2} sharp={e['timeToSharpMs']:>5}ms "
                  f"ref={float(e['refPsnr']):5.1f} worst={float(e['worstPsnr']):5.1f} | action {sa['n']:>3} AUs "
                  f"{sa['bytes']//1024:>5}KB max {sa['maxBytes']//1024}KB keys {sa['keys']} | settle {ss['n']} AUs "
                  f"{ss['bytes']} B [{ss['src']}] first+{first}ms last+{last}ms")
        out.close()


if __name__ == "__main__":
    main(sys.argv[1:])
