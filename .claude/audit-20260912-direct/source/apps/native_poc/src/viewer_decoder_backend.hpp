#pragma once

// Decoder backend request matching, for the "backendFallbackReason" the viewer logs when the
// H.264 decoder comes up with something other than what REMOTE60_NATIVE_DECODER_BACKEND asked for.
//
// Role:    re-exports backend_request_* / backend_fallback_reason from backend_request_match.hpp
//          (shared with the host since viewer split Phase 0-15) into namespace viewer.
// Thread:  none (pure).
// Input:   the requested backend string and the resolved backend name.
// Output:  a fallback-reason token for the log line.
// Callers: recv thread, decoder initialisation log.

#include "backend_request_match.hpp"
#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

using remote60::native_poc::backend_request_is_any;
using remote60::native_poc::backend_request_satisfied;
using remote60::native_poc::backend_request_is_vendor_specific;
using remote60::native_poc::backend_fallback_reason;

}  // namespace remote60::native_poc::viewer
