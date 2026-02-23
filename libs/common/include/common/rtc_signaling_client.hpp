#pragma once

#include <functional>
#include <string>

#include "common/signaling_protocol.hpp"

namespace remote60::common {

class RtcSignalingClient {
 public:
  using OnMessage = std::function<void(const signaling::Message&)>;
  using OnState = std::function<void(const std::string&)>;

  virtual ~RtcSignalingClient() = default;

  virtual bool connect(const std::string& wsUrl) = 0;
  virtual void disconnect() = 0;

  virtual bool register_role(const std::string& role) = 0;
  virtual bool send_message(const signaling::Message& message) = 0;

  virtual void set_on_message(OnMessage cb) = 0;
  virtual void set_on_state(OnState cb) = 0;
};

class NullRtcSignalingClient final : public RtcSignalingClient {
 public:
  bool connect(const std::string& wsUrl) override;
  void disconnect() override;

  bool register_role(const std::string& role) override;
  bool send_message(const signaling::Message& message) override;

  void set_on_message(OnMessage cb) override;
  void set_on_state(OnState cb) override;

 private:
  std::string ws_url_;
  std::string role_;
  OnMessage on_message_;
  OnState on_state_;
};

}  // namespace remote60::common
