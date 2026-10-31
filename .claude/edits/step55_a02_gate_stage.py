import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'

# --- gate: provenance-invalid state + HN07 re-close on unknown/future only ---------------------
edit(S + r'\host_epoch_gate.hpp', [
("  uint64_t epoch = 0;          // the epoch the gate is judging for\n  bool awaitingKey = false;    // true from the flush until the first current-epoch keyframe\n",
 "  uint64_t epoch = 0;          // the epoch the gate is judging for\n  bool awaitingKey = false;    // true from the flush until the first current-epoch keyframe\n"
 "  // A06: the encoder lost input provenance (its accepted-input FIFO overflowed), so no output\n"
 "  // may be trusted -- not even one that looks like the current epoch's keyframe. The stage sets\n"
 "  // this from H264Encoder::provenance_invalid() and clears it after a successful rebuild.\n"
 "  bool provenanceInvalid = false;\n"),
("  uint64_t resetsSuppressed = 0;\n  uint64_t epochsSeen = 0;\n};",
 "  uint64_t resetsSuppressed = 0;\n  uint64_t epochsSeen = 0;\n"
 "  uint64_t reclosedByUnknown = 0;    // HN07: verified chains closed again by an unprovenanced output\n"
 "  uint64_t droppedProvenanceInvalid = 0;  // A06: dropped because the encoder's provenance is void\n};"),
("inline EpochVerdict epoch_gate_judge(EpochGate& g, uint64_t epochNow, uint64_t auEpoch, bool auKey,\n"
 "                                     size_t auBytes, uint64_t nowUs) {\n"
 "  if (epochNow != g.epoch) {",
 "inline EpochVerdict epoch_gate_judge(EpochGate& g, uint64_t epochNow, uint64_t auEpoch, bool auKey,\n"
 "                                     size_t auBytes, uint64_t nowUs) {\n"
 "  if (g.provenanceInvalid && !g.awaitingKey) {\n"
 "    // A06: provenance is void from here until the encoder is rebuilt. Close the verified chain\n"
 "    // now, so nothing rides out on a tag the FIFO can no longer vouch for.\n"
 "    g.awaitingKey = true;\n"
 "    g.awaitSinceUs = nowUs;\n"
 "    g.awaitDropped = 0;\n"
 "  }\n"
 "  if (epochNow != g.epoch) {"),
("  if (auEpoch == epochNow) {\n    if (!g.awaitingKey) return EpochVerdict::Emit;\n    if (auKey) {",
 "  if (auEpoch == epochNow && !g.provenanceInvalid) {\n    if (!g.awaitingKey) return EpochVerdict::Emit;\n    if (auKey) {"),
("  g.droppedBytes += auBytes;\n  if (g.awaitingKey) ++g.awaitDropped;\n  if (auEpoch == 0 || auEpoch > epochNow) {\n    ++g.droppedUnknownEpoch;\n    return bound_exceeded() ? request_reset() : EpochVerdict::DropUnknownEpoch;\n  }",
 "  g.droppedBytes += auBytes;\n  if (g.awaitingKey) ++g.awaitDropped;\n"
 "  if (g.provenanceInvalid) {\n"
 "    // Everything is refused while the latch is up; the stage is rebuilding the encoder. The\n"
 "    // bound still applies, so a rebuild that never happens becomes a ResetEncoder request.\n"
 "    ++g.droppedProvenanceInvalid;\n"
 "    if (auEpoch == 0 || auEpoch > epochNow) ++g.droppedUnknownEpoch;\n"
 "    return bound_exceeded() ? request_reset() : EpochVerdict::DropUnknownEpoch;\n"
 "  }\n"
 "  if (auEpoch == 0 || auEpoch > epochNow) {\n    ++g.droppedUnknownEpoch;\n"
 "    if (!g.awaitingKey) {\n"
 "      // HN07: an output with no provenance (0) or an epoch this host never issued may belong to\n"
 "      // a chain the viewer cannot have. The verified chain is closed again and the caller forces\n"
 "      // a fresh IDR. A DropOldEpoch below deliberately does NOT do this: its provenance is\n"
 "      // certain, the current chain is still valid, and re-closing would only cost an extra IDR.\n"
 "      g.awaitingKey = true;\n"
 "      g.awaitSinceUs = nowUs;\n"
 "      g.awaitDropped = 1;\n"
 "      ++g.reclosedByUnknown;\n"
 "    }\n"
 "    return bound_exceeded() ? request_reset() : EpochVerdict::DropUnknownEpoch;\n  }"),
("/** After the caller rebuilt the encoder: the wait restarts with a fresh bound. */",
 "/**\n"
 " * Takes one rebuild from the gate's budget (kResetMaxPerWindow per kResetWindowUs), the same one\n"
 " * request_reset() uses, so a provenance rebuild cannot bypass it. False = spent for this window;\n"
 " * the caller keeps its request pending and retries on a later tick.\n"
 " */\n"
 "inline bool epoch_gate_take_reset_budget(EpochGate& g, uint64_t nowUs) {\n"
 "  if (g.resetWindowStartUs == 0 || nowUs >= g.resetWindowStartUs + EpochGate::kResetWindowUs) {\n"
 "    g.resetWindowStartUs = nowUs;\n"
 "    g.resetsInWindow = 0;\n"
 "  }\n"
 "  if (g.resetsInWindow >= EpochGate::kResetMaxPerWindow) {\n"
 "    ++g.resetsSuppressed;\n"
 "    return false;\n"
 "  }\n"
 "  ++g.resetsInWindow;\n"
 "  ++g.resetsRequested;\n"
 "  return true;\n"
 "}\n\n"
 "/** After the caller rebuilt the encoder: the wait restarts with a fresh bound. */"),
("  auto request_reset = [&]() -> EpochVerdict {\n"
 "    if (g.resetWindowStartUs == 0 || nowUs >= g.resetWindowStartUs + EpochGate::kResetWindowUs) {\n"
 "      g.resetWindowStartUs = nowUs;\n"
 "      g.resetsInWindow = 0;\n"
 "    }\n"
 "    if (g.resetsInWindow >= EpochGate::kResetMaxPerWindow) {\n"
 "      ++g.resetsSuppressed;\n"
 "      return EpochVerdict::DropAwaitingKey;  // keep dropping + re-forcing, no more rebuilds this window\n"
 "    }\n"
 "    ++g.resetsInWindow;\n"
 "    ++g.resetsRequested;\n"
 "    return EpochVerdict::ResetEncoder;\n"
 "  };",
 "  auto request_reset = [&]() -> EpochVerdict {\n"
 "    // Same budget as a provenance rebuild (epoch_gate_take_reset_budget), declared below.\n"
 "    if (g.resetWindowStartUs == 0 || nowUs >= g.resetWindowStartUs + EpochGate::kResetWindowUs) {\n"
 "      g.resetWindowStartUs = nowUs;\n"
 "      g.resetsInWindow = 0;\n"
 "    }\n"
 "    if (g.resetsInWindow >= EpochGate::kResetMaxPerWindow) {\n"
 "      ++g.resetsSuppressed;\n"
 "      return EpochVerdict::DropAwaitingKey;  // keep dropping + re-forcing, no more rebuilds this window\n"
 "    }\n"
 "    ++g.resetsInWindow;\n"
 "    ++g.resetsRequested;\n"
 "    return EpochVerdict::ResetEncoder;\n"
 "  };"),
])

# --- EncoderState: the pending provenance resync ------------------------------------------------
edit(S + r'\host_encoder_manager.hpp', [
("  EpochGate epochGate;",
 "  EpochGate epochGate;\n"
 "  // A02/A06: the codec latched \"provenance invalid\" and only a rebuild re-synchronises its input\n"
 "  // FIFO with what the MFT still holds. Stays true until one succeeds, so the retry does not\n"
 "  // depend on a new frame arriving (a static screen would never bring one).\n"
 "  bool provenanceResyncPending = false;\n"
 "  uint64_t provenanceResyncCount = 0;\n"
 "  uint64_t provenanceResyncFailed = 0;"),
])
print('A02 gate + encoder state ok')
