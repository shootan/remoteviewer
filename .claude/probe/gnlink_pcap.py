#!/usr/bin/env python3
"""GNLink host-side UDP capture analyzer (audit tool, untracked).

Reads a pcapng produced by `pktmon etl2pcap`, parses Ethernet/IPv4/UDP by hand (snaplen-safe), decodes
GNLink UDP packet kinds (poc_protocol.hpp, #pragma pack(1)) and prints:
  - coverage: first/last packet time, counts/bytes by direction and kind, peer (ip:port) transitions
  - VideoChunk (host->peer): per stream generation seq range, keyframes, retransmits, send-order gaps,
    wire stalls (> --stall-ms between consecutive chunks), per-second timeline (CSV)
  - peer->host: Hello/Punch/Control*/CursorPos/VideoNack timeline, NACK details
  - focus window (--focus HH:MM:SS, +/- --window s): per-second table
Host-only capture: it says what left/entered this NIC, never whether the remote received anything.
"""
import argparse
import collections
import csv
import datetime as dt
import struct
import sys

try:
    import dpkt  # only the pcapng reader is used
except ImportError:  # pragma: no cover
    dpkt = None

KMAGIC = 0x31435052  # "RPC1"
KINDS = {300: "Hello", 301: "HelloAck", 302: "VideoChunk", 303: "Punch", 304: "ControlData",
         305: "ControlAck", 306: "ControlNack", 307: "CursorPos", 308: "VideoNack"}
# UdpVideoChunkHeader (88 bytes, pack(1)):
#   magic u32, kind u16, size u16, seq u32, codec u16, flags u16, width u32, height u32, stride u32,
#   payloadSize u32, chunkOffset u32, chunkSize u32, chunkIndex u16, chunkCount u16, chunkStride u32,
#   streamGeneration u64, captureQpcUs u64, encodeStartQpcUs u64, encodeEndQpcUs u64, sendQpcUs u64
CHUNK_FMT = "<IHHIHHIIIIIIHHIQQQQQ"
CHUNK_LEN = struct.calcsize(CHUNK_FMT)
assert CHUNK_LEN == 88, CHUNK_LEN
# UdpVideoNackPacket head: magic u32, kind u16, size u16, streamGeneration u64, seq u32, chunkCount u16,
#   missingCount u16, round u16, reserved u16, missing[48] u16
NACK_FMT = "<IHHQIHHHH"
NACK_LEN = struct.calcsize(NACK_FMT)

KST = dt.timezone(dt.timedelta(hours=9))


def fmt_ts(ts):
    return dt.datetime.fromtimestamp(ts, KST).strftime("%H:%M:%S.%f")[:-3]


def parse_l2_l4(buf):
    """Return (src_ip, dst_ip, src_port, dst_port, ip_total_len, udp_payload) or None."""
    if len(buf) < 14:
        return None
    ethertype = struct.unpack_from("!H", buf, 12)[0]
    off = 14
    while ethertype in (0x8100, 0x88A8):  # VLAN tags
        if len(buf) < off + 4:
            return None
        ethertype = struct.unpack_from("!H", buf, off + 2)[0]
        off += 4
    if ethertype != 0x0800 or len(buf) < off + 20:
        return None
    vihl = buf[off]
    if vihl >> 4 != 4:
        return None
    ihl = (vihl & 0x0F) * 4
    total_len = struct.unpack_from("!H", buf, off + 2)[0]
    proto = buf[off + 9]
    if proto != 17 or len(buf) < off + ihl + 8:
        return None
    src = ".".join(str(b) for b in buf[off + 12:off + 16])
    dst = ".".join(str(b) for b in buf[off + 16:off + 20])
    uoff = off + ihl
    sport, dport, ulen = struct.unpack_from("!HHH", buf, uoff)
    payload = buf[uoff + 8:]
    return src, dst, sport, dport, total_len, payload


def read_packets(path):
    if dpkt is None:
        sys.exit("dpkt is required (pip install dpkt)")
    with open(path, "rb") as f:
        head = f.read(4)
        f.seek(0)
        if head == b"\x0a\x0d\x0d\x0a":
            reader = dpkt.pcapng.Reader(f)
        else:
            reader = dpkt.pcap.Reader(f)
        for ts, buf in reader:
            yield float(ts), bytes(buf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pcap")
    ap.add_argument("--host-ip", default="192.168.0.76")
    ap.add_argument("--port", type=int, default=43000)
    ap.add_argument("--focus", default=None, help="local time HH:MM:SS (KST) to zoom on")
    ap.add_argument("--window", type=int, default=90, help="seconds around focus")
    ap.add_argument("--stall-ms", type=int, default=500)
    ap.add_argument("--csv", default=None, help="per-second timeline CSV path")
    a = ap.parse_args()

    n_all = n_udp = n_gnlink = 0
    first_ts = last_ts = None
    by_dir_kind = collections.Counter()
    bytes_wire = collections.Counter()
    peers = collections.OrderedDict()  # (dir, ip, port) -> [first, last, count]
    per_sec = collections.defaultdict(lambda: collections.Counter())
    # video
    gens = collections.OrderedDict()  # gen -> dict
    seen_chunk = collections.defaultdict(list)  # (gen, seq, idx) -> [ts...]
    retrans = []
    prev_chunk = None  # (ts, gen, seq, idx)
    stalls = []
    send_gaps = []  # (ts, gen, prev_seq, seq)
    holds = []  # sendQpc - captureQpc per chunk index 0
    nacks = []
    events = []  # (ts, dir, kind, detail)
    unknown_kinds = collections.Counter()
    truncated = 0

    for ts, buf in read_packets(a.pcap):
        n_all += 1
        p = parse_l2_l4(buf)
        if not p:
            continue
        src, dst, sport, dport, total_len, payload = p
        n_udp += 1
        if sport != a.port and dport != a.port:
            continue
        direction = "H>P" if (sport == a.port and src == a.host_ip) or src == a.host_ip else "P>H"
        peer = (dst, dport) if direction == "H>P" else (src, sport)
        if first_ts is None:
            first_ts = ts
        last_ts = ts
        key = (direction, peer[0], peer[1])
        if key not in peers:
            peers[key] = [ts, ts, 0]
        peers[key][1] = ts
        peers[key][2] += 1
        sec = int(ts)
        if len(payload) < 8:
            by_dir_kind[(direction, "short")] += 1
            continue
        magic, kind, size = struct.unpack_from("<IHH", payload, 0)
        if magic != KMAGIC:
            by_dir_kind[(direction, "nonGNLink")] += 1
            continue
        n_gnlink += 1
        kname = KINDS.get(kind, f"kind{kind}")
        if kind not in KINDS:
            unknown_kinds[kind] += 1
        by_dir_kind[(direction, kname)] += 1
        bytes_wire[(direction, kname)] += total_len
        per_sec[sec][f"{direction}:{kname}"] += 1
        per_sec[sec][f"{direction}:bytes"] += total_len
        if kind == 302:
            if len(payload) < CHUNK_LEN:
                truncated += 1
                continue
            (_, _, _, seq, codec, flags, w, h, stride, psize, coff, csize, cidx, ccount, cstride, gen,
             capUs, encS, encE, sendUs) = struct.unpack_from(CHUNK_FMT, payload, 0)
            g = gens.setdefault(gen, {"first": ts, "last": ts, "seq_min": seq, "seq_max": seq, "seqs": set(),
                                      "keys": 0, "chunks": 0, "bytes": 0, "dir": direction})
            g["last"] = ts
            g["seq_min"] = min(g["seq_min"], seq)
            g["seq_max"] = max(g["seq_max"], seq)
            g["seqs"].add(seq)
            g["chunks"] += 1
            g["bytes"] += csize
            if flags & 0x10:  # FEC parity packet: chunkIndex is the parity group, not a data index
                per_sec[sec]["H>P:parity"] += 1
                g["parity"] = g.get("parity", 0) + 1
                continue
            if direction == "H>P":
                per_sec[sec]["H>P:chunkBytes"] += csize
                if cidx == 0:
                    per_sec[sec]["H>P:aus"] += 1
                    if flags & 0x1:
                        g["keys"] += 1
                        per_sec[sec]["H>P:keys"] += 1
                    if sendUs and capUs and sendUs >= capUs:
                        holds.append((ts, gen, seq, sendUs - capUs))
                lst = seen_chunk[(gen, seq, cidx)]
                if lst:
                    retrans.append((ts, gen, seq, cidx, ccount, flags & 1, lst[0]))
                    per_sec[sec]["H>P:retrans"] += 1
                lst.append(ts)
                if prev_chunk is not None:
                    pts, pgen, pseq, pidx = prev_chunk
                    if (ts - pts) * 1000.0 > a.stall_ms:
                        stalls.append((pts, ts, pgen, pseq, gen, seq))
                    if gen == pgen and seq > pseq + 1 and not lst[:-1]:
                        send_gaps.append((ts, gen, pseq, seq))
                prev_chunk = (ts, gen, seq, cidx)
        elif kind == 308 and len(payload) >= NACK_LEN:
            (_, _, _, gen, seq, ccount, mcount, rnd, _) = struct.unpack_from(NACK_FMT, payload, 0)
            miss = []
            for i in range(min(mcount, 48)):
                o = NACK_LEN + 2 * i
                if o + 2 <= len(payload):
                    miss.append(struct.unpack_from("<H", payload, o)[0])
            nacks.append((ts, direction, gen, seq, ccount, mcount, rnd, miss))
            per_sec[sec][f"{direction}:nack"] += 1
        elif kind in (300, 301, 303):
            events.append((ts, direction, kname, f"{peer[0]}:{peer[1]}"))

    print(f"file: {a.pcap}")
    print(f"packets: total={n_all} udp={n_udp} gnlink(port {a.port})={n_gnlink} truncatedChunkHdr={truncated}")
    if first_ts is None:
        print("no GNLink packets")
        return
    print(f"time range (KST): {fmt_ts(first_ts)} .. {fmt_ts(last_ts)}  ({last_ts - first_ts:.1f} s)")
    if unknown_kinds:
        print(f"unknown kinds: {dict(unknown_kinds)}")
    print("\n[by direction/kind]  count / wire bytes")
    for (d, k), c in sorted(by_dir_kind.items()):
        print(f"  {d} {k:12s} {c:9d} / {bytes_wire[(d, k)]:11d}")
    print("\n[peers]")
    for (d, ip, port), (f0, l0, c) in peers.items():
        print(f"  {d} {ip}:{port:<6d} first={fmt_ts(f0)} last={fmt_ts(l0)} pkts={c}")
    print("\n[session events: Hello/HelloAck/Punch]")
    for ts, d, k, det in events[:60]:
        print(f"  {fmt_ts(ts)} {d} {k} {det}")
    print("\n[video generations (H>P chunks)]")
    for gen, g in gens.items():
        span = g["last"] - g["first"]
        print(f"  gen={gen} dir={g['dir']} {fmt_ts(g['first'])}..{fmt_ts(g['last'])} ({span:.1f}s) "
              f"seq {g['seq_min']}..{g['seq_max']} distinct={len(g['seqs'])} chunks={g['chunks']} "
              f"bytes={g['bytes']} keys={g['keys']} parity={g.get('parity', 0)}")
    print(f"\n[retransmitted chunks (same gen/seq/idx sent again)] {len(retrans)}")
    for ts, gen, seq, idx, cc, key, t0 in retrans[:80]:
        print(f"  {fmt_ts(ts)} gen={gen} seq={seq} idx={idx}/{cc} key={key} first={fmt_ts(t0)} (+{(ts - t0) * 1000:.1f} ms)")
    print(f"\n[send-order seq gaps within a generation] {len(send_gaps)}")
    for ts, gen, ps, s in send_gaps[:60]:
        print(f"  {fmt_ts(ts)} gen={gen} {ps} -> {s} (skipped {s - ps - 1})")
    print(f"\n[wire stalls > {a.stall_ms} ms between consecutive H>P chunks] {len(stalls)}")
    for t0, t1, g0, s0, g1, s1 in stalls[:80]:
        print(f"  {fmt_ts(t0)} .. {fmt_ts(t1)}  {(t1 - t0) * 1000:.0f} ms  before gen={g0} seq={s0}  after gen={g1} seq={s1}")
    if holds:
        hs = sorted(h for _, _, _, h in holds)
        print(f"\n[host hold send-capture us per AU] n={len(hs)} min={hs[0]} p50={hs[len(hs)//2]} p95={hs[int(len(hs)*0.95)]} max={hs[-1]}")
        big = [(ts, gen, seq, h) for ts, gen, seq, h in holds if h > 300000]
        print(f"  > 300 ms: {len(big)}")
        for ts, gen, seq, h in big[:40]:
            print(f"    {fmt_ts(ts)} gen={gen} seq={seq} hold={h / 1000:.0f} ms")
    print(f"\n[VideoNack] {len(nacks)}")
    for ts, d, gen, seq, cc, mc, rnd, miss in nacks[:120]:
        print(f"  {fmt_ts(ts)} {d} gen={gen} seq={seq} chunks={cc} missing={mc} round={rnd} idx={miss[:12]}{'...' if len(miss) > 12 else ''}")

    # per-second timeline CSV
    secs = sorted(per_sec)
    cols = ["time", "H>P:aus", "H>P:keys", "H>P:VideoChunk", "H>P:parity", "H>P:chunkBytes", "H>P:retrans", "P>H:VideoNack",
            "P>H:ControlData", "P>H:ControlAck", "H>P:ControlData", "H>P:ControlAck", "P>H:CursorPos",
            "H>P:CursorPos", "P>H:Hello", "H>P:HelloAck", "P>H:Punch", "H>P:Punch", "P>H:bytes", "H>P:bytes"]
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            wri = csv.writer(f)
            wri.writerow(cols)
            for s in secs:
                wri.writerow([fmt_ts(s)[:8]] + [per_sec[s].get(c, 0) for c in cols[1:]])
        print(f"\nper-second CSV: {a.csv} ({len(secs)} rows)")
    # focus table
    if a.focus:
        hh, mm, ss = (int(x) for x in a.focus.split(":"))
        base = dt.datetime.fromtimestamp(first_ts, KST).replace(hour=hh, minute=mm, second=ss, microsecond=0)
        f0 = base.timestamp() - a.window
        f1 = base.timestamp() + a.window
        print(f"\n[focus {a.focus} +/- {a.window}s]  sec | AUs keys chunks bytes retrans | P>H nack ctlData ctlAck cursor | H>P ctlData ctlAck | P>H pkts")
        for s in secs:
            if f0 <= s <= f1:
                c = per_sec[s]
                p2h = sum(v for k, v in c.items() if k.startswith("P>H:") and k not in ("P>H:bytes",))
                print(f"  {fmt_ts(s)[:8]} | {c.get('H>P:aus', 0):4d} {c.get('H>P:keys', 0):3d} {c.get('H>P:VideoChunk', 0):5d} "
                      f"{c.get('H>P:chunkBytes', 0):8d} {c.get('H>P:retrans', 0):4d} | {c.get('P>H:VideoNack', 0):3d} "
                      f"{c.get('P>H:ControlData', 0):4d} {c.get('P>H:ControlAck', 0):4d} {c.get('P>H:CursorPos', 0):4d} | "
                      f"{c.get('H>P:ControlData', 0):4d} {c.get('H>P:ControlAck', 0):4d} | {p2h:5d}")
    print("\nNOTE: host-side capture only -- shows what this NIC sent/received; remote reception is not proven by it.")


if __name__ == "__main__":
    main()
