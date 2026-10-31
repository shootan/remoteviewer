"""Read timingSchema=2 logs without interpreting field names as measurements.

Usage: python automation/summarize_pipeline_latency.py viewer.log --out report.json
No log text, key codes, typed text or credentials are copied to the report.
Cross-machine estimates use the nearest <=3s clock sample and report RTT/2 uncertainty.
They are estimates, not proof of one-way latency (asymmetric paths remain ambiguous).
"""
import argparse
from bisect import bisect_left
import json
import re
from pathlib import Path


def fields(line):
    values = {k: int(v) for k, v in re.findall(r"\b(\w+)=(-?\d+)(?=\s|$)", line)}
    session = re.search(r"\bviewerSession=([\w-]+)", line)
    values["session"] = session.group(1) if session else "unlabelled"
    return values


def elapsed(values, end, start):
    a, b = values.get(end, 0), values.get(start, 0)
    return a - b if a > 0 and b > 0 and a >= b else None


def distribution(values):
    xs = sorted(x for x in values if x is not None)
    if not xs:
        return {"samples": 0}
    return {"samples": len(xs), "p50_us": xs[(len(xs)-1)//2],
            "p95_us": xs[int((len(xs)-1)*.95)], "max_us": xs[-1]}


def analyze(lines):
    frames, inputs, clocks = [], [], []
    present_gaps = []
    for line in lines:
        f = fields(line)
        if "[present]" in line and "frameGapUs" in f:
            present_gaps.append(f["frameGapUs"])
        if "stage=clock" in line and all(k in f for k in ("clientRecvUs", "clockOffsetUs", "rttUs")):
            clocks.append(f)
        elif "[present]" in line and f.get("timingSchema") == 2:
            frames.append(f)
        elif "[input-timing]" in line and "clientDoneUs" in f:
            inputs.append(f)
    pairs = {"capture_to_encode": ("hostEncodeStartUs", "hostCaptureUs"),
             "encode": ("hostEncodeEndUs", "hostEncodeStartUs"),
             "encode_to_send": ("hostSendUs", "hostEncodeEndUs"),
             "receive_to_decode": ("clientDecodeStartUs", "clientRecvUs"),
             "decode": ("clientDecodeEndUs", "clientDecodeStartUs"),
             "decode_to_queue": ("clientQueueSetUs", "clientDecodeEndUs"),
             "queue_wait": ("clientPaintStartUs", "clientQueueSetUs"),
             "paint": ("clientPresentUs", "clientPaintStartUs")}
    clock_groups = {}
    for c in clocks:
        clock_groups.setdefault(c["session"], []).append(c)
    for group in clock_groups.values():
        group.sort(key=lambda c: c["clientRecvUs"])
    clock_times = {s: [c["clientRecvUs"] for c in g] for s, g in clock_groups.items()}
    rows = []
    for f in frames:
        row = {k: f.get(k) for k in ("session", "seq", "gen", "frameVersion", "synthetic", "frameGapUs")}
        row.update({name + "_us": elapsed(f, *pair) for name, pair in pairs.items()})
        row.update(network_estimate_us=None, capture_to_present_estimate_us=None,
                   clock_sample_age_us=None, clock_uncertainty_us=None)
        group = clock_groups.get(f["session"], [])
        if group and f.get("clientPresentUs", 0) > 0:
            at = bisect_left(clock_times[f["session"]], f["clientPresentUs"])
            candidates = group[max(0, at-1):at+1]
            c = min(candidates, key=lambda c: abs(c["clientRecvUs"] - f["clientPresentUs"]))
            age = abs(c["clientRecvUs"] - f["clientPresentUs"])
            if age <= 3_000_000:
                row["clock_sample_age_us"] = age
                row["clock_uncertainty_us"] = (c["rttUs"] + 1) // 2
                for out, client, host in (("network_estimate_us", "clientRecvUs", "hostSendUs"),
                                          ("capture_to_present_estimate_us", "clientPresentUs", "hostCaptureUs")):
                    if f.get(client, 0) > 0 and f.get(host, 0) > 0:
                        value = f[client] - f[host] + c["clockOffsetUs"]
                        # Negative estimates show an incompatible clock sample, never zero lag.
                        if value >= 0:
                            row[out] = value
        if f.get("synthetic"):
            row["capture_to_encode_us"] = None
            row["capture_to_present_estimate_us"] = None
        rows.append(row)
    return {"schema": 2, "frame_samples": len(rows), "input_samples": len(inputs),
            "present_gaps": distribution(present_gaps),
            "stages": {n: distribution(row[n + "_us"] for row in rows) for n in pairs},
            "input_queue": distribution(elapsed(f, "clientSendUs", "clientGeneratedUs") for f in inputs),
            "input_exchange": distribution(elapsed(f, "clientDoneUs", "clientSendUs") for f in inputs),
            "frames": rows, "input_timings": inputs,
            "limits": ["Input ACK does not prove OS injection or visible text.",
                       "Detailed frames are sampled (1Hz baseline, at most 10Hz on slow frames); stage distributions are biased, not all-frame percentiles.",
                       "Present gaps do not measure source capture intervals.",
                       "Clock-aligned one-way times assume symmetric paths; RTT/2 is uncertainty.",
                       "Receive timestamp semantics follow viewer frame publication metadata.",
                       "No schema-2 records means missing evidence, never healthy zero latency."]}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("viewer_log", type=Path)
    p.add_argument("--out", required=True, type=Path)
    a = p.parse_args()
    report = analyze(a.viewer_log.read_text(encoding="utf-8-sig", errors="replace").splitlines())
    a.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"frames={report['frame_samples']} inputs={report['input_samples']} output={a.out}")
    raise SystemExit(0 if report["frame_samples"] else 2)
