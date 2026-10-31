import re

base = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\android_direct_client\app\src\main\cpp'
N = '\r\n'


def edit(name, reps):
    p = base + '\\' + name
    s = open(p, 'rb').read().decode('utf-8')
    for a, b in reps:
        a = a.replace('\n', N)
        b = b.replace('\n', N)
        assert s.count(a) == 1, (name, s.count(a), a[:80])
        s = s.replace(a, b)
    open(p, 'wb').write(s.encode('utf-8'))


edit('android_video_decoder.cpp', [
("""  if (!window_ || width == 0 || height == 0 || csd0_.empty() || csd1_.empty()) {
    return false;
  }

  ResetCodecLocked();
  if (CodecResetPendingLocked()) return false;  // asked to drop the codec meanwhile: the next pass makes it
""", """  if (!window_ || width == 0 || height == 0 || csd0_.empty() || csd1_.empty()) {
    return false;
  }
  // r5 N3 (Codex src r4: one IDR then 40 deltas made 41 creates in 2 s, codecRetries=0 -- every frame called this
  // and pushed the timer). Every attempt to make a codec comes through here, a frame's (IDR or delta) and the
  // worker's timer alike, and after a failure they share ONE deadline and ONE budget: nobody tries before the
  // deadline, each try after it is counted, and past the budget nobody tries at all (terminal) until a new surface,
  // a new selection, a new binding or a stream reset starts the count over (ResetCodecRetryLocked).
  if (codecTerminal_) return false;
  bool retry = false;
  if (codecRetryAtUs_ != 0) {
    if (steady_now_us() < codecRetryAtUs_) return false;  // failed: not yet, whoever asks
    codecRetryAtUs_ = 0;                                   // this try takes the slot
    ++codecAutoRetries_;
    retry = true;
  }

  ResetCodecLocked();
  if (CodecResetPendingLocked()) return false;  // asked to drop the codec meanwhile: the next pass makes it
"""),
("""    // r4 N3: tried again by the worker, bounded (RetryCodecLocked) -- unless the retries for this surface /
    // selection are spent: then the failure is terminal until a new surface / selection / an IDR from the host.
    if (codecAutoRetries_ < kCodecAutoRetryMax) {
      codecRetryAtUs_ = steady_now_us() + kCodecRetryIntervalUs;
    } else {
      codecRetryAtUs_ = 0;
      codecState_ = "failed_terminal";
      char line[192];
      std::snprintf(line, sizeof(line),
                    "codec failed: %u retries spent -- no more until a new surface, a new selection or an IDR",
                    codecAutoRetries_);
      log_error(line);
    }
    return false;""", """    // r4 N3 / r5: tried again after kCodecRetryIntervalUs (by the next frame or the worker's timer, whichever comes
    // first -- one shared deadline), at most kCodecAutoRetryMax times; then terminal until a new surface / selection
    // / binding / stream reset.
    if (codecAutoRetries_ < kCodecAutoRetryMax) {
      codecRetryAtUs_ = steady_now_us() + kCodecRetryIntervalUs;
    } else {
      codecRetryAtUs_ = 0;
      codecTerminal_ = true;
      codecState_ = "failed_terminal";
      char line[192];
      std::snprintf(line, sizeof(line),
                    "codec failed: %u retries spent -- no more until a new surface, selection, binding or stream",
                    codecAutoRetries_);
      log_error(line);
    }
    return false;"""),
("""  codec_ = codec;
  codecState_ = "started";
  codecRetryAtUs_ = 0;  // r4 N3
""", """  codec_ = codec;
  codecState_ = "started";
  codecRetryAtUs_ = 0;  // r4 N3
  if (retry) {
    // r4 N3: a codec made after a failure can take an IDR again -- an owed one is asked for afresh (Rearm: a new
    // token, the session's same bounded asks); r5: whichever path made it.
    gate_.Rearm();
    decoderKeyframeRequest_ = true;
    char rline[160];
    std::snprintf(rline, sizeof(rline), "codec made on retry %u after a failure: an owed IDR is asked for afresh (token=%llu)",
                  codecAutoRetries_, static_cast<unsigned long long>(gate_.OwedToken()));
    log_info(rline);
  }
  codecAutoRetries_ = 0;
"""),
("""void AndroidVideoDecoderSink::RetryCodecLocked() {
  if (codec_ || !window_ || codecRetryAtUs_ == 0 || steady_now_us() < codecRetryAtUs_) return;
  codecRetryAtUs_ = 0;
  ++codecAutoRetries_;
  if (!EnsureCodecLocked(configuredWidth_, configuredHeight_) || !codec_) return;  // failed again: re-scheduled there
  gate_.Rearm();
  decoderKeyframeRequest_ = true;
  char line[160];
  std::snprintf(line, sizeof(line), "codec made on retry %u after a failure: an owed IDR is asked for afresh (token=%llu)",
                codecAutoRetries_, static_cast<unsigned long long>(gate_.OwedToken()));
  log_info(line);
}""", """// r5: the timer's half only -- the deadline, the count and the re-arm are EnsureCodecLocked's, shared with frames.
void AndroidVideoDecoderSink::RetryCodecLocked() {
  if (codec_ || !window_ || codecTerminal_ || codecRetryAtUs_ == 0 || steady_now_us() < codecRetryAtUs_) return;
  EnsureCodecLocked(configuredWidth_, configuredHeight_);
}"""),
])
edit('android_video_decoder.hpp', [
("""  uint64_t codecRetryAtUs_ = 0;    // 0: no retry due
  uint32_t codecAutoRetries_ = 0;
  void ResetCodecRetryLocked() {
    codecRetryAtUs_ = 0;
    codecAutoRetries_ = 0;
  }""", """  // r5 (Codex src r4): ONE deadline and ONE budget for every attempt after a failure -- a frame's (IDR or delta)
  // and the timer's alike (EnsureCodecLocked); a delta cannot make a codec early or spend the budget faster.
  uint64_t codecRetryAtUs_ = 0;    // 0: no failure pending
  uint32_t codecAutoRetries_ = 0;  // tries made after the failure, by any path
  bool codecTerminal_ = false;     // the budget is spent: no try until ResetCodecRetryLocked
  void ResetCodecRetryLocked() {
    codecRetryAtUs_ = 0;
    codecAutoRetries_ = 0;
    codecTerminal_ = false;
  }"""),
("""  // failed_terminal and logged once: from there only a new surface, a new selection or an IDR the host sends
  // brings a codec (an arriving IDR always tries). Nothing here asks the host for anything by itself.""",
 """  // failed_terminal and logged once: from there only a new surface, a new selection, a new binding or a stream
  // reset brings a codec (r5: an arriving frame does not try either). Nothing here asks the host for anything by
  // itself."""),
])
print('ok')
