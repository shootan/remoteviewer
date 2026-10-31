import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'

# --- EncoderState: the rebuild itself, on the gate's budget -------------------------------------
edit(S + r'\host_encoder_manager.hpp', [
("  bool provenanceResyncPending = false;\n  uint64_t provenanceResyncCount = 0;\n  uint64_t provenanceResyncFailed = 0;",
 "  bool provenanceResyncPending = false;\n  uint64_t provenanceResyncCount = 0;\n  uint64_t provenanceResyncFailed = 0;\n"
 "  /**\n"
 "   * A02/A06: rebuilds the encoder because its accepted-input FIFO lost provenance. Only a\n"
 "   * rebuild helps -- clearing the FIFO alone would let the outputs the MFT still holds consume\n"
 "   * the tags of new inputs and desynchronise again -- and the codec's latch clears exactly on a\n"
 "   * successful initialize(). Takes one rebuild from the epoch gate's budget (3 per 10 s), so\n"
 "   * this cannot become a re-initialisation loop; when the budget is spent, or the rebuild fails,\n"
 "   * the request stays pending, the gate stays closed and the caller retries on a later tick\n"
 "   * (a static screen never brings a frame to retry on). Returns true when the encoder is new.\n"
 "   */\n"
 "  bool TryProvenanceResync(CaptureState& capture, uint64_t nowUs) {\n"
 "    if (!provenanceResyncPending) return false;\n"
 "    if (!epoch_gate_take_reset_budget(epochGate, nowUs)) return false;  // budget spent: stay pending\n"
 "    codec.shutdown();\n"
 "    if (!codec.initialize(activeEncodeW, activeEncodeH, activeFps, activeBitrate, activeKeyint)) {\n"
 "      ++provenanceResyncFailed;\n"
 "      return false;  // still invalid, still pending, gate still closed\n"
 "    }\n"
 "    ResetTimelineAnchors(capture);  // a new epoch: the gate re-judges and waits for its IDR\n"
 "    ResetStarvationEpisode();\n"
 "    forceKeySubmittedAtUs = 0;\n"
 "    ++resetCount;\n"
 "    ++provenanceResyncCount;\n"
 "    consecutiveStaleFrames = 0;\n"
 "    forceKeyNext = true;\n"
 "    epochGate.provenanceInvalid = false;\n"
 "    epoch_gate_note_reset(epochGate, nowUs);\n"
 "    provenanceResyncPending = false;\n"
 "    return true;\n"
 "  }"),
])

# --- stage: check right after every encode call, outside the AU batch ---------------------------
edit(S + r'\host_stage_encode_send_h264.cpp', [
("  if (units.empty()) return Flow::Continue;\n\n  stats.captureAgeSumUs += captureAgeAtCallbackUs;",
 "  // A02/A06 -- provenance. Checked here, right after the encode call and OUTSIDE the AU batch\n"
 "  // below, because the call that overflows the accepted-input FIFO often returns no AU at all:\n"
 "  // waiting for one would leave the gate open while the encoder emits AUs whose epoch tag is a\n"
 "  // newer input's. The gate closes now, and the rebuild that re-synchronises input and output\n"
 "  // is asked for on the gate's own budget (EncoderState::TryProvenanceResync). Until it succeeds\n"
 "  // every AU carries epoch 0 and the gate refuses it (DropUnknownEpoch), so nothing reaches the\n"
 "  // wire from an encoder whose output order is no longer described by anything.\n"
 "  if (encoder.codec.provenance_invalid() && !encoder.epochGate.provenanceInvalid) {\n"
 "    encoder.epochGate.provenanceInvalid = true;\n"
 "    encoder.provenanceResyncPending = true;\n"
 "    std::cout << \"[native-video-host] encoder provenance invalid (input FIFO overflow):\"\n"
 "              << \" pendingOverflow=\" << encodeStats.pendingInputOverflowTotal\n"
 "              << \" pendingDepth=\" << encodeStats.pendingInputDepth\n"
 "              << \" -> gate closed, encoder rebuild requested\\n\";\n"
 "  }\n"
 "  if (encoder.provenanceResyncPending) {\n"
 "    const uint64_t resyncNowUs = qpc_now_us();\n"
 "    if (encoder.TryProvenanceResync(capture, resyncNowUs)) {\n"
 "      std::cout << \"[native-video-host] encoder provenance resync ok resets=\" << encoder.provenanceResyncCount\n"
 "                << \" curEpoch=\" << capture.inputEpoch.load(std::memory_order_acquire)\n"
 "                << \" -> forcing a key\\n\";\n"
 "      units.clear();  // this call's AUs came from the encoder that is now gone\n"
 "    } else if ((encoder.provenanceResyncFailed % 30) == 1 || encoder.provenanceResyncFailed == 0) {\n"
 "      std::cout << \"[native-video-host] encoder provenance resync deferred (budget or init failure)\"\n"
 "                << \" failed=\" << encoder.provenanceResyncFailed\n"
 "                << \" resetsInWindow=\" << encoder.epochGate.resetsInWindow\n"
 "                << \" suppressed=\" << encoder.epochGate.resetsSuppressed << \"\\n\";\n"
 "    }\n"
 "  }\n\n"
 "  if (units.empty()) return Flow::Continue;\n\n  stats.captureAgeSumUs += captureAgeAtCallbackUs;"),
])

print('A02 stage ok')
