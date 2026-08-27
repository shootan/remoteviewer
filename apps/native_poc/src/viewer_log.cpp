// See viewer_log.hpp. Extracted verbatim from native_video_client_main.cpp (Phase 0-3).

#include "viewer_log.hpp"

#include <iostream>
#include <mutex>

namespace remote60::native_poc::viewer {

const char* congestion_state_name(ClientCongestionState state) {
  switch (state) {
    case ClientCongestionState::Normal:
      return "normal";
    case ClientCongestionState::Recovering:
      return "recovering";
    case ClientCongestionState::Congested:
      return "congested";
    default:
      return "unknown";
  }
}

void log_client_line(const std::string& line) {
  std::lock_guard<std::mutex> lk(gSession.logMu);
  const std::string withNewline = line + "\n";
  std::cout << withNewline;
}

void request_keyframe(uint16_t reason) {
  const uint64_t nowUs = qpc_now_us();
  const auto attempt = gKeyframeRequests.Request(reason, nowUs);
  if (!attempt.queued && (attempt.throttledCount % 120) == 1) {
    std::cout << "[native-video-client][control] keyframe-request-throttled total=" << attempt.throttledCount
              << " reason=" << (reason == 0 ? 1 : reason)
              << " cause=" << attempt.throttleCause << "\n";
  }
}

}  // namespace remote60::native_poc::viewer
