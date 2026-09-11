#pragma once

#include <string>

// The viewer's startup steps, in call order (viewer split refactor Phase 2-10 / 3). Each is the
// corresponding block of the former main() verbatim; a step that can fail returns the exit code
// main() returned there (0 = go on). Bodies in viewer_startup.cpp.
//
// Thread: main, before the control / recv threads exist (connect_control / start_receiver start them).

#include "viewer_context.hpp"

namespace remote60::native_poc::viewer {

void apply_latency_priority();                          // ABOVE_NORMAL unless REMOTE60_NATIVE_NORMAL_PRIORITY
void apply_dpi_awareness();                             // per-monitor v2, before any window exists
void load_config(ViewerContext& ctx, int argc, char** argv);   // parse_args + the REMOTE60_NATIVE_* switches
int validate_codec_transport(ViewerContext& ctx);       // 10 / 12 / 13
void apply_initial_state(ViewerContext& ctx);           // overlay config, initial view, request/gate resets
int create_window_and_toolbar(ViewerContext& ctx);      // 2
int init_decoder(ViewerContext& ctx);                   // 11 (MFStartup, optional DXGI decode surface)
int open_media_socket(ViewerContext& ctx);              // 3 (directory path or a fresh socket)
int connect_media_socket(ViewerContext& ctx);           // 4 / 5 / 6 (socket options, connect, UDP hello)

/**
 * Puts a failure on the screen and waits for the user to decide.
 *
 * Returns true when they asked to try again. Called only after a startup step has failed, and
 * only from the failure path -- a successful start never reaches it.
 *
 * It exists because the program used to know exactly why it could not connect, write that to
 * stderr, and close the window within about a tenth of a second. What the user got was a black
 * rectangle that vanished.
 */
bool show_startup_failure(ViewerContext& ctx, const std::string& reason);
void attach_control_tunnel_and_log(ViewerContext& ctx); // control over the media socket; the connected/limiter/buffer logs
void connect_control(ViewerContext& ctx);               // TCP control socket, input channel, the control thread
void start_receiver(ViewerContext& ctx);                // the recv thread
void run_message_pump(ViewerContext& ctx);              // the UI loop until running clears or --seconds elapse

}  // namespace remote60::native_poc::viewer
