#include "android_video_decoder.hpp"
#include "android_video_decoder_sh_seam.hpp"

#include <android/log.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

namespace remote60::android_direct {

namespace {

constexpr const char* kLogTag = "remote60_android_direct";
constexpr const char* kMimeTypeAvc = "video/avc";
// How close to its slot a frame is handed over. One 60 Hz vsync of slack: early enough that
// the compositor has it in time, late enough that no second frame is ever waiting behind it.
constexpr uint64_t kOutputReleaseLeadUs = 16000ULL;
constexpr uint64_t kBootstrapReplayIntervalUs = 250000ULL;
constexpr uint64_t kBootstrapReplayMaxCount = 2ULL;

struct NalUnitView {
  const uint8_t* data = nullptr;
  size_t size = 0;
  uint8_t type = 0;
};

void log_info(const char* message) {
  __android_log_write(ANDROID_LOG_INFO, kLogTag, message);
}

void log_error(const char* message) {
  __android_log_write(ANDROID_LOG_ERROR, kLogTag, message);
}

uint64_t steady_now_us() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

bool find_start_code(const std::vector<uint8_t>& data, size_t offset, size_t* outPos, size_t* outCodeSize) {
  if (!outPos || !outCodeSize) return false;
  // Bounds are inclusive of the last byte: a start code ending exactly at the buffer tail
  // is still a start code, and skipping it dropped the final NAL of a frame.
  if (data.size() < 3) return false;
  for (size_t i = offset; i + 2 < data.size(); ++i) {
    if (data[i] != 0 || data[i + 1] != 0) continue;
    if (data[i + 2] == 1) {
      *outPos = i;
      *outCodeSize = 3;
      return true;
    }
    if (i + 3 < data.size() && data[i + 2] == 0 && data[i + 3] == 1) {
      *outPos = i;
      *outCodeSize = 4;
      return true;
    }
  }
  return false;
}

bool next_annexb_nal(const std::vector<uint8_t>& data, size_t* cursor, NalUnitView* out) {
  if (!cursor || !out) return false;
  size_t startPos = 0;
  size_t startCodeSize = 0;
  if (!find_start_code(data, *cursor, &startPos, &startCodeSize)) return false;

  size_t nextPos = data.size();
  size_t nextCodeSize = 0;
  if (find_start_code(data, startPos + startCodeSize, &nextPos, &nextCodeSize)) {
    (void)nextCodeSize;
  }

  const size_t nalStart = startPos + startCodeSize;
  if (nalStart >= data.size()) return false;
  const size_t nalSize = nextPos - nalStart;
  if (nalSize == 0) return false;

  out->data = data.data() + nalStart;
  out->size = nalSize;
  out->type = static_cast<uint8_t>(out->data[0] & 0x1Fu);
  *cursor = nextPos;
  return true;
}

std::vector<uint8_t> with_start_code(const uint8_t* data, size_t size) {
  std::vector<uint8_t> out;
  out.reserve(size + 4);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  out.push_back(1);
  out.insert(out.end(), data, data + size);
  return out;
}

}  // namespace

AndroidVideoDecoderSink::~AndroidVideoDecoderSink() {
  // The worker ends first, joined WITHOUT mu_ (it takes mu_ on every pass). Then nothing else touches the codec:
  // what is left is stopped and released here, inline -- r5 (Codex src r4): so the destructor, once quiescent, DOES
  // call the codec (stop / delete) and release windows under mu_ on the destroying thread; nobody else can be
  // waiting on mu_ by then, but those calls can be as slow as anywhere else.
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
  inbox_.Clear();
}

// apk-inbox C (D8): every ask made before this moment is moot -- the session drops it at its held / accepted point.
void AndroidVideoDecoderSink::InvalidateKeyAsksLocked() {
  ++keyEpoch_;
  recovery_.Invalidate();
  wantCodecUp_ = false;
  wantSelectionAbort_ = false;
}

// apk-inbox C (design r3 §3 보류): a usable IDR (after the loss point) is in the inbox, in the pending slot or going
// into the codec right now -- nothing is asked while one is.
bool AndroidVideoDecoderSink::RecoveryCandidateLocked() const {
  if (recovery_.state() == remote60::native_poc::KeyRecoveryEpisode::State::Idle) return false;
  const uint32_t loss = recovery_.lossSeq();
  if (inbox_.HoldsKeyAfter(loss)) return true;
  if (inFlightKey_ && remote60::native_poc::chain_seq_after(inFlightKeySeq_, loss)) return true;
  const auto* held = pendingFrame_.held();
  return held && (held->header.flags & 1u) != 0 && remote60::native_poc::chain_seq_after(held->header.seq, loss);
}

// A loss of the chain the decoder saw itself (await-key, its pending slot): the episode, and the tail gap.
void AndroidVideoDecoderSink::NoteChainLossLocked(int cause, uint32_t seq) {
  NoteKeyWantLocked(cause);
  inbox_.MarkLoss(seq);
  recovery_.OnLoss(remote60::native_poc::kKeyCauseRecovery, seq, steady_now_us());
}

void AndroidVideoDecoderSink::NoteInboxOfferLocked(const remote60::native_poc::ChainOfferResult& r) {
  if (r.keysLost) gate_.KeyLost(r.keyLostGen);  // r5 N1: an IDR of that generation is owed again
  if (!r.loss) return;
  NoteKeyWantLocked(r.admit == remote60::native_poc::ChainAdmit::Oversize ? kWantOversize : kWantInboxOverflow);
  recovery_.OnLoss(remote60::native_poc::kKeyCauseRecovery, r.lossSeq, steady_now_us());
}

void AndroidVideoDecoderSink::TickRecoveryLocked() {
  recovery_.Tick(steady_now_us(), RecoveryCandidateLocked());
  if (recovery_.exhausted() != recoveryExhaustedLogged_) {
    recoveryExhaustedLogged_ = recovery_.exhausted();
    char line[192];
    std::snprintf(line, sizeof(line),
                  "decoder recovery exhausted episode=%llu asks=%u lossSeq=%u -- no more asks; the next usable IDR "
                  "(the host's periodic one) still decodes",
                  static_cast<unsigned long long>(recovery_.id()), recovery_.attempts(), recovery_.lossSeq());
    log_error(line);
  }
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
  REMOTE60_LT(ltWorkerTid_.store(remote60::native_poc::liveness_tid(), std::memory_order_relaxed));
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
      CodecCall(kApiWindowRelease, [&] {
        for (ANativeWindow* w : release) ANativeWindow_release(w);
      });
      if (surfaceReleasedSeq_ != surfaceApplied_) {
        MarkSurfaceReleasedLocked(surfaceApplied_);
        doneCv_.notify_all();
      }
      continue;
    }
    if (surfaceReleasedSeq_ != surfaceApplied_) {
      MarkSurfaceReleasedLocked(surfaceApplied_);
      doneCv_.notify_all();
    }
    REMOTE60_LT(ltPassFrames_ = 0; ltPassStartUs_ = LtNowUs());
    while (!inbox_.empty() && !workerStop_ && surfaceApplied_ == surfaceSeq_ && !CodecResetPendingLocked()) {
      InboxFrame item;
      inbox_.Pop(&item);
      REMOTE60_LT({
        ++ltPassFrames_;
        if (item.enqueueUs) LtAge(ltAgePop_, LtNowUs() - item.enqueueUs);
      });
      ProcessFrameLocked(std::move(item));
    }
    REMOTE60_LT(if (ltPassFrames_) {
      LtMax(ltPassFramesMax_, ltPassFrames_);
      LtMax(ltPassServiceMaxUs_, LtNowUs() - ltPassStartUs_);
    });
    if (surfaceApplied_ != surfaceSeq_ || CodecResetPendingLocked()) continue;
    RetryCodecLocked();  // r4 N3
    if (surfaceApplied_ != surfaceSeq_ || CodecResetPendingLocked()) continue;
    if (codec_) PumpCodecLocked();
    TickRecoveryLocked();  // apk-inbox C: the final window ends on the clock
    PublishDecodeStateLocked();
    ++workerPasses_;
    REMOTE60_LT({
      ltWorkerPassUs_.store(LtNowUs(), std::memory_order_relaxed);
      ltInbox_.store(inbox_.frames(), std::memory_order_relaxed);
    });
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
  ResetCodecRetryLocked();              // r4 N3: a new surface, its own retries
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

void AndroidVideoDecoderSink::SetTargetFps(uint32_t fps) {
  LockedState lock(this);
  const uint32_t clampedFps = std::clamp<uint32_t>(fps, 1u, 240u);
  // A runtime FPS change starts a new presentation clock on the next frame.
  playoutClock_.SetTargetFrameIntervalUs(
      std::max<uint64_t>(1ULL, 1000000ULL / static_cast<uint64_t>(clampedFps)));
}

// apk-decoder-liveness r1: the UI hands the Surface's window over and returns -- the codec for it is made by the
// worker. r3 D1: a detach (null) waits for the worker to have stopped the codec and let go of the old window -- at
// most kDetachDeadlineMs, and not at all when an earlier detach is still pending (never stacked). Only the UI waits.
int64_t AndroidVideoDecoderSink::SetSurface(JNIEnv* env, jobject surface) {
  constexpr size_t kMaxWindowsToRelease = 4;
  ANativeWindow* window = nullptr;
  if (surface != nullptr) {
    window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
      // r4 (Codex src r1 D1): an attach whose window could not be had is REFUSED (0) -- never turned into a detach of
      // the current surface (that would answer a seq, or -seq, which the caller reads as taken / pending). Nothing
      // of the sink's surface, codec or detach state changes; the caller keeps its surface and producer as they are.
      log_error("failed to acquire ANativeWindow from Surface: attach refused");
      return 0;
    }
  }
  std::unique_lock<std::mutex> lk(mu_);
  if (window && (surfaceReleasedSeq_ < lastDetachSeq_ || windowsToRelease_.size() >= kMaxWindowsToRelease)) {
    // A producer may still be on the old surface (or the worker is behind on letting windows go): no new surface is
    // taken. The app keeps that surface BLOCKED; this reference was never handed over -- give it back here.
    char line[160];
    std::snprintf(line, sizeof(line), "video surface refused: detach pending (released=%llu detach=%llu) toRelease=%zu",
                  static_cast<unsigned long long>(surfaceReleasedSeq_), static_cast<unsigned long long>(lastDetachSeq_),
                  windowsToRelease_.size());
    lk.unlock();
    log_info(line);
    ANativeWindow_release(window);
    return 0;
  }
  const bool earlierDetachPending = surfaceReleasedSeq_ < lastDetachSeq_;
  if (requestedWindow_) windowsToRelease_.push_back(requestedWindow_);  // asked for, never applied
  requestedWindow_ = window;
  surfaceRequested_ = window != nullptr;
  InvalidateKeyAsksLocked();  // apk-inbox C: a new surface (or none) -- the codec starts over; the owed duty re-arms
  const uint64_t seq = ++surfaceSeq_;
  if (!window) lastDetachSeq_ = seq;
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
  if (window) return static_cast<int64_t>(seq);
  if (earlierDetachPending) {
    lk.unlock();
    log_info("video surface cleared: an earlier detach is still pending -- not waited for again");
    return -static_cast<int64_t>(seq);
  }
  const auto t0 = std::chrono::steady_clock::now();
  const bool done = doneCv_.wait_for(lk, std::chrono::milliseconds(kDetachDeadlineMs),
                                     [&] { return surfaceReleasedSeq_ >= seq || workerStop_; }) &&
                    surfaceReleasedSeq_ >= seq;
  lk.unlock();
  const auto waitedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  if (!done || waitedMs >= 50) {
    char line[160];
    std::snprintf(line, sizeof(line), "video surface detach %s seq=%llu waited ms=%lld (the UI only)",
                  done ? "done" : "PENDING past the deadline", static_cast<unsigned long long>(seq),
                  static_cast<long long>(waitedMs));
    log_info(line);
  }
  return done ? static_cast<int64_t>(seq) : -static_cast<int64_t>(seq);
}

// apk-decoder-liveness r1: the receive thread's part -- the owner, the selection gate, the inbox; then it returns.
// Nothing here touches the codec or waits for the worker.
void AndroidVideoDecoderSink::OnEncodedH264FrameAs(uint64_t token, remote60::native_poc::UdpH264AssembledFrame&& frame) {
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
  if (!OwnerLocked(token)) return;
  EnsureWorkerLocked();

  const bool isKeyFrame = (frame.header.flags & 1u) != 0;
  if (!gate_.Admit(frame.header.streamGeneration, isKeyFrame)) {
    ++staleFrameDropCount_;
    if ((staleFrameDropCount_ % 30u) == 1u) {
      char line[192];
      std::snprintf(line, sizeof(line),
                    "dropped stale frame streamGen=%llu expected=%llu pendingSel=%llu drops=%llu",
                    static_cast<unsigned long long>(frame.header.streamGeneration),
                    static_cast<unsigned long long>(gate_.expected()),
                    static_cast<unsigned long long>(gate_.pending()),
                    static_cast<unsigned long long>(staleFrameDropCount_));
      log_info(line);
    }
    return;
  }

  // apk-inbox C (design r3 §1-§2): the chain-aware inbox decides -- the order (a duplicate / older AU dropped), the
  // byte cap for every frame (r4 N2: a frame over the whole cap is refused: a loss), the tail gap (a delta after a loss
  // is refused until a usable IDR), and the trim (the earliest usable IDR + suffix kept; else the loss taken as one).
  // r5 (Codex src r4) still holds: the cap is an AVERAGE of the highest bitrate asked for, an encoder may make one IDR
  // over 4 MiB -- refused here, every time it comes. The assembler itself admits up to 16 MB.
  const uint32_t seq = frame.header.seq;
  const uint64_t gen = frame.header.streamGeneration;
  InboxFrame item;
  item.resetPts = latestInputStreamGeneration_ != 0 && latestInputStreamGeneration_ != gen;
  REMOTE60_LT(item.enqueueUs = LtNowUs());
  item.frame = std::move(frame);
  const auto offer = inbox_.Offer(std::move(item));
  if (offer.admit == remote60::native_poc::ChainAdmit::Duplicate ||
      offer.admit == remote60::native_poc::ChainAdmit::Stale) {
    return;  // counted by the inbox (chainDup / chainStale); not a loss
  }
  latestInputStreamGeneration_ = gen;
  if (offer.admit == remote60::native_poc::ChainAdmit::Oversize) {
    ++inboxOversizeDrops_;
    char line[192];
    std::snprintf(line, sizeof(line), "decoder inbox: frame over the cap refused seq=%u cap=%zu key=%u refused=%llu", seq,
                  kInboxMaxBytes, isKeyFrame ? 1u : 0u, static_cast<unsigned long long>(inboxOversizeDrops_));
    log_error(line);
  }
  NoteInboxOfferLocked(offer);
  if (offer.admit == remote60::native_poc::ChainAdmit::Admitted) {
    REMOTE60_LT({
      // apk-decoder-inbox r2 (Codex codex_apk_inbox_plan_src_r1.md N1): the AU's own enqueue time, by its identity
      // (stream generation + AU seq) -- looked up when THAT AU goes into the codec, wherever it waited.
      LtEnq& e = ltEnq_[seq % kLtEnqRing];
      e.gen = gen;
      e.seq = seq;
      e.us = LtNowUs();
    });
  }
  if (offer.loss && ((inbox_.lost() % 30u) == 1u || offer.lostFrames > 1)) {
    char line[192];
    std::snprintf(line, sizeof(line), "decoder inbox: chain loss at seq=%u lost=%zu (total %llu) skipped=%zu episode=%llu %s",
                  offer.lossSeq, offer.lostFrames, static_cast<unsigned long long>(inbox_.lost()), offer.skippedFrames,
                  static_cast<unsigned long long>(recovery_.id()), recovery_.StateName());
    log_info(line);
  }
  REMOTE60_LT({  // r2: what stays admitted after the trim (never over the caps)
    LtMax(ltInboxHwFrames_, inbox_.frames());
    LtMax(ltInboxHwBytes_, inbox_.bytes());
  });
  workCv_.notify_all();
}

// The worker's part of a frame (what the receive thread did here before r1), in arrival order.
void AndroidVideoDecoderSink::ProcessFrameLocked(InboxFrame&& item) {
  remote60::native_poc::UdpH264AssembledFrame frame = std::move(item.frame);
  const bool isKeyFrame = (frame.header.flags & 1u) != 0;
  if (item.resetPts) ResetPtsStateLocked();
  // apk-inbox C: no worker-side gap any more -- after a loss the inbox holds no delta before a usable IDR (the trim
  // and the tail gap). The IDR going in now is a recovery candidate while the codec calls run (mu_ released).
  struct InFlight {
    AndroidVideoDecoderSink* s;
    ~InFlight() { s->inFlightKey_ = false; }
  } inFlight{this};
  inFlightKey_ = isKeyFrame;
  inFlightKeySeq_ = frame.header.seq;
  configuredWidth_ = frame.header.width;
  configuredHeight_ = frame.header.height;
  const bool codecConfigUpdated = UpdateCodecConfigLocked(frame.payload);
  if (codecConfigUpdated && codec_) {
    ResetCodecLocked();
  }
  if (!EnsureCodecLocked(frame.header.width, frame.header.height, isKeyFrame) || !codec_) {
    // r5 N1: dropped for want of a decoder (no surface, csd, codec create/configure/start): an
    // IDR of the answered generation that ends here was not delivered, so it is still owed.
    if (isKeyFrame) gate_.KeyLost(frame.header.streamGeneration);
    return;
  }
  // keepalive r5 D1: a codec instance starts at an IDR. One the surface started -- from the SPS/PPS an IDR left
  // behind while there was no surface -- would otherwise take the deltas that follow first; measured on a
  // cold-booted AVD (0.2.29 too), its decoder took 12-15 of them, produced nothing and freed no input buffer
  // for the IDRs asked for afterwards: the selection never showed a frame. Dropped here and an IDR asked for
  // (the session's limiter paces the asks); the IDR then starts this instance like any other.
  if (codecAwaitsKey_ && !isKeyFrame) {
    ++deltaBeforeKeyDrops_;
    NoteChainLossLocked(kWantAwaitKey, frame.header.seq);  // apk-inbox C: one episode, not one ask per delta
    if ((deltaBeforeKeyDrops_ % 30u) == 1u) {
      char line[160];
      std::snprintf(line, sizeof(line), "delta before the codec's first IDR dropped streamGen=%llu drops=%llu",
                    static_cast<unsigned long long>(frame.header.streamGeneration),
                    static_cast<unsigned long long>(deltaBeforeKeyDrops_));
      log_info(line);
    }
    return;
  }

  // apk-0232 r2 (N2): queued only if the gate admits its generation at this moment and nothing older is held; else
  // held / replacing the held frame / dropped (an IDR asked for) -- DecoderPendingSlot.
  const bool hadPending = pendingFrame_.has();
  bool keyRequest = false;
  // apk-inbox C: what the slot gives up is the loss point -- the HELD delta when this IDR replaces it, this delta when
  // it is the one dropped. (Not the arriving IDR: that one is the recovery's candidate.)
  const uint32_t offeredSeq = frame.header.seq;
  const bool offeredKey = isKeyFrame;
  const uint32_t heldSeq = pendingFrame_.held() ? pendingFrame_.held()->header.seq : offeredSeq;
  pendingFrame_.Offer(std::move(frame), gate_, [this](remote60::native_poc::UdpH264AssembledFrame& f) {
    return TryQueueFrameLocked(f);
  }, &keyRequest);
  if (keyRequest) NoteChainLossLocked(kWantPendingSlot, offeredSeq);
  if (pendingFrame_.has()) {
    pendingFrameCount_ = 1;
    if (!hadPending) pendingFrameQueueRetryCount_ = 0;
  }
  PumpCodecLocked();
}

// keepalive-multi r1: everything a binding owned goes with it -- the codec and its reference pictures, the
// selection gate (an answer or an owed key of the last PC is not this one's), the counts, the presentation window, a
// pending key request. A new binding starts like a new stream (D1: its codec takes no delta before an IDR).
void AndroidVideoDecoderSink::ResetForBindingLocked() {
  RequestCodecResetLocked();
  ClearInboxLocked();
  ResetCodecRetryLocked();  // r4 N3
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  gate_.Reset();
  readySelectionGeneration_ = 0;
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  csd0_.clear();
  csd1_.clear();
  presentGapsUs_.clear();
  presentScheduledCount_ = 0;
  presentImmediateCount_ = 0;
  InvalidateKeyAsksLocked();  // apk-inbox C (was: the single want flag cleared)
}

void AndroidVideoDecoderSink::Bind(uint64_t token) {
  LockedState lock(this);
  binding_ = token;
  ResetForBindingLocked();
  char line[96];
  std::snprintf(line, sizeof(line), "decoder bound binding=%llu", static_cast<unsigned long long>(token));
  log_info(line);
}

bool AndroidVideoDecoderSink::Unbind(uint64_t token) {
  LockedState lock(this);
  if (token == 0 || binding_ != token) return false;  // a stale unbind (a closed PC's late call) changes nothing
  binding_ = 0;
  ResetForBindingLocked();
  log_info("decoder unbound");
  return true;
}

uint64_t AndroidVideoDecoderSink::Binding() {
  LockedState lock(this);
  return binding_;
}

void AndroidVideoDecoderSink::OnVideoStreamResetAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  RequestCodecResetLocked();
  ClearInboxLocked();
  ResetCodecRetryLocked();  // r4 N3
  InvalidateKeyAsksLocked();  // apk-inbox C
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  gate_.Reset();
  readySelectionGeneration_ = 0;
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  csd0_.clear();
  csd1_.clear();
}

void AndroidVideoDecoderSink::OnVideoDiscontinuityAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  // Keep SPS/PPS, dimensions, and the active selection generation. Only decoder reference
  // pictures and pending compressed frames are invalid after a UDP loss.
  RequestCodecResetLocked();
  log_info("video decoder reset after transport discontinuity");
}

bool AndroidVideoDecoderSink::DrainPresentationStatsAs(
    uint64_t token, remote60::native_poc::ClientPresentationStats* out) {
  if (!out) return false;
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  // mobile-abr r1: the session drains once a second and every drain closes the window. Fewer than two
  // gaps is "no gap sample this window" -- the gaps and counters are dropped with it, so they cannot be
  // folded into a window seconds later.
  if (presentGapsUs_.size() < 2) {
    presentGapsUs_.clear();
    presentWindowStartUs_ = lastPresentSteadyUs_;
    presentScheduledCount_ = 0;
    presentImmediateCount_ = 0;
    presentWindowReanchorBase_ = playoutClock_.ReanchorCount();
    displayedWindowBase_ = displayedFrameCount_.load(std::memory_order_relaxed);
    return false;
  }

  std::vector<uint32_t> gaps = presentGapsUs_;
  const uint64_t windowStartUs = presentWindowStartUs_;
  const uint64_t windowEndUs = lastPresentSteadyUs_;
  presentGapsUs_.clear();
  presentWindowStartUs_ = lastPresentSteadyUs_;
  out->scheduledCount = presentScheduledCount_;
  out->immediateCount = presentImmediateCount_;
  presentScheduledCount_ = 0;
  presentImmediateCount_ = 0;
  const uint64_t reanchors = playoutClock_.ReanchorCount();
  out->reanchorCount = static_cast<uint32_t>(reanchors - presentWindowReanchorBase_);
  presentWindowReanchorBase_ = reanchors;
  const uint64_t displayed = displayedFrameCount_.load(std::memory_order_relaxed);
  out->displayedCount = static_cast<uint32_t>(displayed - displayedWindowBase_);
  displayedWindowBase_ = displayed;

  std::sort(gaps.begin(), gaps.end());
  const size_t n = gaps.size();
  const uint32_t targetUs = playoutClock_.TargetFrameIntervalUs() > 0
                                ? static_cast<uint32_t>(playoutClock_.TargetFrameIntervalUs())
                                : 33333u;
  out->targetIntervalUs = targetUs;
  out->sampleCount = static_cast<uint32_t>(n);
  out->gapP50Us = gaps[n / 2];
  out->gapP95Us = gaps[std::min(n - 1, static_cast<size_t>(n * 95 / 100))];
  out->gapMaxUs = gaps.back();
  for (uint32_t gap : gaps) {
    if (gap >= targetUs + targetUs / 2) ++out->over1_5xCount;
    if (gap >= targetUs * 2) ++out->over2xCount;
  }
  // Frames per second over the window the samples actually span, so a stalled second cannot
  // be hidden by averaging against wall-clock.
  const uint64_t spanUs = (windowEndUs > windowStartUs) ? (windowEndUs - windowStartUs) : 0;
  out->fpsX100 = spanUs > 0
                     ? static_cast<uint32_t>((static_cast<uint64_t>(n) * 100000000ULL) / spanUs)
                     : 0;
  return true;
}

void AndroidVideoDecoderSink::RememberReplayPtsLocked(int64_t ptsUs) {
  if (replayPtsCount_ < kReplayPtsSlots) replayPts_[replayPtsCount_++] = ptsUs;
}

bool AndroidVideoDecoderSink::TakeReplayPtsLocked(int64_t ptsUs) {
  for (size_t i = 0; i < replayPtsCount_; ++i) {
    if (replayPts_[i] == ptsUs) {
      replayPts_[i] = replayPts_[--replayPtsCount_];
      return true;
    }
  }
  return false;
}

void AndroidVideoDecoderSink::PublishDecodeStateLocked() {
  // r5 (Codex F1 contract): no surface = intended inactivity (not measured). A selection being set up is
  // active viewing: it is held back only by the bounded startup grace, not indefinitely.
  // apk-decoder-liveness r1: the surface the UI set (its request), not the worker's applied window -- the
  // validity does not wait for the codec.
  const bool valid = surfaceRequested_;
  // The generation moves whenever what the decoder's count means could have changed: validity, the
  // surface, the selection being prepared / abandoned and the stream generation it was answered with.
  uint64_t key = surfaceSeq_;
  key = key * 1000003ULL ^ binding_;  // keepalive-multi r1: a new binding starts a new measurement
  key = key * 1000003ULL ^ (valid ? 1ULL : 0ULL);
  key = key * 1000003ULL ^ gate_.pending();
  // r5: not readySelectionGeneration_ -- it flips when the selection's first picture is released, after
  // that picture (and the ones decoded with it) were counted into the generation prepare / the answer began.
  key = key * 1000003ULL ^ gate_.expected();
  if (!decodeKeySet_ || key != decodeKeyHash_) {
    decodeKeyHash_ = key;
    decodeKeySet_ = true;
    ++decodeGeneration_;
    // r6: a new measurement -- inside a grace that has not ended, from its deadline.
    span_.OnGeneration(steady_now_us(), decodedTotal_, graceActive_, graceDeadlineUs_);
  }
  remote60::native_poc::ClientDecodeState s;
  s.binding = binding_;
  s.valid = valid;
  s.decodedTotal = decodedTotal_;
  s.width = outputWidth_;
  s.height = outputHeight_;
  s.generation = decodeGeneration_;
  s.spanId = span_.id();
  s.spanStartUs = span_.startUs();
  s.spanStartTotal = span_.startTotal();
  decodeSnapshot_.Publish(s);
}

void AndroidVideoDecoderSink::StartGraceLocked(bool explicitEpisode) {
  if (!explicitEpisode && !graceEligible_) return;  // no stretching: one grace until a picture is counted
  graceActive_ = true;
  graceEligible_ = false;
  graceDeadlineUs_ = steady_now_us() + kStartupGraceUs;
  span_.OnGraceStart(decodedTotal_, graceDeadlineUs_);  // r6: measured from the deadline, or its first picture
}

void AndroidVideoDecoderSink::OnDecodeEpisodeStartAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  StartGraceLocked(true);
}

uint64_t AndroidVideoDecoderSink::DecodeGenerationAs(uint64_t token) {
  if (token == kAnyBinding) return decodeSnapshot_.generation();
  remote60::native_poc::ClientDecodeState s;
  if (!decodeSnapshot_.Read(&s) || s.binding != token) return 0;  // not this binding's decoder: nothing of it
  return s.generation;
}

// Called by the session's control thread on its due passes; reads only what PublishDecodeStateLocked
// stored (r3 R2: one consistent copy, a sequence read, no lock). false = the decoder's state is not
// meaningful now: no surface (intended inactivity), or an active transition's startup still inside its
// grace -- the session then reports nothing for that window instead of "0 fps". A codec reset for a loss
// stays valid: frames that do not decode then are a real stall. A generation change does not make the
// drain invalid (r5): a new measurement begins there.
// r6 (Codex S1): the measurement in progress, cumulative -- the count since it began and the time since
// then, on this sink's clock. Reading consumes nothing, so the session may ask again a little later
// (ClientTelemetryWindow::Hold) without losing what it had.
bool AndroidVideoDecoderSink::DrainDecodeStatsAs(uint64_t token, remote60::native_poc::ClientDecodeStats* out) {
  if (!out) return false;
  remote60::native_poc::ClientDecodeState s;
  if (!decodeSnapshot_.Read(&s)) return false;  // a writer kept overlapping: no copy this window
  // keepalive-multi r1: another PC's decode is not this PC's report (checked in the same consistent copy).
  if (token != kAnyBinding && s.binding != token) return false;
  remote60::native_poc::ClientDecodeSpan span;
  if (!remote60::native_poc::ReadDecodeSpan(s, steady_now_us(), &span)) return false;
  out->spanId = span.id;
  out->spanFrames = span.frames;
  out->spanUs = span.us;
  out->width = s.width;
  out->height = s.height;
  out->generation = s.generation;
  return true;
}

bool AndroidVideoDecoderSink::ConsumeDecoderKeyframeRequestAs(uint64_t token) {
  return TakeDecoderKeyRequestAs(token, nullptr);
}

// apk-inbox C (design r3 §3): the want now, with what it is for. The episode hands out its ask at t0 and then only
// when due (1 / 2 / 4 s after the previous SEND), never while a usable IDR is preserved; the one-shots once each.
bool AndroidVideoDecoderSink::TakeDecoderKeyRequestAs(uint64_t token, remote60::native_poc::DecoderKeyRequest* out) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  remote60::native_poc::DecoderKeyRequest r;
  r.sinkEpoch = keyEpoch_;
  if (wantCodecUp_) r.causes |= remote60::native_poc::kKeyCauseCodecUp;
  if (wantSelectionAbort_) r.causes |= remote60::native_poc::kKeyCauseSelectionAbort;
  wantCodecUp_ = false;
  wantSelectionAbort_ = false;
  uint64_t episode = 0;
  const bool candidate = RecoveryCandidateLocked();
  // While the selection is owed an IDR (gate_.KeyLost / Rearm: the session asks for it, bounded 8 x 1 s) that duty IS
  // this IDR: the episode adds no asks of its own on top (design r3 §3: one logical exchange, not 4 + 8). Its budget is
  // untouched; the commit that pays the duty ends the episode too (an IDR after the loss point).
  const bool owedCovers = gate_.OwedToken() != 0;
  if (!owedCovers && recovery_.TakeWant(steady_now_us(), candidate, &episode)) {
    r.causes |= remote60::native_poc::kKeyCauseRecovery;
    r.episodeId = episode;
  }
  TickRecoveryLocked();  // logs an exhaustion TakeWant's own tick reached
  if (r.empty()) return false;
  ++keyRequestsTaken_;
  if (out) *out = r;
  return true;
}

// apk-inbox C (D8): which causes of a held / accepted request still hold, asked by the session right before it goes
// on (to the limiter again / to the send). A short query under mu_ -- never across the network.
uint32_t AndroidVideoDecoderSink::DecoderKeyRequestValidCausesAs(uint64_t token,
                                                                 const remote60::native_poc::DecoderKeyRequest& r) {
  using namespace remote60::native_poc;
  const uint32_t sessionScoped = r.causes & (kKeyCausePipeline | kKeyCauseLegacy);
  LockedState lock(this);
  if (!OwnerLocked(token)) return sessionScoped;  // another PC on screen: none of this sink's state is that request's
  uint32_t valid = sessionScoped;
  const bool sameEpoch = r.sinkEpoch == keyEpoch_;
  if ((r.causes & kKeyCauseRecovery) != 0 && sameEpoch && recovery_.StillValid(r.episodeId)) {
    if (!RecoveryCandidateLocked()) {
      valid |= kKeyCauseRecovery;
    } else {
      recovery_.OnWantDropped(r.episodeId);  // moot for now: a usable IDR is preserved
      ++recoveryDroppedMoot_;
    }
  }
  if ((r.causes & kKeyCauseCodecUp) != 0 && sameEpoch && codecAwaitsKey_) valid |= kKeyCauseCodecUp;
  if ((r.causes & kKeyCauseSelectionAbort) != 0 && sameEpoch) valid |= kKeyCauseSelectionAbort;
  if ((r.causes & kKeyCauseOwed) != 0 && r.owedToken != 0 && gate_.OwedToken() == r.owedToken) valid |= kKeyCauseOwed;
  return valid;
}

// apk-inbox C: the control exchange for this request started -- the episode's attempt, on the sink's own clock.
void AndroidVideoDecoderSink::OnDecoderKeyRequestSentAs(uint64_t token, const remote60::native_poc::DecoderKeyRequest& r,
                                                        uint64_t /* nowUs: the session's clock */) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  if ((r.causes & remote60::native_poc::kKeyCauseRecovery) != 0 && r.sinkEpoch == keyEpoch_) {
    recovery_.OnSent(r.episodeId, steady_now_us());
  }
}

void AndroidVideoDecoderSink::OnWindowSelectionControlResultAs(
    uint64_t token, const remote60::native_poc::ControlWindowSelectedMessage& msg) {
  // Untagged: taken as the answer to whatever selection is armed (the old behaviour).
  uint64_t tag = 0;
  {
    LockedState lock(this);
    if (!OwnerLocked(token)) return;
    tag = gate_.pending();
  }
  OnWindowSelectionControlResultForAs(token, msg, tag);  // checks the owner again, under the lock it mutates in
}

uint64_t AndroidVideoDecoderSink::KeyframeOwedForAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return 0;
  return gate_.OwedToken();
}

uint64_t AndroidVideoDecoderSink::CurrentSelectionTagAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return 0;
  return gate_.pending();
}

void AndroidVideoDecoderSink::OnWindowSelectionControlResultForAs(
    uint64_t token, const remote60::native_poc::ControlWindowSelectedMessage& msg, uint64_t requestTag) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  const auto ack = gate_.OnAck((msg.flags & 0x1u) != 0, msg.streamGeneration, requestTag);
  char line[224];
  if (ack.result == remote60::native_poc::SelectionAckGate::AckResult::Ignored) {
    // t-970r4zgo: an answer to another selection (an earlier pick, or one already answered).
    std::snprintf(line, sizeof(line), "selection ack ignored localGen=%llu requestTag=%llu flags=%u streamGen=%llu",
                  static_cast<unsigned long long>(gate_.pending()), static_cast<unsigned long long>(requestTag),
                  msg.flags, static_cast<unsigned long long>(msg.streamGeneration));
    log_info(line);
    return;
  }
  if (ack.result == remote60::native_poc::SelectionAckGate::AckResult::Applied) {
    ResetPtsStateLocked();
    // The new generation's IDR got here before its answer and was dropped: ask for another
    // rather than wait on deltas that reference it (rate-limited by the session).
    // An IDR of this generation already came and was dropped: the selection is now owed one
    // (gate_.KeyOwed), which the session asks for until it arrives (KeyframeOwedFor, r4 M1-A).
    std::snprintf(line, sizeof(line),
                  "selection ack localGen=%llu streamGen=%llu hostSendQpcUs=%llu rekey=%u",
                  static_cast<unsigned long long>(gate_.pending()),
                  static_cast<unsigned long long>(gate_.expected()),
                  static_cast<unsigned long long>(msg.hostSendQpcUs), ack.requestKeyframe ? 1u : 0u);
    log_info(line);
    return;
  }
  std::snprintf(line, sizeof(line), "selection ack failed localGen=%llu flags=%u",
                static_cast<unsigned long long>(gate_.pending()), msg.flags);
  log_info(line);
}

// keepalive-r2 merge: the local selection (apk-0232) owner-checked like every selection call (*As, keepalive-multi).
bool AndroidVideoDecoderSink::BeginLocalSelectionForAs(uint64_t token, uint64_t tag) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  const bool begun = gate_.BeginLocal(tag);
  char line[160];
  std::snprintf(line, sizeof(line), "selection local (logon screen) localGen=%llu begun=%u",
                static_cast<unsigned long long>(tag), begun ? 1u : 0u);
  log_info(line);
  return begun;
}

uint64_t AndroidVideoDecoderSink::LocalSelectionBoundForAs(uint64_t token, uint64_t tag) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return 0;
  return gate_.LocalBoundFor(tag);
}

// apk-0232 r3 (Codex N1): judged and recorded under the state Prepare/Abort (the UI's JNI calls) take -- a cancel
// cannot finish between "still this selection, bound" and the panel's record.
remote60::native_poc::ClientEncodedFrameSink::LocalCommit AndroidVideoDecoderSink::CommitLocalSelectionForAs(
    uint64_t token, uint64_t tag, const std::function<void(uint64_t generation)>& commit) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return LocalCommit::Abandoned;  // not on screen: nothing of it completes
  if (tag == 0 || gate_.pending() != tag) return LocalCommit::Abandoned;
  const uint64_t generation = gate_.LocalBoundFor(tag);
  if (generation == 0) return LocalCommit::Unbound;
  commit(generation);
  return LocalCommit::Committed;
}

void AndroidVideoDecoderSink::RefuseSelectionForAs(uint64_t token, uint64_t tag) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  const bool refused = gate_.Refuse(tag);
  char line[160];
  std::snprintf(line, sizeof(line), "selection refused (logon screen) localGen=%llu refused=%u",
                static_cast<unsigned long long>(tag), refused ? 1u : 0u);
  log_info(line);
}

bool AndroidVideoDecoderSink::PrepareForWindowSelectionAs(uint64_t token, uint64_t selectionGeneration) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  RequestCodecResetLocked();
  ClearInboxLocked();
  ResetCodecRetryLocked();  // r4 N3
  InvalidateKeyAsksLocked();  // apk-inbox C: the old selection's asks are moot (inbox contrast [5])
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  gate_.Prepare(selectionGeneration);
  StartGraceLocked(false);  // r5: a selection is an active transition
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  csd0_.clear();
  csd1_.clear();

  char line[160];
  std::snprintf(line, sizeof(line), "selection prepare localGen=%llu",
                static_cast<unsigned long long>(gate_.pending()));
  log_info(line);
  return true;
}

bool AndroidVideoDecoderSink::AbortWindowSelectionAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  RequestCodecResetLocked();
  ClearInboxLocked();
  ResetCodecRetryLocked();  // r4 N3
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  gate_.Reset();
  readySelectionGeneration_ = 0;
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  csd0_.clear();
  csd1_.clear();
  // mobile-abr r5 (Codex F1 contract): a switch abandoned while the stream stays active must resume,
  // not wait for the host's next periodic IDR: the next frame asks the session for one (its limiter
  // applies). Inside the transition's grace only; the app pauses the stream before it aborts, so then
  // nothing is measured anyway.
  StartGraceLocked(false);
  InvalidateKeyAsksLocked();  // apk-inbox C: what was asked before is moot -- the abort's own ask is new
  NoteKeyWantLocked(kWantSelectionAbort);
  wantSelectionAbort_ = true;
  log_info("selection aborted");
  return true;
}

std::string AndroidVideoDecoderSink::DebugStatus() {
  LockedState lock(this);
  std::string status = "surface=";
  status += window_ ? "on" : "off";
  status += " codec=";
  status += codec_ ? "on" : "off";
  status += " size=" + std::to_string(configuredWidth_) + "x" + std::to_string(configuredHeight_);
  status += " output=" + std::to_string(outputWidth_) + "x" + std::to_string(outputHeight_);
  status += " csd=" + std::to_string(csd0_.empty() ? 0 : 1) + "/" + std::to_string(csd1_.empty() ? 0 : 1);
  status += " in=" + std::to_string(inputFrameCount_);
  status += " out=" + std::to_string(outputFrameCount_);
  status += " sel=" + std::to_string(gate_.pending());
  status += " readySel=" + std::to_string(readySelectionGeneration_);
  status += " expectGen=" + std::to_string(gate_.expected());
  status += " inGen=" + std::to_string(latestInputStreamGeneration_);
  status += " outGen=" + std::to_string(latestOutputStreamGeneration_);
  status += " stale=" + std::to_string(staleFrameDropCount_);
  status += " oversize=" + std::to_string(oversizedInputFrameDropCount_);
  status += " ptsGen=" + std::to_string(playoutClock_.StreamGeneration());
  status += " ptsBaseRemote=" + std::to_string(playoutClock_.RemoteBaseUs());
  status += " ptsBaseLocal=" + std::to_string(playoutClock_.LocalBaseUs());
  status += " lastInPtsUs=" + std::to_string(playoutClock_.LastPresentAtUs());
  status += " lastRemoteUs=" + std::to_string(playoutClock_.LastRemoteCaptureUs());
  status += " playoutStepUs=" + std::to_string(playoutClock_.StepUs());
  status += " playoutLeadUs=" + std::to_string(playoutClock_.LeadUs());
  status += " ptsReanchor=" + std::to_string(playoutClock_.ReanchorCount());
  status += " ptsClamp=" + std::to_string(playoutClock_.MonotonicClampCount());
  status += " ptsFallback=" + std::to_string(playoutClock_.FallbackCount());
  status += " pending=" + std::to_string(pendingFrame_.has() ? 1 : 0);
  status += " gateQueueDrops=" + std::to_string(pendingFrame_.gateDrops());  // apk-0232 r2
  status += " pendingRetry=" + std::to_string(pendingFrameQueueRetryCount_);
  status += " bootstrapReplay=" + std::to_string(bootstrapReplayCount_);
  status += " lastOutUs=" + std::to_string(lastOutputPresentationUs_);
  status += " awaitingAck=" + std::to_string(gate_.awaiting() ? 1 : 0);
  status += " localSel=" + std::to_string(gate_.local() ? 1 : 0);  // apk-0232 r1
  // keepalive r5 D1: the codec's own answers (see the members).
  status += " codecs=" + std::to_string(codecsStarted_);
  status += " inDeq=" + std::to_string(lastInputDequeue_);
  status += " outDeq=" + std::to_string(lastOutputDequeue_);
  status += " outDequeued=" + std::to_string(outputDequeued_);
  status += " fmt=" + std::to_string(formatChanges_);
  status += " held=" + std::to_string(heldOutputValid_ ? 1 : 0);
  status += " awaitsKey=" + std::to_string(codecAwaitsKey_ ? 1 : 0);
  status += " coded=" + std::to_string(formatWidth_) + "x" + std::to_string(formatHeight_);
  status += " cropSrc=" + std::string(cropSource_);
  status += " crop=" + std::to_string(cropLeft_) + "," + std::to_string(cropTop_) + "," + std::to_string(cropRight_) +
            "," + std::to_string(cropBottom_);
  status += " deltaBeforeKey=" + std::to_string(deltaBeforeKeyDrops_);
  // apk-decoder-liveness r1: the worker's side.
  status += " codecState=" + std::string(codecState_);
  status += " codecFail=" + std::to_string(codecFailures_);
  status += " codecVoid=" + std::to_string(codecVoidStarts_);
  status += " inbox=" + std::to_string(inbox_.frames());
  status += " inboxBytes=" + std::to_string(inbox_.bytes());  // r4 N2
  status += " inboxDrops=" + std::to_string(inbox_.lost());   // apk-inbox C: frames lost to the chain (trim / gap / oversize)
  status += " gapDrops=" + std::to_string(inbox_.gapDeltas());
  // apk-inbox C: the chain inbox and the recovery episode
  status += " chainSkipped=" + std::to_string(inbox_.skipped()) + " chainDup=" + std::to_string(inbox_.duplicates()) +
            " chainStale=" + std::to_string(inbox_.stale()) + " tailGap=" + std::to_string(inbox_.gapOpen() ? 1 : 0);
  status += " recovery=" + std::string(recovery_.StateName()) + " episode=" + std::to_string(recovery_.id()) +
            " asks=" + std::to_string(recovery_.attempts()) + " episodes=" + std::to_string(recovery_.opened()) +
            " recClosed=" + std::to_string(recovery_.closed()) + " recExhausted=" + std::to_string(recovery_.exhausted()) +
            " recWants=" + std::to_string(recovery_.wants()) + " recSent=" + std::to_string(recovery_.sent()) +
            " recMoot=" + std::to_string(recoveryDroppedMoot_) + " keyTaken=" + std::to_string(keyRequestsTaken_) +
            " keyEpoch=" + std::to_string(keyEpoch_);
  status += " resetPending=" + std::to_string(CodecResetPendingLocked() ? 1 : 0);
  status += " codecRetries=" + std::to_string(codecAutoRetries_);       // r4 N3
  status += " queueVoided=" + std::to_string(queueVoided_);             // r4 N1
  status += " inboxRefused=" + std::to_string(inboxOversizeDrops_);     // r4 N2
  // apk-decoder-inbox r1: how often the decoder set its key-request want, by cause (a want is consumed by the
  // session per frame / answer; it is not a request sent)
  status += " keyWant=ovs:" + std::to_string(keyWant_[kWantOversize]) + ",ovf:" + std::to_string(keyWant_[kWantInboxOverflow]) +
            ",gap:" + std::to_string(keyWant_[kWantGapDelta]) + ",await:" + std::to_string(keyWant_[kWantAwaitKey]) +
            ",pend:" + std::to_string(keyWant_[kWantPendingSlot]) + ",abort:" + std::to_string(keyWant_[kWantSelectionAbort]) +
            ",up:" + std::to_string(keyWant_[kWantCodecUp]);
  return status;
}

std::string AndroidVideoDecoderSink::LivenessTraceLine() const {
#ifdef GNLINK_LIVENESS_TRACE
  const uint64_t now = LtNowUs();
  auto age = [now](uint64_t t) -> long long {
    return t == 0 ? -1 : static_cast<long long>((now >= t ? now - t : 0) / 1000);
  };
  char line[160];
  std::snprintf(line, sizeof(line), "sink pass=%lldms codecCall=%lldms tid=%llu inbox=%llu",
                age(ltWorkerPassUs_.load(std::memory_order_relaxed)),
                age(ltCodecCallSinceUs_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltWorkerTid_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInbox_.load(std::memory_order_relaxed)));
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
  char more[320];
  // r2 labels: inboxHw = admitted after the trim; inboxPeakPre = including the frame just added, before the trim;
  // rxLockScope = from taking mu_ to the end of the call's scope (an upper bound of the hold: it includes what runs
  // after the unlock in the same scope)
  std::snprintf(more, sizeof(more), " | inboxHw=%llu/%lluB inboxPeakPre=%llu/%lluB passFramesMax=%llu passSvcMax=%llums "
                "rxLockWaitMax=%llums rxLockScopeMax=%llums",
                static_cast<unsigned long long>(ltInboxHwFrames_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInboxHwBytes_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInboxPeakPreFrames_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInboxPeakPreBytes_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltPassFramesMax_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltPassServiceMaxUs_.load(std::memory_order_relaxed) / 1000),
                static_cast<unsigned long long>(ltRxLockWaitMaxUs_.load(std::memory_order_relaxed) / 1000),
                static_cast<unsigned long long>(ltRxLockHoldMaxUs_.load(std::memory_order_relaxed) / 1000));
  out += more;
  char aq[96];
  std::snprintf(aq, sizeof(aq), " unmatched=%llu replay=%llu",
                static_cast<unsigned long long>(ltAgeQueueUnmatched_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltAgeQueueReplay_.load(std::memory_order_relaxed)));
  out += " | agePop " + hist(ltAgePop_) + " | ageQueueAU " + hist(ltAgeQueue_) + aq + " | api";
  // r2: "createCfgStart" is ONE span (create + configure + start), not three calls measured apart
  static const char* kApiNames[kApiKinds] = {"other", "createCfgStart", "stopDel", "name", "deqIn", "queueIn",
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
#else
  return std::string();
#endif
}

uint64_t AndroidVideoDecoderSink::VideoSizePacked() {
  LockedState lock(this);
  const uint32_t width = outputWidth_ > 0 ? outputWidth_ : configuredWidth_;
  const uint32_t height = outputHeight_ > 0 ? outputHeight_ : configuredHeight_;
  return (static_cast<uint64_t>(width) << 32u) | static_cast<uint64_t>(height);
}

uint64_t AndroidVideoDecoderSink::ReadySelectionGeneration() {
  LockedState lock(this);
  return readySelectionGeneration_;
}

uint64_t AndroidVideoDecoderSink::LastOutputPresentationUs() {
  LockedState lock(this);
  return lastOutputPresentationUs_;
}

bool AndroidVideoDecoderSink::EnsureCodecLocked(uint32_t width, uint32_t height, bool keyFrame) {
  if (codec_ && configuredWidth_ == width && configuredHeight_ == height) {
    return true;
  }
  if (!window_ || width == 0 || height == 0 || csd0_.empty() || csd1_.empty()) {
    return false;
  }
  // r5 N3 (Codex src r4: one IDR then 40 deltas made 41 creates in 2 s, codecRetries=0 -- every frame called this
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
  }

  ResetCodecLocked();
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
  AMediaCodec* codec = CodecCall(kApiCreate, [&]() -> AMediaCodec* {
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
    // r4 N3 / r5: tried again after kCodecRetryIntervalUs (by the next frame or the worker's timer, whichever comes
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
    return false;
  }
  if (resetSeq != codecResetSeq_ || window != window_ || workerStop_) {
    // Made for a surface / owner / selection that is gone: never used.
    CodecCall(kApiStopDelete, [&] {
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
  codecRetryAtUs_ = 0;  // r4 N3
  if (retry) {
    // r4 N3: a codec made after a failure can take an IDR again -- an owed one is asked for afresh (Rearm: a new
    // token, the session's same bounded asks); r5: whichever path made it.
    gate_.Rearm();
    NoteKeyWantLocked(kWantCodecUp);
    wantCodecUp_ = true;  // apk-inbox C: its own cause, taken once
    char rline[160];
    std::snprintf(rline, sizeof(rline), "codec made on retry %u after a failure: an owed IDR is asked for afresh (token=%llu)",
                  codecAutoRetries_, static_cast<unsigned long long>(gate_.OwedToken()));
    log_info(rline);
  }
  codecAutoRetries_ = 0;

  ++codecsStarted_;
  codecEpoch_.store(static_cast<uint32_t>(codecsStarted_), std::memory_order_release);
  codecAwaitsKey_ = true;
  formatWidth_ = 0;  // viewer-sharpness r1 A: no format of this instance yet -- nothing of the last one is kept
  formatHeight_ = 0;
  cropSource_ = "none";
  cropLeft_ = cropTop_ = cropRight_ = cropBottom_ = 0;
  outputDequeued_ = 0;
  formatChanges_ = 0;
  lastInputDequeue_ = 0;
  lastOutputDequeue_ = 0;
  configuredWidth_ = width;
  configuredHeight_ = height;
  outputWidth_ = width;
  outputHeight_ = height;
  char line[224];
  std::string name = CodecCall(kApiName, [&] {
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
  return true;
}

// r4 N3 (Codex src r1): the worker's own bounded retry of a codec that could not be made (see the members). The
// SPS/PPS and size it needs are the stream's (kept); a codec made here can take an IDR again, so an owed one is
// asked for afresh (Rearm: a new token) -- the session then asks with its same bounded budget.
// r5: the timer's half only -- the deadline, the count and the re-arm are EnsureCodecLocked's, shared with frames.
void AndroidVideoDecoderSink::RetryCodecLocked() {
  if (codec_ || !window_ || codecTerminal_ || codecRetryAtUs_ == 0 || steady_now_us() < codecRetryAtUs_) return;
  EnsureCodecLocked(configuredWidth_, configuredHeight_);
}

void AndroidVideoDecoderSink::PumpCodecLocked() {
  const uint64_t nowUs = steady_now_us();

  if (pendingFrame_.has() && codec_) {
    // apk-0232 r2 (N2): the held frame goes in only if the gate still admits its generation; else it is dropped.
    if (pendingFrame_.Pump(gate_, [this](remote60::native_poc::UdpH264AssembledFrame& f) { return TryQueueFrameLocked(f); })) {
      pendingFrameCount_ = 0;
      pendingFrameQueueRetryCount_ = 0;
    } else {
      ++pendingFrameQueueRetryCount_;
    }
  }

  if (codec_ &&
      bootstrapFrame_.has_value() &&
      gate_.PermitsQueue(bootstrapFrame_->header.streamGeneration, true) &&  // apk-0232 r2 (N2)
      gate_.pending() != 0 &&
      readySelectionGeneration_ != gate_.pending() &&
      outputFrameCount_ == 0 &&
      bootstrapReplayCount_ < kBootstrapReplayMaxCount &&
      lastInputQueueSteadyUs_ > 0 &&
      nowUs >= lastInputQueueSteadyUs_ + kBootstrapReplayIntervalUs) {
    auto replayFrame = *bootstrapFrame_;
    queueingReplay_ = true;  // r3 R1: TryQueueFrameLocked remembers the PTS this replay is queued with
    const bool replayQueued = TryQueueFrameLocked(replayFrame);
    queueingReplay_ = false;
    if (replayQueued) {
      ++bootstrapReplayCount_;
      char line[192];
      std::snprintf(line, sizeof(line),
                    "bootstrap replay queued localGen=%llu streamGen=%llu replay=%llu",
                    static_cast<unsigned long long>(gate_.pending()),
                    static_cast<unsigned long long>(gate_.expected()),
                    static_cast<unsigned long long>(bootstrapReplayCount_));
      log_info(line);
    }
  }

  DrainOutputLocked();
}

// apk-decoder-liveness r1: the worker only (the frame is the worker's -- a local or its pending slot); every codec
// call with mu_ released. The decisions around them (gate, PTS, counts, the owed key) are made under mu_.
//
// r4 N1 (Codex src r1): the gate is RESERVED for the frame before the first codec call (SelectionAckGate::
// ReserveQueue) and the frame counts only if that reservation is still current when the queue has returned
// (CommitQueue). In between the receive side cannot move it (it no longer picks a candidate); a control-side change
// makes it stale. r5 (Codex src r4) -- which of them ask for a codec reset: Prepare, Abort, a stream reset, a new
// binding / unbind (RequestCodecResetLocked with the gate's Prepare / Reset) do; an answer (OnAck), BeginLocal and
// Refuse do NOT -- they only leave the awaiting state, and while awaiting PermitsQueue admits nothing, so no
// reservation can exist for them to make stale. A stale commit with no reset pending is that unreachable case's
// defence (voidExchange: a reset asked for here). The contract, per exchange:
//   * a change seen after the input buffer was dequeued and before the queue: the frame is NOT queued (that codec is
//     voided: a reset is pending or asked for here, so its dequeued buffer goes with it);
//   * a change while the queue call itself runs: the bytes went into a codec the change has voided -- they pay no
//     owed key, bind nothing, start no bootstrap, and no picture of that codec is shown (the reset is pending);
//   * an exchange that begins after the change is judged by the new state.
bool AndroidVideoDecoderSink::TryQueueFrameLocked(remote60::native_poc::UdpH264AssembledFrame& frame) {
  if (!codec_ || CodecResetPendingLocked()) return false;
  AMediaCodec* const codec = codec_;
  const uint64_t resetSeq = codecResetSeq_;
  const bool isKey = (frame.header.flags & 1u) != 0;
  const auto reservation = gate_.ReserveQueue(frame.header.streamGeneration, isKey);
  if (!reservation.valid) return false;  // the callers asked PermitsQueue under this same lock: not reached
  // Ends the exchange without the frame: its candidacy ends, and a codec whose buffer was dequeued for a voided
  // exchange is not kept (a reset already pending covers it).
  auto voidExchange = [&](const char* why) {
    gate_.AbortQueue(reservation);
    if (!CodecResetPendingLocked()) {
      log_error(why);
      RequestCodecResetLocked();
    }
    ++queueVoided_;
  };

  size_t inputBufferSize = 0;
  uint8_t* inputBuffer = nullptr;
  const ssize_t inputIndex = CodecCall(kApiDequeueIn, [&]() -> ssize_t {
    const ssize_t index = AMediaCodec_dequeueInputBuffer(codec, 0);
    if (index >= 0) inputBuffer = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(index), &inputBufferSize);
    return index;
  });
  lastInputDequeue_ = static_cast<int64_t>(inputIndex);
  if (inputIndex < 0) {
    gate_.AbortQueue(reservation);  // no buffer: the frame is held (DecoderPendingSlot) and asked about again
    return false;
  }
  if (resetSeq != codecResetSeq_ || codec_ != codec || !gate_.ReservationCurrent(reservation)) {
    // Changed while the buffer was dequeued: the frame is not queued (r4 N1).
    voidExchange("codec input: the selection / codec changed during the dequeue -- frame not queued, codec voided");
    return false;
  }
  if (!inputBuffer) {
    gate_.AbortQueue(reservation);
    log_error("AMediaCodec_getInputBuffer returned null");
    ResetCodecLocked();
    return false;
  }
  if (frame.payload.size() > inputBufferSize) {
    gate_.AbortQueue(reservation);
    ++oversizedInputFrameDropCount_;
    char line[192];
    std::snprintf(line, sizeof(line),
                  "dropping oversized codec input bytes=%zu inputBuffer=%zu drops=%llu",
                  frame.payload.size(), inputBufferSize,
                  static_cast<unsigned long long>(oversizedInputFrameDropCount_));
    log_error(line);
    ResetCodecLocked();
    return false;
  }

  const int64_t ptsUs = static_cast<int64_t>(ComputeQueuedPtsUsLocked(frame));
  const media_status_t queued = CodecCall(kApiQueueIn, [&] {
    std::memcpy(inputBuffer, frame.payload.data(), frame.payload.size());
    return AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(inputIndex), 0,
                                        static_cast<size_t>(frame.payload.size()), ptsUs, 0);
  });
  if (queued != AMEDIA_OK) {
    gate_.AbortQueue(reservation);
    log_error("AMediaCodec_queueInputBuffer failed");
    ResetCodecLocked();
    return false;
  }

  ++inputFrameCount_;
  // A reset asked for while it was queued: the codec is about to go -- the frame went into a codec that will not
  // show it, so it pays no owed key and starts no bootstrap (r4: and its reservation ends, if still current).
  if (resetSeq != codecResetSeq_) {
    gate_.AbortQueue(reservation);
    return true;
  }
  // r5 N1: this is where an owed IDR is paid -- it is in the decoder's input, not just past the gate. r4 N1: and only
  // under the reservation it was queued in; a stale one (no reset with it -- not expected) voids this codec.
  if (!gate_.CommitQueue(reservation)) {
    voidExchange("codec input: queued under a stale selection with no reset asked for -- codec voided");
    return true;
  }
  if (queueingReplay_) RememberReplayPtsLocked(ptsUs);
  lastInputQueueSteadyUs_ = steady_now_us();
  if (isKey) codecAwaitsKey_ = false;  // keepalive r5 D1: this instance has its IDR
  // apk-inbox C (design r3 §2): the episode ends only here -- an IDR after the loss point committed to the codec.
  if (isKey && recovery_.OnKeyCommitted(frame.header.seq)) {
    char cline[160];
    std::snprintf(cline, sizeof(cline), "decoder recovery: IDR seq=%u committed -- episode %llu closed after %u asks",
                  frame.header.seq, static_cast<unsigned long long>(recovery_.id()), recovery_.attempts());
    log_info(cline);
  }
  REMOTE60_LT({
    // r2: THIS AU's enqueue time (by identity). A bootstrap replay is the same AU queued again: not an arrival, not
    // counted. An AU whose record the ring has lost (64+ AUs later) is counted as unmatched -- never another's time.
    if (queueingReplay_) {
      ltAgeQueueReplay_.fetch_add(1, std::memory_order_relaxed);
    } else {
      LtEnq& e = ltEnq_[frame.header.seq % kLtEnqRing];
      if (e.us != 0 && e.seq == frame.header.seq && e.gen == frame.header.streamGeneration) {
        LtAge(ltAgeQueue_, LtNowUs() - e.us);
        e.us = 0;  // one AU, one age
      } else {
        ltAgeQueueUnmatched_.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });
  if ((frame.header.flags & 1u) != 0 && gate_.pending() != 0 &&
      readySelectionGeneration_ != gate_.pending()) {
    bootstrapFrame_ = frame;
  }
  if ((inputFrameCount_ % 30) == 1) {
    char line[192];
    std::snprintf(line, sizeof(line), "queued h264 frame count=%llu bytes=%zu key=%u ptsUs=%lld",
                  static_cast<unsigned long long>(inputFrameCount_),
                  frame.payload.size(),
                  (frame.header.flags & 1u) ? 1u : 0u,
                  static_cast<long long>(ptsUs));
    log_info(line);
  }
  return true;
}

uint64_t AndroidVideoDecoderSink::ComputeQueuedPtsUsLocked(
    const remote60::native_poc::UdpH264AssembledFrame& frame) {
  const uint64_t localNowUs = steady_now_us();
  const auto decision = playoutClock_.Schedule(localNowUs, frame.header.captureQpcUs,
                                               frame.header.streamGeneration);
  if (decision.reanchored) {
    const uint64_t count = playoutClock_.ReanchorCount();
    if (count == 1 || (count % 30u) == 1u) {
      char line[256];
      std::snprintf(line, sizeof(line),
                    "pts reanchor reason=%s streamGen=%llu remoteUs=%llu localUs=%llu "
                    "stepUs=%llu count=%llu",
                    decision.reanchorReason,
                    static_cast<unsigned long long>(frame.header.streamGeneration),
                    static_cast<unsigned long long>(frame.header.captureQpcUs),
                    static_cast<unsigned long long>(localNowUs),
                    static_cast<unsigned long long>(playoutClock_.StepUs()),
                    static_cast<unsigned long long>(count));
      log_info(line);
    }
  }
  return decision.presentAtUs;
}

void AndroidVideoDecoderSink::ResetPtsStateLocked() { playoutClock_.Reset(); }

void AndroidVideoDecoderSink::ResetCodecLocked() {
  // Deliberately not ResetPtsStateLocked(). Losing the codec invalidates reference pictures,
  // not the relationship between the host's capture clock and this one -- and that
  // relationship is the only thing holding playback steady. Dropping it on every recovered
  // packet loss made each loss cost a visible timeline jump on top of the missing frames.
  // When the timeline really has moved, the clock notices by itself: a new stream generation,
  // a capture timestamp going backwards, or a gap too large to pace across all reanchor it.
  // r5 N1: a held IDR thrown away with the codec was never delivered.
  if (pendingFrame_.held() && (pendingFrame_.held()->header.flags & 1u) != 0) {
    gate_.KeyLost(pendingFrame_.held()->header.streamGeneration);
  }
  pendingFrame_.Reset();
  pendingFrameCount_ = 0;
  pendingFrameQueueRetryCount_ = 0;
  bootstrapFrame_.reset();
  lastInputQueueSteadyUs_ = 0;
  bootstrapReplayCount_ = 0;
  replayPtsCount_ = 0;  // r3 R1: the replays' outputs went with the codec
  // A held buffer index belongs to the codec that produced it. Releasing it against a codec
  // that has been torn down and rebuilt would target an unrelated buffer.
  heldOutputValid_ = false;
  if (codec_) {
    // apk-decoder-liveness r1: stopped with mu_ released (the worker; inline in the destructor).
    AMediaCodec* const codec = codec_;
    codec_ = nullptr;
    codecState_ = "none";
    CodecCall(kApiStopDelete, [&] {
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
}

bool AndroidVideoDecoderSink::UpdateCodecConfigLocked(const std::vector<uint8_t>& annexb) {
  size_t cursor = 0;
  std::vector<uint8_t> sps;
  std::vector<uint8_t> pps;
  NalUnitView nal{};
  while (next_annexb_nal(annexb, &cursor, &nal)) {
    if (nal.type == 7 && sps.empty()) {
      sps = with_start_code(nal.data, nal.size);
    } else if (nal.type == 8 && pps.empty()) {
      pps = with_start_code(nal.data, nal.size);
    }
    if (!sps.empty() && !pps.empty()) break;
  }

  bool updated = false;
  if (!sps.empty() && sps != csd0_) {
    csd0_ = std::move(sps);
    updated = true;
  }
  if (!pps.empty() && pps != csd1_) {
    csd1_ = std::move(pps);
    updated = true;
  }
  if (updated) {
    char line[128];
    std::snprintf(line, sizeof(line), "updated codec config csd0=%zu csd1=%zu", csd0_.size(), csd1_.size());
    log_info(line);
  }
  return updated;
}

void AndroidVideoDecoderSink::UpdateOutputFormatLocked() {
  if (!codec_) return;

  AMediaCodec* const codec = codec_;
  AMediaFormat* format = CodecCall(kApiFormat, [&] { return AMediaCodec_getOutputFormat(codec); });
  if (!format) return;

  // The coded (buffer) size, then the visible part of it. viewer-sharpness r1 A: Codec2 decoders report the
  // visible rectangle as ONE key, "crop" (AMEDIAFORMAT_KEY_DISPLAY_CROP, a rect, inclusive), not as the four
  // crop-left/right/top/bottom integers this read before -- on a phone whose decoder codes 1080 lines as 1088
  // (SM-S948N, 2026-10-15: size=1920x1080 output=1920x1088) the crop was never found and the 8 padding lines
  // were taken as picture. The emulator's decoder reports the exact size, so it never showed there.
  int32_t width = 0;
  int32_t height = 0;
  const bool hasWidth = AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width) && width > 0;
  const bool hasHeight = AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height) && height > 0;
  if (hasWidth) outputWidth_ = static_cast<uint32_t>(width);
  if (hasHeight) outputHeight_ = static_cast<uint32_t>(height);
  formatWidth_ = outputWidth_;
  formatHeight_ = outputHeight_;

  // A crop is used only when it lies inside the coded size: 0 <= left <= right < width, 0 <= top <= bottom
  // < height. Absent and invalid are told apart from a real crop of the whole frame (0,0,w-1,h-1).
  const auto valid = [&](int32_t l, int32_t t, int32_t r, int32_t b) {
    return l >= 0 && t >= 0 && l <= r && t <= b && static_cast<uint32_t>(r) < formatWidth_ &&
           static_cast<uint32_t>(b) < formatHeight_;
  };
  int32_t l = 0, t = 0, r = 0, b = 0;
  cropSource_ = "absent";
  if (AMediaFormat_getRect(format, AMEDIAFORMAT_KEY_DISPLAY_CROP, &l, &t, &r, &b)) {
    cropSource_ = valid(l, t, r, b) ? "rect" : "invalid_rect";
  }
  if (std::strcmp(cropSource_, "rect") != 0) {
    int32_t kl = 0, kt = 0, kr = 0, kb = 0;
    if (AMediaFormat_getInt32(format, "crop-left", &kl) && AMediaFormat_getInt32(format, "crop-top", &kt) &&
        AMediaFormat_getInt32(format, "crop-right", &kr) && AMediaFormat_getInt32(format, "crop-bottom", &kb)) {
      if (valid(kl, kt, kr, kb)) {
        cropSource_ = "keys";
        l = kl, t = kt, r = kr, b = kb;
      } else if (std::strcmp(cropSource_, "absent") == 0) {
        cropSource_ = "invalid_keys";
        l = kl, t = kt, r = kr, b = kb;
      }
    }
  }
  const bool cropUsed = std::strcmp(cropSource_, "rect") == 0 || std::strcmp(cropSource_, "keys") == 0;
  if (cropUsed) {
    outputWidth_ = static_cast<uint32_t>(r - l + 1);
    outputHeight_ = static_cast<uint32_t>(b - t + 1);
  }
  cropLeft_ = l, cropTop_ = t, cropRight_ = r, cropBottom_ = b;

  char line[224];
  std::snprintf(line, sizeof(line),
                "output format coded=%ux%u visible=%ux%u crop=%d,%d,%d,%d cropSource=%s hasWidth=%d hasHeight=%d",
                formatWidth_, formatHeight_, outputWidth_, outputHeight_, l, t, r, b, cropSource_, hasWidth ? 1 : 0,
                hasHeight ? 1 : 0);
  log_info(line);
  AMediaFormat_delete(format);
}

void AndroidVideoDecoderSink::ReleaseHeldOutputLocked(uint64_t nowUs) {
  if (!heldOutputValid_ || !codec_) return;
  if (CodecResetPendingLocked()) return;  // the codec is about to go: its picture is not shown (r1)
  heldOutputValid_ = false;
  AMediaCodec* const codec = codec_;
  const uint64_t resetSeq = codecResetSeq_;

  // sh-filter: both releases stamp the buffer with heldOutputPresentUs_ (the scheduled time, or the PTS of
  // an immediate release -- the same number, in the steady domain); that timestamp is the frame's key in
  // the handoff ring (first-output gate, tap step, S4). r3 (Codex R2): the ring entry is published BEFORE
  // the buffer, so whoever latches the frame always finds it -- the renderer and the view's first latch
  // can come right after the release. `handoffNs` is therefore the moment the release was issued (just
  // before it), not after it returned. A release that fails withdraws the entry: never a frame that was
  // not handed over.
  const int64_t handoffKey = static_cast<int64_t>(heldOutputPresentUs_ * 1000ULL);
  auto publish = [&]() -> size_t {
    const size_t slot = handoffNext_++ % kHandoffRing;
    handoffKey_[slot].store(0, std::memory_order_release);
    handoffNs_[slot].store(static_cast<int64_t>(steady_now_us()) * 1000, std::memory_order_release);
    handoffMeta_[slot].store((static_cast<uint64_t>(latestOutputStreamGeneration_) << 32) |
                                 (static_cast<uint64_t>(codecsStarted_) & 0xffffffffu),
                             std::memory_order_release);
    handoffSize_[slot].store((static_cast<uint64_t>(outputWidth_) << 32) | outputHeight_, std::memory_order_release);
    handoffKey_[slot].store(handoffKey, std::memory_order_release);
    return slot;
  };
#ifdef GNLINK_SH_TEST_SEAM
  // TEST BUILD ONLY (debug + -Pgnlink.shTestSeam=true): delay between publication and release, and r2's
  // order (release, delay, publish) as the counter-case -- android_video_decoder_sh_seam.hpp.
  const bool publishAfter = sh_seam_publish_after_release();
  const size_t slot = publishAfter ? kHandoffRing : publish();
  // apk-decoder-liveness r1: the delay with the state lock released (the worker) -- RX and control never wait on it.
  if (!publishAfter) CodecCall([] { sh_seam_handoff_delay(); });
#else
  const size_t slot = publish();
#endif

  media_status_t status;
  const size_t index = heldOutputIndex_;
  if (heldOutputScheduled_) {
    // NDK timestamps use CLOCK_MONOTONIC, the same domain as steady_clock on Android.
    const int64_t atNs = static_cast<int64_t>(heldOutputPresentUs_ * 1000ULL);
    status = CodecCall(kApiRelease, [&] { return AMediaCodec_releaseOutputBufferAtTime(codec, index, atNs); });
    ++presentScheduledCount_;
  } else {
    status = CodecCall(kApiRelease, [&] { return AMediaCodec_releaseOutputBuffer(codec, index, true); });
    ++presentImmediateCount_;
  }
  if (status != AMEDIA_OK) {
    if (slot < kHandoffRing && handoffKey_[slot].load(std::memory_order_acquire) == handoffKey) {
      handoffKey_[slot].store(0, std::memory_order_release);
    }
    log_error("AMediaCodec output release failed");
    ResetCodecLocked();
    return;
  }
#ifdef GNLINK_SH_TEST_SEAM
  if (publishAfter) {
    // The late publication must not hold the decoder lock: a sleep here stalls whoever waits on the lock
    // (the UI thread's handoff query among them), so the view could never latch ahead of the entry. The
    // slot is taken now, under the lock, and written after the delay by a thread of its own. Test build
    // only; a session teardown inside the <= 2 s delay could touch a freed sink -- accepted for a seam
    // that never ships.
    const size_t lateSlot = handoffNext_++ % kHandoffRing;
    handoffKey_[lateSlot].store(0, std::memory_order_release);
    const uint64_t meta = (static_cast<uint64_t>(latestOutputStreamGeneration_) << 32) |
                          (static_cast<uint64_t>(codecsStarted_) & 0xffffffffu);
    const uint64_t size = (static_cast<uint64_t>(outputWidth_) << 32) | outputHeight_;
    std::thread([this, lateSlot, meta, size, handoffKey]() {
      sh_seam_handoff_delay();
      handoffNs_[lateSlot].store(static_cast<int64_t>(steady_now_us()) * 1000, std::memory_order_release);
      handoffMeta_[lateSlot].store(meta, std::memory_order_release);
      handoffSize_[lateSlot].store(size, std::memory_order_release);
      handoffKey_[lateSlot].store(handoffKey, std::memory_order_release);
    }).detach();
  }
#endif
  ++outputFrameCount_;

  // Spacing between the moments frames were handed over on their due date. This is what the
  // pacing achieved; whether the view then latched each one is counted separately, by the
  // view itself, because only it knows.
  const uint64_t handedOverUs = heldOutputScheduled_ ? heldOutputPresentUs_ : nowUs;
  if (lastPresentSteadyUs_ != 0 && handedOverUs > lastPresentSteadyUs_) {
    const uint64_t gapUs = handedOverUs - lastPresentSteadyUs_;
    if (presentGapsUs_.size() < 4096) {
      presentGapsUs_.push_back(static_cast<uint32_t>(std::min<uint64_t>(gapUs, 4000000ULL)));
    }
  }
  if (presentWindowStartUs_ == 0) presentWindowStartUs_ = handedOverUs;
  if (handedOverUs > lastPresentSteadyUs_) lastPresentSteadyUs_ = handedOverUs;

  // A selection is confirmed by a frame of it reaching the display, so this has to live where
  // frames are handed over. It sat in the drain loop, which now returns early for every
  // normally-paced frame -- leaving the viewer waiting for an acknowledgement that had
  // already happened, and dropping back to the target list on a timeout while video played.
  // r1: not a picture of a codec a reset (a new selection among them) was asked for while it was released.
  // r4 N1: and only for a selection whose generation is decided (answered, or a local one bound by an IDR committed
  // into this codec) -- an unbound selection's codec holds nothing that may complete it.
  if (resetSeq == codecResetSeq_ && gate_.pending() != 0 && gate_.expected() != 0 &&
      readySelectionGeneration_ != gate_.pending()) {
    readySelectionGeneration_ = gate_.pending();
    bootstrapFrame_.reset();
    bootstrapReplayCount_ = 0;
    char line[192];
    std::snprintf(line, sizeof(line),
                  "selection first output localGen=%llu streamGen=%llu ptsUs=%llu out=%llu",
                  static_cast<unsigned long long>(readySelectionGeneration_),
                  static_cast<unsigned long long>(latestOutputStreamGeneration_),
                  static_cast<unsigned long long>(lastOutputPresentationUs_),
                  static_cast<unsigned long long>(outputFrameCount_));
    log_info(line);
  }
}

void AndroidVideoDecoderSink::DrainOutputLocked() {
  if (!codec_) return;

  // A frame handed over early is not a frame shown early -- it is a frame at risk of never
  // being shown at all. The view latches one buffer per vsync and takes the newest, so a
  // burst of three decoded frames pushed to the surface within a millisecond of each other
  // displays as one and discards two, no matter how carefully their timestamps were spaced.
  // Hold each frame in the codec until its slot arrives, so the surface never holds more
  // than one and every frame gets its own vsync to be picked up in.
  if (heldOutputValid_) {
    const uint64_t nowUs = steady_now_us();
    if (nowUs + kOutputReleaseLeadUs < heldOutputPresentUs_) return;  // not due yet
    ReleaseHeldOutputLocked(nowUs);
  }

  AMediaCodecBufferInfo info{};
  for (;;) {
    if (!codec_ || CodecResetPendingLocked()) return;  // r1: the codec is about to go
    AMediaCodec* const codec = codec_;
    const ssize_t outputIndex = CodecCall(kApiDequeueOut, [&] { return AMediaCodec_dequeueOutputBuffer(codec, &info, 0); });
    lastOutputDequeue_ = static_cast<int64_t>(outputIndex);
    if (CodecResetPendingLocked()) return;
    if (outputIndex >= 0) {
      ++outputDequeued_;
      // mobile-abr r1: a decoded picture, counted here once (see decodedTotal_). A codec-config (2)
      // buffer carries no picture; an end-of-stream (4) buffer may carry the last one, so only an empty
      // EOS buffer is left out (GNLink never queues an EOS in a session). A replayed input's output is
      // counted apart (r3 R1).
      constexpr uint32_t kBufferFlagCodecConfig = 2;
      constexpr uint32_t kBufferFlagEndOfStream = 4;
      const uint32_t flags = static_cast<uint32_t>(info.flags);
      const bool picture = (flags & kBufferFlagCodecConfig) == 0 &&
                           !((flags & kBufferFlagEndOfStream) != 0 && info.size <= 0);
      if (picture) {
        if (TakeReplayPtsLocked(info.presentationTimeUs)) {
          ++replayOutputs_;
        } else {
          // r6: the picture that ends a grace starts the measurement, unless the deadline already did.
          if (graceActive_) span_.OnGraceFirstPicture(steady_now_us(), decodedTotal_);
          ++decodedTotal_;
          graceActive_ = false;
          graceEligible_ = true;
        }
      }
      lastOutputPresentationUs_ =
          (info.presentationTimeUs > 0) ? static_cast<uint64_t>(info.presentationTimeUs) : 0;
      latestOutputStreamGeneration_ = gate_.expected();
      const uint64_t nowUs = steady_now_us();
      // Hold it until its slot rather than pushing it to the surface now. The upper bound
      // only rejects nonsense timestamps; anything sane waits here, where waiting is free,
      // instead of in a queue that discards all but the newest entry.
      const bool schedulable = lastOutputPresentationUs_ > nowUs + kOutputReleaseLeadUs &&
                               lastOutputPresentationUs_ < nowUs + 400000ULL;
      if (schedulable) {
        heldOutputValid_ = true;
        heldOutputIndex_ = static_cast<size_t>(outputIndex);
        heldOutputPresentUs_ = lastOutputPresentationUs_;
        heldOutputScheduled_ = true;
        return;  // one frame in flight; the pump releases it when due
      }
      // Past due, or no usable timestamp: it cannot be paced, so show it now.
      heldOutputValid_ = true;
      heldOutputIndex_ = static_cast<size_t>(outputIndex);
      heldOutputPresentUs_ = lastOutputPresentationUs_;
      heldOutputScheduled_ = false;
      ReleaseHeldOutputLocked(nowUs);
      if (codec_ == nullptr) return;  // release failed and reset the codec
      if ((outputFrameCount_ % 30) == 1) {
        char line[128];
        std::snprintf(line, sizeof(line), "released output frame count=%llu flags=%u",
                      static_cast<unsigned long long>(outputFrameCount_), info.flags);
        log_info(line);
      }
      continue;
    }
    if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      ++formatChanges_;
      UpdateOutputFormatLocked();
      continue;
    }
    if (outputIndex == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      continue;
    }
    break;
  }
}

}  // namespace remote60::android_direct
