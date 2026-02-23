#include "common/rtc_signaling_client.hpp"

namespace remote60::common {

bool NullRtcSignalingClient::connect(const std::string& wsUrl) {
  ws_url_ = wsUrl;
  if (on_state_) on_state_("connected:null");
  return !ws_url_.empty();
}

void NullRtcSignalingClient::disconnect() {
  if (on_state_) on_state_("disconnected:null");
}

bool NullRtcSignalingClient::register_role(const std::string& role) {
  if (!signaling::is_valid_role(role)) {
    if (on_state_) on_state_("register_failed:invalid_role");
    return false;
  }
  role_ = role;
  if (on_state_) on_state_("registered:null:" + role_);
  return true;
}

bool NullRtcSignalingClient::send_message(const signaling::Message& message) {
  if (!on_message_) return false;
  on_message_(message);
  return true;
}

void NullRtcSignalingClient::set_on_message(OnMessage cb) { on_message_ = std::move(cb); }

void NullRtcSignalingClient::set_on_state(OnState cb) { on_state_ = std::move(cb); }

}  // namespace remote60::common
