#pragma once

// Control-channel state of the viewer (Phase 1-5 state struct).
//
// Role:    the request states the control scheduler drains (input queue, keyframe requests,
//          runtime tune, stream state, capture mode), the scheduler itself, the UDP control
//          tunnel and whether it is in use, connection status, and the secure-desktop transition
//          latch of the control loop.
// Thread:  the control thread owns the scheduler and writes connected / reportedSecure;
//          UI, recv and main enqueue requests (each request state is atomic- or mutex-backed);
//          UDP ingress alone reads the socket and feeds udpControl.OnPacket. Ingress, receiver
//          maintenance and the control thread can Tick; UdpControlChannel serializes them with
//          its mutex. ControlLink.Receive waits on the channel queue, not on the socket.
// Input:   UI/recv requests, host replies.
// Output:  outbound control actions.
// Callers: main() (connect/setup), control thread, viewer_picker, viewer_input_forward, viewer_log,
//          viewer_overlay_draw, recv thread.
//
// Fields are the former globals gControlScheduler / gKeyframeRequests / gRuntimeTuneState /
// gStreamStateControl / gCaptureModeRequests / gInputQueueState / gUdpControl / gControlOverUdp /
// gControlConnected and the control loop's static reportedSecure, initialisers unchanged (viewer
// split refactor Phase 1-5). The write-only capture-meta mirror that came with them is gone (F-03):
// the pong's fields are logged straight from the message.

#include "viewer_common.hpp"
#include "viewer_constants.hpp"
#include "clipboard_sync.hpp"

namespace remote60::native_poc::viewer {

// Clipboard text sync (K1). The UI thread hears local clipboard changes (WM_CLIPBOARDUPDATE) and
// leaves the ones worth sending as a pending outbound; the control thread drains that pending item,
// sends it, and polls the host for changes the other way. `enabled` is the viewer's on/off toggle
// (default on); `hostSupports` is set from the pong capability bit. The core and the pending item
// are guarded by `mu` because the UI thread produces and the control thread consumes.
struct ClipboardSyncState {
  std::atomic<bool> enabled{true};        // the toolbar toggle; off means no send and no poll
  std::atomic<bool> hostSupports{false};  // host advertised kCaptureFlagClipboardTextV1 (pong)

  std::mutex mu;
  remote60::native_poc::ClipboardSyncCore core;  // echo/duplicate suppression, both directions
  bool hasPending = false;                // a local change waiting for the control thread to send
  std::u16string pendingText;
  uint64_t pendingHash = 0;
  uint32_t nextSeq = 0;

  // control thread only: the session-boundary rules (baseline poll, one-shot push, poll interval),
  // shared with the Android session so both clients behave the same.
  remote60::native_poc::ClipboardClientPolicy policy;
};

struct ControlChannelState {
  // control thread only.
  ClientControlScheduler scheduler;
  // cross-thread: producers UI/recv/main, consumer control (atomic-backed request states).
  KeyframeRequestState keyframeRequests{
      kKeyframeRequestMinIntervalUsDefault,
      kKeyframeRequestTokenRefillUsDefault,
      kKeyframeRequestTokenCapacityDefault};
  RuntimeTuneState runtimeTune{
      kRuntimeBitrateMin,
      kRuntimeBitrateMax,
      kRuntimeBitrateStep,
      kRuntimeKeyintMin,
      kRuntimeKeyintMax};
  remote60::native_poc::StreamStateControl streamState;
  CaptureModeRequestState captureModeRequests;
  ClientInputQueue inputQueue;
  // Control over the media socket, for hosts reached through the directory.
  //
  // A second TCP connection cannot be opened to a host behind NAT: only the UDP socket was
  // punched, so control has to ride it. Everything the session needs -- input, the window list,
  // the monitor list, runtime tuning -- goes through here, which is why a session without it
  // shows a picture and responds to nothing.
  // cross-thread: control writes, recv ticks + OnPacket, main configures/closes.
  remote60::native_poc::UdpControlChannel udpControl;
  std::atomic<bool> overUdp{false};
  // cross-thread: control writes after every pong, recv reads (the A04 give-up's reply allowance
  // is 2 x this RTT, when fresh). 0 = no pong yet.
  std::atomic<uint64_t> lastRttUs{0};
  std::atomic<uint64_t> lastRttAtUs{0};  // qpc of the pong that measured it
  // cross-thread: main/control write, UI/picker read.
  std::atomic<bool> connected{false};
  // control thread only: say the secure-desktop transition once (was a function static). reset: never (F-14).
  std::atomic<bool> reportedSecure{false};
  // Clipboard text sync (K1): UI thread produces local changes, control thread sends + polls.
  ClipboardSyncState clipboard;
};

}  // namespace remote60::native_poc::viewer
