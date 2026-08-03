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
  std::lock_guard<std::mutex> lock(mu_);
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
      std::lock_guard<std::mutex> lock(mu_);
      if (codec_) DrainOutputLocked();
    }
    std::this_thread::sleep_for(kPumpInterval);
  }
}

void AndroidVideoDecoderSink::SetTargetFps(uint32_t fps) {
  std::lock_guard<std::mutex> lock(mu_);
  const uint32_t clampedFps = std::clamp<uint32_t>(fps, 1u, 240u);
  // A runtime FPS change starts a new presentation clock on the next frame.
  playoutClock_.SetTargetFrameIntervalUs(
      std::max<uint64_t>(1ULL, 1000000ULL / static_cast<uint64_t>(clampedFps)));
}

void AndroidVideoDecoderSink::SetSurface(JNIEnv* env, jobject surface) {
  std::lock_guard<std::mutex> lock(mu_);
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
}

void AndroidVideoDecoderSink::OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) {
  std::lock_guard<std::mutex> lock(mu_);

  PumpCodecLocked();

  if (pendingSelectionGeneration_ != 0 && (awaitingSelectionAck_ || expectedStreamGeneration_ == 0)) {
    ++staleFrameDropCount_;
    return;
  }
  if (expectedStreamGeneration_ != 0 && frame.header.streamGeneration != expectedStreamGeneration_) {
    ++staleFrameDropCount_;
    if ((staleFrameDropCount_ % 30u) == 1u) {
      char line[192];
      std::snprintf(line, sizeof(line),
                    "dropped stale frame streamGen=%llu expected=%llu pendingSel=%llu drops=%llu",
                    static_cast<unsigned long long>(frame.header.streamGeneration),
                    static_cast<unsigned long long>(expectedStreamGeneration_),
                    static_cast<unsigned long long>(pendingSelectionGeneration_),
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
  if (!EnsureCodecLocked(frame.header.width, frame.header.height)) {
    return;
  }
  if (!codec_) {
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
  std::lock_guard<std::mutex> lock(mu_);
  ResetCodecLocked();
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  pendingSelectionGeneration_ = 0;
  readySelectionGeneration_ = 0;
  expectedStreamGeneration_ = 0;
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  awaitingSelectionAck_ = false;
  csd0_.clear();
  csd1_.clear();
}

void AndroidVideoDecoderSink::OnVideoDiscontinuity() {
  std::lock_guard<std::mutex> lock(mu_);
  // Keep SPS/PPS, dimensions, and the active selection generation. Only decoder reference
  // pictures and pending compressed frames are invalid after a UDP loss.
  ResetCodecLocked();
  log_info("video decoder reset after transport discontinuity");
}

bool AndroidVideoDecoderSink::DrainPresentationStats(
    remote60::native_poc::ClientPresentationStats* out) {
  if (!out) return false;
  std::lock_guard<std::mutex> lock(mu_);
  if (presentGapsUs_.size() < 2) return false;

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

bool AndroidVideoDecoderSink::ConsumeDecoderKeyframeRequest() {
  std::lock_guard<std::mutex> lock(mu_);
  const bool requested = decoderKeyframeRequest_;
  decoderKeyframeRequest_ = false;
  return requested;
}

void AndroidVideoDecoderSink::OnWindowSelectionControlResult(
    const remote60::native_poc::ControlWindowSelectedMessage& msg) {
  std::lock_guard<std::mutex> lock(mu_);
  if ((msg.flags & 0x1u) != 0 && pendingSelectionGeneration_ != 0) {
    expectedStreamGeneration_ = msg.streamGeneration;
    awaitingSelectionAck_ = false;
    ResetPtsStateLocked();
    char line[192];
    std::snprintf(line, sizeof(line),
                  "selection ack localGen=%llu streamGen=%llu hostSendQpcUs=%llu",
                  static_cast<unsigned long long>(pendingSelectionGeneration_),
                  static_cast<unsigned long long>(expectedStreamGeneration_),
                  static_cast<unsigned long long>(msg.hostSendQpcUs));
    log_info(line);
    return;
  }

  awaitingSelectionAck_ = false;
  expectedStreamGeneration_ = 0;
  char line[160];
  std::snprintf(line, sizeof(line),
                "selection ack failed localGen=%llu flags=%u",
                static_cast<unsigned long long>(pendingSelectionGeneration_),
                msg.flags);
  log_info(line);
}

void AndroidVideoDecoderSink::PrepareForWindowSelection(uint64_t selectionGeneration) {
  std::lock_guard<std::mutex> lock(mu_);
  ResetCodecLocked();
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  pendingSelectionGeneration_ = selectionGeneration;
  expectedStreamGeneration_ = 0;
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  awaitingSelectionAck_ = true;
  csd0_.clear();
  csd1_.clear();

  char line[160];
  std::snprintf(line, sizeof(line), "selection prepare localGen=%llu",
                static_cast<unsigned long long>(pendingSelectionGeneration_));
  log_info(line);
}

void AndroidVideoDecoderSink::AbortWindowSelection() {
  std::lock_guard<std::mutex> lock(mu_);
  ResetCodecLocked();
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  pendingSelectionGeneration_ = 0;
  readySelectionGeneration_ = 0;
  expectedStreamGeneration_ = 0;
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  awaitingSelectionAck_ = false;
  csd0_.clear();
  csd1_.clear();
  log_info("selection aborted");
}

std::string AndroidVideoDecoderSink::DebugStatus() {
  std::lock_guard<std::mutex> lock(mu_);
  std::string status = "surface=";
  status += window_ ? "on" : "off";
  status += " codec=";
  status += codec_ ? "on" : "off";
  status += " size=" + std::to_string(configuredWidth_) + "x" + std::to_string(configuredHeight_);
  status += " output=" + std::to_string(outputWidth_) + "x" + std::to_string(outputHeight_);
  status += " csd=" + std::to_string(csd0_.empty() ? 0 : 1) + "/" + std::to_string(csd1_.empty() ? 0 : 1);
  status += " in=" + std::to_string(inputFrameCount_);
  status += " out=" + std::to_string(outputFrameCount_);
  status += " sel=" + std::to_string(pendingSelectionGeneration_);
  status += " readySel=" + std::to_string(readySelectionGeneration_);
  status += " expectGen=" + std::to_string(expectedStreamGeneration_);
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
  status += " awaitingAck=" + std::to_string(awaitingSelectionAck_ ? 1 : 0);
  return status;
}

uint64_t AndroidVideoDecoderSink::VideoSizePacked() {
  std::lock_guard<std::mutex> lock(mu_);
  PumpCodecLocked();
  const uint32_t width = outputWidth_ > 0 ? outputWidth_ : configuredWidth_;
  const uint32_t height = outputHeight_ > 0 ? outputHeight_ : configuredHeight_;
  return (static_cast<uint64_t>(width) << 32u) | static_cast<uint64_t>(height);
}

uint64_t AndroidVideoDecoderSink::ReadySelectionGeneration() {
  std::lock_guard<std::mutex> lock(mu_);
  PumpCodecLocked();
  return readySelectionGeneration_;
}

uint64_t AndroidVideoDecoderSink::LastOutputPresentationUs() {
  std::lock_guard<std::mutex> lock(mu_);
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

  configuredWidth_ = width;
  configuredHeight_ = height;
  outputWidth_ = width;
  outputHeight_ = height;
  StartOutputPumpLocked();
  char line[128];
  std::snprintf(line, sizeof(line), "MediaCodec started width=%u height=%u csd0=%zu csd1=%zu",
                width, height, csd0_.size(), csd1_.size());
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
      pendingSelectionGeneration_ != 0 &&
      readySelectionGeneration_ != pendingSelectionGeneration_ &&
      outputFrameCount_ == 0 &&
      bootstrapReplayCount_ < kBootstrapReplayMaxCount &&
      lastInputQueueSteadyUs_ > 0 &&
      nowUs >= lastInputQueueSteadyUs_ + kBootstrapReplayIntervalUs) {
    auto replayFrame = *bootstrapFrame_;
    if (TryQueueFrameLocked(replayFrame)) {
      ++bootstrapReplayCount_;
      char line[192];
      std::snprintf(line, sizeof(line),
                    "bootstrap replay queued localGen=%llu streamGen=%llu replay=%llu",
                    static_cast<unsigned long long>(pendingSelectionGeneration_),
                    static_cast<unsigned long long>(expectedStreamGeneration_),
                    static_cast<unsigned long long>(bootstrapReplayCount_));
      log_info(line);
    }
  }

  DrainOutputLocked();
}

bool AndroidVideoDecoderSink::TryQueueFrameLocked(remote60::native_poc::UdpH264AssembledFrame& frame) {
  if (!codec_) return false;

  const ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec_, 0);
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
  lastInputQueueSteadyUs_ = steady_now_us();
  if ((frame.header.flags & 1u) != 0 && pendingSelectionGeneration_ != 0 &&
      readySelectionGeneration_ != pendingSelectionGeneration_) {
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
  pendingFrame_.reset();
  pendingFrameCount_ = 0;
  pendingFrameQueueRetryCount_ = 0;
  bootstrapFrame_.reset();
  lastInputQueueSteadyUs_ = 0;
  bootstrapReplayCount_ = 0;
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

  int32_t width = 0;
  int32_t height = 0;
  if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width) && width > 0) {
    outputWidth_ = static_cast<uint32_t>(width);
  }
  if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height) && height > 0) {
    outputHeight_ = static_cast<uint32_t>(height);
  }

  int32_t cropLeft = 0;
  int32_t cropRight = 0;
  int32_t cropTop = 0;
  int32_t cropBottom = 0;
  const bool hasCrop =
      AMediaFormat_getInt32(format, "crop-left", &cropLeft) &&
      AMediaFormat_getInt32(format, "crop-right", &cropRight) &&
      AMediaFormat_getInt32(format, "crop-top", &cropTop) &&
      AMediaFormat_getInt32(format, "crop-bottom", &cropBottom);
  if (hasCrop && cropRight >= cropLeft && cropBottom >= cropTop) {
    outputWidth_ = static_cast<uint32_t>(cropRight - cropLeft + 1);
    outputHeight_ = static_cast<uint32_t>(cropBottom - cropTop + 1);
  }

  char line[160];
  std::snprintf(line, sizeof(line), "output format width=%u height=%u crop=%d,%d,%d,%d",
                outputWidth_, outputHeight_, cropLeft, cropTop, cropRight, cropBottom);
  log_info(line);
  AMediaFormat_delete(format);
}

void AndroidVideoDecoderSink::DrainOutputLocked() {
  if (!codec_) return;

  AMediaCodecBufferInfo info{};
  for (;;) {
    const ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(codec_, &info, 0);
    if (outputIndex >= 0) {
      lastOutputPresentationUs_ =
          (info.presentationTimeUs > 0) ? static_cast<uint64_t>(info.presentationTimeUs) : 0;
      latestOutputStreamGeneration_ = expectedStreamGeneration_;
      const uint64_t nowUs = steady_now_us();
      media_status_t releaseStatus = AMEDIA_OK;
      // The upper bound only rejects nonsense timestamps. It has to stay clear of the depth
      // the playout clock legitimately reaches -- a burst arriving early sits well past the
      // target lead -- or ordinary buffering would be dumped straight to the screen, which is
      // the stutter the clock exists to prevent.
      const bool scheduledRelease = lastOutputPresentationUs_ > nowUs + 1000ULL &&
                                    lastOutputPresentationUs_ < nowUs + 400000ULL;
      if (scheduledRelease) {
        // NDK timestamps use CLOCK_MONOTONIC, the same domain as steady_clock on Android.
        releaseStatus = AMediaCodec_releaseOutputBufferAtTime(
            codec_, outputIndex, static_cast<int64_t>(lastOutputPresentationUs_ * 1000ULL));
      } else {
        releaseStatus = AMediaCodec_releaseOutputBuffer(codec_, outputIndex, true);
      }
      if (releaseStatus != AMEDIA_OK) {
        log_error("AMediaCodec output release failed");
        ResetCodecLocked();
        return;
      }
      ++outputFrameCount_;
      // Smoothness is the spacing between frames on screen. For a scheduled release that is
      // the presentation timestamp we asked for, not the moment we called release: this loop
      // drains every ready buffer back to back, so timing the calls measured the drain and
      // not the display. An immediate release has no schedule, and lands when it lands --
      // counting those separately is what distinguishes a paced stream from a stuttering one.
      {
        const uint64_t displayedAtUs = scheduledRelease ? lastOutputPresentationUs_
                                                        : steady_now_us();
        if (scheduledRelease) {
          ++presentScheduledCount_;
        } else {
          ++presentImmediateCount_;
        }
        if (lastPresentSteadyUs_ != 0 && displayedAtUs > lastPresentSteadyUs_) {
          const uint64_t gapUs = displayedAtUs - lastPresentSteadyUs_;
          if (presentGapsUs_.size() < 4096) {
            presentGapsUs_.push_back(static_cast<uint32_t>(std::min<uint64_t>(gapUs, 4000000ULL)));
          }
        }
        if (presentWindowStartUs_ == 0) presentWindowStartUs_ = displayedAtUs;
        // An immediate release after a scheduled one can be stamped earlier than its
        // predecessor; keep the window monotonic so the fps span stays meaningful.
        if (displayedAtUs > lastPresentSteadyUs_) lastPresentSteadyUs_ = displayedAtUs;
      }
      if (pendingSelectionGeneration_ != 0 &&
          readySelectionGeneration_ != pendingSelectionGeneration_) {
        readySelectionGeneration_ = pendingSelectionGeneration_;
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
      if ((outputFrameCount_ % 30) == 1) {
        char line[128];
        std::snprintf(line, sizeof(line), "released output frame count=%llu flags=%u",
                      static_cast<unsigned long long>(outputFrameCount_), info.flags);
        log_info(line);
      }
      continue;
    }
    if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
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
