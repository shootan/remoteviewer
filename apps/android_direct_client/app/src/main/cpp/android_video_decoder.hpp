#pragma once

#include <jni.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "native_video_client_session.hpp"

struct ANativeWindow;
struct AMediaCodec;

namespace remote60::android_direct {

class AndroidVideoDecoderSink : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  AndroidVideoDecoderSink() = default;
  ~AndroidVideoDecoderSink() override;

  void SetSurface(JNIEnv* env, jobject surface);
  void SetTargetFps(uint32_t fps);
  void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) override;
  void OnVideoStreamReset() override;
  void OnVideoDiscontinuity() override;
  bool ConsumeDecoderKeyframeRequest() override;
  bool DrainPresentationStats(
      remote60::native_poc::ClientPresentationStats* out) override;
  void OnWindowSelectionControlResult(
      const remote60::native_poc::ControlWindowSelectedMessage& msg) override;
  void PrepareForWindowSelection(uint64_t selectionGeneration);
  void AbortWindowSelection();
  std::string DebugStatus();
  uint64_t VideoSizePacked();
  uint64_t ReadySelectionGeneration();
  uint64_t LastOutputPresentationUs();

 private:
  void PumpCodecLocked();
  bool TryQueueFrameLocked(remote60::native_poc::UdpH264AssembledFrame& frame);
  bool EnsureCodecLocked(uint32_t width, uint32_t height);
  uint64_t ComputeQueuedPtsUsLocked(const remote60::native_poc::UdpH264AssembledFrame& frame);
  void ResetPtsStateLocked();
  void ResetCodecLocked();
  void ReleaseSurfaceLocked();
  bool UpdateCodecConfigLocked(const std::vector<uint8_t>& annexb);
  void UpdateOutputFormatLocked();
  void DrainOutputLocked();

  std::mutex mu_;
  ANativeWindow* window_ = nullptr;
  AMediaCodec* codec_ = nullptr;
  uint32_t configuredWidth_ = 0;
  uint32_t configuredHeight_ = 0;
  uint32_t outputWidth_ = 0;
  uint32_t outputHeight_ = 0;
  uint64_t inputFrameCount_ = 0;
  uint64_t outputFrameCount_ = 0;
  uint64_t pendingSelectionGeneration_ = 0;
  uint64_t readySelectionGeneration_ = 0;
  uint64_t expectedStreamGeneration_ = 0;
  uint64_t latestInputStreamGeneration_ = 0;
  uint64_t latestOutputStreamGeneration_ = 0;
  uint64_t lastOutputPresentationUs_ = 0;
  uint64_t ptsStreamGeneration_ = 0;
  uint64_t ptsRemoteBaseUs_ = 0;
  uint64_t ptsLocalBaseUs_ = 0;
  uint64_t lastQueuedPtsUs_ = 0;
  uint64_t lastRemoteCaptureUs_ = 0;
  uint64_t targetFrameIntervalUs_ = 33333;
  uint64_t ptsReanchorCount_ = 0;
  uint64_t ptsMonotonicClampCount_ = 0;
  uint64_t ptsFallbackCount_ = 0;
  uint64_t pendingFrameCount_ = 0;
  uint64_t pendingFrameQueueRetryCount_ = 0;
  // Set when a delta had to be discarded before reaching the codec; consumed by the session,
  // which turns it into a rate-limited IDR request.
  bool decoderKeyframeRequest_ = false;
  // Intervals between frames actually handed to the display, drained once a second by the
  // session and reported to the host.
  std::vector<uint32_t> presentGapsUs_;
  uint64_t lastPresentSteadyUs_ = 0;
  uint64_t presentWindowStartUs_ = 0;
  uint64_t lastInputQueueSteadyUs_ = 0;
  uint64_t bootstrapReplayCount_ = 0;
  uint64_t staleFrameDropCount_ = 0;
  uint64_t oversizedInputFrameDropCount_ = 0;
  bool awaitingSelectionAck_ = false;
  std::optional<remote60::native_poc::UdpH264AssembledFrame> pendingFrame_;
  std::optional<remote60::native_poc::UdpH264AssembledFrame> bootstrapFrame_;
  std::vector<uint8_t> csd0_;
  std::vector<uint8_t> csd1_;
};

}  // namespace remote60::android_direct
