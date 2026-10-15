"""motion-settle build gate (Codex r7 s5, r8 s5): a shipping GNLinkStream must not carry the switches of
the non-adopted motion-settle paths (the refinement train included -- the r8 candidate is catch-up only)
or the fault-injection knob -- if the environment name is not in the binary, no environment can turn
the path on. The catch-up's own switch must be there (REQUIRED), so a PASS also proves the check looked
at a binary that has the candidate at all. Names are searched as C string literals (followed by NUL).

    python motion_settle_build_gate.py <GNLinkStream.exe> [--expect-experiments]

Default: every listed name ABSENT and every required name PRESENT (shipping candidate).
--expect-experiments: every listed name PRESENT -- the negative control that the search sees them
(run it on a research build). Exit 0 = gate passed, 1 = failed, 2 = usage.
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
    "REMOTE60_NATIVE_SETTLE",
    "REMOTE60_NATIVE_SETTLE_QUIET_MS",
    "REMOTE60_NATIVE_SETTLE_INPUTS",
    "REMOTE60_NATIVE_SETTLE_SPACING_MS",
    "REMOTE60_NATIVE_SETTLE_DEADLINE_MS",
    "REMOTE60_NATIVE_SETTLE_BYTE_PCT",
]
REQUIRED = ["REMOTE60_NATIVE_CATCHUP"]
NUL = bytes([0])


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[1]
    expect_present = "--expect-experiments" in argv[2:]
    data = open(path, "rb").read()
    fail = False
    for n in NAMES:
        present = (n.encode() + NUL) in data
        ok = present if expect_present else not present
        print(f"{'PRESENT' if present else 'ABSENT ':<7} {'ok  ' if ok else 'FAIL'} {n}")
        fail |= not ok
    if not expect_present:
        for n in REQUIRED:
            present = (n.encode() + NUL) in data
            print(f"{'PRESENT' if present else 'ABSENT ':<7} {'ok  ' if present else 'FAIL'} {n} (required)")
            fail |= not present
    sha = hashlib.sha256(data).hexdigest()[:16]
    kind = "experiment" if expect_present else "shipping"
    print(f"GATE {'FAIL' if fail else 'PASS'} ({kind}) {path} sha256={sha}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
