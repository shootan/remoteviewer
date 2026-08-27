#pragma once

// Decoder backend request matching, for the "backendFallbackReason" the viewer logs when the
// H.264 decoder comes up with something other than what REMOTE60_NATIVE_DECODER_BACKEND asked for.
//
// Role:    backend_request_is_any / backend_request_satisfied / backend_request_is_vendor_specific /
//          backend_fallback_reason -- pure string policy.
// Thread:  none (pure).
// Input:   the requested backend string and the resolved backend name.
// Output:  a fallback-reason token for the log line.
// Callers: recv thread, decoder initialisation log.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-5). The host has
// the same four functions in host_capture_device.cpp -- Phase 0-15 unifies them.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

bool backend_request_is_any(const std::string& requestLower, const char* const* values,
                            size_t valueCount);

bool backend_request_satisfied(const std::string& requestLower, const std::string& resolvedLower);

bool backend_request_is_vendor_specific(const std::string& requestLower);

std::string backend_fallback_reason(const std::string& requestedRaw, const char* resolvedBackendRaw);

}  // namespace remote60::native_poc::viewer
