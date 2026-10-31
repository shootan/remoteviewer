#pragma once

// Viewer logging and keyframe-request entry points.
//
// Role:    log_client_line (one serialised stdout line), request_keyframe (rate-limited keyframe
//          request with the throttle log), congestion_state_name.
// Thread:  any -- log_client_line serialises on ctx.session.logMu; request_keyframe only touches the
//          atomic-backed ctx.control.keyframeRequests.
// Input:   a finished log line / a keyframe reason code.
// Output:  stdout / a pending keyframe request for the control thread.
// Callers: recv thread (stats, telemetry, recovery), UI thread (present telemetry), control thread.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-3).

#include "viewer_common.hpp"
#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {


void log_client_line(ViewerState& ctx, const std::string& line);

void request_keyframe(ViewerState& ctx, uint16_t reason);

}  // namespace remote60::native_poc::viewer
