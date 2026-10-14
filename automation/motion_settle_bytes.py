"""motion-settle r4: key frames and wire bytes per run, from host.log (reads only).

    python motion_settle_bytes.py <run dir> [<run dir> ...]

Per run: wall seconds covered by the 1 s stats lines, the final cumulative udpTxWireEstBytes (data + parity + chunk
headers + IP/UDP estimate + NACK) and its mean rate, key AUs by reason and by source (real / kick /
static_refresh / settle / catchup / gop_drain), their bytes, the last cumulative settle bytes and the
gop_drain input count from the [au-qp] line. The same scenes and episode count in two runs make the
difference of their totals the extra wire the feature costs; the scenes are mostly still (9 s holds),
so it is dominated by idle-time traffic.
"""

import collections
import os
import re
import sys

KV = re.compile(r"(\w+)=(\S+)")


def run(d):
    wire = 0
    secs = 0
    keys = collections.Counter()
    key_src = collections.Counter()
    key_bytes = 0
    settle_bytes = 0
    drain = 0
    for line in open(os.path.join(d, "host.log"), encoding="utf-8", errors="replace"):
        if "udpTxWireEstBytes=" in line and "[native-video-host] encodedFrames=" in line:
            kv = dict(KV.findall(line))
            wire = max(wire, int(kv.get("udpTxWireEstBytes", 0)))  # cumulative since start
            secs += 1
        elif "[keyframe]" in line:
            kv = dict(KV.findall(line.split("[keyframe]", 1)[1]))
            keys[kv.get("reasons", "?")] += 1
            key_src[kv.get("src", "?")] += 1
            key_bytes += int(kv.get("bytes", 0))
        elif "[au-qp]" in line:
            kv = dict(KV.findall(line))
            settle_bytes = int(kv.get("settleBytes", settle_bytes))
            drain = int(kv.get("gopDrain", drain))
    return secs, wire, keys, key_src, key_bytes, settle_bytes, drain


def main(dirs):
    for d in dirs:
        secs, wire, keys, key_src, key_bytes, settle_bytes, drain = run(d)
        rate = wire * 8 / secs / 1e6 if secs else 0
        print(f"{os.path.basename(d):<22} {secs:5d}s wire {wire / 1e6:8.2f} MB ({rate:5.3f} Mbps) | keys {sum(keys.values()):3d} "
              f"{dict(keys)} src {dict(key_src)} keyBytes {key_bytes / 1e6:6.2f} MB | settleBytes {settle_bytes / 1e6:6.2f} MB "
              f"gopDrainInputs {drain}")


if __name__ == "__main__":
    main(sys.argv[1:])
