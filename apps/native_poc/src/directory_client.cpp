#include "directory_client.hpp"
#include "host_diag_log.hpp"

#include <windows.h>

#include <iphlpapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

#include "connect_candidates.hpp"
#include "json_profile.hpp"
#include "poc_protocol.hpp"
#include "update_endpoint.hpp"
#include "url_origin.hpp"
#include "winhttp_transport.hpp"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace remote60::native_poc::directory {
namespace {

using json_profile::json_get_string;
using json_profile::json_get_u32;

constexpr int kHttpTimeoutMs = 6000;
constexpr int kObserveAttempts = 6;
constexpr int kObserveWaitMs = 250;
// A punch has to keep going long enough for the other side to start its own, but not so long
// that it delays the next heartbeat. The client is told to punch for five seconds too.
constexpr int kPunchPackets = 25;
constexpr int kPunchIntervalMs = 200;

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
  return s.substr(b, e - b);
}

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

std::string random_token(size_t bytes) {
  std::random_device rd;
  std::string out;
  out.reserve(bytes * 2);
  static const char* kHex = "0123456789abcdef";
  for (size_t i = 0; i < bytes; ++i) {
    const unsigned v = rd() & 0xFFu;
    out.push_back(kHex[v >> 4]);
    out.push_back(kHex[v & 0xF]);
  }
  return out;
}

}  // namespace

/** http://host[:port][/...] -> host, port. Anything else is refused with a reason. */
namespace {

/**
 * The text of a nested JSON object, by key. Empty when it is not there.
 *
 * Enough for one flat object inside another, which is all this metadata is. Written rather than
 * reaching for the flat getters because those find the FIRST key of that name anywhere in the
 * document -- so a top-level "port" belonging to something else would be read as the observe
 * port. It happens to be unambiguous today; it would not stay that way, and the failure would be
 * a wrong port rather than an error.
 */
std::string json_object_field(const std::string& text, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  size_t at = text.find(needle);
  if (at == std::string::npos) return {};
  at = text.find(':', at + needle.size());
  if (at == std::string::npos) return {};
  while (at < text.size() && (text[at] == ':' || isspace(static_cast<unsigned char>(text[at])))) {
    ++at;
  }
  if (at >= text.size() || text[at] != '{') return {};
  int depth = 0;
  for (size_t i = at; i < text.size(); ++i) {
    if (text[i] == '{') ++depth;
    if (text[i] == '}') {
      --depth;
      if (depth == 0) return text.substr(at, i - at + 1);
    }
  }
  return {};
}

}  // namespace

bool observe_host_is_usable(const std::string& host) {
  if (host.empty() || host.size() > 253) return false;
  bool labelHasChar = false;
  for (size_t i = 0; i < host.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(host[i]);
    if (c == '.') {
      if (!labelHasChar) return false;  // empty label: "a..b", ".a", trailing dot
      labelHasChar = false;
      continue;
    }
    // Letters, digits and '-' only. Everything a mistake tends to carry -- "://", ":1234",
    // "/path", spaces, control characters -- lands here and is refused.
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-';
    if (!ok) return false;
    if (!labelHasChar && c == '-') return false;  // a label may not start with '-'
    labelHasChar = true;
  }
  if (!labelHasChar) return false;  // ended on a dot
  if (host.back() == '-') return false;
  return true;
}

namespace {

/**
 * Reads an integer field, refusing anything that only looks like one.
 *
 * The shared getter matches `[0-9]+`, so `"port": 29181.5` yields 29181 -- the fraction is simply
 * not seen. This server rejects such a value before sending it; another server, or an older one,
 * is not bound by that. A client that trusts the server to validate is a client that breaks the
 * moment it meets a different server.
 */
bool json_get_exact_u32(const std::string& text, const std::string& key, uint32_t* out) {
  const std::string needle = "\"" + key + "\"";
  size_t at = text.find(needle);
  if (at == std::string::npos) return false;
  at = text.find(':', at + needle.size());
  if (at == std::string::npos) return false;
  ++at;
  while (at < text.size() && isspace(static_cast<unsigned char>(text[at]))) ++at;
  const size_t begin = at;
  while (at < text.size() && isdigit(static_cast<unsigned char>(text[at]))) ++at;
  if (at == begin) return false;  // no digits: null, a string, a negative
  // What follows has to end the value. A '.' or another digit-ish character means this was never
  // the integer it appeared to be.
  if (at < text.size()) {
    const char after = text[at];
    if (after != ',' && after != '}' && !isspace(static_cast<unsigned char>(after))) return false;
  }
  const unsigned long long v = std::strtoull(text.substr(begin, at - begin).c_str(), nullptr, 10);
  if (v > 0xFFFFFFFFull) return false;
  if (out) *out = static_cast<uint32_t>(v);
  return true;
}

}  // namespace

bool parse_observe_metadata(const std::string& json, ObserveEndpoint* out) {
  if (!out) return false;
  *out = ObserveEndpoint{};
  const std::string object = json_object_field(json, "observe");
  if (object.empty()) return false;

  uint32_t port = 0;
  if (!json_get_exact_u32(object, "port", &port)) return false;
  if (port < 1 || port > 65535) return false;  // absent, not clamped

  out->known = true;
  out->port = static_cast<uint16_t>(port);

  std::string host;
  if (json_get_string(object, "host", &host)) {
    // Trimmed first, because whitespace-only is the same thing as absent.
    while (!host.empty() && isspace(static_cast<unsigned char>(host.front()))) {
      host.erase(host.begin());
    }
    while (!host.empty() && isspace(static_cast<unsigned char>(host.back()))) host.pop_back();
    if (!host.empty()) {
      if (observe_host_is_usable(host)) {
        out->host = host;
      } else {
        // The port is still good; only the host is not. Recorded so the fallback is visible.
        out->hostRejected = true;
      }
    }
  }
  return true;
}

uint16_t observe_port_for(const ObserveEndpoint& advertised, uint16_t httpPort, bool secure) {
  if (advertised.known) return advertised.port;
  if (secure) return 0;  // 443 + 1 is not a fallback; see the header
  if (httpPort >= 65535) return 0;
  return static_cast<uint16_t>(httpPort + 1);
}

namespace {

/**
 * How far into `url` a scheme reaches, or 0 when that scheme is not the one there.
 *
 * One comparison for both schemes, because they were not the same before: https was matched
 * without case and http with it, inside the same function. `HTTP://host` therefore matched
 * neither branch and came out as "unsupported url scheme" -- a url the user typed correctly,
 * refused for its capitals.
 */
size_t scheme_end(const std::string& url, const char* scheme) {
  size_t at = 0;
  while (at < url.size() && isspace(static_cast<unsigned char>(url[at]))) ++at;
  const size_t n = std::strlen(scheme);
  if (url.size() - at < n) return 0;
  for (size_t i = 0; i < n; ++i) {
    if (tolower(static_cast<unsigned char>(url[at + i])) != scheme[i]) return 0;
  }
  return at + n;
}

}  // namespace

bool directory_url_is_secure(const std::string& url) {
  return scheme_end(url, "https://") != 0;
}

std::string directory_origin_key(const std::string& url) {
  // One implementation, in url_origin.cpp, because the updater tree needs the same answer and
  // does not link this file. Two copies would disagree about a trailing slash or a written-out
  // default port on the day it mattered.
  return url_origin_key(url);
}

std::string directory_update_manifest_url(const std::string& directoryUrl,
                                          const std::string& platform) {
  if (directoryUrl.empty() || platform.empty()) return {};
  // http is refused rather than upgraded or followed. See the header.
  if (!directory_url_is_secure(directoryUrl)) return {};
  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!parse_directory_url(directoryUrl, &host, &port, nullptr, &secure) || !secure) return {};
  return directory_origin_key(directoryUrl) + "/api/update/manifest?platform=" + platform;
}

std::string update_manifest_url_for(const std::string& override_, const std::string& directoryUrl,
                                    const std::string& platform) {
  if (!override_.empty()) return override_;
  return directory_update_manifest_url(directoryUrl, platform);
}

update::UpdateEndpoint update_endpoint_for(const std::string& override_,
                                          const std::string& directoryUrl,
                                          const std::string& platform,
                                          const std::string& credentialHeader,
                                          const std::string& ownerKey, uint64_t ownerEpoch) {
  update::UpdateEndpoint endpoint;
  endpoint.ownerKey = ownerKey;
  endpoint.ownerEpoch = ownerEpoch;
  if (!override_.empty()) {
    // An operator's own url. No credential, whatever it points at -- including our own host.
    // What decides is where the url came from, and an override did not come from here.
    endpoint.url = override_;
    endpoint.derived = false;
    return endpoint;
  }
  endpoint.url = directory_update_manifest_url(directoryUrl, platform);
  if (endpoint.url.empty()) return endpoint;  // an http directory, or none at all
  endpoint.derived = true;
  endpoint.credentialHeader = credentialHeader;
  endpoint.origin = directory_origin_key(directoryUrl);
  return endpoint;
}

bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError, bool* outSecure) {
  std::string rest = trim(url);
  // Decided here and handed back, so nobody downstream forms a second opinion about it. The
  // answer picks the default port below and the transport above; those two must be the same
  // answer or the url means one thing to the parser and another to whoever dials.
  const bool secure = scheme_end(rest, "https://") != 0;
  if (outSecure) *outSecure = secure;
  if (secure) {
    rest = rest.substr(scheme_end(rest, "https://"));
  } else if (const size_t after = scheme_end(rest, "http://")) {
    rest = rest.substr(after);
  } else if (rest.find("://") != std::string::npos) {
    if (outError) *outError = "unsupported url scheme";
    return false;
  }
  const size_t slash = rest.find('/');
  if (slash != std::string::npos) rest = rest.substr(0, slash);
  uint16_t port = secure ? 443 : 80;
  const size_t colon = rest.rfind(':');
  if (colon != std::string::npos) {
    const std::string portText = rest.substr(colon + 1);
    if (!portText.empty() && portText.find_first_not_of("0123456789") == std::string::npos) {
      const unsigned long v = std::strtoul(portText.c_str(), nullptr, 10);
      if (v == 0 || v > 65535) {
        if (outError) *outError = "port out of range";
        return false;
      }
      port = static_cast<uint16_t>(v);
      rest = rest.substr(0, colon);
    }
  }
  if (rest.empty()) {
    if (outError) *outError = "missing host";
    return false;
  }
  *outHost = rest;
  *outPort = port;
  return true;
}

namespace {

bool resolve_ipv4(const std::string& host, uint16_t port, sockaddr_in* out) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  char portText[16];
  std::snprintf(portText, sizeof(portText), "%u", static_cast<unsigned>(port));
  if (getaddrinfo(host.c_str(), portText, &hints, &res) != 0 || !res) return false;
  *out = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
  freeaddrinfo(res);
  return true;
}

/** Blocking connect with a bound wait, so an unreachable server cannot wedge the agent. */
SOCKET connect_with_timeout(const sockaddr_in& addr, int timeoutMs) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return INVALID_SOCKET;
  u_long nonBlocking = 1;
  (void)ioctlsocket(s, FIONBIO, &nonBlocking);
  const int rc = connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
  if (rc != 0) {
    if (WSAGetLastError() != WSAEWOULDBLOCK) {
      closesocket(s);
      return INVALID_SOCKET;
    }
    fd_set wr;
    FD_ZERO(&wr);
    FD_SET(s, &wr);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (select(0, nullptr, &wr, nullptr, &tv) <= 0) {
      closesocket(s);
      return INVALID_SOCKET;
    }
    int err = 0;
    int errLen = sizeof(err);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &errLen) != 0 || err != 0) {
      closesocket(s);
      return INVALID_SOCKET;
    }
  }
  nonBlocking = 0;
  (void)ioctlsocket(s, FIONBIO, &nonBlocking);
  (void)setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs),
                   sizeof(timeoutMs));
  (void)setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs),
                   sizeof(timeoutMs));
  return s;
}

}  // namespace

std::string default_host_cache_path() {
  PWSTR wide = nullptr;
  std::string base;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &wide)) && wide) {
    const int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (need > 1) {
      base.resize(static_cast<size_t>(need - 1));
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, base.data(), need, nullptr, nullptr);
    }
  }
  if (wide) CoTaskMemFree(wide);
  if (base.empty()) {
    const char* env = std::getenv("LOCALAPPDATA");
    base = env ? env : ".";
  }
  return base + "\\remote60\\host.json";
}

std::string machine_id() {
  // MachineGuid survives reinstalls of our own software, which is exactly the property we
  // need: re-registering must update the existing host entry, not add another one.
  char buf[128] = {};
  DWORD size = sizeof(buf);
  if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", "MachineGuid",
                   RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) == ERROR_SUCCESS &&
      buf[0] != '\0') {
    return std::string(buf);
  }
  char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD nameLen = sizeof(name);
  if (GetComputerNameA(name, &nameLen) && name[0] != '\0') return std::string("name-") + name;
  return "unknown-machine";
}

HostAgent::~HostAgent() { Stop(); }

bool HostAgent::Start(const HostAgentConfig& cfg, SendFn send, std::string* outError) {
  if (running_.load()) return true;
  if (!send) {
    if (outError) *outError = "no send function";
    return false;
  }
  cfg_ = cfg;
  send_ = std::move(send);
  if (cfg_.cachePath.empty()) cfg_.cachePath = default_host_cache_path();
  if (cfg_.hostName.empty()) {
    char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD nameLen = sizeof(name);
    cfg_.hostName = (GetComputerNameA(name, &nameLen) && name[0]) ? name : "PC";
  }
  if (cfg_.heartbeatSeconds < 5) cfg_.heartbeatSeconds = 5;

  // Straight out of the parse. It decides what an absent advertisement means and whether the
  // socket is a TLS one, and asking a second time is how those come to differ.
  if (!parse_directory_url(cfg_.url, &httpHost_, &httpPort_, outError, &httpSecure_)) return false;

  // Aimed as far as it can be aimed before talking to anyone. A configured port wins outright; a
  // plain-http URL still has its documented default. An https URL has neither until the server
  // answers, and that is not a failure to start -- registration comes first on every cycle, and
  // ApplyObserveEndpoint() aims the socket once the answer is in.
  const uint16_t startupPort =
      cfg_.observeUdpPort ? cfg_.observeUdpPort
                          : observe_port_for(ObserveEndpoint{}, httpPort_, httpSecure_);
  observeAddrReady_ = false;
  if (startupPort != 0) {
    if (!resolve_ipv4(httpHost_, startupPort, &observeAddr_)) {
      if (outError) *outError = "cannot resolve directory host '" + httpHost_ + "'";
      return false;
    }
    observeAddrReady_ = true;
  }

  machineId_ = machine_id();
  observeToken_ = random_token(16);
  LoadCache();

  if (hostToken_.empty() && cfg_.password.empty()) {
    if (outError) {
      *outError = "no cached host token; supply --directory-pw once to register this machine";
    }
    return false;
  }

  refreshRequested_.store(false, std::memory_order_release);
  running_.store(true);
  thread_ = std::thread([this] { Run(); });
  return true;
}

void HostAgent::Stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void HostAgent::SetStatus(const std::string& status) {
  std::lock_guard<std::mutex> lock(mu_);
  status_ = status;
}

std::string HostAgent::StatusLine() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::ostringstream os;
  os << status_;
  if (observedReady_) os << " public=" << observedIp_ << ":" << observedPort_;
  return os.str();
}

bool HostAgent::ConsumeUdpPacket(const void* data, size_t len, const sockaddr_in& from) {
  if (!data || len == 0) return false;
  const auto* bytes = static_cast<const uint8_t*>(data);

  // /api/connect queues the peer's one-time capability for the next heartbeat before the
  // controller starts sending these packets. Treat the first punch as an interrupt for the
  // ordinary heartbeat sleep; otherwise a request just after a poll can wait 25 seconds and
  // its authenticated Hello arrives before the host has learned the capability.
  if (len >= sizeof(UdpHelloPacket)) {
    const auto* hello = reinterpret_cast<const UdpHelloPacket*>(bytes);
    if (hello->magic == kMagic && hello->kind == static_cast<uint16_t>(UdpPacketKind::Punch)) {
      const bool wasSet = refreshRequested_.exchange(true, std::memory_order_acq_rel);
      if (!wasSet) {
        std::cout << "[native-video-host] directory peer punch; refreshing capability\n";
      }
      // pc2-connect-diag: every punch, including the ones the line above hides.
      //
      // That line fires only on false->true, so a punch arriving while a refresh was
      // already pending left no trace -- and "the wake never arrived" and "the wake arrived
      // while the flag was already up" were indistinguishable. They are different faults.
      // The previous value of the flag is the whole point of this line.
      LogPunchArrival(from, wasSet);
      return true;
    }
  }

  sockaddr_in observeAddress{};
  { std::lock_guard<std::mutex> lock(mu_); observeAddress = observeAddr_; }
  const bool fromDirectory = from.sin_addr.s_addr == observeAddress.sin_addr.s_addr &&
                             from.sin_port == observeAddress.sin_port;
  if (!fromDirectory || bytes[0] != '{') return false;

  const std::string text(reinterpret_cast<const char*>(bytes), len);
  std::string ip;
  uint32_t port = 0;
  // Checked in full before anything is stored. The loose reader used here before took the digits
  // in front of a '.' (so 29181.5 became 29181) and had no upper bound at all -- only `port == 0`
  // was refused -- so 65537 survived the cast to uint16_t as **1**. That is not a value anything
  // rejects downstream: it is a plausible port, published as this host's public one, and a host
  // nobody can reach looks exactly like a network fault. A refusal here leaves the previous
  // observation in place and the next probe replaces it.
  if (!json_get_string(text, "ip", &ip) || ip.empty() ||
      !json_get_exact_u32(text, "port", &port) || port == 0 || port > 65535) {
    return true;  // it came from the directory, so do not hand it back to the media protocol
  }
  std::lock_guard<std::mutex> lock(mu_);
  observedIp_ = ip;
  observedPort_ = static_cast<uint16_t>(port);
  // Say so once when NAT rewrote the port. Choosing a bind port that restrictive firewalls
  // allow only helps if the mapping keeps it, and when it does not the symptom on the client
  // side is silence, which reads like a broken host rather than a blocked port.
  if (cfg_.localUdpPort != 0 && observedPort_ != cfg_.localUdpPort && !portRewriteReported_) {
    portRewriteReported_ = true;
    std::cout << "[native-video-host] directory nat-port-rewritten local=" << cfg_.localUdpPort
              << " public=" << observedPort_
              << "; restrictive networks may still be unable to reach this host\n";
  }
  observedReady_ = true;
  return true;
}

/**
 * A GET, on the same two transports and by the same rule as http_post: TLS through WinHTTP, plain
 * http on the socket that already works.
 *
 * Here rather than borrowed from the session client because ten targets link this file and do not
 * link that one -- and a host that needs to ask the directory a question should not drag the
 * viewer's session code in behind it.
 */
bool http_get(const std::string& host, uint16_t port, bool secure, const std::string& path,
              uint32_t* outStatus, std::string* outResponse) {
  if (secure) {
    net::HttpResult result;
    const bool ok = net::http_exchange(host, port, true, "GET", path, std::string(), std::string(),
                                       nullptr, kHttpTimeoutMs, &result);
    if (outStatus) *outStatus = result.status;
    if (outResponse) *outResponse = result.body;
    return ok;
  }

  sockaddr_in addr{};
  if (!resolve_ipv4(host, port, &addr)) return false;
  SOCKET s = connect_with_timeout(addr, kHttpTimeoutMs);
  if (s == INVALID_SOCKET) return false;

  std::ostringstream req;
  req << "GET " << path << " HTTP/1.1\r\n"
      << "Host: " << host << ":" << port << "\r\n"
      << "Connection: close\r\n\r\n";
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
  char buf[2048];
  bool tooLarge = false;
  for (;;) {
    const int n = recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;
    raw.append(buf, static_cast<size_t>(n));
    if (raw.size() > net::kMaxHttpResponseBytes) {
      tooLarge = true;
      break;
    }
  }
  closesocket(s);
  if (tooLarge) return false;

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

bool observe_endpoint_from_health(const std::string& url, ObserveEndpoint* out,
                                  std::string* outError) {
  if (!out) return false;
  *out = ObserveEndpoint{};
  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!parse_directory_url(url, &host, &port, outError, &secure)) return false;

  uint32_t status = 0;
  std::string response;
  if (!http_get(host, port, secure, "/healthz", &status, &response)) {
    if (outError) *outError = "cannot reach the server";
    return false;
  }
  if (status != 200) {
    if (outError) *outError = "the server answered " + std::to_string(status);
    return false;
  }
  // Absence is not an error here: an older directory says nothing and observe_port_for has a rule
  // for that. The caller distinguishes the two by whether outError was set.
  if (outError) outError->clear();
  return parse_observe_metadata(response, out);
}

bool http_post(const std::string& httpHost_, uint16_t httpPort_, bool secure,
               const std::string& path, const std::string& contentType,
               const std::string& extraHeaders, const std::string& body, uint32_t* outStatus,
               std::string* outResponse) {
  if (secure) {
    // TLS goes through WinHTTP: certificate validation, chain building and revocation are not
    // things to hand-roll on top of a socket. Plain http stays on the socket below -- deployments
    // reached that way keep working byte for byte, and nothing about them changes today.
    net::HttpResult result;
    const bool ok = net::http_exchange(httpHost_, httpPort_, true, "POST", path, extraHeaders,
                                       body, contentType.c_str(), kHttpTimeoutMs, &result);
    if (outStatus) *outStatus = result.status;
    if (outResponse) *outResponse = result.body;
    return ok;
  }
  sockaddr_in addr{};
  if (!resolve_ipv4(httpHost_, httpPort_, &addr)) return false;
  SOCKET s = connect_with_timeout(addr, kHttpTimeoutMs);
  if (s == INVALID_SOCKET) return false;

  std::ostringstream req;
  req << "POST " << path << " HTTP/1.1\r\n"
      << "Host: " << httpHost_ << ":" << httpPort_ << "\r\n"
      << "Content-Type: " << contentType << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << extraHeaders
      << "Connection: close\r\n\r\n"
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

  // Connection: close lets us read to EOF instead of parsing chunked bodies.
  std::string raw;
  char buf[2048];
  bool tooLarge = false;
  for (;;) {
    const int n = recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;
    raw.append(buf, static_cast<size_t>(n));
    if (raw.size() > net::kMaxHttpResponseBytes) {
      // Not truncated and returned: the caller cannot tell a cut-off body from a real one, and
      // the limit here used to be a different number from the other two transports'.
      tooLarge = true;
      break;
    }
  }
  closesocket(s);
  if (tooLarge) return false;

  if (raw.rfind("HTTP/", 0) != 0) return false;
  const size_t statusStart = raw.find(' ');
  if (statusStart == std::string::npos) return false;
  if (outStatus) *outStatus = static_cast<uint32_t>(std::strtoul(raw.c_str() + statusStart + 1, nullptr, 10));
  const size_t bodyStart = raw.find("\r\n\r\n");
  if (outResponse) {
    *outResponse = bodyStart == std::string::npos ? std::string() : raw.substr(bodyStart + 4);
  }
  return true;
}

namespace {

/** The json flavour every directory call uses. */
bool post_json(const std::string& httpHost_, uint16_t httpPort_, bool secure,
               const std::string& path, const std::string& body, uint32_t* outStatus,
               std::string* outResponse) {
  return http_post(httpHost_, httpPort_, secure, path, "application/json", std::string(), body,
                   outStatus, outResponse);
}

}  // namespace

bool HostAgent::HttpPostJson(const std::string& path, const std::string& body, uint32_t* outStatus,
                             std::string* outResponse) {
  return post_json(httpHost_, httpPort_, httpSecure_, path, body, outStatus, outResponse);
}

bool register_host(const std::string& url, const std::string& accountId,
                   const std::string& password, const std::string& hostName,
                   const std::string& machineId, std::string* outHostId,
                   std::string* outHostToken, std::string* outError,
                   ObserveEndpoint* outObserve) {
  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!parse_directory_url(url, &host, &port, outError, &secure)) return false;

  std::ostringstream body;
  body << "{\"id\":\"" << json_escape(accountId) << "\","
       << "\"pw\":\"" << json_escape(password) << "\","
       << "\"hostName\":\"" << json_escape(hostName) << "\","
       << "\"machineId\":\"" << json_escape(machineId) << "\"}";

  uint32_t status = 0;
  std::string resp;
  if (!post_json(host, port, secure, "/api/host/register", body.str(), &status, &resp)) {
    if (outError) *outError = "cannot reach the server";
    return false;
  }
  if (status == 401 || status == 403) {
    // The server will not say which of the two was wrong, and neither should we.
    if (outError) *outError = "id or password is not correct";
    return false;
  }
  if (status == 429) {
    if (outError) *outError = "too many attempts; wait a moment";
    return false;
  }
  if (status != 200) {
    if (outError) *outError = "server rejected the registration (http " + std::to_string(status) + ")";
    return false;
  }
  std::string token;
  if (!json_get_string(resp, "hostToken", &token) || token.empty()) {
    if (outError) *outError = "server response was malformed";
    return false;
  }
  if (outHostToken) *outHostToken = token;
  if (outHostId) json_get_string(resp, "hostId", outHostId);
  // Optional, and absence is not an error: an older server does not send it and the caller has a
  // rule for that.
  if (outObserve) parse_observe_metadata(resp, outObserve);
  return true;
}

bool create_account(const std::string& url, const std::string& accountId,
                    const std::string& password, const std::string& signupKey,
                    std::string* outError) {
  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!parse_directory_url(url, &host, &port, outError, &secure)) return false;

  std::ostringstream body;
  body << "{\"id\":\"" << json_escape(accountId) << "\","
       << "\"pw\":\"" << json_escape(password) << "\","
       << "\"signupKey\":\"" << json_escape(signupKey) << "\"}";

  uint32_t status = 0;
  std::string resp;
  if (!post_json(host, port, secure, "/api/signup", body.str(), &status, &resp)) {
    if (outError) *outError = "cannot reach the server";
    return false;
  }
  if (status == 200) return true;

  // The server's message is the useful one here: it says which rule was broken.
  std::string serverError;
  json_get_string(resp, "error", &serverError);
  if (outError) {
    *outError = serverError.empty()
                    ? "could not create the account (http " + std::to_string(status) + ")"
                    : serverError;
  }
  return false;
}

bool load_host_cache(const std::string& path, HostCache* out) {
  if (!out) return false;
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) return false;
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  json_get_string(text, "directoryUrl", &out->directoryUrl);
  json_get_string(text, "accountId", &out->accountId);
  json_get_string(text, "machineId", &out->machineId);
  json_get_string(text, "hostName", &out->hostName);
  json_get_string(text, "hostId", &out->hostId);
  json_get_string(text, "hostToken", &out->hostToken);
  return !out->hostToken.empty();
}

bool save_host_cache(const std::string& path, const HostCache& cache) {
  const size_t slash = path.find_last_of("\\/");
  if (slash != std::string::npos) {
    (void)CreateDirectoryA(path.substr(0, slash).c_str(), nullptr);
  }
  std::ostringstream os;
  os << "{\n"
     << "  \"directoryUrl\": \"" << json_escape(cache.directoryUrl) << "\",\n"
     << "  \"accountId\": \"" << json_escape(cache.accountId) << "\",\n"
     << "  \"machineId\": \"" << json_escape(cache.machineId) << "\",\n"
     << "  \"hostName\": \"" << json_escape(cache.hostName) << "\",\n"
     << "  \"hostId\": \"" << json_escape(cache.hostId) << "\",\n"
     << "  \"hostToken\": \"" << json_escape(cache.hostToken) << "\"\n"
     << "}\n";
  const std::string tmp = path + ".tmp";
  {
    std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << os.str();
  }
  return MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool HostAgent::LoadCache() {
  HostCache cached;
  if (!load_host_cache(cfg_.cachePath, &cached)) return false;

  // A token is only meaningful for the account, server and machine it was issued against.
  // Compared by origin, so a trailing slash or a written-out default port does not read as a
  // different server -- and so http and https still do.
  if (directory_origin_key(cached.directoryUrl) != directory_origin_key(cfg_.url) ||
      cached.machineId != machineId_) {
    return false;
  }
  if (!cfg_.accountId.empty() && cached.accountId != cfg_.accountId) return false;

  hostToken_ = cached.hostToken;
  hostId_ = cached.hostId;
  if (cfg_.accountId.empty()) cfg_.accountId = cached.accountId;
  return true;
}

void HostAgent::SaveCache() const {
  HostCache cache;
  cache.directoryUrl = cfg_.url;
  cache.accountId = cfg_.accountId;
  cache.machineId = machineId_;
  cache.hostName = cfg_.hostName;
  cache.hostId = hostId_;
  cache.hostToken = hostToken_;
  (void)save_host_cache(cfg_.cachePath, cache);
}

bool HostAgent::EnsureRegistered() {
  if (!hostToken_.empty()) return true;
  if (cfg_.accountId.empty() || cfg_.password.empty()) {
    SetStatus("registration needs id/pw");
    return false;
  }
  std::string token, id, error;
  ObserveEndpoint advertised;
  if (!register_host(cfg_.url, cfg_.accountId, cfg_.password, cfg_.hostName, machineId_, &id,
                     &token, &error, &advertised)) {
    SetStatus(error);
    return false;
  }
  hostToken_ = token;
  hostId_ = id;
  observeAdvertised_ = advertised;
  if (!ApplyObserveEndpoint()) return false;
  SaveCache();
  std::cout << "[native-video-host] directory registered hostId=" << hostId_
            << " name=" << cfg_.hostName << "\n";
  return true;
}

bool HostAgent::FetchObserveEndpointFromHealth() {
  // Only when there is nothing else to go on. A configured port is the operator's decision and an
  // advertisement already in hand does not need refreshing.
  if (cfg_.observeUdpPort != 0 || observeAdvertised_.known) return true;

  // Bounded, and deliberately not a poll. A directory that has not been given an observe port may
  // be given one later, so asking again is right; asking forever would turn a server-side
  // omission into a machine that talks to it every twenty-five seconds until someone notices.
  constexpr int kMaxFetchAttempts = 6;
  if (observeFetchCooldown_ > 0) {
    --observeFetchCooldown_;
    return false;
  }
  observeFetchAttempts_ = (std::min)(observeFetchAttempts_ + 1, kMaxFetchAttempts);

  ObserveEndpoint advertised;
  std::string error;
  if (!observe_endpoint_from_health(cfg_.url, &advertised, &error)) {
    // Absent is not an error -- an older directory says nothing, and observe_port_for has a rule
    // for that. Unreachable is an error, and the two read differently to whoever is looking.
    SetStatus(error.empty()
                  ? "this directory does not say where to send address observations"
                  : "could not ask the directory where observations go: " + error);
    // Cycles of the loop below, not seconds: 2, 4, 6 ... at the default 25 s heartbeat interval.
    observeFetchCooldown_ = observeFetchAttempts_ * 2;
    return false;
  }
  observeAdvertised_ = advertised;
  observeFetchAttempts_ = 0;
  observeFetchCooldown_ = 0;
  return ApplyObserveEndpoint();
}

bool HostAgent::ApplyObserveEndpoint() {
  // A configured port is the operator's decision and outranks anything the server says.
  const uint16_t port = cfg_.observeUdpPort
                            ? cfg_.observeUdpPort
                            : observe_port_for(observeAdvertised_, httpPort_, httpSecure_);
  if (port == 0) {
    // Only reachable on https with a server that says nothing. Said plainly, because the machine
    // will otherwise sit there looking like a network problem: the observation times out, the
    // heartbeat is skipped, and the host simply never appears.
    SetStatus("this directory has not told us where to send address observations; the server "
              "needs REMOTE60_DIR_OBSERVE_PORT set (or use an http url)");
    observeAddrReady_ = false;
    return false;
  }
  // An advertised host is used when given; otherwise the directory's own hostname, which is the
  // ordinary case and the documented default.
  if (observeAdvertised_.hostRejected) {
    // Not fatal -- the directory's own hostname is the documented default and it is used below.
    // Said out loud because the value came from server configuration and nobody will find the
    // mistake by watching a timeout.
    SetStatus("the directory advertised an observe host that is not a usable name; using the "
              "directory host instead");
  }
  const std::string& target = observeAdvertised_.host.empty() ? httpHost_ : observeAdvertised_.host;
  sockaddr_in resolvedAddress{};
  if (!resolve_ipv4(target, port, &resolvedAddress)) {
    SetStatus("cannot resolve the observe host '" + target + "'");
    observeAddrReady_ = false;
    return false;
  }
  { std::lock_guard<std::mutex> lock(mu_); observeAddr_ = resolvedAddress; }
  observeAddrReady_ = true;
  return true;
}

bool HostAgent::RefreshObservedAddress() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    observedReady_ = false;
  }
  const std::string probe = "OBSERVE " + observeToken_;
  for (int attempt = 0; attempt < kObserveAttempts && running_.load(); ++attempt) {
    if (!observeAddrReady_) return false;  // nowhere to send it; the status already says why
    sockaddr_in address{};
    { std::lock_guard<std::mutex> lock(mu_); address = observeAddr_; }
    send_(probe.data(), probe.size(), address);
    std::this_thread::sleep_for(std::chrono::milliseconds(kObserveWaitMs));
    std::lock_guard<std::mutex> lock(mu_);
    if (observedReady_) return true;
  }
  return false;
}

// Every IPv4 address this machine holds that could plausibly reach a peer. A client on the same
// network reaches one of these without leaving the LAN, which is both faster and free -- and it
// is the only route that works at all when the router will not hairpin.
std::vector<std::string> local_ipv4_addresses() {
  std::vector<std::string> out;
  ULONG size = 16 * 1024;
  std::vector<uint8_t> buffer(size);
  auto* table = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
  ULONG result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                                   GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr, table, &size);
  if (result == ERROR_BUFFER_OVERFLOW) {
    buffer.assign(size, 0);
    table = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                               GAA_FLAG_SKIP_DNS_SERVER,
                                  nullptr, table, &size);
  }
  if (result != NO_ERROR) return out;

  for (auto* adapter = table; adapter; adapter = adapter->Next) {
    // A down adapter's address is stale; punching it burns part of the connect budget.
    if (adapter->OperStatus != IfOperStatusUp) continue;
    if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
      if (!unicast->Address.lpSockaddr) continue;
      if (unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
      const auto* addr = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
      char text[INET_ADDRSTRLEN] = {};
      if (!inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text))) continue;
      const std::string ip = text;
      if (!is_usable_local_ipv4(ip)) continue;
      if (std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
    }
  }
  return out;
}

bool HostAgent::Heartbeat(std::vector<PunchTarget>* outPunch) {
  uint32_t status = 0;
  std::string serverError;
  if (HeartbeatAttempt(outPunch, &status, &serverError)) return true;

  // 409 means the directory has no usable observation for this host. It is not an authentication
  // problem -- the token is fine, and treating it as one would sign the host out and make it
  // re-register, which fixes nothing and loses the cached token. The observation is something
  // this end can produce: send one from the socket the host streams on and try again.
  if (status != 409) return false;  // the attempt already said what went wrong

  SetStatus(serverError == "observation_expired"
                ? "the directory's address observation expired; sending a new one"
                : "the directory has no address observation yet; sending one");
  observedReady_ = false;
  if (!RefreshObservedAddress()) {
    // Nothing more to try this cycle. The heartbeat loop's ordinary wait is the backoff, and no
    // stream in progress is touched: the socket was only used to send a probe on.
    SetStatus("could not reach the directory's observe port; retrying on the next heartbeat");
    return false;
  }

  // Exactly one retry. A loop here would spin against a server that is refusing for a reason
  // this end cannot fix, and the caller already comes back in 25 seconds.
  status = 0;
  serverError.clear();
  if (HeartbeatAttempt(outPunch, &status, &serverError)) return true;
  if (status == 409) {
    SetStatus("the directory did not accept the address observation (" +
              (serverError.empty() ? std::string("no reason given") : serverError) +
              "); retrying on the next heartbeat");
  }
  return false;
}

bool HostAgent::HeartbeatAttempt(std::vector<PunchTarget>* outPunch, uint32_t* outStatus,
                                 std::string* outServerError) {
  std::ostringstream body;
  body << "{\"hostToken\":\"" << json_escape(hostToken_) << "\","
       << "\"hostName\":\"" << json_escape(cfg_.hostName) << "\","
       << "\"observeToken\":\"" << json_escape(observeToken_) << "\"";

  // Tell the directory where else this host can be reached. The observed public address is the
  // server's to determine -- it sees the mapping NAT actually made -- but the private addresses
  // and the second port are only knowable here.
  if (cfg_.localUdpPort != 0) {
    body << ",\"localUdpPort\":" << cfg_.localUdpPort;
  }
  if (cfg_.alternateUdpPort != 0 && cfg_.alternateUdpPort != cfg_.localUdpPort) {
    body << ",\"alternateUdpPort\":" << cfg_.alternateUdpPort;
  }
  const std::vector<std::string> localIps = local_ipv4_addresses();
  if (!localIps.empty()) {
    body << ",\"localIps\":[";
    for (size_t i = 0; i < localIps.size(); ++i) {
      if (i) body << ",";
      body << "\"" << json_escape(localIps[i]) << "\"";
    }
    body << "]";
  }
  body << "}";

  // Say once what this host is offering as reachable addresses.
  //
  // Without it, "the client never tried the LAN route" and "the host never advertised one" look
  // identical from here, and the only place the difference showed was a log on the phone that
  // Android will not let anyone copy off the device. This line moves that answer onto the PC,
  // where it can actually be read: a private address here means the offer was made, so anything
  // still failing is the network between the two, not a missing candidate.
  //
  // Once, not per heartbeat -- it repeats every 25 seconds and the answer does not change.
  if (!announcedCandidates_) {
    announcedCandidates_ = true;
    std::cout << "[directory] advertising localPort=" << cfg_.localUdpPort
              << " altPort=" << cfg_.alternateUdpPort << " localIps=";
    if (localIps.empty()) {
      std::cout << "(none)";
    } else {
      for (size_t i = 0; i < localIps.size(); ++i) {
        if (i) std::cout << ",";
        std::cout << localIps[i];
      }
    }
    std::cout << "\n";
  }

  uint32_t status = 0;
  std::string resp;
  if (!HttpPostJson("/api/host/heartbeat", body.str(), &status, &resp)) {
    SetStatus("directory unreachable");
    return false;
  }
  if (outStatus) *outStatus = status;
  if (outServerError) {
    outServerError->clear();
    json_get_string(resp, "error", outServerError);
  }
  if (status == 409) {
    // Left to Heartbeat(): this is a state the client can repair, and the status it deserves
    // depends on whether repairing it worked.
    return false;
  }
  if (status == 401) {
    // The server forgot us (restored from an older store, or the token was revoked).
    // Drop the cached token so the next pass re-registers if we still hold a password.
    hostToken_.clear();
    hostId_.clear();
    SetStatus("host token rejected; re-registering");
    return false;
  }
  if (status != 200) {
    SetStatus("heartbeat failed (http " + std::to_string(status) + ")");
    return false;
  }

  if (outPunch) {
    outPunch->clear();
    const size_t arrayStart = resp.find("\"pendingPunch\"");
    if (arrayStart != std::string::npos) {
      size_t cursor = resp.find('[', arrayStart);
      const size_t arrayEnd = cursor == std::string::npos ? std::string::npos : resp.find(']', cursor);
      while (cursor != std::string::npos && arrayEnd != std::string::npos) {
        const size_t objStart = resp.find('{', cursor);
        if (objStart == std::string::npos || objStart > arrayEnd) break;
        const size_t objEnd = resp.find('}', objStart);
        if (objEnd == std::string::npos || objEnd > arrayEnd) break;
        const std::string entry = resp.substr(objStart, objEnd - objStart + 1);
        std::string ip;
        uint32_t port = 0;
        std::string punchToken;
        if (json_get_string(entry, "ip", &ip) && json_get_u32(entry, "port", &port) &&
            json_get_string(entry, "punchToken", &punchToken) && punchToken.size() == 32 && port &&
            port <= 65535) {
          in_addr parsed{};
          if (inet_pton(AF_INET, ip.c_str(), &parsed) == 1) {
            outPunch->push_back({parsed.s_addr, static_cast<uint16_t>(port), punchToken});
          }
        }
        cursor = objEnd + 1;
      }
    }
  }
  return true;
}

void HostAgent::Punch(const std::vector<PunchTarget>& targets) {
  if (targets.empty()) return;
  for (const auto& t : targets) {
    in_addr shown{};
    shown.s_addr = t.ipv4NetworkOrder;
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &shown, text, sizeof(text));
    std::cout << "[native-video-host] directory punch -> " << text << ":" << t.port << "\n";
  }
  SetStatus("punching");

  {
    std::lock_guard<std::mutex> lock(mu_);
    const auto now = std::chrono::steady_clock::now();
    authorizedPeers_.erase(
        std::remove_if(authorizedPeers_.begin(), authorizedPeers_.end(),
                       [&](const AuthorizedPeer& peer) { return peer.expiresAt <= now; }),
        authorizedPeers_.end());
    for (const auto& target : targets) {
      authorizedPeers_.push_back(
          AuthorizedPeer{target, now + std::chrono::seconds(30)});
    }
  }

  UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
  for (int i = 0; i < kPunchPackets && running_.load(); ++i) {
    for (const auto& t : targets) {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_addr.s_addr = t.ipv4NetworkOrder;
      to.sin_port = htons(t.port);
      send_(&packet, sizeof(packet), to);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPunchIntervalMs));
  }
}

/**
 * One line per punch, up to a ceiling, with the suppressed ones accounted for.
 *
 * Bounded because this is driven by whatever arrives on a UDP port: at most
 * kPunchLogPerSecond lines in any second, and the next line printed says how many were
 * skipped. A running total means a burst is still countable after the fact.
 */
void HostAgent::LogPunchArrival(const sockaddr_in& from, bool refreshWasPending) {
  const uint64_t nowUs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count());
  PunchLogDecision decided;
  {
    std::lock_guard<std::mutex> lock(mu_);
    decided = punch_log_decide(&punchLogWindow_, nowUs);
  }
  if (!decided.emit) return;
  const uint64_t total = decided.total;
  const uint64_t skipped = decided.skippedSince;
  char text[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
  std::cout << "[native-video-host][dir-punch] from=" << text << ":" << ntohs(from.sin_port)
            << " refreshWasPending=" << (refreshWasPending ? 1 : 0)
            << " total=" << total;
  if (skipped) std::cout << " skippedSinceLast=" << skipped;
  std::cout << "\n";
}

bool HostAgent::AuthorizePeer(const std::string& punchToken, const sockaddr_in& from,
                              PeerAuthDiag* diag) {
  if (diag) *diag = PeerAuthDiag{};
  if (punchToken.empty()) return false;
  std::lock_guard<std::mutex> lock(mu_);
  const auto now = std::chrono::steady_clock::now();
  const size_t beforeSweep = authorizedPeers_.size();
  authorizedPeers_.erase(
      std::remove_if(authorizedPeers_.begin(), authorizedPeers_.end(),
                     [&](const AuthorizedPeer& peer) { return peer.expiresAt <= now; }),
      authorizedPeers_.end());
  if (diag) {
    diag->expiredNow = beforeSweep - authorizedPeers_.size();
    diag->held = authorizedPeers_.size();
  }
  // The directory's observed tuple is useful for opening the NAT path, but it is not an
  // authentication invariant. Hairpin and symmetric NATs may translate the same prepared
  // client socket differently when it changes destination from the directory to the host.
  // The 128-bit random capability is single-use and expires after 30 seconds; consume it by
  // value, then bind the live session to the endpoint that actually presented it.
  const auto match = std::find_if(authorizedPeers_.begin(), authorizedPeers_.end(),
                                  [&](const AuthorizedPeer& peer) {
                                    return peer.target.punchToken == punchToken;
                                  });
  if (match == authorizedPeers_.end()) return false;
  if (diag) diag->matched = true;
  if (match->target.ipv4NetworkOrder != from.sin_addr.s_addr ||
      htons(match->target.port) != from.sin_port) {
    if (diag) diag->endpointMoved = true;
    in_addr expectedAddress{};
    expectedAddress.s_addr = match->target.ipv4NetworkOrder;
    in_addr actualAddress{};
    actualAddress.s_addr = from.sin_addr.s_addr;
    char expectedIp[INET_ADDRSTRLEN] = {};
    char actualIp[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &expectedAddress, expectedIp, sizeof(expectedIp));
    inet_ntop(AF_INET, &actualAddress, actualIp, sizeof(actualIp));
    std::cout << "[native-video-host] directory capability endpoint translated expected="
              << expectedIp << ":" << match->target.port << " actual=" << actualIp << ":"
              << ntohs(from.sin_port) << "\n";
  }
  authorizedPeers_.erase(match);  // one connection, one capability
  return true;
}

void HostAgent::Run() {
  bool announcedOnline = false;
  // pc2-connect-diag: the cycle, step by step.
  //
  // A capability can only reach this host through this loop, and none of it was logged:
  // not when a cycle began, not what woke it, not how long each of the three HTTP calls
  // took, not how many capabilities came back. "The host learned about it 25 seconds
  // later" was an inference from the punch timestamps, and a cycle stalled in an HTTP
  // timeout looked exactly like one that had not started.
  uint64_t cycleNo = 0;
  const char* cycleCause = "first";
  const auto stepMs = [](std::chrono::steady_clock::time_point from) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - from).count();
  };
  while (running_.load()) {
    const auto cycleStart = std::chrono::steady_clock::now();
    ++cycleNo;
    std::cout << "[native-video-host][dir-cycle] n=" << cycleNo << " start cause="
              << cycleCause << "\n";

    if (EnsureRegistered()) {
      // A host that started from a cached token never registered, so nothing has told it where
      // observations go. Asked here rather than inside EnsureRegistered, because that function
      // returns immediately when a token is cached -- which is exactly the case that needs this.
      //
      // The trigger is "we are guessing", not "we have nowhere to aim". On https there is nowhere
      // to aim and the symptom is obvious; on http the guess is httpPort + 1, which RESOLVES and
      // then goes nowhere if the server listens elsewhere -- a working-looking aim at the wrong
      // place. Asking covers both, and the answer is only used when the server gives one.
      {
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = FetchObserveEndpointFromHealth();
        std::cout << "[native-video-host][dir-cycle] n=" << cycleNo << " step=health ok="
                  << (ok ? 1 : 0) << " ms=" << stepMs(t0) << "\n";
      }

      // The observation must precede the heartbeat: the heartbeat is what publishes the
      // address, and it publishes whatever the observation last recorded.
      const auto observeStart = std::chrono::steady_clock::now();
      const bool observeOk = RefreshObservedAddress();
      std::cout << "[native-video-host][dir-cycle] n=" << cycleNo << " step=observe ok="
                << (observeOk ? 1 : 0) << " ms=" << stepMs(observeStart) << "\n";
      if (!observeOk) {
        // Only when there is nothing better to say. The specific reason -- no advertisement, an
        // unusable host, a port that cannot be derived -- was already set by whoever found it,
        // and overwriting it with "timed out" turned every one of those into a network symptom.
        if (observeAddrReady_) SetStatus("address observation timed out");
      } else {
        std::vector<PunchTarget> punch;
        const auto hbStart = std::chrono::steady_clock::now();
        const bool hbOk = Heartbeat(&punch);
        std::cout << "[native-video-host][dir-cycle] n=" << cycleNo << " step=heartbeat ok="
                  << (hbOk ? 1 : 0) << " ms=" << stepMs(hbStart)
                  << " capabilities=" << punch.size() << "\n";
        if (hbOk) {
          SetStatus("online");
          if (!announcedOnline) {
            announcedOnline = true;
            std::cout << "[native-video-host] directory " << StatusLine() << "\n";
          }
          Punch(punch);
        } else {
          announcedOnline = false;
        }
      }
    }

    // Sleep in slices so Stop() is honoured promptly instead of after a whole heartbeat.
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - cycleStart);
    auto remaining = std::chrono::milliseconds(cfg_.heartbeatSeconds * 1000u) - elapsed;
    // The work above is what a punch has to wait out: the flag is only read here, so a
    // punch arriving during the HTTP calls is not acted on until this loop is reached.
    std::cout << "[native-video-host][dir-cycle] n=" << cycleNo << " sleep planMs="
              << remaining.count() << " workMs=" << elapsed.count() << "\n";
    const auto sleepStart = std::chrono::steady_clock::now();
    const char* wake = "elapsed";
    while (running_.load() && remaining.count() > 0) {
      if (refreshRequested_.exchange(false, std::memory_order_acq_rel)) {
        wake = "refresh";
        break;
      }
      const auto slice = std::min<std::chrono::milliseconds>(remaining,
                                                             std::chrono::milliseconds(200));
      std::this_thread::sleep_for(slice);
      remaining -= slice;
    }
    wake = cycle_wake_cause(std::string(wake) == "refresh", running_.load());
    std::cout << "[native-video-host][dir-cycle] n=" << cycleNo << " wake cause=" << wake
              << " sleptMs=" << stepMs(sleepStart) << "\n";
    cycleCause = wake;
  }
}

}  // namespace remote60::native_poc::directory
