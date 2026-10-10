#pragma once

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <chrono>
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

// One deadline covers the entire framed message, not each successful partial read. recv without
// MSG_WAITALL returns the currently available bytes after select; the next iteration rechecks time.
inline bool recv_all_until(SocketHandle socketHandle, void* destination, size_t length,
                           std::chrono::steady_clock::time_point deadline) {
  auto* bytes = static_cast<char*>(destination);
  size_t done = 0;
  while (done < length) {
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) return false;
    const auto slice = (std::min<int64_t>)(remaining, 200000);
    timeval timeout{0, static_cast<long>(slice)};
    fd_set readable; FD_ZERO(&readable); FD_SET(socketHandle, &readable);
#if defined(_WIN32)
    const int ready = select(0, &readable, nullptr, nullptr, &timeout);
#else
    const int ready = select(socketHandle + 1, &readable, nullptr, nullptr, &timeout);
#endif
    if (ready < 0) return false;
    if (ready == 0) continue;
    const int count = recv(socketHandle, bytes + done,
                            static_cast<int>((std::min<size_t>)(length - done, 1024 * 1024)), 0);
    if (count <= 0) return false;
    done += static_cast<size_t>(count);
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

// The socket error of the LAST socket call, captured now. Call it immediately after a SOCKET_ERROR so a
// later call cannot overwrite it (udp-recv-exit r1 diagnostic contract).
inline int socket_last_error() {
#if defined(_WIN32)
  return WSAGetLastError();
#else
  return errno;
#endif
}

// How a UDP recv() result should be handled (udp-recv-exit r1). A UDP-ONLY policy: it does NOT change
// last_socket_error_is_retryable() (used elsewhere) or recv_all()'s TCP semantics, where a 0 return is a
// real EOF. For UDP a 0 return is a legal empty datagram and a single error indication (truncation, or a
// Windows ICMP-driven reset) is about ONE datagram, not the whole session.
enum class UdpRecvClass {
  Datagram,       // n > 0: a datagram to validate/dispatch
  Empty,          // n == 0: a legal empty UDP datagram -- discard and continue (NOT a TCP EOF)
  Retryable,      // timeout / would-block / interrupted: the maintenance tick, then continue
  TruncatedDrop,  // WSAEMSGSIZE: the datagram was larger than the buffer; discard the whole thing
  ResetAdvisory,  // WSAECONNRESET (Windows UDP: an earlier send's ICMP Port Unreachable): advisory
  Terminal        // anything else: keep the existing terminal path (bad/closed socket, ...)
};

// Classify a UDP recv() result. `nBytes` is recv's return; `err` is socket_last_error() captured
// IMMEDIATELY after a SOCKET_ERROR (ignored when nBytes >= 0). The n==0 (empty datagram) and the
// Retryable set are platform-neutral. The TruncatedDrop / ResetAdvisory handling is WINDOWS-ONLY: it is
// WSAEMSGSIZE (Windows returns a recv error for a truncated datagram) and WSAECONNRESET (Windows reports
// an earlier send's ICMP Port Unreachable on the next recv). On POSIX a truncation is a normal n>0 read
// with MSG_TRUNC (not EMSGSIZE on recv), and an unreachable peer is reported as ECONNREFUSED, not
// ECONNRESET -- so the Windows advisory is NOT ported to POSIX by name; POSIX keeps its existing terminal
// policy for those (only n==0 and the Retryable set change there). WSA/errno are matched by symbol.
inline UdpRecvClass classify_udp_recv(int nBytes, int err) {
  if (nBytes > 0) return UdpRecvClass::Datagram;
  if (nBytes == 0) return UdpRecvClass::Empty;
#if defined(_WIN32)
  switch (err) {
    case WSAEWOULDBLOCK:
    case WSAETIMEDOUT:
    case WSAEINTR:
      return UdpRecvClass::Retryable;
    case WSAEMSGSIZE:
      return UdpRecvClass::TruncatedDrop;
    case WSAECONNRESET:
      return UdpRecvClass::ResetAdvisory;
    default:
      return UdpRecvClass::Terminal;
  }
#else
  // POSIX: only n==0 (handled above) and the Retryable set are non-terminal. EMSGSIZE/ECONNRESET keep the
  // existing terminal policy -- the Windows advisory is not assumed equivalent here (udp-recv-exit r2 U3).
  switch (err) {
    case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case ETIMEDOUT:
    case EINTR:
      return UdpRecvClass::Retryable;
    default:
      return UdpRecvClass::Terminal;
  }
#endif
}

// A non-terminal class is discarded/advisory and MUST NOT be counted as a healthy/ACK/peer-alive event;
// it just lets the receive loop continue and keep its maintenance going.
inline bool udp_recv_is_terminal(UdpRecvClass c) { return c == UdpRecvClass::Terminal; }

}  // namespace remote60::native_poc
