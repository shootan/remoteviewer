#include "directory_rendezvous.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "poc_protocol.hpp"

namespace remote60::native_poc {
namespace {

constexpr uint32_t kObserveAttempts = 6;
constexpr uint32_t kObserveWaitMs = 300;
constexpr uint32_t kPunchIntervalMs = 150;

bool resolve_udp(const std::string& host, int port, sockaddr_in* out) {
  addrinfo* results = nullptr;
  std::string error;
  if (!resolve_endpoint(host, port, SOCK_DGRAM, IPPROTO_UDP, &results, &error)) return false;
  bool found = false;
  for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
    if (it->ai_family != AF_INET) continue;
    std::memcpy(out, it->ai_addr, sizeof(sockaddr_in));
    found = true;
    break;
  }
  freeaddrinfo(results);
  return found;
}

/** Extracts a value from the directory's tiny {"ip":"..","port":N} reply. */
bool parse_observed(const std::string& json, std::string* outIp, int* outPort) {
  const size_t ipKey = json.find("\"ip\"");
  const size_t portKey = json.find("\"port\"");
  if (ipKey == std::string::npos || portKey == std::string::npos) return false;
  const size_t ipStart = json.find('"', json.find(':', ipKey) + 1);
  if (ipStart == std::string::npos) return false;
  const size_t ipEnd = json.find('"', ipStart + 1);
  if (ipEnd == std::string::npos) return false;
  *outIp = json.substr(ipStart + 1, ipEnd - ipStart - 1);

  size_t cursor = json.find(':', portKey);
  if (cursor == std::string::npos) return false;
  ++cursor;
  while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t')) ++cursor;
  int port = 0;
  while (cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9') {
    port = port * 10 + (json[cursor] - '0');
    ++cursor;
  }
  if (port <= 0 || port > 65535) return false;
  *outPort = port;
  return !outIp->empty();
}

}  // namespace

DirectoryRendezvous::~DirectoryRendezvous() { Close(); }

void DirectoryRendezvous::Close() { close_socket(&socket_); }

SocketHandle DirectoryRendezvous::Release() {
  const SocketHandle handle = socket_;
  socket_ = kInvalidSocket;
  return handle;
}

bool DirectoryRendezvous::Observe(const std::string& directoryHost, int directoryUdpPort,
                                  const std::string& observeToken, std::string* outObserved,
                                  std::string* outError) {
  Close();
  if (!initialize_sockets(outError)) return false;

  sockaddr_in directory{};
  if (!resolve_udp(directoryHost, directoryUdpPort, &directory)) {
    if (outError) *outError = "cannot resolve directory host";
    return false;
  }

  SocketHandle sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == kInvalidSocket) {
    if (outError) *outError = "udp socket create failed";
    return false;
  }
  // Matches what the session would have set: a keyframe burst overruns a default-sized buffer.
  {
    int recvBuf = 4 * 1024 * 1024;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf),
                   sizeof(recvBuf)) != 0) {
      recvBuf = 1024 * 1024;
      (void)setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf),
                       sizeof(recvBuf));
    }
  }
  (void)set_recv_timeout(sock, kObserveWaitMs);

  const std::string probe = "OBSERVE " + observeToken;
  char reply[256];
  for (uint32_t attempt = 0; attempt < kObserveAttempts; ++attempt) {
    (void)sendto(sock, probe.data(), static_cast<int>(probe.size()), 0,
                 reinterpret_cast<const sockaddr*>(&directory), sizeof(directory));

    sockaddr_in from{};
#if defined(_WIN32)
    int fromLen = sizeof(from);
#else
    socklen_t fromLen = sizeof(from);
#endif
    const int n = recvfrom(sock, reply, sizeof(reply) - 1, 0,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n <= 0) continue;
    reply[n] = '\0';

    std::string ip;
    int port = 0;
    if (!parse_observed(std::string(reply, static_cast<size_t>(n)), &ip, &port)) continue;
    if (outObserved) *outObserved = ip + ":" + std::to_string(port);
    socket_ = sock;
    return true;
  }

  close_socket(&sock);
  if (outError) *outError = "directory did not answer the address probe";
  return false;
}

bool DirectoryRendezvous::Punch(const std::string& hostIp, int hostPort, uint32_t budgetMs,
                                std::string* outError) {
  if (socket_ == kInvalidSocket) {
    if (outError) *outError = "no prepared socket; observe first";
    return false;
  }

  sockaddr_in host{};
  if (!resolve_udp(hostIp, hostPort, &host)) {
    if (outError) *outError = "cannot resolve host address";
    return false;
  }

  (void)set_recv_timeout(socket_, kPunchIntervalMs);
  UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(UdpPacketKind::Punch);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  char scratch[512];
  while (std::chrono::steady_clock::now() < deadline) {
    (void)sendto(socket_, reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
                 reinterpret_cast<const sockaddr*>(&host), sizeof(host));

    sockaddr_in from{};
#if defined(_WIN32)
    int fromLen = sizeof(from);
#else
    socklen_t fromLen = sizeof(from);
#endif
    const int n = recvfrom(socket_, scratch, sizeof(scratch), 0,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
    // Anything at all from the host proves the path is open in this direction.
    if (n > 0 && from.sin_addr.s_addr == host.sin_addr.s_addr && from.sin_port == host.sin_port) {
      return true;
    }
  }

  if (outError) *outError = "punch timed out";
  return false;
}

}  // namespace remote60::native_poc
