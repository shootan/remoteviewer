P = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp'


def edit(path, reps):
    s = open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (a[:90], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print('ok', path)


edit(P, [
    # ---- includes
    ('''#include <algorithm>
#include <cassert>
#include <chrono>''', '''#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>'''),
    # ---- destructor, pump -> worker
    ('''AndroidVideoDecoderSink::~AndroidVideoDecoderSink() {
  // Before the lock: the pump takes it every tick, so joining while holding it would deadlock.
  StopOutputPump();
  LockedState lock(this);
  ResetCodecLocked();
  ReleaseSurfaceLocked();
}

void AndroidVideoDecoderSink::StartOutputPumpLocked() {
  if (outputPumpThread_.joinable()) return;
  outputPumpStop_.store(false, std::memory_order_relaxed);
  outputPumpThread_ = std::thread([this] { OutputPumpMain(); });
}

void AndroidVideoDecoderSink::StopOutputPump() {
  outputPumpStop_.store(true, std::memory_order_relaxed);
  if (outputPumpThread_.joinable()) outputPumpThread_.join();
}

void AndroidVideoDecoderSink::OutputPumpMain() {
  // A frame is due at a wall-clock instant, so something has to be watching the clock. The
  // tick is well under a frame period: an empty dequeue costs almost nothing, and being a few
  // milliseconds late to hand over a frame is the entire defect this exists to avoid.
  constexpr auto kPumpInterval = std::chrono::milliseconds(3);
  while (!outputPumpStop_.load(std::memory_order_relaxed)) {
    {
      LockedState lock(this);
      if (codec_) DrainOutputLocked();
    }
    std::this_thread::sleep_for(kPumpInterval);
  }
}
''', '''AndroidVideoDecoderSink::~AndroidVideoDecoderSink() {
  // The worker ends first, joined WITHOUT mu_ (it takes mu_ on every pass). Then nothing else touches the codec:
  // what is left is stopped and released here, inline.
  {
    std::lock_guard<std::mutex> lk(mu_);
    workerStop_ = true;
  }
  workCv_.notify_all();
  doneCv_.notify_all();
  if (worker_.joinable()) worker_.join();
  LockedState lock(this);
  ResetCodecLocked();
  ReleaseSurfaceLocked();
  if (requestedWindow_) windowsToRelease_.push_back(requestedWindow_);
  requestedWindow_ = nullptr;
  for (ANativeWindow* w : windowsToRelease_) ANativeWindow_release(w);
  windowsToRelease_.clear();
}

// apk-decoder-liveness r1: started on first use (the sink is a process global; no thread at static init).
void AndroidVideoDecoderSink::EnsureWorkerLocked() {
  if (worker_.joinable() || workerStop_) return;
  worker_ = std::thread([this] { WorkerMain(); });
}

void AndroidVideoDecoderSink::RequestCodecResetLocked() {
  ++codecResetSeq_;
  EnsureWorkerLocked();
  workCv_.notify_all();
}

void AndroidVideoDecoderSink::ClearInboxLocked() {
  inbox_.clear();
  inboxBytes_ = 0;
  inboxGap_ = false;
}

// The worker's pass: an applied surface, an applied reset, the frames in order, the codec pumped (held frame,
// bootstrap replay, output due), the decode state published. Between passes it sleeps at most the pump tick:
// a frame is due at a wall-clock instant, so something has to be watching the clock, and being a few
// milliseconds late to hand one over is the defect the pump exists to avoid.
void AndroidVideoDecoderSink::WorkerMain() {
  constexpr auto kPumpInterval = std::chrono::milliseconds(3);
  std::unique_lock<std::mutex> lk(mu_);
  workerId_ = std::this_thread::get_id();
  workerLock_ = &lk;
  while (!workerStop_) {
    if (surfaceApplied_ != surfaceSeq_) ApplySurfaceLocked();
    if (CodecResetPendingLocked()) {
      const uint64_t seq = codecResetSeq_;
      ResetCodecLocked();  // mu_ released around the codec's stop / delete
      codecResetApplied_ = seq;
      continue;  // a surface or another reset may have come meanwhile
    }
    if (!windowsToRelease_.empty()) {
      std::vector<ANativeWindow*> release;
      release.swap(windowsToRelease_);
      CodecCall([&] {
        for (ANativeWindow* w : release) ANativeWindow_release(w);
      });
      if (surfaceReleasedSeq_ != surfaceApplied_) {
        surfaceReleasedSeq_ = surfaceApplied_;
        doneCv_.notify_all();
      }
      continue;
    }
    if (surfaceReleasedSeq_ != surfaceApplied_) {
      surfaceReleasedSeq_ = surfaceApplied_;
      doneCv_.notify_all();
    }
    while (!inbox_.empty() && !workerStop_ && surfaceApplied_ == surfaceSeq_ && !CodecResetPendingLocked()) {
      InboxFrame item = std::move(inbox_.front());
      inbox_.pop_front();
      inboxBytes_ -= std::min(inboxBytes_, item.frame.payload.size());
      ProcessFrameLocked(std::move(item));
    }
    if (surfaceApplied_ != surfaceSeq_ || CodecResetPendingLocked()) continue;
    if (codec_) PumpCodecLocked();
    PublishDecodeStateLocked();
    ++workerPasses_;
    doneCv_.notify_all();
    if (workerStop_) break;
    workCv_.wait_for(lk, kPumpInterval, [this] {
      return workerStop_ || !inbox_.empty() || surfaceApplied_ != surfaceSeq_ || CodecResetPendingLocked();
    });
  }
  workerLock_ = nullptr;
}

// The surface the UI asked for, made current: the codec (bound to the old one) stopped, the old window let go
// (outside mu_, next pass), and -- with the stream's SPS/PPS known -- a codec for the new one.
void AndroidVideoDecoderSink::ApplySurfaceLocked() {
  ResetCodecLocked();  // mu_ released around stop / delete; the request may change meanwhile -- read it after
  if (window_) windowsToRelease_.push_back(window_);
  window_ = requestedWindow_;
  requestedWindow_ = nullptr;
  surfaceApplied_ = surfaceSeq_;
  codecResetApplied_ = codecResetSeq_;  // this reset is the one the surface asked for
  if (window_ && !EnsureCodecLocked(configuredWidth_, configuredHeight_)) {
    log_info("video surface set; codec waiting for keyframe/csd");
  }
}

#ifdef REMOTE60_DECODER_TEST_HOOKS
bool AndroidVideoDecoderSink::WaitIdleForTest(int timeoutMs) {
  std::unique_lock<std::mutex> lk(mu_);
  EnsureWorkerLocked();
  workCv_.notify_all();
  const uint64_t start = workerPasses_;
  return doneCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
    return workerPasses_ >= start + 2 && inbox_.empty() && surfaceApplied_ == surfaceSeq_ &&
           surfaceReleasedSeq_ == surfaceApplied_ && !CodecResetPendingLocked() && windowsToRelease_.empty();
  });
}
#endif
'''),
    # ---- SetSurface
    ('''void AndroidVideoDecoderSink::SetSurface(JNIEnv* env, jobject surface) {
  LockedState lock(this);
  ResetCodecLocked();
  ReleaseSurfaceLocked();

  if (surface == nullptr) {
    log_info("video surface cleared");
    return;
  }

  window_ = ANativeWindow_fromSurface(env, surface);
  if (!window_) {
    log_error("failed to acquire ANativeWindow from Surface");
    return;
  }

  if (!EnsureCodecLocked(configuredWidth_, configuredHeight_)) {
    log_info("video surface set; codec waiting for keyframe/csd");
  }
  // r5 N1: an IDR still owed (one may have been dropped while there was no surface) is asked for
  // afresh now that it can be decoded.
  gate_.Rearm();
  StartGraceLocked(false);  // mobile-abr r5: the surface back is an active transition
}
''', '''// apk-decoder-liveness r1: the UI hands the Surface's window over and returns -- the codec for it is made by the
// worker. A detach (null) waits for the worker to have stopped the codec and let go of the old window: the app
// connects another producer to that surface right after (MainActivity DetachDecoder). Only the UI waits here.
void AndroidVideoDecoderSink::SetSurface(JNIEnv* env, jobject surface) {
  ANativeWindow* window = nullptr;
  if (surface != nullptr) {
    window = ANativeWindow_fromSurface(env, surface);
    if (!window) log_error("failed to acquire ANativeWindow from Surface");
  }
  std::unique_lock<std::mutex> lk(mu_);
  if (requestedWindow_) windowsToRelease_.push_back(requestedWindow_);  // asked for, never applied
  requestedWindow_ = window;
  surfaceRequested_ = window != nullptr;
  const uint64_t seq = ++surfaceSeq_;
  ++codecResetSeq_;
  EnsureWorkerLocked();
  if (window) {
    // r5 N1: an IDR still owed (one may have been dropped while there was no surface) is asked for
    // afresh now that it can be decoded.
    gate_.Rearm();
    StartGraceLocked(false);  // mobile-abr r5: the surface back is an active transition
  } else {
    log_info("video surface cleared");
  }
  PublishDecodeStateLocked();
  workCv_.notify_all();
  if (window) return;
  const auto t0 = std::chrono::steady_clock::now();
  doneCv_.wait(lk, [&] { return surfaceReleasedSeq_ >= seq || workerStop_; });
  const auto waitedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  if (waitedMs >= 50) {
    char line[128];
    std::snprintf(line, sizeof(line), "video surface detach waited ms=%lld (the UI only)",
                  static_cast<long long>(waitedMs));
    log_info(line);
  }
}
'''),
    # ---- RX entry -> inbox; the rest moves to the worker
    ('''void AndroidVideoDecoderSink::OnEncodedH264FrameAs(uint64_t token, remote60::native_poc::UdpH264AssembledFrame&& frame) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;

  PumpCodecLocked();

  const bool isKeyFrame = (frame.header.flags & 1u) != 0;''',
     '''// apk-decoder-liveness r1: the receive thread's part -- the owner, the selection gate, the inbox; then it returns.
// Nothing here touches the codec or waits for the worker.
void AndroidVideoDecoderSink::OnEncodedH264FrameAs(uint64_t token, remote60::native_poc::UdpH264AssembledFrame&& frame) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  EnsureWorkerLocked();

  const bool isKeyFrame = (frame.header.flags & 1u) != 0;'''),
    ('''  if (latestInputStreamGeneration_ != 0 &&
      latestInputStreamGeneration_ != frame.header.streamGeneration) {
    ResetPtsStateLocked();
  }
  latestInputStreamGeneration_ = frame.header.streamGeneration;
  configuredWidth_ = frame.header.width;''',
     '''  InboxFrame item;
  item.resetPts = latestInputStreamGeneration_ != 0 &&
                  latestInputStreamGeneration_ != frame.header.streamGeneration;
  latestInputStreamGeneration_ = frame.header.streamGeneration;
  inboxBytes_ += frame.payload.size();
  item.frame = std::move(frame);
  inbox_.push_back(std::move(item));
  // Bounded: the oldest goes. Its loss is the codec's loss -- an IDR is owed again and asked for, and the deltas
  // after the gap are not fed before the next IDR (ProcessFrameLocked).
  while (inbox_.size() > kInboxMaxFrames || (inboxBytes_ > kInboxMaxBytes && inbox_.size() > 1)) {
    const auto& old = inbox_.front().frame;
    if ((old.header.flags & 1u) != 0) gate_.KeyLost(old.header.streamGeneration);
    inboxBytes_ -= std::min(inboxBytes_, old.payload.size());
    inbox_.pop_front();
    ++inboxDrops_;
    inboxGap_ = true;
    decoderKeyframeRequest_ = true;
    if ((inboxDrops_ % 30u) == 1u) {
      char line[160];
      std::snprintf(line, sizeof(line), "decoder inbox full: oldest frame dropped drops=%llu",
                    static_cast<unsigned long long>(inboxDrops_));
      log_info(line);
    }
  }
  workCv_.notify_all();
}

// The worker's part of a frame (what the receive thread did here before r1), in arrival order.
void AndroidVideoDecoderSink::ProcessFrameLocked(InboxFrame&& item) {
  remote60::native_poc::UdpH264AssembledFrame frame = std::move(item.frame);
  const bool isKeyFrame = (frame.header.flags & 1u) != 0;
  if (item.resetPts) ResetPtsStateLocked();
  if (inboxGap_) {
    if (!isKeyFrame) {
      ++inboxGapDrops_;
      decoderKeyframeRequest_ = true;
      return;
    }
    inboxGap_ = false;
  }
  configuredWidth_ = frame.header.width;'''),
    # ---- resets become requests
    ('''void AndroidVideoDecoderSink::ResetForBindingLocked() {
  ResetCodecLocked();''', '''void AndroidVideoDecoderSink::ResetForBindingLocked() {
  RequestCodecResetLocked();
  ClearInboxLocked();'''),
    ('''void AndroidVideoDecoderSink::OnVideoStreamResetAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  ResetCodecLocked();''', '''void AndroidVideoDecoderSink::OnVideoStreamResetAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  RequestCodecResetLocked();
  ClearInboxLocked();'''),
    ('''  // Keep SPS/PPS, dimensions, and the active selection generation. Only decoder reference
  // pictures and pending compressed frames are invalid after a UDP loss.
  ResetCodecLocked();''', '''  // Keep SPS/PPS, dimensions, and the active selection generation. Only decoder reference
  // pictures and pending compressed frames are invalid after a UDP loss.
  RequestCodecResetLocked();'''),
    ('''bool AndroidVideoDecoderSink::PrepareForWindowSelectionAs(uint64_t token, uint64_t selectionGeneration) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  ResetCodecLocked();''', '''bool AndroidVideoDecoderSink::PrepareForWindowSelectionAs(uint64_t token, uint64_t selectionGeneration) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  RequestCodecResetLocked();
  ClearInboxLocked();'''),
    ('''bool AndroidVideoDecoderSink::AbortWindowSelectionAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  ResetCodecLocked();''', '''bool AndroidVideoDecoderSink::AbortWindowSelectionAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  RequestCodecResetLocked();
  ClearInboxLocked();'''),
    # ---- decode state: the UI's surface intent
    ('''  const bool valid = window_ != nullptr;
  // The generation moves whenever what the decoder's count means could have changed: validity, the
  // surface, the selection being prepared / abandoned and the stream generation it was answered with.
  uint64_t key = reinterpret_cast<uintptr_t>(window_);''',
     '''  // apk-decoder-liveness r1: the surface the UI set (its request), not the worker's applied window -- the
  // validity does not wait for the codec.
  const bool valid = surfaceRequested_;
  // The generation moves whenever what the decoder's count means could have changed: validity, the
  // surface, the selection being prepared / abandoned and the stream generation it was answered with.
  uint64_t key = surfaceSeq_;'''),
    # ---- debug status
    ('''  status += " deltaBeforeKey=" + std::to_string(deltaBeforeKeyDrops_);
  return status;''', '''  status += " deltaBeforeKey=" + std::to_string(deltaBeforeKeyDrops_);
  // apk-decoder-liveness r1: the worker's side.
  status += " codecState=" + std::string(codecState_);
  status += " codecFail=" + std::to_string(codecFailures_);
  status += " codecVoid=" + std::to_string(codecVoidStarts_);
  status += " inbox=" + std::to_string(inbox_.size());
  status += " inboxDrops=" + std::to_string(inboxDrops_);
  status += " gapDrops=" + std::to_string(inboxGapDrops_);
  status += " resetPending=" + std::to_string(CodecResetPendingLocked() ? 1 : 0);
  return status;'''),
    # ---- UI queries: no codec pump on the caller's thread
    ('''uint64_t AndroidVideoDecoderSink::VideoSizePacked() {
  LockedState lock(this);
  PumpCodecLocked();''', '''uint64_t AndroidVideoDecoderSink::VideoSizePacked() {
  LockedState lock(this);'''),
    ('''uint64_t AndroidVideoDecoderSink::ReadySelectionGeneration() {
  LockedState lock(this);
  PumpCodecLocked();''', '''uint64_t AndroidVideoDecoderSink::ReadySelectionGeneration() {
  LockedState lock(this);'''),
    ('''uint64_t AndroidVideoDecoderSink::LastOutputPresentationUs() {
  LockedState lock(this);
  PumpCodecLocked();''', '''uint64_t AndroidVideoDecoderSink::LastOutputPresentationUs() {
  LockedState lock(this);'''),
    # ---- codec creation off the lock
    ('''  ResetCodecLocked();

  codec_ = AMediaCodec_createDecoderByType(kMimeTypeAvc);
  if (!codec_) {
    log_error("AMediaCodec_createDecoderByType(video/avc) failed");
    return false;
  }

  AMediaFormat* format = AMediaFormat_new();
  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, kMimeTypeAvc);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, static_cast<int32_t>(width));
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, static_cast<int32_t>(height));
  const uint8_t* csd0Data = csd0_.data();
  const uint8_t* csd1Data = csd1_.data();
  const size_t csd0Size = csd0_.size();
  const size_t csd1Size = csd1_.size();
  // Capture stable pointers once and keep the backing vectors immutable until configure().
  assert(csd0Data != nullptr && csd1Data != nullptr);
  assert(csd0_.capacity() >= csd0Size);
  assert(csd1_.capacity() >= csd1Size);
  AMediaFormat_setBuffer(format, AMEDIAFORMAT_KEY_CSD_0, csd0Data, csd0Size);
  AMediaFormat_setBuffer(format, AMEDIAFORMAT_KEY_CSD_1, csd1Data, csd1Size);
  // Remote control is interactive, so ask the decoder not to build an output reorder queue.
  // "low-latency" is API 30+; older or non-supporting codecs ignore unknown keys, and the
  // vendor spellings cover devices that shipped the behaviour before it was standardised.
  AMediaFormat_setInt32(format, "low-latency", 1);
  AMediaFormat_setInt32(format, "vendor.low-latency.enable", 1);
  AMediaFormat_setInt32(format, "vdec-lowlatency", 1);
  // 0 selects realtime priority.
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PRIORITY, 0);

  media_status_t status = AMediaCodec_configure(codec_, format, window_, nullptr, 0);
  AMediaFormat_delete(format);
  if (status != AMEDIA_OK) {
    log_error("AMediaCodec_configure failed");
    ResetCodecLocked();
    return false;
  }
  if (AMediaCodec_start(codec_) != AMEDIA_OK) {
    log_error("AMediaCodec_start failed");
    ResetCodecLocked();
    return false;
  }

  ++codecsStarted_;''', '''  ResetCodecLocked();
  if (CodecResetPendingLocked()) return false;  // asked to drop the codec meanwhile: the next pass makes it

  // apk-decoder-liveness r1: made with mu_ RELEASED (CodecCall) -- on a loaded device this took seconds
  // (ke-r23: 26.4 -> 29.6 s) and nobody may wait on it. Everything it uses is copied first; the window is the
  // worker's own (only the worker replaces it). If a reset was asked for while it was made, it is void.
  const uint64_t resetSeq = codecResetSeq_;
  ANativeWindow* window = window_;
  const std::vector<uint8_t> csd0 = csd0_;
  const std::vector<uint8_t> csd1 = csd1_;
  codecState_ = "creating";
  const char* failed = nullptr;
  AMediaCodec* codec = CodecCall([&]() -> AMediaCodec* {
    AMediaCodec* c = AMediaCodec_createDecoderByType(kMimeTypeAvc);
    if (!c) {
      failed = "AMediaCodec_createDecoderByType(video/avc) failed";
      return nullptr;
    }
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, kMimeTypeAvc);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, static_cast<int32_t>(width));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, static_cast<int32_t>(height));
    assert(!csd0.empty() && !csd1.empty());
    AMediaFormat_setBuffer(format, AMEDIAFORMAT_KEY_CSD_0, csd0.data(), csd0.size());
    AMediaFormat_setBuffer(format, AMEDIAFORMAT_KEY_CSD_1, csd1.data(), csd1.size());
    // Remote control is interactive, so ask the decoder not to build an output reorder queue.
    // "low-latency" is API 30+; older or non-supporting codecs ignore unknown keys, and the
    // vendor spellings cover devices that shipped the behaviour before it was standardised.
    AMediaFormat_setInt32(format, "low-latency", 1);
    AMediaFormat_setInt32(format, "vendor.low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vdec-lowlatency", 1);
    // 0 selects realtime priority.
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PRIORITY, 0);
    const media_status_t status = AMediaCodec_configure(c, format, window, nullptr, 0);
    AMediaFormat_delete(format);
    if (status != AMEDIA_OK) {
      failed = "AMediaCodec_configure failed";
    } else if (AMediaCodec_start(c) != AMEDIA_OK) {
      failed = "AMediaCodec_start failed";
    }
    if (failed) {
      AMediaCodec_stop(c);
      AMediaCodec_delete(c);
      return nullptr;
    }
    return c;
  });
  if (!codec) {
    log_error(failed ? failed : "codec failed");
    codecState_ = "failed";
    ++codecFailures_;
    return false;
  }
  if (resetSeq != codecResetSeq_ || window != window_ || workerStop_) {
    // Made for a surface / owner / selection that is gone: never used.
    CodecCall([&] {
      AMediaCodec_stop(codec);
      AMediaCodec_delete(codec);
    });
    ++codecVoidStarts_;
    codecState_ = "none";
    log_info("codec made after a reset was asked for: discarded");
    return false;
  }
  codec_ = codec;
  codecState_ = "started";

  ++codecsStarted_;'''),
    ('''  outputWidth_ = width;
  outputHeight_ = height;
  StartOutputPumpLocked();
  char line[224];
  char* codecName = nullptr;
  const bool named = AMediaCodec_getName(codec_, &codecName) == AMEDIA_OK && codecName;
  std::snprintf(line, sizeof(line), "MediaCodec started width=%u height=%u csd0=%zu csd1=%zu name=%s",
                width, height, csd0_.size(), csd1_.size(), named ? codecName : "?");
  if (named) AMediaCodec_releaseName(codec_, codecName);
  log_info(line);
  return true;''', '''  outputWidth_ = width;
  outputHeight_ = height;
  char line[224];
  std::string name = CodecCall([&] {
    char* codecName = nullptr;
    std::string n = "?";
    if (AMediaCodec_getName(codec, &codecName) == AMEDIA_OK && codecName) {
      n = codecName;
      AMediaCodec_releaseName(codec, codecName);
    }
    return n;
  });
  std::snprintf(line, sizeof(line), "MediaCodec started width=%u height=%u csd0=%zu csd1=%zu name=%s",
                width, height, csd0.size(), csd1.size(), name.c_str());
  log_info(line);
  return true;'''),
    # ---- queueing off the lock
    ('''bool AndroidVideoDecoderSink::TryQueueFrameLocked(remote60::native_poc::UdpH264AssembledFrame& frame) {
  if (!codec_) return false;

  const ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec_, 0);
  lastInputDequeue_ = static_cast<int64_t>(inputIndex);
  if (inputIndex < 0) {
    return false;
  }

  size_t inputBufferSize = 0;
  uint8_t* inputBuffer = AMediaCodec_getInputBuffer(codec_, inputIndex, &inputBufferSize);
  if (!inputBuffer) {''', '''// apk-decoder-liveness r1: the worker only (the frame is the worker's -- a local or its pending slot); every codec
// call with mu_ released. The decisions around them (gate, PTS, counts, the owed key) are made under mu_.
bool AndroidVideoDecoderSink::TryQueueFrameLocked(remote60::native_poc::UdpH264AssembledFrame& frame) {
  if (!codec_ || CodecResetPendingLocked()) return false;
  AMediaCodec* const codec = codec_;
  const uint64_t resetSeq = codecResetSeq_;

  size_t inputBufferSize = 0;
  uint8_t* inputBuffer = nullptr;
  const ssize_t inputIndex = CodecCall([&]() -> ssize_t {
    const ssize_t index = AMediaCodec_dequeueInputBuffer(codec, 0);
    if (index >= 0) inputBuffer = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(index), &inputBufferSize);
    return index;
  });
  lastInputDequeue_ = static_cast<int64_t>(inputIndex);
  if (inputIndex < 0) {
    return false;
  }
  if (!inputBuffer) {'''),
    ('''  std::memcpy(inputBuffer, frame.payload.data(), frame.payload.size());
  const int64_t ptsUs = static_cast<int64_t>(ComputeQueuedPtsUsLocked(frame));
  if (AMediaCodec_queueInputBuffer(codec_, inputIndex, 0,
                                   static_cast<size_t>(frame.payload.size()),
                                   ptsUs, 0) != AMEDIA_OK) {
    log_error("AMediaCodec_queueInputBuffer failed");
    ResetCodecLocked();
    return false;
  }

  ++inputFrameCount_;''', '''  const int64_t ptsUs = static_cast<int64_t>(ComputeQueuedPtsUsLocked(frame));
  const media_status_t queued = CodecCall([&] {
    std::memcpy(inputBuffer, frame.payload.data(), frame.payload.size());
    return AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(inputIndex), 0,
                                        static_cast<size_t>(frame.payload.size()), ptsUs, 0);
  });
  if (queued != AMEDIA_OK) {
    log_error("AMediaCodec_queueInputBuffer failed");
    ResetCodecLocked();
    return false;
  }

  ++inputFrameCount_;
  // A reset asked for while it was queued: the codec is about to go -- the frame went into a codec that will not
  // show it, so it pays no owed key and starts no bootstrap.
  if (resetSeq != codecResetSeq_) return true;'''),
    # ---- codec stop off the lock
    ('''  heldOutputValid_ = false;
  if (codec_) {
    AMediaCodec_stop(codec_);
    AMediaCodec_delete(codec_);
    codec_ = nullptr;
  }
}

void AndroidVideoDecoderSink::ReleaseSurfaceLocked() {
  if (window_) {
    ANativeWindow_release(window_);
    window_ = nullptr;
  }
}''', '''  heldOutputValid_ = false;
  if (codec_) {
    // apk-decoder-liveness r1: stopped with mu_ released (the worker; inline in the destructor).
    AMediaCodec* const codec = codec_;
    codec_ = nullptr;
    codecState_ = "none";
    CodecCall([&] {
      AMediaCodec_stop(codec);
      AMediaCodec_delete(codec);
    });
  }
}

// The destructor only (the worker hands windows over through windowsToRelease_).
void AndroidVideoDecoderSink::ReleaseSurfaceLocked() {
  if (window_) {
    ANativeWindow_release(window_);
    window_ = nullptr;
  }
}'''),
    # ---- output format
    ('''  if (!codec_) return;

  AMediaFormat* format = AMediaCodec_getOutputFormat(codec_);
  if (!format) return;''', '''  if (!codec_) return;

  AMediaCodec* const codec = codec_;
  AMediaFormat* format = CodecCall([&] { return AMediaCodec_getOutputFormat(codec); });
  if (!format) return;'''),
    # ---- output release
    ('''void AndroidVideoDecoderSink::ReleaseHeldOutputLocked(uint64_t nowUs) {
  if (!heldOutputValid_ || !codec_) return;
  heldOutputValid_ = false;''', '''void AndroidVideoDecoderSink::ReleaseHeldOutputLocked(uint64_t nowUs) {
  if (!heldOutputValid_ || !codec_) return;
  if (CodecResetPendingLocked()) return;  // the codec is about to go: its picture is not shown (r1)
  heldOutputValid_ = false;
  AMediaCodec* const codec = codec_;
  const uint64_t resetSeq = codecResetSeq_;'''),
    ('''  media_status_t status;
  if (heldOutputScheduled_) {
    // NDK timestamps use CLOCK_MONOTONIC, the same domain as steady_clock on Android.
    status = AMediaCodec_releaseOutputBufferAtTime(
        codec_, heldOutputIndex_, static_cast<int64_t>(heldOutputPresentUs_ * 1000ULL));
    ++presentScheduledCount_;
  } else {
    status = AMediaCodec_releaseOutputBuffer(codec_, heldOutputIndex_, true);
    ++presentImmediateCount_;
  }''', '''  media_status_t status;
  const size_t index = heldOutputIndex_;
  if (heldOutputScheduled_) {
    // NDK timestamps use CLOCK_MONOTONIC, the same domain as steady_clock on Android.
    const int64_t atNs = static_cast<int64_t>(heldOutputPresentUs_ * 1000ULL);
    status = CodecCall([&] { return AMediaCodec_releaseOutputBufferAtTime(codec, index, atNs); });
    ++presentScheduledCount_;
  } else {
    status = CodecCall([&] { return AMediaCodec_releaseOutputBuffer(codec, index, true); });
    ++presentImmediateCount_;
  }'''),
    ('''  // A selection is confirmed by a frame of it reaching the display, so this has to live where
  // frames are handed over. It sat in the drain loop, which now returns early for every
  // normally-paced frame -- leaving the viewer waiting for an acknowledgement that had
  // already happened, and dropping back to the target list on a timeout while video played.
  if (gate_.pending() != 0 &&''', '''  // A selection is confirmed by a frame of it reaching the display, so this has to live where
  // frames are handed over. It sat in the drain loop, which now returns early for every
  // normally-paced frame -- leaving the viewer waiting for an acknowledgement that had
  // already happened, and dropping back to the target list on a timeout while video played.
  // r1: not a picture of a codec a reset (a new selection among them) was asked for while it was released.
  if (resetSeq == codecResetSeq_ && gate_.pending() != 0 &&'''),
    # ---- output drain
    ('''  AMediaCodecBufferInfo info{};
  for (;;) {
    const ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(codec_, &info, 0);
    lastOutputDequeue_ = static_cast<int64_t>(outputIndex);''', '''  AMediaCodecBufferInfo info{};
  for (;;) {
    if (!codec_ || CodecResetPendingLocked()) return;  // r1: the codec is about to go
    AMediaCodec* const codec = codec_;
    const ssize_t outputIndex = CodecCall([&] { return AMediaCodec_dequeueOutputBuffer(codec, &info, 0); });
    lastOutputDequeue_ = static_cast<int64_t>(outputIndex);
    if (CodecResetPendingLocked()) return;'''),
])
