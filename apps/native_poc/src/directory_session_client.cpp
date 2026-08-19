#include "directory_session_client.hpp"

// windows.h defines min/max as macros, which breaks std::min in the socket header below.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdlib>
#include <cstring>
#include <sstream>

#include "directory_client.hpp"
#include "json_profile.hpp"
#include "native_socket.hpp"

namespace remote60::native_poc {

using json_profile::json_get_bool;
using json_profile::json_get_string;
using json_profile::json_get_u32;

namespace {

// Long enough for a signup that hashes a password on a small VM, short enough that a wrong
// address does not look like a hang.
constexpr int kHttpTimeoutMs = 8000;
// Nothing the directory returns comes close; this only stops a hostile or broken server from
// making us read forever.
constexpr size_t kMaxResponseBytes = 256 * 1024;

bool resolve_ipv4(const std::string& host, uint16_t port, sockaddr_in* out) {
  if (!out) return false;
  addrinfoW hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  const std::wstring whost(host.begin(), host.end());
  const std::wstring wport = std::to_wstring(port);
  PADDRINFOW result = nullptr;
  if (GetAddrInfoW(whost.c_str(), wport.c_str(), &hints, &result) != 0 || !result) return false;
  std::memcpy(out, result->ai_addr, sizeof(sockaddr_in));
  FreeAddrInfoW(result);
  return true;
}

SOCKET connect_with_timeout(const sockaddr_in& addr, int timeoutMs) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return INVALID_SOCKET;
  u_long nonBlocking = 1;
  ioctlsocket(s, FIONBIO, &nonBlocking);
  connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));

  fd_set writeSet;
  FD_ZERO(&writeSet);
  FD_SET(s, &writeSet);
  timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  const int ready = select(0, nullptr, &writeSet, nullptr, &tv);
  u_long blocking = 0;
  ioctlsocket(s, FIONBIO, &blocking);
  if (ready <= 0) {
    closesocket(s);
    return INVALID_SOCKET;
  }
  return s;
}

/**
 * One request, one response, connection closed.
 *
 * Closing per request rather than keeping the socket alive: these calls happen at human pace --
 * a sign-in, a list refresh, a connect -- so a pool would be complexity bought for nothing, and
 * `Connection: close` means the body ends at EOF instead of needing a chunked-encoding parser.
 */
bool http_request(const std::string& host, uint16_t port, const char* method,
                  const std::string& path, const std::string& bearer, const std::string& body,
                  uint32_t* outStatus, std::string* outResponse) {
  sockaddr_in addr{};
  if (!resolve_ipv4(host, port, &addr)) return false;
  SOCKET s = connect_with_timeout(addr, kHttpTimeoutMs);
  if (s == INVALID_SOCKET) return false;

  // A server that accepts the connection and then says nothing would otherwise hang the UI
  // thread for as long as the OS allows.
  DWORD recvTimeout = static_cast<DWORD>(kHttpTimeoutMs);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeout),
             sizeof(recvTimeout));

  std::ostringstream req;
  req << method << " " << path << " HTTP/1.1\r\n"
      << "Host: " << host << ":" << port << "\r\n";
  if (!bearer.empty()) req << "Authorization: Bearer " << bearer << "\r\n";
  if (!body.empty()) {
    req << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n";
  }
  req << "Connection: close\r\n\r\n"
      << body;
  const std::string reqText = req.str();

  size_t sent = 0;
  while (sent < reqText.size()) {
    const int n = send(s, reqText.data() + sent, static_cast<int>(reqText.size() - sent), 0);
    if (n <= 0) {
      closesocket(s);
      return false;
    }
    sent += static_cast<size_t>(n);
  }

  std::string raw;
  char buf[4096];
  for (;;) {
    const int n = recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;
    raw.append(buf, static_cast<size_t>(n));
    if (raw.size() > kMaxResponseBytes) break;
  }
  closesocket(s);

  if (raw.rfind("HTTP/", 0) != 0) return false;
  const size_t statusStart = raw.find(' ');
  if (statusStart == std::string::npos) return false;
  if (outStatus) {
    *outStatus = static_cast<uint32_t>(std::strtoul(raw.c_str() + statusStart + 1, nullptr, 10));
  }
  const size_t bodyStart = raw.find("\r\n\r\n");
  if (outResponse) {
    *outResponse = bodyStart == std::string::npos ? std::string() : raw.substr(bodyStart + 4);
  }
  return true;
}

/** The server's own wording when it has one; the status code alone explains too little. */
std::string error_from_response(uint32_t status, const std::string& body) {
  std::string message;
  if (json_get_string(body, "error", &message) && !message.empty()) return message;
  return "server returned HTTP " + std::to_string(status);
}

std::string json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

/**
 * Walks the top-level objects of a JSON array named `key`.
 *
 * The directory's replies are small and shaped by us at both ends, so a brace-depth scan is
 * enough and keeps this file free of a JSON dependency. It does mean a string containing a brace
 * would confuse it -- host names and ids come from the same server, and nothing else is parsed
 * this way.
 */
std::vector<std::string> json_array_objects(const std::string& json, const std::string& key) {
  std::vector<std::string> out;
  const size_t keyPos = json.find("\"" + key + "\"");
  if (keyPos == std::string::npos) return out;
  const size_t arrayStart = json.find('[', keyPos);
  if (arrayStart == std::string::npos) return out;

  int depth = 0;
  size_t objectStart = std::string::npos;
  for (size_t i = arrayStart; i < json.size(); ++i) {
    const char c = json[i];
    if (c == '{') {
      if (depth == 0) objectStart = i;
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0 && objectStart != std::string::npos) {
        out.push_back(json.substr(objectStart, i - objectStart + 1));
        objectStart = std::string::npos;
      }
    } else if (c == ']' && depth == 0) {
      break;
    }
  }
  return out;
}

bool split_url(const std::string& url, std::string* host, uint16_t* port, std::string* error) {
  return directory::parse_directory_url(url, host, port, error);
}

}  // namespace

bool directory_login(const std::string& url, const std::string& accountId,
                     const std::string& password, std::string* outSessionToken,
                     std::string* outError) {
  std::string host;
  uint16_t port = 0;
  if (!split_url(url, &host, &port, outError)) return false;

  std::ostringstream body;
  body << "{\"id\":\"" << json_escape(accountId) << "\",\"pw\":\"" << json_escape(password) << "\"}";

  uint32_t status = 0;
  std::string response;
  if (!http_request(host, port, "POST", "/api/login", {}, body.str(), &status, &response)) {
    if (outError) *outError = "cannot reach the server";
    return false;
  }
  if (status != 200) {
    if (outError) *outError = error_from_response(status, response);
    return false;
  }
  std::string token;
  if (!json_get_string(response, "sessionToken", &token) || token.empty()) {
    if (outError) *outError = "server did not return a session";
    return false;
  }
  if (outSessionToken) *outSessionToken = token;
  return true;
}

bool directory_list_hosts(const std::string& url, const std::string& sessionToken,
                          std::vector<DirectoryHostEntry>* outHosts, std::string* outError) {
  std::string host;
  uint16_t port = 0;
  if (!split_url(url, &host, &port, outError)) return false;

  uint32_t status = 0;
  std::string response;
  if (!http_request(host, port, "GET", "/api/hosts", sessionToken, {}, &status, &response)) {
    if (outError) *outError = "cannot reach the server";
    return false;
  }
  if (status != 200) {
    if (outError) *outError = error_from_response(status, response);
    return false;
  }

  if (outHosts) {
    outHosts->clear();
    for (const auto& object : json_array_objects(response, "hosts")) {
      DirectoryHostEntry entry{};
      if (!json_get_string(object, "hostId", &entry.hostId) || entry.hostId.empty()) continue;
      json_get_string(object, "hostName", &entry.hostName);
      json_get_bool(object, "online", &entry.online);
      uint32_t lastSeen = 0;
      if (json_get_u32(object, "lastSeen", &lastSeen)) entry.lastSeenMs = lastSeen;
      outHosts->push_back(std::move(entry));
    }
  }
  return true;
}

bool directory_connect(const std::string& url, const std::string& sessionToken,
                       const std::string& hostId, const std::string& observeToken,
                       DirectoryConnectTarget* outTarget, std::string* outError) {
  std::string host;
  uint16_t port = 0;
  if (!split_url(url, &host, &port, outError)) return false;

  std::ostringstream body;
  body << "{\"hostId\":\"" << json_escape(hostId) << "\",\"observeToken\":\""
       << json_escape(observeToken) << "\"}";

  uint32_t status = 0;
  std::string response;
  if (!http_request(host, port, "POST", "/api/connect", sessionToken, body.str(), &status,
                    &response)) {
    if (outError) *outError = "cannot reach the server";
    return false;
  }
  if (status != 200) {
    if (outError) *outError = error_from_response(status, response);
    return false;
  }
  if (!outTarget) return true;

  *outTarget = DirectoryConnectTarget{};
  json_get_string(response, "punchToken", &outTarget->punchToken);
  json_get_string(response, "hostPublicIp", &outTarget->hostPublicIp);
  uint32_t publicPort = 0;
  if (json_get_u32(response, "hostPublicUdpPort", &publicPort)) {
    outTarget->hostPublicUdpPort = static_cast<uint16_t>(publicPort);
  }

  for (const auto& object : json_array_objects(response, "candidates")) {
    ConnectCandidate candidate{};
    uint32_t candidatePort = 0;
    if (!json_get_string(object, "ip", &candidate.ip) || candidate.ip.empty()) continue;
    if (!json_get_u32(object, "port", &candidatePort) || candidatePort == 0) continue;
    candidate.port = static_cast<uint16_t>(candidatePort);
    std::string kind;
    json_get_string(object, "kind", &kind);
    // An unknown kind is kept rather than dropped: the server may learn new ones, and a
    // candidate we cannot label is still a candidate we can punch.
    if (!candidate_kind_from_name(kind, &candidate.kind)) {
      candidate.kind = CandidateKind::Public;
    }
    outTarget->candidates.push_back(std::move(candidate));
  }

  // A host that predates the candidate list still answers with the single address, and dialling
  // it is exactly what the old client did.
  if (outTarget->candidates.empty() && !outTarget->hostPublicIp.empty() &&
      outTarget->hostPublicUdpPort != 0) {
    ConnectCandidate fallback{};
    fallback.ip = outTarget->hostPublicIp;
    fallback.port = outTarget->hostPublicUdpPort;
    fallback.kind = CandidateKind::Public;
    outTarget->candidates.push_back(std::move(fallback));
  }

  if (outTarget->punchToken.empty()) {
    if (outError) *outError = "server did not return a capability";
    return false;
  }
  return true;
}

}  // namespace remote60::native_poc
