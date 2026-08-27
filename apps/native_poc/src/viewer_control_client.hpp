#pragma once

// The viewer's control thread: drives the control scheduler over the TCP control socket or the
// UDP control tunnel and applies the host's replies.
//
// Role:    Run() is the former controlThread lambda of main(): build the ControlLink, then loop
//          NextAction -> execute_control_action -> apply the reply (pong: host capture meta, RTT and
//          clock telemetry; window list; monitor list; window selected; input ack), fetching one
//          picker thumbnail per idle turn; on link failure mark control disconnected and clear the
//          selection.
// Thread:  control only. Owns the ControlLink and the scheduler (gControl.scheduler); writes
//          gControl.connected / host capture meta / reportedSecure, gPicker.windowPanel and thumbs;
//          reads the request states the UI/recv threads fill.
// Input:   gControl request states, the host's replies.
// Output:  control messages on the wire; picker/thumbnail state; log lines.
// Callers: main() (controlThread = std::thread([&]{ control.Run(); })).
//
// Bodies are the lambda bodies of native_video_client_main.cpp, verbatim (viewer split refactor
// Phase 2-4); the captured state (args, startInPicker, controlSock) are members with the same names.

#include "viewer_args.hpp"
#include "viewer_common.hpp"
#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

class ControlClient {
 public:
  ControlClient(const Args& args, bool startInPicker) : args(args), startInPicker(startInPicker) {}
  // The TCP control socket when the host was dialled directly (INVALID_SOCKET on the UDP tunnel).
  // Set by main() before the thread starts; main() still owns and closes it at shutdown.
  SOCKET controlSock = INVALID_SOCKET;
  // The thread body (formerly the controlThread lambda).
  void Run();

 private:
  const Args& args;
  const bool startInPicker;

  // Fetch one queued preview over the control socket. Runs between scheduler
  // actions on the same strict request/response pipeline, one card per call so a
  // large backlog cannot starve input events. Only invoked when the host advertised
  // the capability, because an older host would drain the request and never reply.
  // Returns: 1 fetched, 0 nothing to do, -1 socket failure (stream desynced).
  int fetch_one_thumbnail(remote60::native_poc::ControlLink& link);
  // the reply switch of Run(), one member per reply kind (verbatim case bodies)
  void handle_pong(const ControlOutboundAction& action, const ControlPongMessage& pong);
  void handle_window_list(const ControlWindowListMessage& windowList);
  void handle_window_selected(const ControlWindowSelectedMessage& windowSelected);
  void handle_input_ack(const ControlInputAckMessage& inputAck);
};

}  // namespace remote60::native_poc::viewer
