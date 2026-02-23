#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>
#include <winhttp.h>

#include "common/rtc_signaling_client.hpp"

namespace remote60::common {

class WsRtcSignalingClient final : public RtcSignalingClient {
 public:
  WsRtcSignalingClient();
  ~WsRtcSignalingClient() override;

  bool connect(const std::string& wsUrl) override;
  void disconnect() override;

  bool register_role(const std::string& role) override;
  bool send_message(const signaling::Message& message) override;

  void set_on_message(OnMessage cb) override;
  void set_on_state(OnState cb) override;

 private:
  bool parse_ws_url(const std::string& url, std::wstring* host, INTERNET_PORT* port, std::wstring* path,
                    bool* secure);
  bool send_raw_json(const std::string& json);
  void recv_loop();

  signaling::Message parse_message(const std::string& json) const;
  static std::string json_escape(const std::string& s);

  HINTERNET session_ = nullptr;
  HINTERNET connection_ = nullptr;
  HINTERNET request_ = nullptr;
  HINTERNET websocket_ = nullptr;

  std::thread recv_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> disconnected_notified_{true};
  std::mutex send_mu_;

  OnMessage on_message_;
  OnState on_state_;
};

}  // namespace remote60::common
