#pragma once

// The viewer's control thread: drives the control scheduler over the TCP control socket or the
// UDP control tunnel and applies the host's replies.
//
// Role:    Run() is the former controlThread lambda of main(): build the ControlLink, then loop
//          NextAction -> execute_control_action -> apply the reply (pong: host capture meta, RTT and
//          clock telemetry; window list; monitor list; window selected; input ack), fetching one
//          picker thumbnail per idle turn; on link failure mark control disconnected and clear the
//          selection.
// Thread:  control only. Owns the ControlLink and the scheduler (ctx.control.scheduler); writes
//          ctx.control.connected / host capture meta / reportedSecure, ctx.picker.windowPanel and thumbs;
//          reads the request states the UI/recv threads fill.
// Input:   ctx.control request states, the host's replies.
// Output:  control messages on the wire; picker/thumbnail state; log lines.
// Callers: main() (controlThread = std::thread([&]{ control.Run(); })).
//
// Bodies are the lambda bodies of native_video_client_main.cpp, verbatim (viewer split refactor
// Phase 2-4); the captured state (args, startInPicker, controlSock) are members with the same names.

#include <memory>

#include "viewer_args.hpp"
#include "viewer_common.hpp"
#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {

class ControlClient {
 public:
  ControlClient(ViewerState& ctx, const Args& args, bool startInPicker)
      : ctx(ctx), args(args), startInPicker(startInPicker) {}
  // The TCP control socket when the host was dialled directly (INVALID_SOCKET on the UDP tunnel).
  // Set by main() before the thread starts; main() still owns and closes it at shutdown.
  SOCKET controlSock = INVALID_SOCKET;
  // The thread body (formerly the controlThread lambda).
  void Run();

 private:
  ViewerState& ctx;  // the session state (F-17)
  const Args& args;
  const bool startInPicker;

  // Fetch one queued preview over the control socket. Runs between scheduler
  // actions on the same strict request/response pipeline, one card per call so a
  // large backlog cannot starve input events. Only invoked when the host advertised
  // the capability, because an older host would drain the request and never reply.
  // Returns: 1 fetched, 0 nothing to do, -1 socket failure (stream desynced).
  int fetch_one_thumbnail(remote60::native_poc::ControlLink& link);
  // Clipboard text sync (K1): send a pending local clipboard change, else poll the host for one on
  // an interval. Runs on the same idle turns as the thumbnail fetch, and only when the host
  // advertised the capability. Returns: 1 did work, 0 nothing to do, -1 link failure (drop session).
  int pump_clipboard_sync(remote60::native_poc::ControlLink& link);
  // Paste on demand (t-y4wj64jw): the text paste exchange (80/81) and the image / file paste
  // outcomes, handed to the UI thread. 1 = exchanged, 0 = nothing, -1 = link failure.
  int pump_paste(remote60::native_poc::ControlLink& link);
  // One clipboard poll (text R->P and the host's copy generation). `forced`: not waiting for the
  // interval (a paste's probe). 1 = polled, 0 = not yet due, -1 = link failure.
  int poll_host_clipboard(remote60::native_poc::ControlLink& link, bool forced);
  // --- control resume (item 8, C3) ---
  // The worker's three moves. Kept here rather than in the loop because the loop already
  // has one job, and because "is the picture still arriving" has to be answered with the
  // SAME definition the session watchdog uses or the two clocks drift apart.
  bool video_alive(uint64_t nowUs) const;
  // Control just failed. True when a recovery has begun and the loop should enter Resuming.
  bool begin_control_resume(uint64_t nowUs);
  // One turn of Resuming. False means the session is over (ceiling passed, or called off).
  bool pump_control_resume(std::unique_ptr<remote60::native_poc::ControlLink>& link,
                           remote60::native_poc::ControlWorkerState& state);
  // A real exchange on the re-keyed channel. An Ack is the host's claim; this is the proof.
  bool control_round_trip(remote60::native_poc::ControlLink& link);

  // the reply switch of Run(), one member per reply kind (verbatim case bodies)
  void handle_pong(const ControlOutboundAction& action, const ControlPongMessage& pong);
  void handle_window_list(const ControlWindowListMessage& windowList);
  void handle_window_selected(const ControlWindowSelectedMessage& windowSelected);
  void handle_input_ack(const ControlInputAckMessage& inputAck);
};

}  // namespace remote60::native_poc::viewer
