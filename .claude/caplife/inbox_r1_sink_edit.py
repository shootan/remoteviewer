base = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\android_direct_client\app\src\main\cpp'


def load(name):
    raw = open(base + '\\' + name, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(name, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(base + '\\' + name, 'wb').write(s.encode('utf-8'))


# ------------------------------------------------------------------ cpp
s, crlf = load('android_video_decoder.cpp')
# 1. codec API kinds: one line each, by its current text (the line that opens the CodecCall)
kinds = [
    ('      CodecCall([&] {\n        for (ANativeWindow* w : release)', 'kApiWindowRelease'),
    ('  AMediaCodec* codec = CodecCall([&]() -> AMediaCodec* {', 'kApiCreate'),
    ('    CodecCall([&] {\n      AMediaCodec_stop(codec);\n      AMediaCodec_delete(codec);\n    });\n    ++codecVoidStarts_;', 'kApiStopDelete'),
    ('  std::string name = CodecCall([&] {', 'kApiName'),
    ('  const ssize_t inputIndex = CodecCall([&]() -> ssize_t {', 'kApiDequeueIn'),
    ('  const media_status_t queued = CodecCall([&] {', 'kApiQueueIn'),
    ('    codecState_ = "none";\n    CodecCall([&] {', 'kApiStopDelete'),
    ('  AMediaFormat* format = CodecCall([&] { return AMediaCodec_getOutputFormat(codec); });', 'kApiFormat'),
    ('    status = CodecCall([&] { return AMediaCodec_releaseOutputBufferAtTime(codec, index, atNs); });', 'kApiRelease'),
    ('    status = CodecCall([&] { return AMediaCodec_releaseOutputBuffer(codec, index, true); });', 'kApiRelease'),
    ('    const ssize_t outputIndex = CodecCall([&] { return AMediaCodec_dequeueOutputBuffer(codec, &info, 0); });', 'kApiDequeueOut'),
]
for a, k in kinds:
    assert s.count(a) == 1, ('kind', s.count(a), a[:70])
    s = s.replace(a, a.replace('CodecCall(', f'CodecCall({k}, ', 1))
assert s.count('CodecCall([') == 1  # the SH seam's delay: kApiOther by default

# 2. the decoder's key-request want, by cause (same flag, same places -- only counted)
reps = [
    ("""    ++inboxOversizeDrops_;
    inboxGap_ = true;
    decoderKeyframeRequest_ = true;""", """    ++inboxOversizeDrops_;
    inboxGap_ = true;
    NoteKeyWantLocked(kWantOversize);"""),
    ("""    ++inboxDrops_;
    inboxGap_ = true;
    decoderKeyframeRequest_ = true;""", """    ++inboxDrops_;
    inboxGap_ = true;
    NoteKeyWantLocked(kWantInboxOverflow);"""),
    ("""      ++inboxGapDrops_;
      decoderKeyframeRequest_ = true;
      return;""", """      ++inboxGapDrops_;
      NoteKeyWantLocked(kWantGapDelta);
      return;"""),
    ("""    ++deltaBeforeKeyDrops_;
    decoderKeyframeRequest_ = true;""", """    ++deltaBeforeKeyDrops_;
    NoteKeyWantLocked(kWantAwaitKey);"""),
    ("""  if (keyRequest) decoderKeyframeRequest_ = true;""", """  if (keyRequest) NoteKeyWantLocked(kWantPendingSlot);"""),
    ("""  StartGraceLocked(false);
  decoderKeyframeRequest_ = true;
  log_info("selection aborted");""", """  StartGraceLocked(false);
  NoteKeyWantLocked(kWantSelectionAbort);
  log_info("selection aborted");"""),
    ("""    gate_.Rearm();
    decoderKeyframeRequest_ = true;
    char rline[160];""", """    gate_.Rearm();
    NoteKeyWantLocked(kWantCodecUp);
    char rline[160];"""),
    # 3. RX: the state lock's wait / hold, the inbox high-water, the enqueue time (trace builds)
    ("""void AndroidVideoDecoderSink::OnEncodedH264FrameAs(uint64_t token, remote60::native_poc::UdpH264AssembledFrame&& frame) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;""", """void AndroidVideoDecoderSink::OnEncodedH264FrameAs(uint64_t token, remote60::native_poc::UdpH264AssembledFrame&& frame) {
#ifdef GNLINK_LIVENESS_TRACE
  // apk-decoder-inbox r1: the receive thread's wait for the state lock and how long it holds it (trace builds)
  const uint64_t ltWait0 = LtNowUs();
  struct LtHold {
    AndroidVideoDecoderSink* s;
    uint64_t t0 = 0;
    ~LtHold() { LtMax(s->ltRxLockHoldMaxUs_, LtNowUs() - t0); }
  } ltHold{this};
#endif
  LockedState lock(this);
  REMOTE60_LT({
    ltHold.t0 = LtNowUs();
    LtMax(ltRxLockWaitMaxUs_, ltHold.t0 - ltWait0);
  });
  if (!OwnerLocked(token)) return;"""),
    ("""  inboxBytes_ += frame.payload.size();
  item.frame = std::move(frame);
  inbox_.push_back(std::move(item));""", """  inboxBytes_ += frame.payload.size();
  REMOTE60_LT(item.enqueueUs = LtNowUs());
  item.frame = std::move(frame);
  inbox_.push_back(std::move(item));
  REMOTE60_LT({
    LtMax(ltInboxHwFrames_, inbox_.size());
    LtMax(ltInboxHwBytes_, inboxBytes_);
  });"""),
    # 4. the worker: frames taken per pass, the time they took, their age at the pop
    ("""    while (!inbox_.empty() && !workerStop_ && surfaceApplied_ == surfaceSeq_ && !CodecResetPendingLocked()) {
      InboxFrame item = std::move(inbox_.front());
      inbox_.pop_front();
      inboxBytes_ -= std::min(inboxBytes_, item.frame.payload.size());
      ProcessFrameLocked(std::move(item));
    }""", """    REMOTE60_LT(ltPassFrames_ = 0; ltPassStartUs_ = LtNowUs());
    while (!inbox_.empty() && !workerStop_ && surfaceApplied_ == surfaceSeq_ && !CodecResetPendingLocked()) {
      InboxFrame item = std::move(inbox_.front());
      inbox_.pop_front();
      inboxBytes_ -= std::min(inboxBytes_, item.frame.payload.size());
      REMOTE60_LT({
        ++ltPassFrames_;
        if (item.enqueueUs) LtAge(ltAgePop_, LtNowUs() - item.enqueueUs);
      });
      ProcessFrameLocked(std::move(item));
    }
    REMOTE60_LT(if (ltPassFrames_) {
      LtMax(ltPassFramesMax_, ltPassFrames_);
      LtMax(ltPassServiceMaxUs_, LtNowUs() - ltPassStartUs_);
    });"""),
    ("""void AndroidVideoDecoderSink::ProcessFrameLocked(InboxFrame&& item) {
  remote60::native_poc::UdpH264AssembledFrame frame = std::move(item.frame);""", """void AndroidVideoDecoderSink::ProcessFrameLocked(InboxFrame&& item) {
  REMOTE60_LT(ltCurEnqueueUs_ = item.enqueueUs);  // its age when it is queued into the codec (directly from here)
  remote60::native_poc::UdpH264AssembledFrame frame = std::move(item.frame);"""),
    ("""  if (isKey) codecAwaitsKey_ = false;  // keepalive r5 D1: this instance has its IDR""",
     """  if (isKey) codecAwaitsKey_ = false;  // keepalive r5 D1: this instance has its IDR
  REMOTE60_LT(if (ltCurEnqueueUs_) {
    LtAge(ltAgeQueue_, LtNowUs() - ltCurEnqueueUs_);
    ltCurEnqueueUs_ = 0;  // a held frame queued later has no enqueue time here: not counted
  });"""),
    # 5. the debug status: the want causes (always), the trace line: the new measurements (trace builds)
    ("""  status += " inboxRefused=" + std::to_string(inboxOversizeDrops_);     // r4 N2""",
     """  status += " inboxRefused=" + std::to_string(inboxOversizeDrops_);     // r4 N2
  // apk-decoder-inbox r1: how often the decoder set its key-request want, by cause (a want is consumed by the
  // session per frame / answer; it is not a request sent)
  status += " keyWant=ovs:" + std::to_string(keyWant_[kWantOversize]) + ",ovf:" + std::to_string(keyWant_[kWantInboxOverflow]) +
            ",gap:" + std::to_string(keyWant_[kWantGapDelta]) + ",await:" + std::to_string(keyWant_[kWantAwaitKey]) +
            ",pend:" + std::to_string(keyWant_[kWantPendingSlot]) + ",abort:" + std::to_string(keyWant_[kWantSelectionAbort]) +
            ",up:" + std::to_string(keyWant_[kWantCodecUp]);"""),
    ("""                static_cast<unsigned long long>(ltInbox_.load(std::memory_order_relaxed)));
  return line;
#else""", """                static_cast<unsigned long long>(ltInbox_.load(std::memory_order_relaxed)));
  // apk-decoder-inbox r1: maxima since the start (never reset), and age histograms (ms: <50 <100 <250 <500 <1000 >=)
  auto hist = [](const LtAgeHist& h) {
    char b[120];
    std::snprintf(b, sizeof(b), "max=%llums n=%llu h=%llu/%llu/%llu/%llu/%llu/%llu",
                  static_cast<unsigned long long>(h.maxUs.load(std::memory_order_relaxed) / 1000),
                  static_cast<unsigned long long>(h.count.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(h.bucket[0].load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(h.bucket[1].load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(h.bucket[2].load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(h.bucket[3].load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(h.bucket[4].load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(h.bucket[5].load(std::memory_order_relaxed)));
    return std::string(b);
  };
  std::string out = line;
  char more[256];
  std::snprintf(more, sizeof(more), " | inboxHw=%llu/%lluB passFramesMax=%llu passSvcMax=%llums rxLockWaitMax=%llums "
                "rxLockHoldMax=%llums",
                static_cast<unsigned long long>(ltInboxHwFrames_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInboxHwBytes_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltPassFramesMax_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltPassServiceMaxUs_.load(std::memory_order_relaxed) / 1000),
                static_cast<unsigned long long>(ltRxLockWaitMaxUs_.load(std::memory_order_relaxed) / 1000),
                static_cast<unsigned long long>(ltRxLockHoldMaxUs_.load(std::memory_order_relaxed) / 1000));
  out += more;
  out += " | agePop " + hist(ltAgePop_) + " | ageQueue " + hist(ltAgeQueue_) + " | api";
  static const char* kApiNames[kApiKinds] = {"other", "create", "stopDel", "name", "deqIn", "queueIn",
                                             "fmt", "release", "deqOut", "winRel"};
  for (int k = 0; k < kApiKinds; ++k) {
    const uint64_t n = ltApiCount_[k].load(std::memory_order_relaxed);
    if (n == 0) continue;
    char a[80];
    std::snprintf(a, sizeof(a), " %s=%llu/%llums/%llums", kApiNames[k], static_cast<unsigned long long>(n),
                  static_cast<unsigned long long>(ltApiTotalUs_[k].load(std::memory_order_relaxed) / n / 1000),
                  static_cast<unsigned long long>(ltApiMaxUs_[k].load(std::memory_order_relaxed) / 1000));
    out += a;  // count / mean / max
  }
  return out;
#else"""),
]
for a, b in reps:
    assert s.count(a) == 1, ('rep', s.count(a), a[:80])
    s = s.replace(a, b)
save('android_video_decoder.cpp', s, crlf)

# ------------------------------------------------------------------ hpp
h, hcrlf = load('android_video_decoder.hpp')
hreps = [
    ("""  // Runs f -- codec / window work -- with mu_ released when called on the worker; inline otherwise (the
  // destructor, after the worker has ended).
  template <class F>
  decltype(auto) CodecCall(F&& f) {
    if (workerLock_ == nullptr || std::this_thread::get_id() != workerId_) return f();
    REMOTE60_LT(ltCodecCallSinceUs_.store(LtNowUs(), std::memory_order_relaxed));
    workerLock_->unlock();
    struct Relock {
      std::unique_lock<std::mutex>* l;
      std::atomic<uint64_t>* since;
      ~Relock() {
        REMOTE60_LT(since->store(0, std::memory_order_relaxed));
        l->lock();
      }
    } relock{workerLock_, &ltCodecCallSinceUs_};
    return f();
  }""", """  // Runs f -- codec / window work -- with mu_ released when called on the worker; inline otherwise (the
  // destructor, after the worker has ended). apk-decoder-inbox r1: `kind` says which codec API it is, for the
  // test-build latency per kind (count / total / max of the call itself, mu_ released).
  enum ApiKind : int {
    kApiOther = 0, kApiCreate, kApiStopDelete, kApiName, kApiDequeueIn, kApiQueueIn, kApiFormat, kApiRelease,
    kApiDequeueOut, kApiWindowRelease, kApiKinds
  };
  template <class F>
  decltype(auto) CodecCall(F&& f) {
    return CodecCall(kApiOther, std::forward<F>(f));
  }
  template <class F>
  decltype(auto) CodecCall(int kind, F&& f) {
    if (workerLock_ == nullptr || std::this_thread::get_id() != workerId_) return f();
    REMOTE60_LT(ltCodecCallSinceUs_.store(LtNowUs(), std::memory_order_relaxed));
    workerLock_->unlock();
    struct Relock {
      std::unique_lock<std::mutex>* l;
      std::atomic<uint64_t>* since;
      AndroidVideoDecoderSink* s;
      int kind;
      ~Relock() {
        REMOTE60_LT({
          const uint64_t t0 = since->load(std::memory_order_relaxed);
          const uint64_t d = t0 ? LtNowUs() - t0 : 0;
          s->ltApiCount_[kind].fetch_add(1, std::memory_order_relaxed);
          s->ltApiTotalUs_[kind].fetch_add(d, std::memory_order_relaxed);
          LtMax(s->ltApiMaxUs_[kind], d);
          since->store(0, std::memory_order_relaxed);
        });
        l->lock();
      }
    } relock{workerLock_, &ltCodecCallSinceUs_, this, kind};
    return f();
  }"""),
    ("""  struct InboxFrame {
    remote60::native_poc::UdpH264AssembledFrame frame;
    bool resetPts = false;  // the receive side saw the stream generation change (the old ResetPtsStateLocked rule)
  };""", """  struct InboxFrame {
    remote60::native_poc::UdpH264AssembledFrame frame;
    bool resetPts = false;  // the receive side saw the stream generation change (the old ResetPtsStateLocked rule)
    uint64_t enqueueUs = 0;  // apk-decoder-inbox r1: set only in test builds (GNLINK_LIVENESS_TRACE)
  };"""),
    ("""  std::atomic<uint64_t> ltCodecCallSinceUs_{0};""", """  std::atomic<uint64_t> ltCodecCallSinceUs_{0};
  // apk-decoder-inbox r1 (Codex codex_apk_decoder_inbox_decision_r1.md (3)), written only with GNLINK_LIVENESS_TRACE:
  // the inbox's high-water, the age of a frame when the worker takes it and when it goes into the codec, how much a
  // worker pass takes from the inbox and how long that takes, the receive thread's wait for / hold of mu_, and each
  // codec API call's own time by kind. Maxima since the start.
  static void LtMax(std::atomic<uint64_t>& a, uint64_t v) {
    uint64_t cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
  }
  struct LtAgeHist {
    std::atomic<uint64_t> maxUs{0};
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> bucket[6] = {};  // ms: <50 <100 <250 <500 <1000 >=1000
  };
  static void LtAge(LtAgeHist& h, uint64_t us) {
    LtMax(h.maxUs, us);
    h.count.fetch_add(1, std::memory_order_relaxed);
    const uint64_t ms = us / 1000;
    const int b = ms < 50 ? 0 : ms < 100 ? 1 : ms < 250 ? 2 : ms < 500 ? 3 : ms < 1000 ? 4 : 5;
    h.bucket[b].fetch_add(1, std::memory_order_relaxed);
  }
  std::atomic<uint64_t> ltInboxHwFrames_{0};
  std::atomic<uint64_t> ltInboxHwBytes_{0};
  std::atomic<uint64_t> ltPassFramesMax_{0};
  std::atomic<uint64_t> ltPassServiceMaxUs_{0};
  std::atomic<uint64_t> ltRxLockWaitMaxUs_{0};
  std::atomic<uint64_t> ltRxLockHoldMaxUs_{0};
  uint64_t ltPassFrames_ = 0;    // the worker's own (under mu_)
  uint64_t ltPassStartUs_ = 0;
  uint64_t ltCurEnqueueUs_ = 0;
  LtAgeHist ltAgePop_;
  LtAgeHist ltAgeQueue_;
  std::atomic<uint64_t> ltApiCount_[kApiKinds] = {};
  std::atomic<uint64_t> ltApiTotalUs_[kApiKinds] = {};
  std::atomic<uint64_t> ltApiMaxUs_[kApiKinds] = {};"""),
    ("""  bool decoderKeyframeRequest_ = false;""", """  bool decoderKeyframeRequest_ = false;
  // apk-decoder-inbox r1: each time the want is set, by cause (always counted; DebugStatus `keyWant=`). Setting it is
  // unchanged -- the same flag at the same places; the session consumes it per frame / answer.
  enum KeyWant : int {
    kWantOversize = 0, kWantInboxOverflow, kWantGapDelta, kWantAwaitKey, kWantPendingSlot, kWantSelectionAbort,
    kWantCodecUp, kWantKinds
  };
  uint64_t keyWant_[kWantKinds] = {};
  void NoteKeyWantLocked(int cause) {
    decoderKeyframeRequest_ = true;
    ++keyWant_[cause];
  }"""),
]
for a, b in hreps:
    assert h.count(a) == 1, ('hrep', h.count(a), a[:80])
    h = h.replace(a, b)
save('android_video_decoder.hpp', h, hcrlf)
print('ok')
