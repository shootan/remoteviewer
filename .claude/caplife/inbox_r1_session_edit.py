base = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\native_poc\src'


def edit(name, reps):
    p = base + '\\' + name
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    s = raw.decode('utf-8').replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


# ---- the trace: every Request(2) site counted with its origin, accepted / refused
edit('client_liveness_trace.hpp', [
("""  WkRead = 13,       // reading the control link (it ticks the channel while it waits)
};""", """  WkRead = 13,       // reading the control link (it ticks the channel while it waits)
};

// apk-decoder-inbox r1 (Codex codex_apk_decoder_inbox_decision_r1.md (3)): EVERY KeyframeRequestState::Request(2)
// site of the session, by origin -- accepted (queued) or refused by the limiter. keyWantQueued / keyWantRefused below
// counted only some of them (owed / per-frame / answer), which is why `kq` was not "requests sent".
enum class LtKeyOrigin : uint32_t {
  Owed = 0,      // the selection's owed IDR (control loop, 1 s x 8)
  Held = 1,      // a held (refused) decoder request retried by the control loop
  Answer = 2,    // the decoder's request right after a selection answer
  Frame = 3,     // the decoder's request right after a frame (receive thread)
  Pipeline = 4,  // the assembler's own requestKeyframe callback (a gap it saw)
  Count = 5,
};""")
, ("""  void MaxDeliver(uint64_t us) {""", """  std::atomic<uint64_t> keyReqAccepted[static_cast<int>(LtKeyOrigin::Count)] = {};
  std::atomic<uint64_t> keyReqRefused[static_cast<int>(LtKeyOrigin::Count)] = {};
  void KeyReq(LtKeyOrigin o, bool accepted) {
    (accepted ? keyReqAccepted : keyReqRefused)[static_cast<int>(o)].fetch_add(1, std::memory_order_relaxed);
  }
  // " | kreq owed=a/r held=a/r ans=a/r frame=a/r pipe=a/r" (accepted / refused by the limiter)
  std::string FormatKeyRequests() const {
    char b[200];
    auto a = [this](LtKeyOrigin o) {
      return static_cast<unsigned long long>(keyReqAccepted[static_cast<int>(o)].load(std::memory_order_relaxed));
    };
    auto r = [this](LtKeyOrigin o) {
      return static_cast<unsigned long long>(keyReqRefused[static_cast<int>(o)].load(std::memory_order_relaxed));
    };
    std::snprintf(b, sizeof(b), " | kreq owed=%llu/%llu held=%llu/%llu ans=%llu/%llu frame=%llu/%llu pipe=%llu/%llu",
                  a(LtKeyOrigin::Owed), r(LtKeyOrigin::Owed), a(LtKeyOrigin::Held), r(LtKeyOrigin::Held),
                  a(LtKeyOrigin::Answer), r(LtKeyOrigin::Answer), a(LtKeyOrigin::Frame), r(LtKeyOrigin::Frame),
                  a(LtKeyOrigin::Pipeline), r(LtKeyOrigin::Pipeline));
    return b;
  }
  void MaxDeliver(uint64_t us) {"""),
])

# ---- the control channel: Tick's timeout resend of the head, apart from selective fragment retransmits
edit('udp_control_channel.hpp', [
("""    uint64_t nacksReceived = 0;     // NACKs for the head (trace builds)
  };""", """    uint64_t nacksReceived = 0;     // NACKs for the head (trace builds)
    // apk-decoder-inbox r1: Tick's TIMEOUT resend of the whole head (trace builds). fragmentRetransmits above counts
    // only selective fragment retries (a NACK's missing list) -- a head resent on timeout was never in it.
    uint64_t headTimeoutResends = 0;
  };"""),
])
edit('udp_control_channel.cpp', [
("""      head.lastSendUs = now;
      SendFragments(head, nullptr);
    }
  }""", """      head.lastSendUs = now;
      REMOTE60_LT(++stats_.headTimeoutResends);  // apk-decoder-inbox r1
      SendFragments(head, nullptr);
    }
  }"""),
])

# ---- the session: each Request(2) site counted; the trace line labels corrected
edit('native_video_client_session.cpp', [
("""      if (owed != 0 && owedAttempts_ < kOwedKeyMaxAttempts && loopNowUs >= owedNextUs_ &&
          keyframeRequests_.Request(2, loopNowUs).queued) {
        ++owedAttempts_;
        owedNextUs_ = loopNowUs + kOwedKeyRetryUs;
        REMOTE60_LT(livenessTrace_.keyWantQueued.fetch_add(1, std::memory_order_relaxed));
        std::fprintf(stderr, "[native-video-client-session] keyframe owed to selection %llu: asked (%u)\\n",
                     static_cast<unsigned long long>(owed), owedAttempts_);
      }""", """      if (owed != 0 && owedAttempts_ < kOwedKeyMaxAttempts && loopNowUs >= owedNextUs_) {
        const bool queued = keyframeRequests_.Request(2, loopNowUs).queued;
        REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Owed, queued));  // apk-decoder-inbox r1
        if (queued) {
          ++owedAttempts_;
          owedNextUs_ = loopNowUs + kOwedKeyRetryUs;
          REMOTE60_LT(livenessTrace_.keyWantQueued.fetch_add(1, std::memory_order_relaxed));
          std::fprintf(stderr, "[native-video-client-session] keyframe owed to selection %llu: asked (%u)\\n",
                       static_cast<unsigned long long>(owed), owedAttempts_);
        }
      }"""),
("""    if (!parking && decoderRekeyPending_.load(std::memory_order_acquire) &&
        keyframeRequests_.Request(2, loopNowUs).queued) {
      decoderRekeyPending_.store(false, std::memory_order_release);
      std::fprintf(stderr, "[native-video-client-session] held keyframe request queued\\n");
    }""", """    if (!parking && decoderRekeyPending_.load(std::memory_order_acquire)) {
      const bool queued = keyframeRequests_.Request(2, loopNowUs).queued;
      REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Held, queued));  // apk-decoder-inbox r1
      if (queued) {
        decoderRekeyPending_.store(false, std::memory_order_release);
        std::fprintf(stderr, "[native-video-client-session] held keyframe request queued\\n");
      }
    }"""),
("""            if (encodedFrameSink_->ConsumeDecoderKeyframeRequest() &&
                !keyframeRequests_.Request(2, now_us()).queued) {
              decoderRekeyPending_.store(true, std::memory_order_release);""", """            bool answerQueued = true;
            if (encodedFrameSink_->ConsumeDecoderKeyframeRequest()) {
              answerQueued = keyframeRequests_.Request(2, now_us()).queued;
              REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Answer, answerQueued));  // apk-decoder-inbox r1
            }
            if (!answerQueued) {
              decoderRekeyPending_.store(true, std::memory_order_release);"""),
("""      if (!keyframeRequests_.Request(2, now_us()).queued) {
        decoderRekeyPending_.store(true, std::memory_order_release);
        REMOTE60_LT(livenessTrace_.keyWantRefused.fetch_add(1, std::memory_order_relaxed));""", """      const bool frameQueued = keyframeRequests_.Request(2, now_us()).queued;
      REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Frame, frameQueued));  // apk-decoder-inbox r1
      if (!frameQueued) {
        decoderRekeyPending_.store(true, std::memory_order_release);
        REMOTE60_LT(livenessTrace_.keyWantRefused.fetch_add(1, std::memory_order_relaxed));"""),
("""    (void)keyframeRequests_.Request(2, now_us());
  };""", """    const bool pipeQueued = keyframeRequests_.Request(2, now_us()).queued;
    REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Pipeline, pipeQueued));  // apk-decoder-inbox r1
    (void)pipeQueued;
  };"""),
("""  std::string line = std::string(kLivenessTraceMarker) + " " + livenessTrace_.Format(now_us());""",
 """  std::string line = std::string(kLivenessTraceMarker) + " " + livenessTrace_.Format(now_us()) +
                     livenessTrace_.FormatKeyRequests();  // apk-decoder-inbox r1"""),
("""                  " | ch sent=%llu recv=%llu retx=%llu nack=%llu pend=%llu txq=%llu head=%u/%u first=%lldms last=%lldms "
                  "acked=%u ack=%lldms ackOk=%llu ackIgn=%llu nackRx=%llu closed=%d reason=%s",
                  static_cast<unsigned long long>(st.messagesSent), static_cast<unsigned long long>(st.messagesReceived),
                  static_cast<unsigned long long>(st.fragmentRetransmits), static_cast<unsigned long long>(st.nacksSent),""",
 """                  // apk-decoder-inbox r1: labels as they are -- fragRetx = selective fragment retries only, headResend =
                  // Tick's timeout resends of the head, rxPend = incomplete inbound messages (not the tx queue)
                  " | ch sent=%llu recv=%llu fragRetx=%llu headResend=%llu nack=%llu rxPend=%llu txq=%llu head=%u/%u "
                  "first=%lldms last=%lldms acked=%u ack=%lldms ackOk=%llu ackIgn=%llu nackRx=%llu closed=%d reason=%s",
                  static_cast<unsigned long long>(st.messagesSent), static_cast<unsigned long long>(st.messagesReceived),
                  static_cast<unsigned long long>(st.fragmentRetransmits),
                  static_cast<unsigned long long>(st.headTimeoutResends), static_cast<unsigned long long>(st.nacksSent),"""),
("""    char ch[400];""", """    char ch[480];"""),
])
print('ok')
