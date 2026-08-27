#pragma once

// Control-channel state of the viewer (Phase 1-5 state struct).
//
// Role:    the request states the control scheduler drains (input queue, keyframe requests,
//          runtime tune, stream state, capture mode), the scheduler itself, the UDP control
//          tunnel and whether it is in use, connection status, and the secure-desktop transition
//          latch of the control loop.
// Thread:  the control thread owns the scheduler/tunnel and writes connected / reportedSecure;
//          UI, recv and main enqueue requests (each request state is atomic- or mutex-backed);
//          recv ticks/feeds udpControl (one reader, one writer on the shared socket).
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

namespace remote60::native_poc::viewer {

struct ControlChannelState {
  // control thread only.
  ClientControlScheduler scheduler;
  // cross-thread: producers UI/recv/main, consumer control (atomic-backed request states).
  KeyframeRequestState keyframeRequests{
      kKeyframeRequestMinIntervalUsDefault,
      kKeyframeRequestTokenRefillUsDefault,
      kKeyframeRequestTokenCapacityDefault};
  RuntimeTuneState runtimeTune{
      300000,
      30000000,
      250000,
      1,
      240};
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
  // cross-thread: main/control write, UI/picker read.
  std::atomic<bool> connected{false};
  // control thread only: say the secure-desktop transition once (was a function static). reset: never (F-14).
  bool reportedSecure = false;
};

}  // namespace remote60::native_poc::viewer
