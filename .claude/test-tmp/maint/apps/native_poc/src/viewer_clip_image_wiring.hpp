#pragma once

// Clipboard image v1: how the viewer wires its image client and the transfer bar.
//
// One implementation for the product (viewer_startup.cpp) and for viewer_clip_bar_e2e_test, so
// the test drives the product's wiring rather than a copy of it.

#include <cstdint>

namespace remote60::native_poc::viewer {

struct ViewerState;

/**
 * Starts ctx.control.clipImage once the Hello ack is in: only when the host agreed to the bulk
 * channel. It sends on the session's (connected) socket, reads the control RTT as evidence and
 * yields to control.
 */
void start_clip_image_client(ViewerState& ctx, uint32_t udpMtu);

/**
 * The transfer bar (viewer_clip_transfer_bar.hpp) over ctx.session.hwnd: progress from the image
 * client, Cancel into ClipImageClient::CancelByUser. Destroyed in WM_DESTROY.
 */
void create_clip_transfer_bar(ViewerState& ctx);

}  // namespace remote60::native_poc::viewer
