import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'

# --- assembler: per-AU progress stamp, exact-identity give-up -------------------------------
edit(S + r'\native_video_client_shared_core.hpp', [
("  // Gives up the incomplete assembly `seq`/`generation` and anything older: its repair is over\n"
 "  // (the caller's NACK rounds are spent or the hold is past). The next delivery reports the seq\n"
 "  // gap exactly as an expired hold does. False when no such incomplete assembly is held.\n"
 "  bool DropIncomplete(uint32_t seq, uint64_t generation);\n",
 "  // Gives up exactly the incomplete assembly (generation, seq) the caller judged -- never an\n"
 "  // \"oldest\" by arrival or sequence order -- because its repair is over (A04 rule). False, and\n"
 "  // nothing removed, when it is not held or completed meanwhile (the give-up is cancelled). The\n"
 "  // next delivery reports the seq gap exactly as an expired hold does.\n"
 "  bool GiveUpIncomplete(uint64_t generation, uint32_t seq);\n"),
("    bool keyFrame = false;  // repairing the keyframe is allowed even while waiting for a keyframe\n    uint64_t firstPacketUs = 0;\n  };",
 "    bool keyFrame = false;  // repairing the keyframe is allowed even while waiting for a keyframe\n    uint64_t firstPacketUs = 0;\n"
 "    // Last time this AU made progress: a NEW data chunk arrived or FEC recovered one. A duplicate\n"
 "    // chunk, a parity packet that repairs nothing, control traffic or another AU do not count.\n"
 "    uint64_t lastProgressUs = 0;\n  };"),
("    uint64_t firstPacketUs = 0;\n    bool complete = false;\n    uint32_t fecRecoveredChunks = 0;\n  };",
 "    uint64_t firstPacketUs = 0;\n    uint64_t lastProgressUs = 0;  // see IncompleteAuInfo::lastProgressUs\n    bool complete = false;\n    uint32_t fecRecoveredChunks = 0;\n  };"),
])
edit(S + r'\native_video_client_shared_core.cpp', [
("bool UdpH264FrameAssembler::DropIncomplete(uint32_t seq, uint64_t generation) {\n"
 "  const bool held = std::any_of(assemblies_.begin(), assemblies_.end(), [&](const Assembly& a) {\n"
 "    return a.seq == seq && a.header.streamGeneration == generation && !a.complete;\n"
 "  });\n"
 "  if (!held) return false;\n"
 "  assemblies_.erase(std::remove_if(assemblies_.begin(), assemblies_.end(),\n"
 "                                   [&](const Assembly& a) { return !sequence_is_newer(a.seq, seq); }),\n"
 "                    assemblies_.end());\n"
 "  return true;\n"
 "}\n",
 "bool UdpH264FrameAssembler::GiveUpIncomplete(uint64_t generation, uint32_t seq) {\n"
 "  auto it = std::find_if(assemblies_.begin(), assemblies_.end(), [&](const Assembly& a) {\n"
 "    return a.seq == seq && a.header.streamGeneration == generation;\n"
 "  });\n"
 "  if (it == assemblies_.end() || it->complete) return false;  // gone, or completed meanwhile: cancelled\n"
 "  assemblies_.erase(it);\n"
 "  return true;\n"
 "}\n"),
("      info->keyFrame = (a.header.flags & kEncodedFrameFlagKeyFrame) != 0;\n      info->firstPacketUs = a.firstPacketUs;\n",
 "      info->keyFrame = (a.header.flags & kEncodedFrameFlagKeyFrame) != 0;\n      info->firstPacketUs = a.firstPacketUs;\n      info->lastProgressUs = a.lastProgressUs;\n"),
("    created.firstPacketUs = nowUs;\n",
 "    created.firstPacketUs = nowUs;\n    created.lastProgressUs = nowUs;\n"),
("    assembly.received[packet.chunkIndex] = 1;\n    ++assembly.receivedCount;\n",
 "    assembly.received[packet.chunkIndex] = 1;\n    ++assembly.receivedCount;\n    assembly.lastProgressUs = nowUs;  // a new data chunk is progress; a duplicate (above) is not\n"),
("    assembly.received[missingIndex] = 1;\n    ++assembly.receivedCount;\n",
 "    assembly.received[missingIndex] = 1;\n    ++assembly.receivedCount;\n    assembly.lastProgressUs = nowUs;  // an FEC-recovered chunk is progress too\n"),
])

# --- NACK scheduler: "spent through the last applicable phase" ------------------------------
edit(S + r'\udp_video_nack.hpp', [
("  // Every round for the chased AU has been spent and nothing completed it: the keyframe path is\n"
 "  // expected to take over (see busy()). last_sent_us() lets the caller wait one more round for\n"
 "  // the final retransmit before it gives the AU up.\n"
 "  bool exhausted() const { return seq_ != 0 && rounds_ >= cfg_.maxRounds; }\n"
 "  uint64_t last_sent_us() const { return lastUs_; }\n",
 "  // The chased AU has used every round of its LAST applicable phase: the tail rounds when the\n"
 "  // loss includes a tail (`hasTail`, from the assembler's missing list vs highWater), else the\n"
 "  // hole rounds. Hole rounds spent while a tail phase is still to come are not \"spent\" (the\n"
 "  // tail phase restarts the budget). last_sent_us() is the last NACK actually sent (0 = none), so\n"
 "  // the caller can leave one reply allowance after it before giving the AU up (A04 rule).\n"
 "  bool spent_for(bool hasTail) const { return seq_ != 0 && rounds_ >= cfg_.maxRounds && (tailPhase_ || !hasTail); }\n"
 "  uint64_t last_sent_us() const { return lastUs_; }\n"),
])

# --- control state: the control thread's RTT, readable by the receive thread ------------------
edit(S + r'\viewer_control_state.hpp', [
("  std::atomic<bool> overUdp{false};\n",
 "  std::atomic<bool> overUdp{false};\n"
 "  // cross-thread: control writes after every pong, recv reads (the A04 give-up's reply allowance\n"
 "  // is 2 x this RTT, when fresh). 0 = no pong yet.\n"
 "  std::atomic<uint64_t> lastRttUs{0};\n"
 "  std::atomic<uint64_t> lastRttAtUs{0};  // qpc of the pong that measured it\n"),
])
edit(S + r'\viewer_control_client.cpp', [
("  const uint64_t rttUs =\n      (doneUs >= action.ping.clientSendQpcUs) ? (doneUs - action.ping.clientSendQpcUs) : 0;\n",
 "  const uint64_t rttUs =\n      (doneUs >= action.ping.clientSendQpcUs) ? (doneUs - action.ping.clientSendQpcUs) : 0;\n"
 "  ctx.control.lastRttUs.store(rttUs, std::memory_order_relaxed);\n"
 "  ctx.control.lastRttAtUs.store(doneUs, std::memory_order_relaxed);\n"),
])

# --- receiver options: the give-up rule's tunables --------------------------------------------
edit(S + r'\viewer_video_receiver.hpp', [
("  struct NackOptions {\n    bool enabled = false;\n    uint64_t holdUs = 0;\n  };",
 "  struct NackOptions {\n    bool enabled = false;\n    uint64_t holdUs = 0;\n"
 "    // A04 give-up rule (viewer_video_receiver.cpp maintenance): a stuck incomplete head with no\n"
 "    // complete AU behind it is given up once every repair avenue had its chance. replyAllowance =\n"
 "    // clamp(max(replyAllowanceMinUs, 2 x recent control RTT), replyAllowanceMaxUs), the minimum\n"
 "    // when the RTT is unknown or older than rttStaleUs. giveUpHardCapUs is a failure budget for\n"
 "    // an AU that keeps making (useless) progress, never less than terminalMin.\n"
 "    uint64_t replyAllowanceMinUs = 50000;\n"
 "    uint64_t replyAllowanceMaxUs = 1000000;\n"
 "    uint64_t rttStaleUs = 5000000;\n"
 "    uint64_t giveUpHardCapUs = 5000000;\n"
 "  };"),
])
edit(S + r'\viewer_startup.cpp', [
("  nack.enabled = ctx.session.hostSupportsNack;\n  nack.holdUs = ctx.videoNackHoldUs;\n  ctx.receiver.emplace(",
 "  nack.enabled = ctx.session.hostSupportsNack;\n  nack.holdUs = ctx.videoNackHoldUs;\n"
 "  nack.giveUpHardCapUs = static_cast<uint64_t>(env_u32_clamped(\"REMOTE60_NATIVE_VIDEO_GIVEUP_HARDCAP_MS\", 5000, 300, 60000)) * 1000ULL;\n"
 "  nack.replyAllowanceMaxUs = static_cast<uint64_t>(env_u32_clamped(\"REMOTE60_NATIVE_VIDEO_REPLY_ALLOWANCE_MAX_MS\", 1000, 50, 5000)) * 1000ULL;\n"
 "  ctx.receiver.emplace("),
])

# --- receiver: replace the interim give-up with the confirmed rule ----------------------------
OLD_N3 = '''    if (holdEnabled && !gate.waitForKeyFrame && !assembler.AnyComplete()) {
      UdpH264FrameAssembler::IncompleteAuInfo info{};
      if (assembler.OldestIncomplete(nullptr, 0, &info)) {
        const bool chased = nackScheduler.current_seq() == info.seq &&
                            nackScheduler.current_generation() == info.generation;
        // One round after the last request either way, so the final retransmit gets its chance.
        const bool lastRoundOver = !chased || nackScheduler.last_sent_us() == 0 ||
                                   nowUs >= nackScheduler.last_sent_us() + nackScheduler.config().roundUs;
        const bool roundsSpent = chased && nackScheduler.exhausted() && lastRoundOver;
        const bool holdPast = info.firstPacketUs != 0 && nowUs >= info.firstPacketUs + nack.holdUs &&
                              !nackScheduler.busy() && lastRoundOver;
        if (roundsSpent || holdPast) {
          (void)assembler.DropIncomplete(info.seq, info.generation);
          nackScheduler.Reset();
          ++assemblyDropped;
          ++st.udpAssemblyDroppedCount;
          ++stuckHeadGiveUps;
          if (stuckHeadGiveUps <= 5 || (stuckHeadGiveUps % 100) == 1) {
            std::cout << "[native-video-client] stuck head given up seq=" << info.seq
                      << " gen=" << info.generation << " missing=" << info.missingTotal
                      << " of=" << info.chunkCount
                      << " reason=" << (roundsSpent ? "nack-exhausted" : "hold-expired")
                      << " ageUs=" << (info.firstPacketUs != 0 && nowUs >= info.firstPacketUs ? nowUs - info.firstPacketUs : 0)
                      << " total=" << stuckHeadGiveUps << "''' + NL + '''";
          }
          handle_udp_discontinuity();
        }
      }
    }
'''
NEW_N3 = '''    // N3 (A04), the confirmed give-up rule, for an incomplete head with NOTHING complete behind it
    // (a static screen: PopDelivery has nothing to release; A03's hold policy for a head with a
    // complete successor is untouched). The head is given up only when every repair avenue had
    // its chance: (1) the scheduler spent its LAST applicable phase for this AU (tail rounds when
    // the loss includes a tail, else hole rounds), (2) one reply allowance passed since the last
    // NACK actually sent, (3) one reply allowance passed since the AU last made progress (a new
    // data chunk or an FEC-recovered chunk; duplicates, control and other AUs are not progress),
    // (4) the AU is at least terminalMin old. Without NACK (not negotiated, or the scheduler is
    // not chasing it) (3)+(4) alone bound the wait. (5) Past hardCap it is given up whatever its
    // progress -- a failure budget, not a delivery guarantee: an AU that keeps completing chunks
    // for over a second is preserved until then. replyAllowance = clamp(max(50 ms, 2 x the control
    // thread's recent RTT), replyAllowanceMax), 50 ms when the RTT is unknown or stale;
    // terminalMin = tailGrace + maxRounds x round + replyAllowance (245 ms with the defaults and
    // no RTT). While a keyframe is awaited a non-key head goes on (3) alone -- no new request or
    // reset per head (handle_udp_discontinuity's alreadyWaiting guard) -- so a following incomplete
    // IDR keeps its repair chance; a key head takes the full rule. Only the judged (gen, seq) is
    // removed, and only if it is still incomplete (GiveUpIncomplete cancels otherwise).
    if (!assembler.AnyComplete()) {
      uint16_t missing[kUdpVideoNackMaxMissing];
      UdpH264FrameAssembler::IncompleteAuInfo info{};
      if (assembler.OldestIncomplete(missing, kUdpVideoNackMaxMissing, &info) && info.firstPacketUs != 0) {
        const auto& ncfg = nackScheduler.config();
        const uint64_t rttUs = ctx.control.lastRttUs.load(std::memory_order_relaxed);
        const uint64_t rttAtUs = ctx.control.lastRttAtUs.load(std::memory_order_relaxed);
        const bool rttFresh = rttAtUs != 0 && nowUs >= rttAtUs && nowUs - rttAtUs <= nack.rttStaleUs;
        uint64_t allowanceUs = nack.replyAllowanceMinUs;
        if (rttFresh) allowanceUs = std::max<uint64_t>(allowanceUs, 2 * rttUs);
        allowanceUs = std::min<uint64_t>(allowanceUs, nack.replyAllowanceMaxUs);
        const uint64_t terminalMinUs =
            ncfg.tailGraceUs + static_cast<uint64_t>(ncfg.maxRounds) * ncfg.roundUs + allowanceUs;
        const uint64_t hardCapUs = std::max<uint64_t>(nack.giveUpHardCapUs, terminalMinUs);
        const uint64_t ageUs = nowUs >= info.firstPacketUs ? nowUs - info.firstPacketUs : 0;
        const uint64_t sinceProgressUs =
            (info.lastProgressUs != 0 && nowUs >= info.lastProgressUs) ? nowUs - info.lastProgressUs : ageUs;
        const uint16_t have = std::min<uint16_t>(info.missingTotal, kUdpVideoNackMaxMissing);
        const bool hasTail = have > 0 && missing[have - 1] >= info.highWater;
        const bool chased = nackEnabled && nackScheduler.current_seq() == info.seq &&
                            nackScheduler.current_generation() == info.generation;
        const bool noProgress = sinceProgressUs >= allowanceUs;  // (3)
        const bool oldEnough = ageUs >= terminalMinUs;          // (4)
        const char* why = nullptr;
        if (ageUs >= hardCapUs) {
          why = "hard-cap";  // (5)
        } else if (gate.waitForKeyFrame && !info.keyFrame) {
          if (noProgress) why = "no-progress-during-key-wait";
        } else if (chased) {
          const bool spent = nackScheduler.spent_for(hasTail);  // (1)
          const bool replyOver = nackScheduler.last_sent_us() != 0 &&
                                 nowUs >= nackScheduler.last_sent_us() + allowanceUs;  // (2)
          if (spent && replyOver && noProgress && oldEnough) why = "nack-spent";
        } else if (noProgress && oldEnough) {
          why = nackEnabled ? "no-progress" : "no-progress-nack-off";
        }
        if (why && assembler.GiveUpIncomplete(info.generation, info.seq)) {
          if (chased) nackScheduler.Reset();
          ++assemblyDropped;
          ++st.udpAssemblyDroppedCount;
          ++stuckHeadGiveUps;
          if (stuckHeadGiveUps <= 5 || (stuckHeadGiveUps % 100) == 1) {
            std::cout << "[native-video-client] stuck head given up seq=" << info.seq
                      << " gen=" << info.generation << " missing=" << info.missingTotal
                      << " of=" << info.chunkCount << " reason=" << why
                      << " ageUs=" << ageUs << " sinceProgressUs=" << sinceProgressUs
                      << " hasTail=" << (hasTail ? 1 : 0) << " chased=" << (chased ? 1 : 0)
                      << " rttUs=" << rttUs << (rttFresh ? "" : "(stale)")
                      << " replyAllowanceUs=" << allowanceUs << " terminalMinUs=" << terminalMinUs
                      << " hardCapUs=" << hardCapUs << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0)
                      << " total=" << stuckHeadGiveUps << "''' + NL + '''";
          }
          handle_udp_discontinuity();
        }
      }
    }
'''
edit(S + r'\viewer_video_receiver.cpp', [(OLD_N3, NEW_N3)])
print('N3 confirmed-rule product edits ok')
