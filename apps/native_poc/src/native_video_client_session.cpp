#include "native_video_client_session.hpp"

#include "poc_protocol.hpp"

#include <cstring>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace remote60::native_poc {

namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

#if defined(_WIN32)
struct WinsockScope {
  WinsockScope() {
    WSADATA wsa{};
    ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
  }
  ~WinsockScope() {
    if (ok) WSACleanup();
  }
  bool ok = false;
};
#endif

bool initialize_sockets(std::string* error) {
#if defined(_WIN32)
  static WinsockScope scope;
  if (!scope.ok) {
    if (error) *error = "winsock startup failed";
    return false;
  }
#else
  (void)error;
#endif
  return true;
}

void close_socket(SocketHandle* socketHandle) {
  if (!socketHandle || *socketHandle == kInvalidSocket) return;
#if defined(_WIN32)
  closesocket(*socketHandle);
#else
  close(*socketHandle);
#endif
  *socketHandle = kInvalidSocket;
}

bool set_recv_timeout(SocketHandle socketHandle, uint32_t timeoutMs) {
#if defined(_WIN32)
  const DWORD timeout = timeoutMs;
  return setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
                    reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
#else
  timeval tv{};
  tv.tv_sec = static_cast<long>(timeoutMs / 1000u);
  tv.tv_usec = static_cast<long>((timeoutMs % 1000u) * 1000u);
  return setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool resolve_endpoint(const std::string& host, int port, int socktype, int protocol,
                      addrinfo** out, std::string* error) {
  if (!out) return false;
  *out = nullptr;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;
  hints.ai_protocol = protocol;
  const std::string service = std::to_string(port);
  const int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, out);
  if (rc != 0 || !*out) {
    if (error) {
#if defined(_WIN32)
      *error = "getaddrinfo failed";
#else
      *error = gai_strerror(rc);
#endif
    }
    return false;
  }
  return true;
}

std::string connected_status_text(const ClientSessionSnapshot& snapshot) {
  std::string status = "connected: " + snapshot.host + ":" + std::to_string(snapshot.videoPort);
  status += " control=" + std::to_string(snapshot.controlPort);
  status += " tcp=" + std::string(snapshot.transport.tcpControlConnected ? "on" : "off");
  status += " udp=" + std::string(snapshot.transport.udpVideoReady ? "on" : "off");
  return status;
}

}  // namespace

ClientSessionController::ClientSessionController() = default;

ClientSessionController::~ClientSessionController() {
  Disconnect();
}

bool ClientSessionController::Connect(const ClientSessionConnectArgs& args) {
  std::string error;
  if (!initialize_sockets(&error)) {
    std::lock_guard<std::mutex> lock(mu_);
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = error;
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);

  ResetUnlocked();
  snapshot_.state = ClientSessionState::Connecting;
  snapshot_.status = "connecting";

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

  snapshot_.host = args.host;
  snapshot_.videoPort = args.videoPort;
  snapshot_.controlPort = args.controlPort;

  if (args.requireTcpControl && !ConnectTcpControl(args, &snapshot_.lastError)) {
    const std::string errorMessage = snapshot_.lastError;
    CloseSocketsUnlocked();
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = errorMessage;
    return false;
  }
  if (args.requireUdpHello && !ConnectUdpVideo(args, &snapshot_.lastError)) {
    const std::string errorMessage = snapshot_.lastError;
    CloseSocketsUnlocked();
    snapshot_.state = ClientSessionState::Error;
    snapshot_.status = "error";
    snapshot_.lastError = errorMessage;
    return false;
  }

  snapshot_.state = ClientSessionState::Connected;
  snapshot_.status = connected_status_text(snapshot_);
  return true;
}

void ClientSessionController::Disconnect() {
  std::lock_guard<std::mutex> lock(mu_);
  ResetUnlocked();
}

ClientSessionSnapshot ClientSessionController::Snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return snapshot_;
}

bool ClientSessionController::IsValidPort(int port) {
  return port > 0 && port <= 65535;
}

bool ClientSessionController::ConnectTcpControl(const ClientSessionConnectArgs& args, std::string* error) {
  addrinfo* results = nullptr;
  if (!resolve_endpoint(args.host, args.controlPort, SOCK_STREAM, IPPROTO_TCP, &results, error)) {
    return false;
  }

  SocketHandle connected = kInvalidSocket;
  for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
    SocketHandle candidate = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (candidate == kInvalidSocket) continue;
    if (connect(candidate, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
      connected = candidate;
      break;
    }
    close_socket(&candidate);
  }
  freeaddrinfo(results);

  if (connected == kInvalidSocket) {
    if (error) *error = "tcp control connect failed";
    return false;
  }
  tcpControlSocket_ = connected;
  snapshot_.transport.tcpControlConnected = true;
  return true;
}

bool ClientSessionController::ConnectUdpVideo(const ClientSessionConnectArgs& args, std::string* error) {
  addrinfo* results = nullptr;
  if (!resolve_endpoint(args.host, args.videoPort, SOCK_DGRAM, IPPROTO_UDP, &results, error)) {
    return false;
  }

  SocketHandle connected = kInvalidSocket;
  for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
    SocketHandle candidate = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (candidate == kInvalidSocket) continue;
    if (connect(candidate, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
      connected = candidate;
      break;
    }
    close_socket(&candidate);
  }
  freeaddrinfo(results);

  if (connected == kInvalidSocket) {
    if (error) *error = "udp video connect failed";
    return false;
  }

  (void)set_recv_timeout(connected, args.udpHandshakeTimeoutMs);
  UdpHelloPacket hello{};
  const int sent = send(connected, reinterpret_cast<const char*>(&hello), sizeof(hello), 0);
  if (sent != static_cast<int>(sizeof(hello))) {
    if (error) *error = "udp hello send failed";
    close_socket(&connected);
    return false;
  }

  UdpHelloPacket ack{};
  const int received = recv(connected, reinterpret_cast<char*>(&ack), sizeof(ack), 0);
  if (received < static_cast<int>(sizeof(UdpHelloPacket)) ||
      ack.magic != kMagic ||
      ack.kind != static_cast<uint16_t>(UdpPacketKind::HelloAck)) {
    if (error) *error = "udp hello ack failed";
    close_socket(&connected);
    return false;
  }

  udpVideoSocket_ = connected;
  snapshot_.transport.udpVideoReady = true;
  return true;
}

void ClientSessionController::ResetUnlocked() {
  CloseSocketsUnlocked();
  snapshot_ = ClientSessionSnapshot{};
}

void ClientSessionController::CloseSocketsUnlocked() {
#if defined(_WIN32)
  SocketHandle tcp = static_cast<SocketHandle>(tcpControlSocket_);
  SocketHandle udp = static_cast<SocketHandle>(udpVideoSocket_);
  close_socket(&tcp);
  close_socket(&udp);
  tcpControlSocket_ = static_cast<uintptr_t>(kInvalidSocket);
  udpVideoSocket_ = static_cast<uintptr_t>(kInvalidSocket);
#else
  close_socket(&tcpControlSocket_);
  close_socket(&udpVideoSocket_);
#endif
}

}  // namespace remote60::native_poc
