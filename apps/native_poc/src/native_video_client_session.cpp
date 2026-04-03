#include "native_video_client_session.hpp"

#include <utility>

namespace remote60::native_poc {

bool ClientSessionController::Connect(const ClientSessionConnectArgs& args) {
  std::lock_guard<std::mutex> lock(mu_);

  snapshot_.lastError.clear();
  snapshot_.host.clear();
  snapshot_.videoPort = 0;
  snapshot_.controlPort = 0;

  if (args.host.empty()) {
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = "host is required";
    return false;
  }
  if (!IsValidPort(args.videoPort)) {
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = "video port is invalid";
    return false;
  }
  if (!IsValidPort(args.controlPort)) {
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = "control port is invalid";
    return false;
  }

  snapshot_.state = ClientSessionState::Connected;
  snapshot_.host = args.host;
  snapshot_.videoPort = args.videoPort;
  snapshot_.controlPort = args.controlPort;
  snapshot_.status = "connected(shell-controller): " + snapshot_.host + ":" +
                     std::to_string(snapshot_.videoPort) + " control=" +
                     std::to_string(snapshot_.controlPort);
  return true;
}

void ClientSessionController::Disconnect() {
  std::lock_guard<std::mutex> lock(mu_);
  snapshot_ = ClientSessionSnapshot{};
}

ClientSessionSnapshot ClientSessionController::Snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return snapshot_;
}

bool ClientSessionController::IsValidPort(int port) {
  return port > 0 && port <= 65535;
}

}  // namespace remote60::native_poc
