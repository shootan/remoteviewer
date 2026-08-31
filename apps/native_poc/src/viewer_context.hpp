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
  bool startInStreamView = false;   // --initial-view / REMOTE60_NATIVE_START_STREAM_VIEW
  bool startInPicker = false;
  SOCKET controlSock = INVALID_SOCKET;  // the TCP control socket (direct hosts); main closes it
  bool controlReady = false;
  uint64_t startUs = 0;             // session start, for --seconds
  std::optional<ControlClient> controlClient;  // not `control`: that is ViewerState::control (the channel state)
  std::optional<VideoReceiver> receiver;
  std::thread controlThread;
  std::thread recvThread;
};

}  // namespace remote60::native_poc::viewer
