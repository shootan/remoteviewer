#pragma once

// Viewer logging and keyframe-request entry points.
//
// Role:    log_client_line (one serialised stdout line), request_keyframe (rate-limited keyframe
//          request with the throttle log), congestion_state_name.
// Thread:  any -- log_client_line serialises on gLogMu; request_keyframe only touches the
//          atomic-backed gKeyframeRequests.
// Input:   a finished log line / a keyframe reason code.
// Output:  stdout / a pending keyframe request for the control thread.
// Callers: recv thread (stats, telemetry, recovery), UI thread (present telemetry), control thread.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-3).

#include "viewer_common.hpp"
#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

const char* congestion_state_name(ClientCongestionState state);

void log_client_line(const std::string& line);

void request_keyframe(uint16_t reason);

}  // namespace remote60::native_poc::viewer
