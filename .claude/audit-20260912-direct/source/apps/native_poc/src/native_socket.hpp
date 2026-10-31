#pragma once

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace remote60::native_poc {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
constexpr int kShutdownBoth = SD_BOTH;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
constexpr int kShutdownBoth = SHUT_RDWR;
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

inline bool initialize_sockets(std::string* error) {
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

inline void close_socket(SocketHandle* socketHandle) {
  if (!socketHandle || *socketHandle == kInvalidSocket) return;
#if defined(_WIN32)
  closesocket(*socketHandle);
#else
  close(*socketHandle);
#endif
  *socketHandle = kInvalidSocket;
}

inline void shutdown_socket(SocketHandle* socketHandle) {
  if (!socketHandle || *socketHandle == kInvalidSocket) return;
  shutdown(*socketHandle, kShutdownBoth);
  close_socket(socketHandle);
}

inline bool set_recv_timeout(SocketHandle socketHandle, uint32_t timeoutMs) {
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

inline bool set_tcp_nodelay(SocketHandle socketHandle) {
  const int flag = 1;
#if defined(_WIN32)
  return setsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY,
                    reinterpret_cast<const char*>(&flag), sizeof(flag)) == 0;
#else
  return setsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == 0;
#endif
}

inline bool resolve_endpoint(const std::string& host, int port, int socktype, int protocol,
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

inline SocketHandle connect_first_endpoint(const std::string& host, int port, int socktype, int protocol,
                                           std::string* error) {
  addrinfo* results = nullptr;
  if (!resolve_endpoint(host, port, socktype, protocol, &results, error)) {
    return kInvalidSocket;
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

  if (connected == kInvalidSocket && error && error->empty()) {
    *error = "connect failed";
  }
  return connected;
}

inline bool recv_all(SocketHandle socketHandle, void* out, size_t len) {
  auto* bytes = reinterpret_cast<uint8_t*>(out);
  size_t received = 0;
  while (received < len) {
    const int n =
        recv(socketHandle, reinterpret_cast<char*>(bytes + received), static_cast<int>(len - received), 0);
    if (n <= 0) return false;
    received += static_cast<size_t>(n);
  }
  return true;
}

inline bool send_all(SocketHandle socketHandle, const void* data, size_t len) {
  const char* bytes = reinterpret_cast<const char*>(data);
  size_t sent = 0;
  while (sent < len) {
    const int n = send(socketHandle, bytes + sent, static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

inline bool recv_discard(SocketHandle socketHandle, size_t len) {
  std::vector<uint8_t> scratch(1024);
  size_t left = len;
  while (left > 0) {
    const size_t chunk = std::min(left, scratch.size());
    if (!recv_all(socketHandle, scratch.data(), chunk)) return false;
    left -= chunk;
  }
  return true;
}

inline bool last_socket_error_is_retryable() {
#if defined(_WIN32)
  const int err = WSAGetLastError();
  return err == WSAEWOULDBLOCK || err == WSAETIMEDOUT || err == WSAEINTR;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT || errno == EINTR;
#endif
}

}  // namespace remote60::native_poc
