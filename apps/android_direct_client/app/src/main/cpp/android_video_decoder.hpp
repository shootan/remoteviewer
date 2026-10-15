#pragma once

#include <jni.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "client_decode_snapshot.hpp"
#include "native_video_client_session.hpp"
#include "selection_ack_gate.hpp"
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
  bool DrainDecodeStats(remote60::native_poc::ClientDecodeStats* out) override;
  uint64_t DecodeGeneration() override;
  void OnDecodeEpisodeStart() override;
  void OnWindowSelectionControlResult(
      const remote60::native_poc::ControlWindowSelectedMessage& msg) override;
  void PrepareForWindowSelection(uint64_t selectionGeneration);
  // t-970r4zgo: the selection a select request is queued under, and its answer for that selection.
  uint64_t CurrentSelectionTag() override;
  uint64_t KeyframeOwedFor() override;
  void OnWindowSelectionControlResultFor(const remote60::native_poc::ControlWindowSelectedMessage& msg,
                                         uint64_t requestTag) override;
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
  // mobile-abr r1: every section under mu_ ends by publishing what the session's control thread reads
  // WITHOUT mu_ (DrainDecodeStats) -- that thread must never wait on a lock the codec work holds.
  void PublishDecodeStateLocked();
  class LockedState {
   public:
    explicit LockedState(AndroidVideoDecoderSink* s) : s_(s), lock_(s->mu_) {}
    ~LockedState() { s_->PublishDecodeStateLocked(); }  // runs before lock_ is released
    LockedState(const LockedState&) = delete;
    LockedState& operator=(const LockedState&) = delete;

   private:
    AndroidVideoDecoderSink* s_;
    std::lock_guard<std::mutex> lock_;
  };
  // r3 R2: (valid, count, size, generation) as one consistent copy (client_decode_snapshot.hpp). The
  // generation bumps whenever validity / the surface / the selection changes.
  remote60::native_poc::ClientDecodeSnapshot decodeSnapshot_;
  uint64_t decodeGeneration_ = 0;     // under mu_
  uint64_t decodeKeyHash_ = 0;        // under mu_: what the generation last changed on
  bool decodeKeySet_ = false;
  // r5 (Codex F1 contract): the startup grace of an active transition. A grace begins at an explicit
  // episode start (connection, stream resumed: always) or a transition inside active viewing (selection
  // prepared, surface back, a switch abandoned while active: only if a picture has been counted since the
  // last grace -- so prepare / abort / retry / codec retries / loss resets cannot stretch it). It ends at
  // the first counted picture, or by itself at the deadline, which the reader observes on its own clock
  // (no frame, poll or lock needed).
  // r6 (Codex S1): where the valid measurement now in progress began (client_decode_snapshot.hpp) -- a
  // new generation, a grace's deadline, or the picture that ended a grace. The drain reports the count
  // and the time since then together, and keeps no state of its own.
  static constexpr uint64_t kStartupGraceUs = 2000000ULL;
  bool graceActive_ = false;
  bool graceEligible_ = true;
  uint64_t graceDeadlineUs_ = 0;
  remote60::native_poc::ClientDecodeSpanStart span_;  // under mu_
  void StartGraceLocked(bool explicitEpisode);
  ANativeWindow* window_ = nullptr;
  AMediaCodec* codec_ = nullptr;
  uint32_t configuredWidth_ = 0;
  uint32_t configuredHeight_ = 0;
  uint32_t outputWidth_ = 0;
  uint32_t outputHeight_ = 0;
  uint64_t inputFrameCount_ = 0;
  uint64_t outputFrameCount_ = 0;
  // mobile-abr r1 (Codex contract section 3): valid decoded pictures, counted where the decoder hands
  // one out (dequeueOutputBuffer >= 0; not a codec-config buffer, not an end-of-stream buffer without a
  // picture) -- not when it is released to the display later. Never reset: outputFrameCount_ is reset
  // with the codec.
  // r3 R1: the selection's bootstrap IDR re-queued into a codec that had not answered yet is the same
  // picture again, but the playout clock gives each queue a NEW PTS. The PTS of every replay queue that
  // succeeded is remembered here (a few at most -- kBootstrapReplayMaxCount per selection) and its output
  // is counted apart, in replayOutputs_. Forgotten with the codec.
  uint64_t decodedTotal_ = 0;
  uint64_t replayOutputs_ = 0;
  bool queueingReplay_ = false;
  static constexpr size_t kReplayPtsSlots = 8;
  int64_t replayPts_[kReplayPtsSlots] = {};
  size_t replayPtsCount_ = 0;
  void RememberReplayPtsLocked(int64_t ptsUs);
  bool TakeReplayPtsLocked(int64_t ptsUs);
  // Which selection is armed, whether its answer is still awaited and which stream generation it
  // answered with (selection_ack_gate.hpp; shared with the Windows tests).
  remote60::native_poc::SelectionAckGate gate_;
  uint64_t readySelectionGeneration_ = 0;
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
  // keepalive r5 D1: what the codec itself answered, for the video_debug line -- "out=0" alone does not
  // say whether no picture was ever dequeued, one is held, or the input side is what stopped.
  int64_t lastInputDequeue_ = 0;    // last dequeueInputBuffer result (>= 0 an index)
  int64_t lastOutputDequeue_ = 0;   // last dequeueOutputBuffer result (-1 try again, -2 format, -3 buffers)
  uint64_t outputDequeued_ = 0;     // output buffers dequeued by this codec instance (pictures or not)
  uint64_t formatChanges_ = 0;      // output-format-changed answers, this codec instance
  uint64_t codecsStarted_ = 0;      // codec instances started over the sink's life
  // keepalive r5 D1: this codec instance has had no IDR queued yet -- deltas are not fed to it (an IDR is asked
  // for instead). The surface can start a codec from the SPS/PPS of an IDR it never received.
  bool codecAwaitsKey_ = false;
  uint64_t deltaBeforeKeyDrops_ = 0;
  std::optional<remote60::native_poc::UdpH264AssembledFrame> pendingFrame_;
  std::optional<remote60::native_poc::UdpH264AssembledFrame> bootstrapFrame_;
  std::vector<uint8_t> csd0_;
  std::vector<uint8_t> csd1_;
};

}  // namespace remote60::android_direct
