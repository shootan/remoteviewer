#pragma once

// Requests posted to the host main loop from the threads that cannot act on them.
//
// Role:    MainLoopMailbox -- a coalescing, allocation-free mailbox. The control thread, the
//          sender thread and the UDP reader ask the main loop to do things it alone owns
//          (rebuild capture, re-tune the encoder, force a keyframe); the loop drains the mailbox
//          at the top of a tick.
// Thread:  any producer; one consumer (the main loop). Everything is under mu_.
// Callers: host_control_session.cpp, host_encoded_sender.cpp, the loop stages.
//
// Why (plan §7.1(b)): each of these used to be a hand-rolled mailbox -- a `*Pending` atomic that
// the loop consumed with exchange(), plus the request's payload scattered across two to four more
// atomics stored just before it. Two things were wrong with that. The payload was not published
// with the flag, so a second request landing between the loop's exchange() and its payload reads
// was silently half-applied; and because a bool cannot carry a reason, "force a keyframe" needed
// three separate flags with three different consumption points. Here a request is one value,
// published under one lock, and coalescing is explicit: latest-wins per kind, which is exactly
// what the overwriting atomics meant.
//
// Allocation-free on purpose: producers include threads that must not block for long, and one
// future producer is the capture callback.

#include <cstdint>
#include <mutex>
#include <optional>

namespace remote60::native_poc {

// --- request payloads ---

struct SelectMonitorRequest {
  uint32_t monitorId = 0;
};

struct CaptureModeRequest {
  uint32_t seq = 0;
  uint16_t mode = 0;         // 1 = overview, 2 = focus
  uint32_t xPermille = 5000;
  uint32_t yPermille = 5000;
};

struct TuneEncoderRequest {
  uint32_t seq = 0;
  uint32_t bitrate = 0;  // 0 = leave alone
  uint32_t keyint = 0;   // 0 = leave alone
  uint32_t fps = 0;      // 0 = leave alone
};

struct BackendRequest {
  uint32_t seq = 0;
  uint32_t backend = 0;  // 1 = dxgi, 2 = wgc, 3 = gdi
};

// Why a keyframe is being asked for. A bitmask, not an enum value: several reasons can accrue
// between two ticks and each one has a different follow-up, so coalescing must not lose any.
//
// This is what dissolves the "never touch encoder.forceKeyNext from the sender thread" rule the
// old code carried in a comment. forceKeyNext is a plain bool the main loop owns; the sender
// worked around it with a second flag (recoveryPending) consumed at a different point in the tick
// than the first (requestKey), because requestKey alone was only read after a real frame was
// popped -- and on a static desktop no frame is ever popped, so a recovery IDR never happened.
// With a typed request the sender simply says what it needs and the loop decides where to act.
enum KeyframeReason : uint32_t {
  kKeyframeReasonNone = 0,
  kKeyframeReasonViewer = 1u << 0,        // the viewer asked (ControlRequestKeyFrame)
  kKeyframeReasonSenderBarrier = 1u << 1, // a send failed; the media barrier was re-armed
  kKeyframeReasonSenderBacklog = 1u << 2, // the sender queue dropped a backlog and needs a resync
};

class MainLoopMailbox {
 public:
  // --- producers ---
  void PostSelectMonitor(const SelectMonitorRequest& r) {
    std::lock_guard<std::mutex> lk(mu_);
    selectMonitor_ = r;
  }
  void PostCaptureMode(const CaptureModeRequest& r) {
    std::lock_guard<std::mutex> lk(mu_);
    captureMode_ = r;
  }
  void PostTuneEncoder(const TuneEncoderRequest& r) {
    std::lock_guard<std::mutex> lk(mu_);
    // Coalesced field-wise, not whole-record: a viewer may send bitrate and keyint in separate
    // messages and losing the earlier one would silently ignore half the request.
    if (!tuneEncoder_) tuneEncoder_ = TuneEncoderRequest{};
    tuneEncoder_->seq = r.seq;
    if (r.bitrate) tuneEncoder_->bitrate = r.bitrate;
    if (r.keyint) tuneEncoder_->keyint = r.keyint;
    if (r.fps) tuneEncoder_->fps = r.fps;
  }
  void PostBackendRequest(const BackendRequest& r) {
    std::lock_guard<std::mutex> lk(mu_);
    backend_ = r;
  }
  // reasons is a mask; viewerReason is the wire code of the viewer's request, kept for the log.
  void PostRequestKeyframe(uint32_t reasons, uint16_t viewerReason = 0) {
    std::lock_guard<std::mutex> lk(mu_);
    keyframeReasons_ |= reasons;
    if (viewerReason != 0) viewerKeyframeReason_ = viewerReason;
  }

  // --- consumer (main loop) ---
  std::optional<SelectMonitorRequest> TakeSelectMonitor() { return Take(selectMonitor_); }
  std::optional<CaptureModeRequest> TakeCaptureMode() { return Take(captureMode_); }
  std::optional<TuneEncoderRequest> TakeTuneEncoder() { return Take(tuneEncoder_); }
  std::optional<BackendRequest> TakeBackendRequest() { return Take(backend_); }
  // Returns the accumulated reason mask and clears it; kKeyframeReasonNone when nothing pending.
  uint32_t TakeKeyframeReasons(uint16_t* outViewerReason = nullptr) {
    std::lock_guard<std::mutex> lk(mu_);
    const uint32_t r = keyframeReasons_;
    if (outViewerReason) *outViewerReason = viewerKeyframeReason_;
    keyframeReasons_ = kKeyframeReasonNone;
    return r;
  }
  // Peek without consuming. The static-frame gate needs to know a keyframe is coming so it does
  // not throttle the frame that will carry it.
  bool KeyframePending() const {
    std::lock_guard<std::mutex> lk(mu_);
    return keyframeReasons_ != kKeyframeReasonNone;
  }

  // A new session starts with nothing outstanding: requests belong to the client that made them.
  void Clear() {
    std::lock_guard<std::mutex> lk(mu_);
    selectMonitor_.reset();
    captureMode_.reset();
    tuneEncoder_.reset();
    backend_.reset();
    keyframeReasons_ = kKeyframeReasonNone;
    viewerKeyframeReason_ = 0;
  }

 private:
  template <typename T>
  std::optional<T> Take(std::optional<T>& slot) {
    std::lock_guard<std::mutex> lk(mu_);
    std::optional<T> out;
    out.swap(slot);
    return out;
  }

  mutable std::mutex mu_;
  std::optional<SelectMonitorRequest> selectMonitor_;
  std::optional<CaptureModeRequest> captureMode_;
  std::optional<TuneEncoderRequest> tuneEncoder_;
  std::optional<BackendRequest> backend_;
  uint32_t keyframeReasons_ = kKeyframeReasonNone;
  uint16_t viewerKeyframeReason_ = 0;
};

}  // namespace remote60::native_poc
