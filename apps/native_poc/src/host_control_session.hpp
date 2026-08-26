#pragma once

// Control session server: one viewer's control conversation on the host.
//
// Role:    receives the ControlXxx messages of one connected viewer (ping, stream state, client
//          metrics, keyframe / runtime-tune / backend / capture-mode requests, window + monitor
//          lists and selection, thumbnails, input events and text) and either answers them on the
//          link or hands them to the main loop through the shared state structs (request atomics,
//          the WindowSelectionTxn handshake). Serve() is the former serve_control_session lambda,
//          moved verbatim (Phase 2-2 step 1); step 2 splits it into one Handle* per message.
// Thread:  control thread only (TCP: one thread per accepted socket; UDP: the control dispatcher
//          thread). Everything it shares with the main loop is an atomic / mutex-guarded field of
//          the state structs it holds references to; it never touches capture resources itself.
// Input:   ControlLink (TCP or UDP-tunnelled), Args, the host state structs.
// Output:  control replies on the link; request flags/values in the state structs; counters.
// Callers: native_video_host_main.cpp (TCP control accept loop, UDP control dispatcher thread).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

#include "host_args.hpp"
#include "host_backend_policy.hpp"
#include "host_capture_session.hpp"
#include "host_client_metrics.hpp"
#include "host_encoder_manager.hpp"
#include "host_input_router.hpp"
#include "host_session.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

// Window-selection handshake between the control thread (which receives ControlWindowSelect) and
// the main loop (which owns the capture item and performs the switch). The control thread posts
// the request and waits on cv for the main loop to fill in the response fields.
struct WindowSelectionTxn {
  std::mutex mu;
  std::condition_variable cv;
  bool pending = false;
  bool completed = false;
  uint32_t reqSeq = 0;
  uint64_t requestedWindowId = 0;
  uint32_t responseFlags = 0;
  uint64_t responseWindowId = 0;
  uint64_t responseStreamGeneration = 0;
  std::string responseReason;
  std::string responseTitle;
};

class ControlSessionServer {
 public:
  ControlSessionServer(const Args& args, std::atomic<bool>& stop, SessionState& clientSession,
                       CaptureState& capture, ClientMetricsSnapshot& clientMetrics,
                       EncoderState& encoder, InputRouterState& inputRouter,
                       DesktopBackendState& backend, WindowSelectionTxn& windowSelectionTxn);

  // Serve one control conversation until the link dies or the host stops.
  void Serve(ControlLink& link);

 private:
  // Named exactly like the main() locals they alias so the moved body reads unchanged.
  const Args& args;
  std::atomic<bool>& stop;
  SessionState& clientSession;
  CaptureState& capture;
  ClientMetricsSnapshot& clientMetrics;
  EncoderState& encoder;
  InputRouterState& inputRouter;
  DesktopBackendState& backend;
  WindowSelectionTxn& windowSelectionTxn;
};

}  // namespace remote60::native_poc
