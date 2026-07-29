#include "directory_client.hpp"

#include <windows.h>

#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#include "json_profile.hpp"
#include "poc_protocol.hpp"

#pragma comment(lib, "shell32.lib")

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
bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError) {
  std::string rest = trim(url);
  if (rest.rfind("https://", 0) == 0) {
    if (outError) {
      *outError =
          "https is not supported by the host yet; terminate TLS in front of the directory "
          "service or use http:// on a trusted network";
    }
    return false;
  }
  if (rest.rfind("http://", 0) == 0) {
    rest = rest.substr(7);
  } else if (rest.find("://") != std::string::npos) {
    if (outError) *outError = "unsupported url scheme";
    return false;
  }
  const size_t slash = rest.find('/');
  if (slash != std::string::npos) rest = rest.substr(0, slash);
  uint16_t port = 80;
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

  if (!parse_directory_url(cfg_.url, &httpHost_, &httpPort_, outError)) return false;

  const uint16_t observePort = cfg_.observeUdpPort ? cfg_.observeUdpPort
                                                   : static_cast<uint16_t>(httpPort_ + 1);
  if (!resolve_ipv4(httpHost_, observePort, &observeAddr_)) {
    if (outError) *outError = "cannot resolve directory host '" + httpHost_ + "'";
    return false;
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

  // A punch packet from a peer only exists to open our NAT mapping; it carries no state.
  if (len >= sizeof(UdpHelloPacket)) {
    const auto* hello = reinterpret_cast<const UdpHelloPacket*>(bytes);
    if (hello->magic == kMagic && hello->kind == static_cast<uint16_t>(UdpPacketKind::Punch)) {
      return true;
    }
  }

  const bool fromDirectory = from.sin_addr.s_addr == observeAddr_.sin_addr.s_addr &&
                             from.sin_port == observeAddr_.sin_port;
  if (!fromDirectory || bytes[0] != '{') return false;

  const std::string text(reinterpret_cast<const char*>(bytes), len);
  std::string ip;
  uint32_t port = 0;
  if (!json_get_string(text, "ip", &ip) || !json_get_u32(text, "port", &port) || port == 0) {
    return true;  // it came from the directory, so do not hand it back to the media protocol
  }
  std::lock_guard<std::mutex> lock(mu_);
  observedIp_ = ip;
  observedPort_ = static_cast<uint16_t>(port);
  observedReady_ = true;
  return true;
}

namespace {

bool post_json(const std::string& httpHost_, uint16_t httpPort_, const std::string& path,
               const std::string& body, uint32_t* outStatus, std::string* outResponse) {
  sockaddr_in addr{};
  if (!resolve_ipv4(httpHost_, httpPort_, &addr)) return false;
  SOCKET s = connect_with_timeout(addr, kHttpTimeoutMs);
  if (s == INVALID_SOCKET) return false;

  std::ostringstream req;
  req << "POST " << path << " HTTP/1.1\r\n"
      << "Host: " << httpHost_ << ":" << httpPort_ << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << body.size() << "\r\n"
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
  for (;;) {
    const int n = recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;
    raw.append(buf, static_cast<size_t>(n));
    if (raw.size() > 64 * 1024) break;
  }
  closesocket(s);

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

}  // namespace

bool HostAgent::HttpPostJson(const std::string& path, const std::string& body, uint32_t* outStatus,
                             std::string* outResponse) {
  return post_json(httpHost_, httpPort_, path, body, outStatus, outResponse);
}

bool register_host(const std::string& url, const std::string& accountId,
                   const std::string& password, const std::string& hostName,
                   const std::string& machineId, std::string* outHostId,
                   std::string* outHostToken, std::string* outError) {
  std::string host;
  uint16_t port = 0;
  if (!parse_directory_url(url, &host, &port, outError)) return false;

  std::ostringstream body;
  body << "{\"id\":\"" << json_escape(accountId) << "\","
       << "\"pw\":\"" << json_escape(password) << "\","
       << "\"hostName\":\"" << json_escape(hostName) << "\","
       << "\"machineId\":\"" << json_escape(machineId) << "\"}";

  uint32_t status = 0;
  std::string resp;
  if (!post_json(host, port, "/api/host/register", body.str(), &status, &resp)) {
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
  return true;
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
  if (cached.directoryUrl != cfg_.url || cached.machineId != machineId_) return false;
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
  if (!register_host(cfg_.url, cfg_.accountId, cfg_.password, cfg_.hostName, machineId_, &id,
                     &token, &error)) {
    SetStatus(error);
    return false;
  }
  hostToken_ = token;
  hostId_ = id;
  SaveCache();
  std::cout << "[native-video-host] directory registered hostId=" << hostId_
            << " name=" << cfg_.hostName << "\n";
  return true;
}

bool HostAgent::RefreshObservedAddress() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    observedReady_ = false;
  }
  const std::string probe = "OBSERVE " + observeToken_;
  for (int attempt = 0; attempt < kObserveAttempts && running_.load(); ++attempt) {
    send_(probe.data(), probe.size(), observeAddr_);
    std::this_thread::sleep_for(std::chrono::milliseconds(kObserveWaitMs));
    std::lock_guard<std::mutex> lock(mu_);
    if (observedReady_) return true;
  }
  return false;
}

bool HostAgent::Heartbeat(std::vector<PunchTarget>* outPunch) {
  std::ostringstream body;
  body << "{\"hostToken\":\"" << json_escape(hostToken_) << "\","
       << "\"hostName\":\"" << json_escape(cfg_.hostName) << "\","
       << "\"observeToken\":\"" << json_escape(observeToken_) << "\"}";

  uint32_t status = 0;
  std::string resp;
  if (!HttpPostJson("/api/host/heartbeat", body.str(), &status, &resp)) {
    SetStatus("directory unreachable");
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
        if (json_get_string(entry, "ip", &ip) && json_get_u32(entry, "port", &port) && port &&
            port <= 65535) {
          in_addr parsed{};
          if (inet_pton(AF_INET, ip.c_str(), &parsed) == 1) {
            outPunch->push_back({parsed.s_addr, static_cast<uint16_t>(port)});
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

void HostAgent::Run() {
  bool announcedOnline = false;
  while (running_.load()) {
    const auto cycleStart = std::chrono::steady_clock::now();

    if (EnsureRegistered()) {
      // The observation must precede the heartbeat: the heartbeat is what publishes the
      // address, and it publishes whatever the observation last recorded.
      if (!RefreshObservedAddress()) {
        SetStatus("address observation timed out");
      } else {
        std::vector<PunchTarget> punch;
        if (Heartbeat(&punch)) {
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
    while (running_.load() && remaining.count() > 0) {
      const auto slice = std::min<std::chrono::milliseconds>(remaining,
                                                             std::chrono::milliseconds(200));
      std::this_thread::sleep_for(slice);
      remaining -= slice;
    }
  }
}

}  // namespace remote60::native_poc::directory
