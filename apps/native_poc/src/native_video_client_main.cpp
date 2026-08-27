// GNLinkViewer: the viewer's main(). The session is a ViewerContext (viewer_context.hpp) driven
// through the startup steps of viewer_startup.hpp in order, the message pump, and shutdown_viewer;
// every step is a verbatim block of the monolith's main(). Exit codes are the ones the monolith
// returned at the same points. (viewer split refactor Phase 3)

#include "viewer_common.hpp"
#include "viewer_context.hpp"
#include "viewer_shutdown.hpp"
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
  if (const int rc = open_media_socket(ctx)) return rc;
  if (const int rc = connect_media_socket(ctx)) return rc;
  attach_control_tunnel_and_log(ctx);
  connect_control(ctx);
  start_receiver(ctx);

  run_message_pump(ctx);

  shutdown_viewer(ctx);
  std::cout << "[native-video-client] done\n";
  return 0;
}
