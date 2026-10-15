#include "android_video_decoder.hpp"

#include <android/log.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <cassert>
#include <chrono>
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

void AndroidVideoDecoderSink::SetTargetFps(uint32_t fps) {
  LockedState lock(this);
  const uint32_t clampedFps = std::clamp<uint32_t>(fps, 1u, 240u);
  // A runtime FPS change starts a new presentation clock on the next frame.
  playoutClock_.SetTargetFrameIntervalUs(
      std::max<uint64_t>(1ULL, 1000000ULL / static_cast<uint64_t>(clampedFps)));
}

void AndroidVideoDecoderSink::SetSurface(JNIEnv* env, jobject surface) {
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

void AndroidVideoDecoderSink::OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) {
  LockedState lock(this);

  PumpCodecLocked();

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

  if (latestInputStreamGeneration_ != 0 &&
      latestInputStreamGeneration_ != frame.header.streamGeneration) {
    ResetPtsStateLocked();
  }
  latestInputStreamGeneration_ = frame.header.streamGeneration;
  configuredWidth_ = frame.header.width;
  configuredHeight_ = frame.header.height;
  const bool codecConfigUpdated = UpdateCodecConfigLocked(frame.payload);
  if (codecConfigUpdated && codec_) {
    ResetCodecLocked();
  }
  if (!EnsureCodecLocked(frame.header.width, frame.header.height) || !codec_) {
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
    decoderKeyframeRequest_ = true;
    if ((deltaBeforeKeyDrops_ % 30u) == 1u) {
      char line[160];
      std::snprintf(line, sizeof(line), "delta before the codec's first IDR dropped streamGen=%llu drops=%llu",
                    static_cast<unsigned long long>(frame.header.streamGeneration),
                    static_cast<unsigned long long>(deltaBeforeKeyDrops_));
      log_info(line);
    }
    return;
  }

  if (!TryQueueFrameLocked(frame)) {
    if (!pendingFrame_.has_value()) {
      pendingFrame_ = std::move(frame);
      pendingFrameCount_ = 1;
      pendingFrameQueueRetryCount_ = 0;
    } else {
      const bool replacePending =
          ((frame.header.flags & 1u) != 0) || pendingFrame_->payload.empty();
      if (replacePending) {
        // The pending delta is being discarded in favour of this frame.
        if ((pendingFrame_->header.flags & 1u) == 0) decoderKeyframeRequest_ = true;
        pendingFrame_ = std::move(frame);
      } else if ((frame.header.flags & 1u) == 0) {
        // This delta is dropped outright. Everything after it references a picture the
        // decoder will never see, so the stream cannot recover without a fresh IDR.
        decoderKeyframeRequest_ = true;
      }
      pendingFrameCount_ = 1;
    }
  }
  PumpCodecLocked();
}

void AndroidVideoDecoderSink::OnVideoStreamReset() {
  LockedState lock(this);
  ResetCodecLocked();
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

void AndroidVideoDecoderSink::OnVideoDiscontinuity() {
  LockedState lock(this);
  // Keep SPS/PPS, dimensions, and the active selection generation. Only decoder reference
  // pictures and pending compressed frames are invalid after a UDP loss.
  ResetCodecLocked();
  log_info("video decoder reset after transport discontinuity");
}

bool AndroidVideoDecoderSink::DrainPresentationStats(
    remote60::native_poc::ClientPresentationStats* out) {
  if (!out) return false;
  LockedState lock(this);
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
  const bool valid = window_ != nullptr;
  // The generation moves whenever what the decoder's count means could have changed: validity, the
  // surface, the selection being prepared / abandoned and the stream generation it was answered with.
  uint64_t key = reinterpret_cast<uintptr_t>(window_);
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

void AndroidVideoDecoderSink::OnDecodeEpisodeStart() {
  LockedState lock(this);
  StartGraceLocked(true);
}

uint64_t AndroidVideoDecoderSink::DecodeGeneration() { return decodeSnapshot_.generation(); }

// Called by the session's control thread on its due passes; reads only what PublishDecodeStateLocked
// stored (r3 R2: one consistent copy, a sequence read, no lock). false = the decoder's state is not
// meaningful now: no surface (intended inactivity), or an active transition's startup still inside its
// grace -- the session then reports nothing for that window instead of "0 fps". A codec reset for a loss
// stays valid: frames that do not decode then are a real stall. A generation change does not make the
// drain invalid (r5): a new measurement begins there.
// r6 (Codex S1): the measurement in progress, cumulative -- the count since it began and the time since
// then, on this sink's clock. Reading consumes nothing, so the session may ask again a little later
// (ClientTelemetryWindow::Hold) without losing what it had.
bool AndroidVideoDecoderSink::DrainDecodeStats(remote60::native_poc::ClientDecodeStats* out) {
  if (!out) return false;
  remote60::native_poc::ClientDecodeState s;
  if (!decodeSnapshot_.Read(&s)) return false;  // a writer kept overlapping: no copy this window
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

bool AndroidVideoDecoderSink::ConsumeDecoderKeyframeRequest() {
  LockedState lock(this);
  const bool requested = decoderKeyframeRequest_;
  decoderKeyframeRequest_ = false;
  return requested;
}

void AndroidVideoDecoderSink::OnWindowSelectionControlResult(
    const remote60::native_poc::ControlWindowSelectedMessage& msg) {
  // Untagged: taken as the answer to whatever selection is armed (the old behaviour).
  uint64_t tag = 0;
  {
    LockedState lock(this);
    tag = gate_.pending();
  }
  OnWindowSelectionControlResultFor(msg, tag);
}

uint64_t AndroidVideoDecoderSink::KeyframeOwedFor() {
  LockedState lock(this);
  return gate_.OwedToken();
}

uint64_t AndroidVideoDecoderSink::CurrentSelectionTag() {
  LockedState lock(this);
  return gate_.pending();
}

void AndroidVideoDecoderSink::OnWindowSelectionControlResultFor(
    const remote60::native_poc::ControlWindowSelectedMessage& msg, uint64_t requestTag) {
  LockedState lock(this);
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

void AndroidVideoDecoderSink::PrepareForWindowSelection(uint64_t selectionGeneration) {
  LockedState lock(this);
  ResetCodecLocked();
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
}

void AndroidVideoDecoderSink::AbortWindowSelection() {
  LockedState lock(this);
  ResetCodecLocked();
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
  decoderKeyframeRequest_ = true;
  log_info("selection aborted");
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
  status += " pending=" + std::to_string(pendingFrame_.has_value() ? 1 : 0);
  status += " pendingRetry=" + std::to_string(pendingFrameQueueRetryCount_);
  status += " bootstrapReplay=" + std::to_string(bootstrapReplayCount_);
  status += " lastOutUs=" + std::to_string(lastOutputPresentationUs_);
  status += " awaitingAck=" + std::to_string(gate_.awaiting() ? 1 : 0);
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
  return status;
}

uint64_t AndroidVideoDecoderSink::VideoSizePacked() {
  LockedState lock(this);
  PumpCodecLocked();
  const uint32_t width = outputWidth_ > 0 ? outputWidth_ : configuredWidth_;
  const uint32_t height = outputHeight_ > 0 ? outputHeight_ : configuredHeight_;
  return (static_cast<uint64_t>(width) << 32u) | static_cast<uint64_t>(height);
}

uint64_t AndroidVideoDecoderSink::ReadySelectionGeneration() {
  LockedState lock(this);
  PumpCodecLocked();
  return readySelectionGeneration_;
}

uint64_t AndroidVideoDecoderSink::LastOutputPresentationUs() {
  LockedState lock(this);
  PumpCodecLocked();
  return lastOutputPresentationUs_;
}

bool AndroidVideoDecoderSink::EnsureCodecLocked(uint32_t width, uint32_t height) {
  if (codec_ && configuredWidth_ == width && configuredHeight_ == height) {
    return true;
  }
  if (!window_ || width == 0 || height == 0 || csd0_.empty() || csd1_.empty()) {
    return false;
  }

  ResetCodecLocked();

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

  ++codecsStarted_;
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
  StartOutputPumpLocked();
  char line[224];
  char* codecName = nullptr;
  const bool named = AMediaCodec_getName(codec_, &codecName) == AMEDIA_OK && codecName;
  std::snprintf(line, sizeof(line), "MediaCodec started width=%u height=%u csd0=%zu csd1=%zu name=%s",
                width, height, csd0_.size(), csd1_.size(), named ? codecName : "?");
  if (named) AMediaCodec_releaseName(codec_, codecName);
  log_info(line);
  return true;
}

void AndroidVideoDecoderSink::PumpCodecLocked() {
  const uint64_t nowUs = steady_now_us();

  if (pendingFrame_.has_value() && codec_) {
    if (TryQueueFrameLocked(*pendingFrame_)) {
      pendingFrame_.reset();
      pendingFrameCount_ = 0;
      pendingFrameQueueRetryCount_ = 0;
    } else {
      ++pendingFrameQueueRetryCount_;
    }
  }

  if (codec_ &&
      bootstrapFrame_.has_value() &&
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

bool AndroidVideoDecoderSink::TryQueueFrameLocked(remote60::native_poc::UdpH264AssembledFrame& frame) {
  if (!codec_) return false;

  const ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec_, 0);
  lastInputDequeue_ = static_cast<int64_t>(inputIndex);
  if (inputIndex < 0) {
    return false;
  }

  size_t inputBufferSize = 0;
  uint8_t* inputBuffer = AMediaCodec_getInputBuffer(codec_, inputIndex, &inputBufferSize);
  if (!inputBuffer) {
    log_error("AMediaCodec_getInputBuffer returned null");
    ResetCodecLocked();
    return false;
  }
  if (frame.payload.size() > inputBufferSize) {
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

  std::memcpy(inputBuffer, frame.payload.data(), frame.payload.size());
  const int64_t ptsUs = static_cast<int64_t>(ComputeQueuedPtsUsLocked(frame));
  if (AMediaCodec_queueInputBuffer(codec_, inputIndex, 0,
                                   static_cast<size_t>(frame.payload.size()),
                                   ptsUs, 0) != AMEDIA_OK) {
    log_error("AMediaCodec_queueInputBuffer failed");
    ResetCodecLocked();
    return false;
  }

  ++inputFrameCount_;
  if (queueingReplay_) RememberReplayPtsLocked(ptsUs);
  lastInputQueueSteadyUs_ = steady_now_us();
  // r5 N1: this is where an owed IDR is paid -- it is in the decoder's input, not just past the gate.
  if ((frame.header.flags & 1u) != 0) {
    gate_.KeyDelivered(frame.header.streamGeneration);
    codecAwaitsKey_ = false;  // keepalive r5 D1: this instance has its IDR
  }
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
  if (pendingFrame_.has_value() && (pendingFrame_->header.flags & 1u) != 0) {
    gate_.KeyLost(pendingFrame_->header.streamGeneration);
  }
  pendingFrame_.reset();
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

  AMediaFormat* format = AMediaCodec_getOutputFormat(codec_);
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
  heldOutputValid_ = false;

  media_status_t status;
  if (heldOutputScheduled_) {
    // NDK timestamps use CLOCK_MONOTONIC, the same domain as steady_clock on Android.
    status = AMediaCodec_releaseOutputBufferAtTime(
        codec_, heldOutputIndex_, static_cast<int64_t>(heldOutputPresentUs_ * 1000ULL));
    ++presentScheduledCount_;
  } else {
    status = AMediaCodec_releaseOutputBuffer(codec_, heldOutputIndex_, true);
    ++presentImmediateCount_;
  }
  if (status != AMEDIA_OK) {
    log_error("AMediaCodec output release failed");
    ResetCodecLocked();
    return;
  }
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
  if (gate_.pending() != 0 &&
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
    const ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(codec_, &info, 0);
    lastOutputDequeue_ = static_cast<int64_t>(outputIndex);
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
