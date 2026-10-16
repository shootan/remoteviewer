"""encoder-output-poll build gate (r1, Codex motion-settle r13 s5): a shipping GNLinkStream must not carry the
encoder-output-poll measurement trace (CMake REMOTE60_ENCODER_POLL_DIAG) nor any switch or trace of the
motion-settle research line (catch-up, the output probe, the r11/r12 traces) -- this branch was cut from main
and must not have picked any of it up. Names are searched as C string literals (followed by NUL).

    python encoder_poll_build_gate.py <GNLinkStream.exe> [--expect-diag]

Default: every DIAG and research name ABSENT (shipping build).
--expect-diag: every DIAG name PRESENT and every research name still ABSENT -- the negative control that the
search sees the trace (run it on a REMOTE60_ENCODER_POLL_DIAG=ON build). Exit 0 = passed, 1 = failed, 2 = usage.
"""

import hashlib
import sys

DIAG = [
    "REMOTE60_NATIVE_AU_TRACE",            # the trace's runtime switch
    "[native-video-host][enc] src=",       # per encode call
    "[native-video-host][au] seq=",        # per AU
]
RESEARCH = [
    "REMOTE60_NATIVE_CATCHUP",             # catch-up (shipping hold; not on this branch)
    "REMOTE60_NATIVE_CATCHUP_AWAIT_MODE",
    "REMOTE60_NATIVE_DIAG_OUTPUT_PROBE",   # r12 test-only output probe
    "[native-video-host][cap] version=",   # r11 capture trace
    " sliceQp=",                           # r11 per-AU slice QP trace
    "REMOTE60_NATIVE_SETTLE",
    "REMOTE60_NATIVE_GOP_DRAIN",
]
NUL = bytes([0])


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[1]
    expect_diag = "--expect-diag" in argv[2:]
    data = open(path, "rb").read()
    fail = False
    for group, want_present in ((DIAG, expect_diag), (RESEARCH, False)):
        for n in group:
            present = (n.encode() + NUL) in data
            ok = present == want_present
            print(f"{'PRESENT' if present else 'ABSENT ':<7} {'ok  ' if ok else 'FAIL'} {n}")
            fail |= not ok
    sha = hashlib.sha256(data).hexdigest()[:16]
    kind = "diag" if expect_diag else "shipping"
    print(f"GATE {'FAIL' if fail else 'PASS'} ({kind}) {path} sha256={sha}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
