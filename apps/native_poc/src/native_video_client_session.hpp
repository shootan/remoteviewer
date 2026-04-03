#pragma once

#include <mutex>
#include <string>

namespace remote60::native_poc {

enum class ClientSessionState : uint8_t {
  Disconnected = 0,
  Connected = 1,
  Error = 2,
};

struct ClientSessionConnectArgs {
  std::string host;
  int videoPort = 0;
  int controlPort = 0;
};

struct ClientSessionSnapshot {
  ClientSessionState state = ClientSessionState::Disconnected;
  std::string status = "disconnected";
  std::string lastError;
  std::string host;
  int videoPort = 0;
  int controlPort = 0;
};

class ClientSessionController {
 public:
  bool Connect(const ClientSessionConnectArgs& args);
  void Disconnect();
  ClientSessionSnapshot Snapshot() const;

 private:
  static bool IsValidPort(int port);

  mutable std::mutex mu_;
  ClientSessionSnapshot snapshot_;
};

}  // namespace remote60::native_poc
