// GNLinkViewer: the viewer's main(). The session is a ViewerContext (viewer_context.hpp) driven
// through the startup steps of viewer_startup.hpp in order, the message pump, and shutdown_viewer;
// every step is a verbatim block of the monolith's main(). Exit codes are the ones the monolith
// returned at the same points. (viewer split refactor Phase 3)

#include <string>
#include "viewer_common.hpp"
#include "viewer_context.hpp"
#include "viewer_shutdown.hpp"
#include "viewer_cancel_channel.hpp"
#include "viewer_startup.hpp"

#include <iostream>

using namespace remote60::native_poc::viewer;

int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);
  apply_latency_priority();
  apply_dpi_awareness();

  ViewerContext ctx;
  load_config(ctx, argc, argv);
  if (const int rc = validate_codec_transport(ctx)) return rc;
  apply_initial_state(ctx);

  remote60::native_poc::WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-client] WSAStartup failed\n";
    return 1;
  }

  if (const int rc = create_window_and_toolbar(ctx)) return rc;
  if (const int rc = init_decoder(ctx)) return rc;

  // The connection attempt, and the only thing that changes when it fails: the window stays and
  // says why. The reason was already known here -- it went to stderr -- and the program closed
  // before anyone could read it, which is what a user experiences as "it did nothing".
  //
  // Success is untouched: a connection that works leaves this loop on the first pass with the
  // same calls in the same order as before.
  // The shell starts another GNLinkViewer when the user picks a different PC, and does not close
  // this one. Until this existed, this process carried on punching and saying hello -- now for up
  // to thirty seconds -- and could still establish a session afterwards, against a host that had
  // moved on. The watcher is what turns that into a stop the connect path can read.
  //
  // Started before the first attempt and stopped after the last, so a cancel that arrives between
  // attempts is still seen.
  // The handle the shell created and let this process inherit, named on the command line by
  // value. Nothing is opened here: an unnamed event has no name to open, which is the point --
  // the earlier named one could be signalled by any process in the session that knew a pid.
  //
  // The value is ASCII digits, so it converts without needing the viewer's wide-string
  // helpers, which live in the shell rather than here.
  HANDLE cancelEvent = nullptr;
  if (!ctx.args.cancelEventHandle.empty()) {
    cancelEvent = viewer_cancel_handle_from_arg(
        std::wstring(ctx.args.cancelEventHandle.begin(), ctx.args.cancelEventHandle.end()));
    if (!cancelEvent) {
      std::cerr << "[native-video-client] the --cancel-event value is not a usable handle; "
                << "this session cannot be called off" << std::endl;
    }
  }
  (void)viewer_start_cancel_watcher(cancelEvent, &ctx.connectCancelled,
                                    &ctx.connectCancelledByOwner, &ctx.cancelWatcherStop,
                                    &ctx.cancelWatcher);
  for (;;) {
    // A new attempt. The generation moves first, so anything still in flight from the previous
    // one is already superseded when it comes back.
    ctx.connectGeneration.fetch_add(1, std::memory_order_acq_rel);
    const int opened = open_media_socket(ctx);
    const int rc = opened != 0 ? opened : connect_media_socket(ctx);
    if (rc == 0) break;

    // Called off rather than failed. Showing "try again" here would be asking about a session
    // the user has already replaced.
    if (ctx.connectCancelled.load(std::memory_order_acquire)) {
      viewer_stop_cancel_watcher(&ctx.cancelWatcherStop, &ctx.cancelWatcher);
      if (ctx.dec.mfStarted) MFShutdown();
      return 0;
    }

    // Only what the code actually knows. No guess about firewalls or accounts.
    std::string reason;
    if (rc == 3) {
      reason = "연결을 준비하지 못했습니다.";
    } else if (rc == 4) {
      reason = "주소가 올바르지 않습니다: " + ctx.resolvedArgs.host;
    } else if (rc == 5) {
      reason = ctx.resolvedArgs.host + ":" + std::to_string(ctx.resolvedArgs.port) +
               " 에 연결하지 못했습니다.\n그 PC 에서 GNLink 가 실행 중인지 확인해 주세요.";
    } else if (rc == 7) {
      // Not a failure, and the user must not be asked to retry something they replaced. This
      // is unreachable in practice -- the cancel check above returns first -- and it is here
      // so that a future path which returns 7 without going through that check cannot end up
      // telling the user their connection failed.
      reason = "연결이 취소되었습니다.";
    } else {
      reason = "연결하지 못했습니다.";
    }
    reason += "\n(코드 " + std::to_string(rc) + ")";

    if (!show_startup_failure(ctx, reason)) {
      viewer_stop_cancel_watcher(&ctx.cancelWatcherStop, &ctx.cancelWatcher);
      return rc;
    }
    // Asked for, never automatic: an automatic retry would overwrite the log line that says what
    // went wrong.
    std::cout << "[native-video-client] retry requested by the user\n";
  }
  // Connect is over, one way or the other. The watcher has nothing left to interrupt, and the
  // session that follows is torn down through the ordinary path.
  viewer_stop_cancel_watcher(&ctx.cancelWatcherStop, &ctx.cancelWatcher);

  attach_control_tunnel_and_log(ctx);
  try {
    connect_control(ctx);
    start_receiver(ctx);
    run_message_pump(ctx);
  } catch (...) {
    ctx.session.recoveryExitCode.store(43);
  }

  shutdown_viewer(ctx);
  std::cout << "[native-video-client] done\n";
  return ctx.session.recoveryExitCode.load();
}
