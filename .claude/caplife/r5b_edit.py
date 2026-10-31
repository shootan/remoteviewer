base = r'C:\work\gnlink-wt\apk-decoder-liveness\apps'
N = '\r\n'


def edit(rel, reps):
    p = base + '\\' + rel
    s = open(p, 'rb').read().decode('utf-8')
    for a, b in reps:
        a = a.replace('\n', N)
        b = b.replace('\n', N)
        assert s.count(a) == 1, (rel, s.count(a), a[:80])
        s = s.replace(a, b)
    open(p, 'wb').write(s.encode('utf-8'))


edit(r'android_direct_client\app\src\main\cpp\android_video_decoder.cpp', [
("""bool AndroidVideoDecoderSink::EnsureCodecLocked(uint32_t width, uint32_t height) {""",
 """bool AndroidVideoDecoderSink::EnsureCodecLocked(uint32_t width, uint32_t height, bool keyFrame) {"""),
("""  // r5 N3 (Codex src r4: one IDR then 40 deltas made 41 creates in 2 s, codecRetries=0 -- every frame called this
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
  }""", """  // r5 N3 (Codex src r4: one IDR then 40 deltas made 41 creates in 2 s, codecRetries=0 -- every frame called this
  // and pushed the timer). Every attempt to make a codec comes through here -- a frame's and the worker's timer --
  // and after a failure they share ONE budget: each try is counted, and past the budget nobody tries at all
  // (terminal) until a new surface, a new selection, a new binding or a stream reset starts the count over
  // (ResetCodecRetryLocked). Before the shared deadline only an IDR may try (keyFrame: the frame the codec needs
  // anyway, and the one an owed / periodic key brings -- its recovery stays immediate, as before r4); a delta never
  // does (a stream of deltas cannot spend the budget or set the pace) and neither does the timer.
  if (codecTerminal_) return false;
  bool retry = false;
  if (codecRetryAtUs_ != 0) {
    if (!keyFrame && steady_now_us() < codecRetryAtUs_) return false;  // failed: not yet (a delta, the timer)
    codecRetryAtUs_ = 0;                                                // this try takes the slot
    ++codecAutoRetries_;
    retry = true;
  }"""),
("""  if (!EnsureCodecLocked(frame.header.width, frame.header.height) || !codec_) {""",
 """  if (!EnsureCodecLocked(frame.header.width, frame.header.height, isKeyFrame) || !codec_) {"""),
])
edit(r'android_direct_client\app\src\main\cpp\android_video_decoder.hpp', [
("""  bool EnsureCodecLocked(uint32_t width, uint32_t height);""",
 """  bool EnsureCodecLocked(uint32_t width, uint32_t height, bool keyFrame = false);"""),
("""  // r5 (Codex src r4): ONE deadline and ONE budget for every attempt after a failure -- a frame's (IDR or delta)
  // and the timer's alike (EnsureCodecLocked); a delta cannot make a codec early or spend the budget faster.""",
 """  // r5 (Codex src r4): ONE budget for every attempt after a failure -- a frame's and the timer's alike
  // (EnsureCodecLocked); before the deadline only an IDR may try, never a delta or the timer."""),
])
edit(r'native_poc\src\android_decoder_liveness_test.cpp', [
("""    check("[14] the budget ends in TERMINAL: 1 + 20 tries, about 20 s, frames coming all along",
          terminal && atTerminal == 21 && terminalAfterMs > 15000,""",
 """    check("[14] the budget ends in TERMINAL after exactly 1 + 20 tries (deltas, IDRs and the timer all counted)",
          terminal && atTerminal == 21,"""),
])
print('ok')
