#pragma once

// The latest decoded frame handed from the recv thread to the UI thread (Phase 1-2 state struct).
//
// Role:    SharedFrame (one slot, latest wins, versioned) plus the present bookkeeping around it:
//          what was last presented, the InvalidateRect coalescing flag, the overwrite / coalesce
//          counters and the catch-up suppression window the picker sets.
// Thread:  recv publishes under `frame.mu` and bumps `frame.version`; UI copies under the same mutex
//          in WM_PAINT and stamps lastPresented*; `paintQueued` is exchanged by both to coalesce
//          invalidations; the picker (UI) resets lastPresentedCaptureUs / catchupSuppressUntilUs.
// Input:   decoded NV12 bytes or a decoder surface + timestamps.
// Output:  the picture on screen; present-lag inputs for the recv thread's catch-up logic.
// Callers: recv thread (publish), viewer_window_proc WM_PAINT, viewer_layout, viewer_picker.
//
// Fields are the former globals gFrame / gLastPresentedVersion / gLastPresentedCaptureUs /
// gPaintQueued / gPaintCoalescedCount / gOverwriteBeforePresentCount / gCatchupSuppressUntilUs,
// initialisers unchanged (viewer split refactor Phase 1-2).

#include "viewer_common.hpp"
#include "video_playout_clock.hpp"

namespace remote60::native_poc::viewer {

struct SharedFrame {
  enum class PixelFormat : uint8_t {
    Unknown = 0,
    Bgra32 = 1,
    Nv12 = 2,
  };
  std::mutex mu;
  PixelFormat format = PixelFormat::Unknown;
  // Visible content size -- what aspect fit, input mapping, and rendering treat as the
  // picture. For H.264 this is the display aperture (1080), not the coded plane (1088).
  uint32_t width = 0;
  uint32_t height = 0;
  // Coded plane the byte buffer is actually laid out in, plus where the visible rect starts.
  uint32_t codedWidth = 0;
  uint32_t codedHeight = 0;
  uint32_t visibleLeft = 0;
  uint32_t visibleTop = 0;
  uint32_t stride = 0;
  uint32_t seq = 0;
  uint64_t captureUs = 0;
  uint64_t encodeStartUs = 0;
  uint64_t encodeEndUs = 0;
  uint64_t sendUs = 0;
  uint64_t recvUs = 0;
  uint64_t decodeStartUs = 0;
  uint64_t decodeEndUs = 0;
  uint64_t queueSetUs = 0;
  uint64_t decodeToQueueUs = 0;
  uint64_t streamGeneration = 0;
  // Diagnostics-only: keyframe flag carried to the present stage for stream telemetry.
  bool key = false;
  // Paced playout (F-11 / P3): when the frame may be shown, on the local QPC clock; 0 = now.
  // Set by the recv thread from VideoPlayoutClock when pacing is on; WM_PAINT holds a frame that
  // is not yet due and re-arms itself for the remainder instead of presenting it early.
  uint64_t presentAtUs = 0;
  uint64_t version = 0;
  std::shared_ptr<std::vector<uint8_t>> bytes;
  Microsoft::WRL::ComPtr<IMFSample> surfaceSample;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> surfaceTexture;
  uint32_t surfaceSubresource = 0;
};

struct FrameBuffer {
  // cross-thread: recv writes under frame.mu, UI reads under frame.mu.
  SharedFrame frame;
  // cross-thread: UI stamps after an actual present, recv reads for lag estimates.
  std::atomic<uint64_t> lastPresentedVersion{0};
  std::atomic<uint64_t> lastPresentedCaptureUs{0};  // updated after actual present, not at queue time
  // While the picker overlays a live stream (mid-session picker no longer stops it), presents pause
  // but frames keep arriving, so lag-vs-last-presented would misread the overlay as decode backlog
  // and start catchup churn. Suppress catchup while the picker is up and briefly after it closes
  // (until the first present re-anchors lastPresentedCaptureUs).
  std::atomic<uint64_t> catchupSuppressUntilUs{0};
  // cross-thread: exchanged by recv (publish) and UI (WM_PAINT) to coalesce InvalidateRect.
  //
  // This latch mirrors state the OS already owns (the window's update region), and getting the two
  // out of step wedges the viewer permanently: with the latch set but no invalid region, no
  // WM_PAINT can ever arrive, and every later frame takes the "already queued" branch instead of
  // asking for one. That is exactly what a field freeze turned out to be -- present stopped dead
  // while decode kept running and the decode-queue lag estimate climbed past 100 seconds.
  // Only request_video_paint() may set it, and only paint_video_frame() may clear it -- after
  // BeginPaint has validated the old region, never before. (Viewer ledger F-20.)
  std::atomic<bool> paintQueued{false};
  std::atomic<uint64_t> paintCoalescedCount{0};
  std::atomic<uint64_t> overwriteBeforePresentCount{0};
  // Liveness telemetry for the above. paintEnterCount rising while presents stay at zero is the
  // signature of a paint that runs but skips the video branch; both frozen is the stuck latch.
  // Paced playout (F-11 / P3). Off unless REMOTE60_NATIVE_PACED_PLAYOUT=1: the host's async
  // encoder releases access units in bursts (several per encode call), and presenting each the
  // instant it decodes reproduces that burst on screen. The clock re-times frames onto a steady
  // cadence anchored to the host's capture timeline, holding ~2.5 frames of headroom against
  // arrival jitter. Same VideoPlayoutClock the Android client ships with. Default off until the
  // field says whether the added headroom is worth its latency on a PC.
  bool pacedPlayout = false;                 // main sets once at startup
  VideoPlayoutClock playout;                 // recv thread only
  std::atomic<uint64_t> pacedHoldCount{0};   // WM_PAINTs that deferred a not-yet-due frame
  std::atomic<uint64_t> pacedReanchorCount{0};
  std::atomic<uint64_t> paintEnterCount{0};
  std::atomic<uint64_t> beginPaintFailCount{0};
  std::atomic<uint64_t> paintSelfHealCount{0};
  std::atomic<uint64_t> invalidateFailCount{0};
  std::atomic<uint64_t> lastPaintEnterUs{0};
};

}  // namespace remote60::native_poc::viewer
