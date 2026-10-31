import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'

FIFO = '''// One entry per input the encoder's MFT accepted, in order (A02). An asynchronous MFT answers a
// call with an OLDER input's access unit, so every output is stamped from the front of this queue
// rather than from the current call: the capture timestamp the viewer paces on, the synthetic flag
// the wire carries, and the flush epoch the emit gate judges (host_epoch_gate.hpp). The three used
// to be three parallel deques and one path (an output with no usable bytes) popped only two of
// them, which shifted every later AU's epoch by one for the rest of the session -- so they are one
// record now and every path moves them together.
struct PendingInput {
  int64_t tsHns = 0;
  bool synthetic = false;
  uint64_t epoch = 0;
};

// The FIFO itself, separate from the codec so the rules are testable without an MFT.
//
// Overflow (A06): 64 accepted inputs with no output means the queue no longer describes what the
// MFT will emit. Dropping the oldest entry would silently re-label later outputs with a NEWER
// input's provenance -- including its epoch, which can open the emit gate for a pre-flush picture.
// Instead the FIFO latches `provenance invalid`: it empties, every later output is stamped epoch 0
// (unknown -> the gate fails closed), and only a successful encoder rebuild (Reset, from
// H264Encoder::initialize) clears the latch, because only a rebuild also empties what the MFT is
// still holding. Clear() -- the shutdown half of a rebuild -- deliberately keeps the latch.
class PendingInputFifo {
 public:
  static constexpr size_t kMaxEntries = 64;

  void Push(const PendingInput& in) {
    entries_.push_back(in);
    if (entries_.size() <= kMaxEntries) return;
    ++overflowTotal_;
    ++fallbackTotal_;
    provenanceInvalid_ = true;
    entries_.clear();
  }

  // An output that carries bytes: takes the provenance of the input that produced it. False when
  // the queue is empty (no provenance: the caller stamps epoch 0 and counts a fallback).
  bool Pop(PendingInput* out) {
    if (entries_.empty()) {
      ++fallbackTotal_;
      return false;
    }
    if (out) *out = entries_.front();
    entries_.pop_front();
    return true;
  }

  // An output with no usable bytes: the MFT consumed an input for it all the same, so its entry
  // goes too -- all three fields at once (the 0.2.97 fix, and the epoch the pre-A02 code forgot).
  void PopForEmptyOutput() {
    if (entries_.empty()) return;
    entries_.pop_front();
    ++fallbackTotal_;
  }

  // True while no output may be trusted with provenance (see the overflow note above).
  bool provenance_invalid() const { return provenanceInvalid_; }
  size_t size() const { return entries_.size(); }
  uint64_t overflow_total() const { return overflowTotal_; }
  uint64_t fallback_total() const { return fallbackTotal_; }
  void NoteFallback() { ++fallbackTotal_; }

  // shutdown(): the queue is meaningless without the MFT, but the latch survives -- a rebuild is
  // only complete when initialize() succeeds.
  void Clear() { entries_.clear(); }
  // A successful initialize(): a fresh MFT holds nothing, so provenance starts trustworthy again.
  void Reset() {
    entries_.clear();
    provenanceInvalid_ = false;
  }

 private:
  std::deque<PendingInput> entries_;
  bool provenanceInvalid_ = false;
  uint64_t overflowTotal_ = 0;
  uint64_t fallbackTotal_ = 0;
};

'''

edit(S + r'\mf_h264_codec.hpp', [
("class H264Encoder {\n public:\n  H264Encoder() = default;", FIFO + "class H264Encoder {\n public:\n  H264Encoder() = default;"),
("  uint32_t pendingInputDepth = 0;\n  uint64_t pendingInputOverflowTotal = 0;\n  uint8_t asyncEnabled = 0;\n};",
 "  uint32_t pendingInputDepth = 0;\n  uint64_t pendingInputOverflowTotal = 0;\n"
 "  // A06: the accepted-input FIFO overflowed and no output carries trustworthy provenance until\n"
 "  // the encoder is rebuilt. The stage closes the emit gate and asks for that rebuild.\n"
 "  uint8_t provenanceInvalid = 0;\n"
 "  uint8_t asyncEnabled = 0;\n};"),
("  // Flush epoch of the NEXT input (CaptureState::inputEpoch); same FIFO, same reason. (P11)\n  void set_next_input_epoch(uint64_t epoch) { nextInputEpoch_ = epoch; }\n  void shutdown();",
 "  // Flush epoch of the NEXT input (CaptureState::inputEpoch); same FIFO, same reason. (P11)\n  void set_next_input_epoch(uint64_t epoch) { nextInputEpoch_ = epoch; }\n"
 "  // A06: the accepted-input FIFO overflowed, so nothing this encoder emits may be trusted with\n"
 "  // provenance (every AU goes out tagged epoch 0, which the emit gate refuses). Only a rebuild --\n"
 "  // shutdown() followed by a successful initialize() -- clears it.\n"
 "  bool provenance_invalid() const { return pendingInputs_.provenance_invalid(); }\n"
 "  void shutdown();"),
("  std::deque<int64_t> pendingInputSampleTimesHns_;\n  std::deque<bool> pendingInputSynthetic_;  // lockstep with pendingInputSampleTimesHns_ (0.2.97)\n  bool nextInputSynthetic_ = false;\n  std::deque<uint64_t> pendingInputEpoch_;  // lockstep too (P11)\n  uint64_t nextInputEpoch_ = 0;",
 "  PendingInputFifo pendingInputs_;\n  bool nextInputSynthetic_ = false;\n  uint64_t nextInputEpoch_ = 0;"),
])

edit(S + r'\mf_h264_codec.cpp', [
# empty output: one record, all fields
("      if (!haveBytes && produced && !pendingInputSampleTimesHns_.empty()) {\n"
 "        // The MFT consumed an input to produce this output even though nothing usable came\n"
 "        // back. Leaving its timestamp in the queue would offset every later access unit by a\n"
 "        // frame, permanently, and the client paces playout off those timestamps.\n"
 "        pendingInputSampleTimesHns_.pop_front();\n"
 "        if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();\n"
 "        ++sampleTimeOutputTimestampFallbackCount_;\n"
 "      }",
 "      if (!haveBytes && produced) {\n"
 "        // The MFT consumed an input to produce this output even though nothing usable came\n"
 "        // back. Leaving its entry in the queue would offset every later access unit by a frame,\n"
 "        // permanently -- the client paces playout off those timestamps, and the emit gate judges\n"
 "        // the epoch that rides with them (the pre-A02 code popped the timestamp and the synthetic\n"
 "        // flag here but not the epoch, so every later AU carried the previous input's epoch: the\n"
 "        // IDR forced by a flush was then judged old and dropped).\n"
 "        pendingInputs_.PopForEmptyOutput();\n"
 "        ++sampleTimeOutputTimestampFallbackCount_;\n"
 "      }"),
# normal pop
("        uint64_t auEpoch = 0;\n"
 "        if (!pendingInputSampleTimesHns_.empty()) {\n"
 "          normalizedAuSampleTimeHns = pendingInputSampleTimesHns_.front();\n"
 "          if (!pendingInputSynthetic_.empty()) auSynthetic = pendingInputSynthetic_.front();\n"
 "          if (!pendingInputEpoch_.empty()) auEpoch = pendingInputEpoch_.front();\n"
 "          pendingInputSampleTimesHns_.pop_front();\n"
 "          if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();\n"
 "          if (!pendingInputEpoch_.empty()) pendingInputEpoch_.pop_front();\n"
 "        } else {\n"
 "          ++sampleTimeOutputTimestampFallbackCount_;\n"
 "        }",
 "        uint64_t auEpoch = 0;\n"
 "        PendingInput provenance{};\n"
 "        if (pendingInputs_.Pop(&provenance)) {\n"
 "          normalizedAuSampleTimeHns = provenance.tsHns;\n"
 "          auSynthetic = provenance.synthetic;\n"
 "          auEpoch = provenance.epoch;\n"
 "        } else {\n"
 "          ++sampleTimeOutputTimestampFallbackCount_;\n"
 "        }\n"
 "        // A06: after an overflow the queue no longer describes what the MFT holds, so no output\n"
 "        // may claim an epoch until the encoder is rebuilt -- 0 is \"unknown\" and the gate refuses it.\n"
 "        if (pendingInputs_.provenance_invalid()) auEpoch = 0;"),
# push + overflow latch
("  pendingInputSampleTimesHns_.push_back(sampleTime);\n"
 "  pendingInputSynthetic_.push_back(nextInputSynthetic_);\n"
 "  pendingInputEpoch_.push_back(nextInputEpoch_);\n"
 "  constexpr size_t kPendingEncoderTimestampMax = 64;\n"
 "  if (pendingInputSampleTimesHns_.size() > kPendingEncoderTimestampMax) {\n"
 "    pendingInputSampleTimesHns_.pop_front();\n"
 "    if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();\n"
 "    if (!pendingInputEpoch_.empty()) pendingInputEpoch_.pop_front();\n"
 "    ++sampleTimeOutputTimestampFallbackCount_;\n"
 "    ++pendingInputOverflowTotal_;\n"
 "    if (env_truthy_local(\"REMOTE60_NATIVE_DEBUG_CODEC\")) {\n"
 "      codec_debug_log(\"encoder pending input timestamp queue overflowed\");\n"
 "    }\n"
 "  }",
 "  const bool overflowedBefore = pendingInputs_.provenance_invalid();\n"
 "  pendingInputs_.Push(PendingInput{sampleTime, nextInputSynthetic_, nextInputEpoch_});\n"
 "  if (pendingInputs_.provenance_invalid() && !overflowedBefore) {\n"
 "    // A06: 64 accepted inputs with no output. The FIFO has latched itself invalid and emptied;\n"
 "    // from here every AU is tagged epoch 0 until the stage rebuilds this encoder.\n"
 "    ++sampleTimeOutputTimestampFallbackCount_;\n"
 "    ++pendingInputOverflowTotal_;\n"
 "    codec_debug_log(\"encoder pending input FIFO overflowed: provenance invalid until rebuild\");\n"
 "  }"),
# stats
("    encodeStats->pendingInputDepth = static_cast<uint32_t>(pendingInputSampleTimesHns_.size());\n    encodeStats->pendingInputOverflowTotal = pendingInputOverflowTotal_;",
 "    encodeStats->pendingInputDepth = static_cast<uint32_t>(pendingInputs_.size());\n    encodeStats->pendingInputOverflowTotal = pendingInputOverflowTotal_;\n    encodeStats->provenanceInvalid = pendingInputs_.provenance_invalid() ? 1u : 0u;"),
# initialize: a fresh MFT holds nothing -> the latch clears here and only here
("  sampleTimeOutputTimestampTotalSamples_ = 0;\n  sampleTimeOutputTimestampFallbackCount_ = 0;\n  pendingInputSampleTimesHns_.clear();\n  pendingInputSynthetic_.clear();\n  pendingInputEpoch_.clear();\n  frameIndex_ = 0;\n  sequenceHeaderAnnexb_.clear();\n\n  Microsoft::WRL::ComPtr<IMFMediaType> outType;",
 "  sampleTimeOutputTimestampTotalSamples_ = 0;\n  sampleTimeOutputTimestampFallbackCount_ = 0;\n  pendingInputs_.Reset();  // a fresh MFT holds nothing: provenance is trustworthy again (A06)\n  frameIndex_ = 0;\n  sequenceHeaderAnnexb_.clear();\n\n  Microsoft::WRL::ComPtr<IMFMediaType> outType;"),
# shutdown: queue goes, latch stays until initialize succeeds
("  started_ = false;\n  sampleTimeOutputTimestampTotalSamples_ = 0;\n  sampleTimeOutputTimestampFallbackCount_ = 0;\n  pendingInputSampleTimesHns_.clear();\n  pendingInputSynthetic_.clear();\n  pendingInputEpoch_.clear();\n  frameIndex_ = 0;",
 "  started_ = false;\n  sampleTimeOutputTimestampTotalSamples_ = 0;\n  sampleTimeOutputTimestampFallbackCount_ = 0;\n  pendingInputs_.Clear();  // the latch (A06) survives: only a successful initialize() clears it\n  frameIndex_ = 0;"),
])
print('A02 codec ok')
