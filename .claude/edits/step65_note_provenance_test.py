import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'

TEST = '''  // ---- i. the signal survives an encode call that FAILED (A06 wiring, Codex round 2) ----
  // An encode can fail after the MFT accepted the input -- that call may be the one that
  // overflowed the FIFO -- and the stage returns early on failure. NoteProvenance is what those
  // early returns call, so the latch and the pending rebuild outlive the failure and the tick
  // retry (which only looks at provenanceResyncPending) still runs. Driven directly here; the
  // early-return call sites themselves are covered by reading, not by an executed test, because
  // H264Encoder has no transform-injection seam to force an overflow with.
  {
    CaptureState capture;
    EncoderState enc;
    enc.activeEncodeW = kW;
    enc.activeEncodeH = kH;
    enc.activeFps = 60;
    enc.activeBitrate = 2000000;
    enc.activeKeyint = 600;
    capture.inputEpoch.store(9, std::memory_order_release);
    enc.epochGate.epoch = 9;
    CHECK(!enc.NoteProvenance(false));                 // a healthy call changes nothing
    CHECK(!enc.epochGate.provenanceInvalid && !enc.provenanceResyncPending);
    CHECK(enc.NoteProvenance(true));                   // the failing call reported an overflow
    CHECK(enc.epochGate.provenanceInvalid && enc.provenanceResyncPending);
    CHECK(!enc.NoteProvenance(true));                  // idempotent: no second log, no second latch
    // The gate refuses everything meanwhile, whatever it looks like.
    uint64_t t = 7000000;
    CHECK(epoch_gate_judge(enc.epochGate, 9, 9, true, 3000, t += 16667) == EpochVerdict::DropUnknownEpoch);
    // The budget is spent by other rebuilds: the request stays pending and nothing is cleared,
    // so a later tick (no new frame needed) still has something to retry.
    for (int i = 0; i < 3; ++i) CHECK(epoch_gate_take_reset_budget(enc.epochGate, 7100000));
    CHECK(!enc.TryProvenanceResync(capture, 7100000));
    CHECK(enc.provenanceResyncPending && enc.epochGate.provenanceInvalid);
    CHECK(enc.provenanceResyncFailed == 0);            // a spent budget is not an initialize failure
    // Next window: the retry runs and succeeds (the rebuild is what clears the latch).
    const uint64_t nextWindowUs = 7100000 + EpochGate::kResetWindowUs + 1;
    if (enc.TryProvenanceResync(capture, nextWindowUs)) {
      CHECK(!enc.provenanceResyncPending && !enc.epochGate.provenanceInvalid && enc.forceKeyNext);
      enc.codec.shutdown();
    } else {
      std::printf("  note: encoder rebuild unavailable here; the pending/budget half was checked''' + NL + '''");
    }
  }
'''

edit(S + r'\host_encode_epoch_test.cpp', [
("  // ---- h. HN07: unknown re-closes the verified chain, a known-old AU does not ----",
 TEST + "  // ---- h. HN07: unknown re-closes the verified chain, a known-old AU does not ----"),
("void test_provenance_fifo_latch_and_gate() {\n  std::printf(\"[4] provenance: empty-output pop keeps the FIFO in lockstep, overflow latches invalid, the gate and the rebuild follow" + NL + "\");",
 "void test_provenance_fifo_latch_and_gate() {\n  std::printf(\"[4] provenance: FIFO lockstep, overflow latch, gate refusal, rebuild budget, and the signal surviving a failed encode" + NL + "\");"),
])
print('NoteProvenance regression ok')
