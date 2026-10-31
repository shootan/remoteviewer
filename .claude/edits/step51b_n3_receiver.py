import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'

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
