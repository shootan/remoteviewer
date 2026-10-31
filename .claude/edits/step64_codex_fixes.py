import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'

# ---- (1) A06 wiring: the provenance signal must survive every path out of the encode call ----
edit(S + r'\host_encoder_manager.hpp', [
("  bool provenanceResyncPending = false;\n  uint64_t provenanceResyncCount = 0;\n  uint64_t provenanceResyncFailed = 0;",
 "  bool provenanceResyncPending = false;\n  uint64_t provenanceResyncCount = 0;\n  uint64_t provenanceResyncFailed = 0;\n"
 "  uint64_t provenanceDeferLogUs = 0;  // last \"resync deferred\" line (time-throttled)\n"
 "  /**\n"
 "   * Records what the codec reported about its input provenance after an encode call -- whatever\n"
 "   * that call returned. An encode that fails LATE (the MFT accepted the input, then a drain or\n"
 "   * ProcessOutput failed) can be the very call that overflowed the FIFO, and the stage returns\n"
 "   * early on failure: without this the gate would stay open and the rebuild would never be\n"
 "   * asked for, because the tick retry only looks at `provenanceResyncPending`. Idempotent.\n"
 "   * Returns true the first time it latches, so the caller can log it once.\n"
 "   */\n"
 "  bool NoteProvenance(bool codecProvenanceInvalid) {\n"
 "    if (!codecProvenanceInvalid || epochGate.provenanceInvalid) return false;\n"
 "    epochGate.provenanceInvalid = true;\n"
 "    provenanceResyncPending = true;\n"
 "    return true;\n"
 "  }"),
])
edit(S + r'\host_stage_encode_send_h264.cpp', [
# surface path rejection
("        encoder.surfaceEncodeHealthy = false;\n"
 "        res.captureReadback.SetNv12Enabled(false);\n"
 "        std::cout << \"[native-video-host] nv12 surface encode rejected backend=\"\n"
 "                  << encoder.codec.backend_name() << \"; falling back to cpu nv12" + NL + "\";\n"
 "        return Flow::Continue;",
 "        encoder.surfaceEncodeHealthy = false;\n"
 "        res.captureReadback.SetNv12Enabled(false);\n"
 "        std::cout << \"[native-video-host] nv12 surface encode rejected backend=\"\n"
 "                  << encoder.codec.backend_name() << \"; falling back to cpu nv12" + NL + "\";\n"
 "        // A06: this call may still have overflowed the accepted-input FIFO before it failed.\n"
 "        if (encoder.NoteProvenance(encoder.codec.provenance_invalid())) {\n"
 "          std::cout << \"[native-video-host] encoder provenance invalid (surface encode rejected)\"\n"
 "                    << \" -> gate closed, encoder rebuild requested" + NL + "\";\n"
 "        }\n"
 "        return Flow::Continue;"),
# bgra path failure
("    ++encoder.encodeFailCount;\n"
 "    if ((encoder.encodeFailCount % 60) == 1) {\n"
 "      std::cout << \"[native-video-host] encode failed count=\" << encoder.encodeFailCount << \"" + NL + "\";\n"
 "    }\n"
 "    return Flow::Continue;\n  }",
 "    ++encoder.encodeFailCount;\n"
 "    if ((encoder.encodeFailCount % 60) == 1) {\n"
 "      std::cout << \"[native-video-host] encode failed count=\" << encoder.encodeFailCount << \"" + NL + "\";\n"
 "    }\n"
 "    // A06: an encode can fail AFTER the MFT accepted the input -- that call may be the one that\n"
 "    // overflowed the FIFO. Record it before leaving, or the tick retry never sees a pending\n"
 "    // request and the gate stays open on an encoder whose output order is undescribed.\n"
 "    if (encoder.NoteProvenance(encoder.codec.provenance_invalid())) {\n"
 "      std::cout << \"[native-video-host] encoder provenance invalid (encode failed):\"\n"
 "                << \" pendingOverflow=\" << encodeStats.pendingInputOverflowTotal\n"
 "                << \" -> gate closed, encoder rebuild requested" + NL + "\";\n"
 "    }\n"
 "    return Flow::Continue;\n  }"),
# main site: use the same helper, and throttle the deferred line by time (3)
("  if (encoder.codec.provenance_invalid() && !encoder.epochGate.provenanceInvalid) {\n"
 "    encoder.epochGate.provenanceInvalid = true;\n"
 "    encoder.provenanceResyncPending = true;\n"
 "    std::cout << \"[native-video-host] encoder provenance invalid (input FIFO overflow):\"\n"
 "              << \" pendingOverflow=\" << encodeStats.pendingInputOverflowTotal\n"
 "              << \" pendingDepth=\" << encodeStats.pendingInputDepth\n"
 "              << \" -> gate closed, encoder rebuild requested" + NL + "\";\n"
 "  }",
 "  if (encoder.NoteProvenance(encoder.codec.provenance_invalid())) {\n"
 "    std::cout << \"[native-video-host] encoder provenance invalid (input FIFO overflow):\"\n"
 "              << \" pendingOverflow=\" << encodeStats.pendingInputOverflowTotal\n"
 "              << \" pendingDepth=\" << encodeStats.pendingInputDepth\n"
 "              << \" -> gate closed, encoder rebuild requested" + NL + "\";\n"
 "  }"),
("    } else if ((encoder.provenanceResyncFailed % 30) == 1 || encoder.provenanceResyncFailed == 0) {\n"
 "      std::cout << \"[native-video-host] encoder provenance resync deferred (budget or init failure)\"\n"
 "                << \" failed=\" << encoder.provenanceResyncFailed\n"
 "                << \" resetsInWindow=\" << encoder.epochGate.resetsInWindow\n"
 "                << \" suppressed=\" << encoder.epochGate.resetsSuppressed << \"" + NL + "\";\n"
 "    }",
 "    } else if (encoder.provenanceDeferLogUs == 0 ||\n"
 "               resyncNowUs >= encoder.provenanceDeferLogUs + 1'000'000) {\n"
 "      // Time-throttled: a spent budget does not touch provenanceResyncFailed, so a count-based\n"
 "      // throttle printed this on every frame while the window ran out.\n"
 "      encoder.provenanceDeferLogUs = resyncNowUs;\n"
 "      std::cout << \"[native-video-host] encoder provenance resync deferred (budget or init failure)\"\n"
 "                << \" failed=\" << encoder.provenanceResyncFailed\n"
 "                << \" resetsInWindow=\" << encoder.epochGate.resetsInWindow\n"
 "                << \" suppressed=\" << encoder.epochGate.resetsSuppressed << \"" + NL + "\";\n"
 "    }"),
])

# ---- (2) tombstone: retire on a meaningful boundary, not on a clock ---------------------------
edit(S + r'\native_video_client_shared_core.hpp', [
("  // The identity is remembered (a tombstone), because the host may still be sending that AU's\n"
 "  // chunks: without it the next late chunk re-creates the very assembly that was just abandoned\n"
 "  // and blocks the head again, so the caller gives the same AU up over and over (measured: the\n"
 "  // same seq abandoned 3 times, and the good AU behind it never delivered). `nowUs` stamps the\n"
 "  // tombstone; 0 (the legacy overload's clock) means it only ages out by count.\n"
 "  bool GiveUpIncomplete(uint64_t generation, uint32_t seq, uint64_t nowUs = 0);\n"
 "  // True while (generation, seq) is tombstoned: its chunks are ignored as stale traffic.\n"
 "  bool IsAbandoned(uint64_t generation, uint32_t seq, uint64_t nowUs = 0) const;\n",
 "  // The identity is remembered (a tombstone), because the host may still be sending that AU's\n"
 "  // chunks: without it the next late chunk re-creates the very assembly that was just abandoned\n"
 "  // and blocks the head again, so the caller gives the same AU up over and over (measured: the\n"
 "  // same seq abandoned 3 times, and the good AU behind it never delivered). `nowUs` only stamps\n"
 "  // the record for diagnostics -- retirement is by boundary, not by clock (see abandoned_).\n"
 "  bool GiveUpIncomplete(uint64_t generation, uint32_t seq, uint64_t nowUs = 0);\n"
 "  // True while (generation, seq) is tombstoned: its chunks are ignored as stale traffic.\n"
 "  bool IsAbandoned(uint64_t generation, uint32_t seq) const;\n"
 "  size_t AbandonedCount() const { return abandoned_.size(); }\n"),
("  // A04: identities given up on. Deliberately NOT expressed by moving lastDeliveredSeq_ forward:\n"
 "  // that would claim the AU was delivered, change what the next delivery reports as a sequence\n"
 "  // gap (and with it the IDR recovery contract), and swallow AUs that were never judged. A\n"
 "  // tombstone blocks exactly the judged (generation, seq) and nothing else -- another generation\n"
 "  // restarts the seq space and never matches one. Bounded both ways: at most kAbandonedMax\n"
 "  // entries (oldest dropped first) and kAbandonedTtlUs of age when the caller passes a clock, so\n"
 "  // a long session cannot accumulate them; Reset() (a new session / decoder resync) clears them.\n"
 "  struct AbandonedAu {\n"
 "    uint64_t generation = 0;\n"
 "    uint32_t seq = 0;\n"
 "    uint64_t atUs = 0;  // 0 = no clock: ages out by count only\n"
 "  };\n"
 "  static constexpr size_t kAbandonedMax = 16;\n"
 "  static constexpr uint64_t kAbandonedTtlUs = 5'000'000;\n"
 "  std::deque<AbandonedAu> abandoned_;\n",
 "  // A04: identities given up on. Deliberately NOT expressed by moving lastDeliveredSeq_ forward:\n"
 "  // that would claim the AU was delivered, change what the next delivery reports as a sequence\n"
 "  // gap (and with it the IDR recovery contract), and swallow AUs that were never judged. A\n"
 "  // tombstone blocks exactly the judged (generation, seq) and nothing else -- another generation\n"
 "  // restarts the seq space and never matches one.\n"
 "  //\n"
 "  // Retirement is by BOUNDARY, not by a clock: a host that keeps resending the abandoned AU for\n"
 "  // longer than any timeout would otherwise get it re-assembled and abandoned a second time. An\n"
 "  // entry is retired when\n"
 "  //   - delivery has moved past it in its own generation (`lastDeliveredSeq_` is at or beyond it):\n"
 "  //     the ordinary stale guard covers those chunks from then on, so the tombstone is redundant;\n"
 "  //   - a delivery of a DIFFERENT generation happens: the old seq space is gone and its entries\n"
 "  //     can never match a packet again;\n"
 "  //   - Reset() (a new session / decoder resync);\n"
 "  //   - or the capacity bound is reached: kAbandonedMax identities may be outstanding with no\n"
 "  //     delivery at all between them, and the oldest is dropped. That is a real (bounded) hole --\n"
 "  //     16 heads abandoned before a single AU gets through -- and it is preferred to unbounded\n"
 "  //     growth; the give-up rule itself is what keeps that number small.\n"
 "  struct AbandonedAu {\n"
 "    uint64_t generation = 0;\n"
 "    uint32_t seq = 0;\n"
 "    uint64_t atUs = 0;  // diagnostics only\n"
 "  };\n"
 "  static constexpr size_t kAbandonedMax = 16;\n"
 "  std::deque<AbandonedAu> abandoned_;\n"),
])
edit(S + r'\native_video_client_shared_core.cpp', [
("bool UdpH264FrameAssembler::IsAbandoned(uint64_t generation, uint32_t seq, uint64_t nowUs) const {\n"
 "  for (const AbandonedAu& a : abandoned_) {\n"
 "    if (a.seq != seq || a.generation != generation) continue;\n"
 "    if (nowUs != 0 && a.atUs != 0 && nowUs > a.atUs + kAbandonedTtlUs) return false;  // aged out\n"
 "    return true;\n"
 "  }\n"
 "  return false;\n"
 "}",
 "bool UdpH264FrameAssembler::IsAbandoned(uint64_t generation, uint32_t seq) const {\n"
 "  for (const AbandonedAu& a : abandoned_) {\n"
 "    if (a.seq == seq && a.generation == generation) return true;\n"
 "  }\n"
 "  return false;\n"
 "}"),
("  if (IsAbandoned(packet.streamGeneration, packet.seq, nowUs)) {",
 "  if (IsAbandoned(packet.streamGeneration, packet.seq)) {"),
("  deliveredAny_ = true;\n  lastDeliveredSeq_ = assembly.seq;",
 "  deliveredAny_ = true;\n  lastDeliveredSeq_ = assembly.seq;\n"
 "  // Retire the tombstones this delivery makes redundant (see the header note): everything the\n"
 "  // stale guard now covers in this generation, and everything from any other generation.\n"
 "  {\n"
 "    const uint64_t deliveredGeneration = assembly.header.streamGeneration;\n"
 "    const uint32_t deliveredSeq = assembly.seq;\n"
 "    abandoned_.erase(std::remove_if(abandoned_.begin(), abandoned_.end(),\n"
 "                                    [&](const AbandonedAu& a) {\n"
 "                                      if (a.generation != deliveredGeneration) return true;\n"
 "                                      return !sequence_is_newer(a.seq, deliveredSeq);\n"
 "                                    }),\n"
 "                     abandoned_.end());\n"
 "  }"),
])
print('Codex fixes (wiring, tombstone boundary, log throttle) ok')
