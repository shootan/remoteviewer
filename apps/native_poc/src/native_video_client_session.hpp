#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace remote60::native_poc {

enum class ClientSessionState : uint8_t {
  Disconnected = 0,
  Connecting = 1,
  Connected = 2,
  Error = 3,
};

struct ClientSessionTransportStatus {
  bool tcpControlConnected = false;
  bool udpVideoReady = false;
};

struct ClientSessionConnectArgs {
  std::string host;
  int videoPort = 0;
  int controlPort = 0;
  bool requireUdpHello = true;
  bool requireTcpControl = true;
  uint32_t udpHandshakeTimeoutMs = 800;
};

struct ClientSessionSnapshot {
  ClientSessionState state = ClientSessionState::Disconnected;
  std::string status = "disconnected";
  std::string lastError;
  std::string host;
  int videoPort = 0;
  int controlPort = 0;
  ClientSessionTransportStatus transport{};
};

class ClientSessionController {
 public:
  ClientSessionController();
  ~ClientSessionController();

  bool Connect(const ClientSessionConnectArgs& args);
  void Disconnect();
  ClientSessionSnapshot Snapshot() const;

 private:
  ClientSessionController(const ClientSessionController&) = delete;
  ClientSessionController& operator=(const ClientSessionController&) = delete;

  bool ConnectTcpControl(const ClientSessionConnectArgs& args, std::string* error);
  bool ConnectUdpVideo(const ClientSessionConnectArgs& args, std::string* error);
  void CloseSocketsUnlocked();
  void ResetUnlocked();
  static bool IsValidPort(int port);

  mutable std::mutex mu_;
  ClientSessionSnapshot snapshot_;

#if defined(_WIN32)
  uintptr_t tcpControlSocket_ = static_cast<uintptr_t>(~0ULL);
  uintptr_t udpVideoSocket_ = static_cast<uintptr_t>(~0ULL);
#else
  int tcpControlSocket_ = -1;
  int udpVideoSocket_ = -1;
#endif
};

}  // namespace remote60::native_poc
