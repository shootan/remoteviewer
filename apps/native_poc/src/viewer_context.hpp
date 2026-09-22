#pragma once

// What main() owns for the life of the session and hands to the startup steps and the two threads
// (viewer split refactor Phase 3; the ten state structs joined it under viewer ledger F-17).
//
// ViewerContext IS the ViewerState (the ten feature structs, viewer_state.hpp) plus the session
// objects built on it. Destruction runs the members below in reverse -- the threads and the
// receiver / control client (which reference args / dec / gate and the state) go first -- and the
// ViewerState base last, which is the relation the former globals had to main()'s locals.

#include <optional>
#include <string>
#include <thread>

#include "viewer_args.hpp"
#include "viewer_common.hpp"
#include "viewer_control_client.hpp"
#include "viewer_decoder_state.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_state.hpp"
#include "viewer_video_receiver.hpp"

namespace remote60::native_poc::viewer {

struct ViewerContext : ViewerState {
  Args args;                        // the command line
  Args resolvedArgs;                // the directory path replaces host/port/controlPort
  std::string directoryPunchToken;  // capability from /api/connect, carried in the UDP hello
  DecoderState dec;
  FrameGateState gate;
  uint32_t udpSimDropPm = 0;        // REMOTE60_NATIVE_UDP_SIM_DROP_PM
  uint32_t udpSimDropSeed = 0;      // REMOTE60_NATIVE_UDP_SIM_DROP_SEED
  // Video NACK policy (REMOTE60_NATIVE_VIDEO_NACK / _NACK_HOLD_US), effective only once the host
  // acknowledged kUdpFeatureVideoNack; and the media socket's receive timeout for UDP
  // (REMOTE60_NATIVE_UDP_RECV_TIMEOUT_MS), which is the clock of every timer the recv thread runs
  // on a quiet link (NACK rounds, the in-order hold, the keyframe recovery deadline).
  bool videoNackEnabled = true;
  uint64_t videoNackHoldUs = 120000;
  uint32_t udpRecvTimeoutMs = 25;
  bool startInStreamView = false;   // --initial-view / REMOTE60_NATIVE_START_STREAM_VIEW
  bool startInPicker = false;
  SOCKET controlSock = INVALID_SOCKET;  // the TCP control socket (direct hosts); main closes it
  bool controlReady = false;
  uint64_t startUs = 0;             // session start, for --seconds
  std::optional<ControlClient> controlClient;  // not `control`: that is ViewerState::control (the channel state)
  std::optional<VideoReceiver> receiver;
  std::thread controlThread;
  std::thread recvThread;
  std::atomic<bool> uiWatchdogStop{false};
  std::thread uiWatchdog;

  /**
   * Which connect attempt this is, and whether it has been called off.
   *
   * Neither existed. Every other generation counter in this viewer fences something INSIDE an
   * established session -- IME responses, selection frames, cursor samples -- and none of them
   * says anything about a connect. Cancellation did not reach the connect path either: the
   * hello handshake takes a stop pointer and this viewer passed nullptr, and Observe/PunchAny
   * had no such parameter at all.
   *
   * What that cost: the shell starts a NEW GNLinkViewer when the user picks another PC, and
   * never closes the old one. The old process kept punching for four seconds and saying hello
   * for ten, and could still establish a session afterwards -- against a host that had moved
   * on to the new one.
   *
   * `connectCancelled` is what the handshake, Observe and PunchAny are given as their stop.
   * `connectGeneration` fences a result that arrives after a cancel: the attempt records the
   * generation it started under and drops anything it is handed under a different one.
   */
  std::atomic<uint32_t> connectGeneration{0};
  std::atomic<bool> connectCancelled{false};
  // Set when the cancel came from outside (the shell replacing this viewer), so the failure
  // path can say that rather than "the host did not answer", which is what it looked like.
  std::atomic<bool> connectCancelledByOwner{false};
  std::thread cancelWatcher;
  std::atomic<bool> cancelWatcherStop{false};
};

}  // namespace remote60::native_poc::viewer
