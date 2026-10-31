CPP = r'C:\work\gnlink-wt\apk-inbox-c\apps\android_direct_client\app\src\main\cpp'
SRC = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


# episode: an explicit exhausted (the covered duty spent)
p = SRC + r'\decoder_chain_inbox.hpp'
s, crlf = load(p)
s = rep(s, '''  // A new pick / binding / stream / surface, an abort: whatever was open or exhausted is moot.''',
        '''  // C r2 (Codex §8 (a)): the owed duty that covered this episode is spent and its final window passed with no usable
  // IDR preserved -- the episode is exhausted too (it never asked: owed was asking for the same IDR). true: it was open.
  bool MarkExhausted() {
    if (state_ != State::Open) return false;
    state_ = State::Exhausted;
    wantOut_ = false;
    ++exhausted_;
    return true;
  }

  // A new pick / binding / stream / surface, an abort: whatever was open or exhausted is moot.''')
s = rep(s, '''  size_t frames() const { return items_.size(); }''', '''  // C r2: any IDR queued (the covered duty's final window: a usable IDR on its way).
  bool HoldsAnyKey() const {
    for (const T& t : items_) {
      if (info_(t).key) return true;
    }
    return false;
  }

  size_t frames() const { return items_.size(); }''')
save(p, s, crlf)

p = CPP + r'\android_video_decoder.hpp'
s, crlf = load(p)
s = rep(s, '''  void OnDecoderKeyRequestSent(const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs) override {
    OnDecoderKeyRequestSentAs(kAnyBinding, request, nowUs);
  }
''', '''  void OnDecoderKeyRequestSent(const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs) override {
    OnDecoderKeyRequestSentAs(kAnyBinding, request, nowUs);
  }
  void OnDecoderKeyOwedExhausted(uint64_t owedToken, uint64_t nowUs) override {
    OnDecoderKeyOwedExhaustedAs(kAnyBinding, owedToken, nowUs);
  }
''')
s = rep(s, '''  void OnDecoderKeyRequestSentAs(uint64_t token, const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs);
''', '''  void OnDecoderKeyRequestSentAs(uint64_t token, const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs);
  void OnDecoderKeyOwedExhaustedAs(uint64_t token, uint64_t owedToken, uint64_t nowUs);
''')
s = rep(s, '''  uint64_t lossAlreadyRecovered_ = 0;  // losses noted behind a committed IDR (no episode, no gap)''', '''  uint64_t lossAlreadyRecovered_ = 0;  // losses noted behind a committed IDR (no episode, no gap)
  // C r2 (Codex §8 (a)): the owed duty the session spent its asks on (covered) -- its token, the end of the final
  // answer window, and whether it ran out with no usable IDR (coveredExhausted). Ends when the gate's token moves (paid,
  // re-armed, reset). A stale token's notice changes nothing (coveredStale counts it).
  uint64_t coveredToken_ = 0;
  uint64_t coveredDeadlineUs_ = 0;
  bool coveredExhausted_ = false;
  uint64_t coveredStale_ = 0;
  uint64_t coveredExhaustedCount_ = 0;
  bool AnyKeyPreservedLocked() const;  // a usable IDR is in the inbox, the pending slot or going in''')
save(p, s, crlf)

p = CPP + r'\android_video_decoder.cpp'
s, crlf = load(p)
s = rep(s, '''void AndroidVideoDecoderSink::TickRecoveryLocked() {
  recovery_.Tick(steady_now_us(), RecoveryCandidateLocked());''', '''bool AndroidVideoDecoderSink::AnyKeyPreservedLocked() const {
  if (inFlightKey_ || inbox_.HoldsAnyKey()) return true;
  const auto* held = pendingFrame_.held();
  return held && (held->header.flags & 1u) != 0;
}

void AndroidVideoDecoderSink::TickRecoveryLocked() {
  // C r2 (Codex §8 (a)): the covered duty -- over when the gate's token moved (paid by a commit, re-armed, reset);
  // exhausted once its final window passed with no usable IDR preserved (and the episode with it).
  if (coveredToken_ != 0) {
    if (gate_.OwedToken() != coveredToken_) {
      coveredToken_ = 0;
      coveredExhausted_ = false;
    } else if (!coveredExhausted_ && steady_now_us() >= coveredDeadlineUs_ && !AnyKeyPreservedLocked()) {
      coveredExhausted_ = true;
      ++coveredExhaustedCount_;
      recovery_.MarkExhausted();
      char line[192];
      std::snprintf(line, sizeof(line),
                    "decoder: the owed IDR's asks are spent (token=%llu) and its final window passed -- exhausted; the "
                    "next usable IDR (the host's periodic one) still decodes",
                    static_cast<unsigned long long>(coveredToken_));
      log_error(line);
    }
  }
  recovery_.Tick(steady_now_us(), RecoveryCandidateLocked());''')
s = rep(s, '''// apk-inbox C: the control exchange for this request started -- the episode's attempt, on the sink's own clock.''',
        '''// C r2 (Codex §8 (a)): the session's asks for the owed IDR `owedToken` are spent (the last one left or was dropped).
// No new asks come of it (the episode stays covered: owed is the same IDR); the final window starts now.
void AndroidVideoDecoderSink::OnDecoderKeyOwedExhaustedAs(uint64_t token, uint64_t owedToken, uint64_t /* session clock */) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  if (owedToken == 0 || gate_.OwedToken() != owedToken) {
    ++coveredStale_;  // re-armed / paid / another selection since: not this duty
    return;
  }
  coveredToken_ = owedToken;
  coveredDeadlineUs_ = steady_now_us() + remote60::native_poc::KeyRecoveryEpisode::kFinalWindowUs;
  coveredExhausted_ = false;
  char line[160];
  std::snprintf(line, sizeof(line), "decoder: the owed IDR's asks are spent (token=%llu): a final %llu ms window, no new asks",
                static_cast<unsigned long long>(owedToken),
                static_cast<unsigned long long>(remote60::native_poc::KeyRecoveryEpisode::kFinalWindowUs / 1000));
  log_info(line);
}

// apk-inbox C: the control exchange for this request started -- the episode's attempt, on the sink's own clock.''')
s = rep(s, '''            " recMoot=" + std::to_string(recoveryDroppedMoot_) + " keyTaken=" + std::to_string(keyRequestsTaken_) +''',
        '''            " recMoot=" + std::to_string(recoveryDroppedMoot_) + " keyTaken=" + std::to_string(keyRequestsTaken_) +
            " covered=" + std::to_string(coveredToken_ != 0 ? 1 : 0) + " coveredExh=" + std::to_string(coveredExhausted_ ? 1 : 0) +
            " coveredStale=" + std::to_string(coveredStale_) +''')
# the covered state goes with the asks
s = rep(s, '''  ++keyEpoch_;
  committedKeyValid_ = false;  // C r2
  recovery_.Invalidate();''', '''  ++keyEpoch_;
  committedKeyValid_ = false;  // C r2
  coveredToken_ = 0;           // C r2 §8
  coveredExhausted_ = false;
  recovery_.Invalidate();''')
save(p, s, crlf)

p = CPP + r'\entry_sink_adapter.hpp'
s, crlf = load(p)
s = rep(s, '''  void OnDecoderKeyRequestSent(const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs) override {
    if (const uint64_t t = Token()) decoder_->OnDecoderKeyRequestSentAs(t, request, nowUs);
  }
''', '''  void OnDecoderKeyRequestSent(const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs) override {
    if (const uint64_t t = Token()) decoder_->OnDecoderKeyRequestSentAs(t, request, nowUs);
  }
  void OnDecoderKeyOwedExhausted(uint64_t owedToken, uint64_t nowUs) override {  // C r2 §8
    if (const uint64_t t = Token()) decoder_->OnDecoderKeyOwedExhaustedAs(t, owedToken, nowUs);
  }
''')
save(p, s, crlf)
print('ok')
