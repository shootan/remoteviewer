"""encoder-output-poll build gate (r1, Codex motion-settle r13 s5): a shipping GNLinkStream must not carry the
encoder-output-poll measurement trace (CMake REMOTE60_ENCODER_POLL_DIAG) nor any switch or trace of the
motion-settle research line (catch-up, the output probe, the r11/r12 traces) -- this branch was cut from main
and must not have picked any of it up. Names are searched as C string literals (followed by NUL).

    python encoder_poll_build_gate.py <GNLinkStream.exe> [--expect-diag] [--expect-sliceqp] [--expect-refine]

Default: every DIAG and research name ABSENT (shipping build).
--expect-diag: every DIAG name PRESENT and every research name still ABSENT -- the negative control that the
search sees the trace (run it on a REMOTE60_ENCODER_POLL_DIAG=ON build).
--expect-refine: the r2 experiment C strings (REMOTE60_TRAILING_REFINE_EXPERIMENT=ON) PRESENT; without it they
must be ABSENT. --expect-sliceqp: the r3 DIAG slice-QP key PRESENT (DIAG builds from r3 on).
Exit 0 = passed, 1 = failed, 2 = usage.
"""

import hashlib
import sys

DIAG = [
    "REMOTE60_NATIVE_AU_TRACE",            # the trace's runtime switch
    "[native-video-host][enc] src=",       # per encode call
    "[native-video-host][au] seq=",        # per AU
]
SLICEQP = [
    " sliceQp=",                           # r3 DIAG [au] first-slice QP (r10 reader); also the r11 research key
]
REFINE = [
    " trailingRefineCount=",               # r2 experiment C telemetry
    "[native-video-host][refine] input=",  # r2 experiment C decision trace
]
RESEARCH = [
    "REMOTE60_NATIVE_CATCHUP",             # catch-up (shipping hold; not on this branch)
    "REMOTE60_NATIVE_CATCHUP_AWAIT_MODE",
    "REMOTE60_NATIVE_DIAG_OUTPUT_PROBE",   # r12 test-only output probe
    "[native-video-host][cap] version=",   # r11 capture trace
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
    expect_refine = "--expect-refine" in argv[2:]
    expect_sliceqp = "--expect-sliceqp" in argv[2:]
    data = open(path, "rb").read()
    fail = False
    for group, want_present in ((DIAG, expect_diag), (SLICEQP, expect_sliceqp), (REFINE, expect_refine), (RESEARCH, False)):
        for n in group:
            present = (n.encode() + NUL) in data
            ok = present == want_present
            print(f"{'PRESENT' if present else 'ABSENT ':<7} {'ok  ' if ok else 'FAIL'} {n}")
            fail |= not ok
    sha = hashlib.sha256(data).hexdigest()[:16]
    kind = "+".join(k for k, on in (("diag", expect_diag), ("sliceqp", expect_sliceqp), ("refine", expect_refine)) if on) or "shipping"
    print(f"GATE {'FAIL' if fail else 'PASS'} ({kind}) {path} sha256={sha}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
