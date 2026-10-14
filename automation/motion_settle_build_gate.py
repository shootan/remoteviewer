"""motion-settle r7 build gate (Codex r7 s5): a shipping GNLinkStream must not carry the switches of the
non-adopted motion-settle paths or the fault-injection knob -- if the environment name is not in the
binary, no environment can turn the path on. Searches the executable's bytes for each name as a C
string literal (the name followed by NUL, so GOP_DRAIN does not match GOP_DRAIN_TEST_SKEW).

    python motion_settle_build_gate.py <GNLinkStream.exe> [--expect-experiments]

Default: every name ABSENT (shipping). --expect-experiments: every name PRESENT -- the negative control
that the search sees them at all (run it on an experiment / pre-gate build).
Exit 0 = gate passed, 1 = failed, 2 = usage.
"""

import hashlib
import sys

NAMES = [
    "REMOTE60_NATIVE_SETTLE_DIP_QP",
    "REMOTE60_NATIVE_ONSET_FLOOR_QP",
    "REMOTE60_NATIVE_ONSET_FLOOR_INPUTS",
    "REMOTE60_NATIVE_ONSET_FLOOR_MS",
    "REMOTE60_NATIVE_SETTLE_PARK_QP",
    "REMOTE60_NATIVE_SETTLE_TRAIN_MIN_QP",
    "REMOTE60_NATIVE_GOP_DRAIN",
    "REMOTE60_NATIVE_GOP_DRAIN_TEST_SKEW",
    "REMOTE60_NATIVE_FAST_PRESSURE",
    "REMOTE60_NATIVE_SETTLE_IDR",
]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[1]
    expect_present = "--expect-experiments" in argv[2:]
    data = open(path, "rb").read()
    fail = False
    for n in NAMES:
        present = (n.encode() + b"\0") in data
        ok = present if expect_present else not present
        print(f"{'PRESENT' if present else 'ABSENT ':<7} {'ok  ' if ok else 'FAIL'} {n}")
        fail |= not ok
    sha = hashlib.sha256(data).hexdigest()[:16]
    kind = "experiment" if expect_present else "shipping"
    print(f"GATE {'FAIL' if fail else 'PASS'} ({kind}) {path} sha256={sha}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
