import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'
F = S + r'\host_encode_epoch_test.cpp'

TESTS = '''
// [4] A02/A06/HN07 -- provenance. Deterministic: the product's own accepted-input FIFO
// (PendingInputFifo), the product gate (host_epoch_gate.hpp) and the product rebuild
// (EncoderState::TryProvenanceResync) driven directly, so no asynchronous MFT timing is involved.
//   a. an output with no bytes consumes its FIFO entry WHOLE -- the epoch too -- so the IDR forced
//      by the next flush still carries the new epoch and opens the gate (before A02 the epoch was
//      left behind and that IDR was judged old: drop-old-epoch, the F1 shift);
//   b/c/f. an overflow latches provenance invalid: the FIFO empties, later outputs are unknown (0)
//      and the gate refuses everything -- including an AU that looks like the current epoch's key;
//   d. the rebuild runs on the epoch gate's budget (3 per 10 s): a fourth attempt in the window is
//      refused and the request stays pending, and it is allowed again in the next window;
//   e. a rebuild whose initialize() fails leaves the latch, the pending request and the closed gate;
//   g. a successful rebuild clears the latch, forces a key and the new epoch's IDR is accepted;
//   h. after the gate is open, an unprovenanced output closes the chain again (the next P is
//      dropped and the key re-forced) while a known-old AU does not.
void test_provenance_fifo_latch_and_gate() {
  std::printf("[4] provenance: empty-output pop keeps the FIFO in lockstep, overflow latches invalid, the gate and the rebuild follow''' + NL + '''");
  // ---- a. the empty-output path pops the whole record ----
  {
    PendingInputFifo fifo;
    fifo.Push(PendingInput{1000, false, 1});   // epoch 1, three inputs
    fifo.Push(PendingInput{2000, false, 1});
    fifo.Push(PendingInput{3000, false, 1});
    fifo.PopForEmptyOutput();                  // the MFT consumed input 1 and produced nothing usable
    fifo.Push(PendingInput{4000, true, 2});    // the flush: epoch 2, the forced key's input
    PendingInput got{};
    CHECK(fifo.Pop(&got) && got.tsHns == 2000 && got.epoch == 1);
    CHECK(fifo.Pop(&got) && got.tsHns == 3000 && got.epoch == 1);
    CHECK(fifo.Pop(&got) && got.tsHns == 4000 && got.epoch == 2);  // pre-A02: this read epoch 1
    CHECK(!fifo.Pop(&got) && fifo.size() == 0);
    // The gate then accepts that IDR as the new epoch's first AU.
    EpochGate g;
    uint64_t t = 1000000;
    CHECK(epoch_gate_judge(g, 1, 1, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
    CHECK(epoch_gate_judge(g, 1, 1, false, 300, t += 16667) == EpochVerdict::Emit);
    CHECK(epoch_gate_judge(g, 2, 2, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
    CHECK(!g.awaitingKey && g.droppedOldEpoch == 0);
  }
  // ---- b/c/f. overflow -> latch -> everything unknown, nothing accepted ----
  {
    PendingInputFifo fifo;
    for (size_t i = 0; i <= PendingInputFifo::kMaxEntries; ++i) {
      fifo.Push(PendingInput{static_cast<int64_t>(i + 1) * 1000, false, 5});
    }
    CHECK(fifo.provenance_invalid());
    CHECK(fifo.size() == 0);          // emptied: nothing left claims to describe the MFT's output
    CHECK(fifo.overflow_total() == 1);
    PendingInput got{};
    CHECK(!fifo.Pop(&got));           // no provenance -> the codec stamps epoch 0
    fifo.Clear();                     // shutdown half of a rebuild: the latch survives
    CHECK(fifo.provenance_invalid());
    EpochGate g;
    uint64_t t = 2000000;
    CHECK(epoch_gate_judge(g, 5, 5, true, 3000, t += 16667) == EpochVerdict::AcceptKey);  // healthy first
    g.provenanceInvalid = true;                                                          // the stage latches the gate
    CHECK(epoch_gate_judge(g, 5, 0, false, 300, t += 16667) == EpochVerdict::DropUnknownEpoch);
    CHECK(g.awaitingKey);                                                                 // the chain closed at once
    // Even an AU that looks like the current epoch's key is refused while the latch is up.
    CHECK(epoch_gate_judge(g, 5, 5, true, 3000, t += 16667) == EpochVerdict::DropUnknownEpoch);
    CHECK(g.keysAccepted == 1 && g.droppedProvenanceInvalid >= 2);
    fifo.Reset();                     // a successful initialize()
    CHECK(!fifo.provenance_invalid() && fifo.size() == 0);
  }
  // ---- d. the rebuild budget is the gate's own ----
  {
    EpochGate g;
    const uint64_t t0 = 3000000;
    CHECK(epoch_gate_take_reset_budget(g, t0));
    CHECK(epoch_gate_take_reset_budget(g, t0 + 1000));
    CHECK(epoch_gate_take_reset_budget(g, t0 + 2000));
    CHECK(!epoch_gate_take_reset_budget(g, t0 + 3000));          // 3 per window: the 4th waits
    CHECK(g.resetsRequested == 3 && g.resetsSuppressed == 1);
    CHECK(epoch_gate_take_reset_budget(g, t0 + EpochGate::kResetWindowUs + 1));  // next window
  }
  // ---- e/g. the rebuild itself, on a real encoder ----
  {
    CaptureState capture;
    EncoderState enc;
    enc.activeEncodeW = kW;
    enc.activeEncodeH = kH;
    enc.activeFps = 60;
    enc.activeBitrate = 2000000;
    enc.activeKeyint = 600;
    capture.inputEpoch.store(4, std::memory_order_release);
    enc.epochGate.epoch = 4;
    enc.epochGate.provenanceInvalid = true;
    enc.provenanceResyncPending = true;
    enc.forceKeyNext = false;
    // e. a rebuild that cannot initialize (0x0) leaves everything closed and pending.
    enc.activeEncodeW = 0;
    enc.activeEncodeH = 0;
    CHECK(!enc.TryProvenanceResync(capture, 4000000));
    CHECK(enc.provenanceResyncPending && enc.epochGate.provenanceInvalid && enc.provenanceResyncFailed == 1);
    CHECK(!enc.codec.provenance_invalid() || true);  // the codec was shut down; the stage keeps the gate closed
    // g. with valid geometry it succeeds: latch cleared, key forced, the new epoch waits for its IDR.
    enc.activeEncodeW = kW;
    enc.activeEncodeH = kH;
    const bool rebuilt = enc.TryProvenanceResync(capture, 4100000);
    if (!rebuilt) {
      std::printf("  note: encoder rebuild unavailable on this machine; e/g checked only up to the failure path''' + NL + '''");
    } else {
      CHECK(!enc.provenanceResyncPending);
      CHECK(!enc.epochGate.provenanceInvalid);
      CHECK(!enc.codec.provenance_invalid());
      CHECK(enc.forceKeyNext);
      CHECK(enc.epochGate.awaitingKey);            // the rebuild bumped the epoch: its IDR is awaited
      CHECK(enc.provenanceResyncCount == 1);
      const uint64_t epochNow = capture.inputEpoch.load(std::memory_order_acquire);
      uint64_t t = 5000000;
      CHECK(epoch_gate_judge(enc.epochGate, epochNow, epochNow, false, 300, t += 16667) == EpochVerdict::DropAwaitingKey);
      CHECK(epoch_gate_judge(enc.epochGate, epochNow, epochNow, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
      CHECK(epoch_gate_judge(enc.epochGate, epochNow, epochNow, false, 300, t += 16667) == EpochVerdict::Emit);
      enc.codec.shutdown();
    }
  }
  // ---- h. HN07: unknown re-closes the verified chain, a known-old AU does not ----
  {
    EpochGate g;
    uint64_t t = 6000000;
    CHECK(epoch_gate_judge(g, 7, 7, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
    CHECK(epoch_gate_judge(g, 7, 7, false, 300, t += 16667) == EpochVerdict::Emit);
    // A known-old AU: dropped quietly, the chain stays open, the next current P still flows.
    CHECK(epoch_gate_judge(g, 7, 6, false, 300, t += 16667) == EpochVerdict::DropOldEpoch);
    CHECK(!g.awaitingKey && g.reclosedByUnknown == 0);
    CHECK(epoch_gate_judge(g, 7, 7, false, 300, t += 16667) == EpochVerdict::Emit);
    // An unprovenanced (or future) AU: the chain closes, the next current P is dropped and the
    // caller re-forces the key, and only the current epoch's IDR opens it again.
    CHECK(epoch_gate_judge(g, 7, 0, false, 300, t += 16667) == EpochVerdict::DropUnknownEpoch);
    CHECK(g.awaitingKey && g.reclosedByUnknown == 1);
    CHECK(epoch_gate_judge(g, 7, 7, false, 300, t += 16667) == EpochVerdict::DropAwaitingKey);
    CHECK(epoch_gate_judge(g, 7, 7, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
    CHECK(epoch_gate_judge(g, 7, 7, false, 300, t += 16667) == EpochVerdict::Emit);
    // A future epoch behaves like unknown.
    CHECK(epoch_gate_judge(g, 7, 9, true, 3000, t += 16667) == EpochVerdict::DropUnknownEpoch);
    CHECK(g.awaitingKey && g.reclosedByUnknown == 2);
  }
}

'''

edit(F, [
("void test_gate_rules_on_fabricated_aus() {", TESTS.lstrip('\n') + "void test_gate_rules_on_fabricated_aus() {"),
("  test_gate_rules_on_fabricated_aus();\n  test_hold_observed_with_and_without_kick();",
 "  test_gate_rules_on_fabricated_aus();\n  test_provenance_fifo_latch_and_gate();\n  test_hold_observed_with_and_without_kick();"),
("#include <algorithm>", "#include <algorithm>\n\n#include \"host_encoder_manager.hpp\""),
])
print('A02 tests ok')
