#pragma once

#include <jni.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "native_video_client_session.hpp"
#include "video_playout_clock.hpp"

struct ANativeWindow;
struct AMediaCodec;

namespace remote60::android_direct {

class AndroidVideoDecoderSink : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  AndroidVideoDecoderSink() = default;
  ~AndroidVideoDecoderSink() override;

  void SetSurface(JNIEnv* env, jobject surface);
  /** One call per frame the view latched. Lock-free: this runs on the UI thread every vsync
   *  and must never contend with the decoder. */
  void NotifyFrameDisplayed() {
    displayedFrameCount_.fetch_add(1, std::memory_order_relaxed);
  }
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
  void ReleaseHeldOutputLocked(uint64_t nowUs);
  void StartOutputPumpLocked();
  void StopOutputPump();
  void OutputPumpMain();

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
  // Decides when each frame reaches the screen. Shared with the desktop viewer and unit
  // tested against recorded arrival patterns, because it is control logic whose failure mode
  // is "looks slightly wrong on a device you are not holding".
  remote60::native_poc::VideoPlayoutClock playoutClock_;
  // Hands decoded frames to the display on their own schedule. Draining only when the next
  // frame arrives meant a frame scheduled 80 ms out sat in the codec until something else
  // showed up -- so the moment arrivals bunched, which is exactly when pacing matters, the
  // schedule was missed and the frame went straight to the screen.
  std::thread outputPumpThread_;
  std::atomic<bool> outputPumpStop_{false};
  // At most one decoded frame waits here for its slot. Handing it over sooner would put it in
  // a queue the view drains newest-first, where an earlier frame is simply thrown away.
  bool heldOutputValid_ = false;
  bool heldOutputScheduled_ = false;
  size_t heldOutputIndex_ = 0;
  uint64_t heldOutputPresentUs_ = 0;
  // Counted by the view, once per frame it actually latched.
  std::atomic<uint64_t> displayedFrameCount_{0};
  uint64_t displayedWindowBase_ = 0;
  uint64_t pendingFrameCount_ = 0;
  uint64_t pendingFrameQueueRetryCount_ = 0;
  // Set when a delta had to be discarded before reaching the codec; consumed by the session,
  // which turns it into a rate-limited IDR request.
  bool decoderKeyframeRequest_ = false;
  // Intervals between the times frames actually reach the display, drained once a second by
  // the session and reported to the host. A scheduled release lands at its presentation
  // timestamp, not at the moment we called release -- draining several ready buffers in one
  // pass would otherwise read as a burst of near-zero gaps while the screen was fine.
  std::vector<uint32_t> presentGapsUs_;
  uint64_t lastPresentSteadyUs_ = 0;
  uint64_t presentWindowStartUs_ = 0;
  uint32_t presentScheduledCount_ = 0;
  uint32_t presentImmediateCount_ = 0;
  uint64_t presentWindowReanchorBase_ = 0;
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
