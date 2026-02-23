#include "common/ws_rtc_signaling_client.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>

namespace remote60::common {
namespace {

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(needed, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), needed);
  return out;
}

std::string narrow(const wchar_t* ws) {
  if (!ws) return {};
  const int len = static_cast<int>(wcslen(ws));
  const int needed = WideCharToMultiByte(CP_UTF8, 0, ws, len, nullptr, 0, nullptr, nullptr);
  std::string out(needed, '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws, len, out.data(), needed, nullptr, nullptr);
  return out;
}

std::string find_json_string(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const size_t k = json.find(needle);
  if (k == std::string::npos) return {};
  const size_t colon = json.find(':', k + needle.size());
  if (colon == std::string::npos) return {};
  const size_t q1 = json.find('"', colon + 1);
  if (q1 == std::string::npos) return {};

  std::string out;
  out.reserve(256);
  bool esc = false;
  for (size_t i = q1 + 1; i < json.size(); ++i) {
    const char c = json[i];
    if (esc) {
      switch (c) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '\\': out.push_back('\\'); break;
        case '"': out.push_back('"'); break;
        default: out.push_back(c); break;
      }
      esc = false;
      continue;
    }
    if (c == '\\') {
      esc = true;
      continue;
    }
    if (c == '"') return out;
    out.push_back(c);
  }
  return {};
}

std::optional<int> find_json_int(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const size_t k = json.find(needle);
  if (k == std::string::npos) return std::nullopt;
  const size_t colon = json.find(':', k + needle.size());
  if (colon == std::string::npos) return std::nullopt;

  size_t p = colon + 1;
  while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) ++p;
  if (p >= json.size()) return std::nullopt;

  size_t e = p;
  if (json[e] == '-') ++e;
  while (e < json.size() && json[e] >= '0' && json[e] <= '9') ++e;
  if (e == p || (json[p] == '-' && e == p + 1)) return std::nullopt;

  try {
    return std::stoi(json.substr(p, e - p));
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace

WsRtcSignalingClient::WsRtcSignalingClient() = default;

WsRtcSignalingClient::~WsRtcSignalingClient() { disconnect(); }

bool WsRtcSignalingClient::parse_ws_url(const std::string& url, std::wstring* host, INTERNET_PORT* port,
                                        std::wstring* path, bool* secure) {
  constexpr char kPrefixWs[] = "ws://";
  constexpr char kPrefixWss[] = "wss://";

  std::string rest;
  if (url.rfind(kPrefixWs, 0) == 0) {
    *secure = false;
    rest = url.substr(sizeof(kPrefixWs) - 1);
  } else if (url.rfind(kPrefixWss, 0) == 0) {
    *secure = true;
    rest = url.substr(sizeof(kPrefixWss) - 1);
  } else {
    return false;
  }

  std::string hostport = rest;
  std::string raw_path = "/";
  const size_t slash = rest.find('/');
  if (slash != std::string::npos) {
    hostport = rest.substr(0, slash);
    raw_path = rest.substr(slash);
  }

  std::string host_str = hostport;
  *port = *secure ? 443 : 80;
  const size_t colon = hostport.rfind(':');
  if (colon != std::string::npos) {
    host_str = hostport.substr(0, colon);
    *port = static_cast<INTERNET_PORT>(std::stoi(hostport.substr(colon + 1)));
  }

  *host = widen(host_str);
  *path = widen(raw_path);
  return !host->empty();
}

bool WsRtcSignalingClient::connect(const std::string& wsUrl) {
  disconnect();

  std::wstring host;
  INTERNET_PORT port = 80;
  std::wstring path;
  bool secure = false;
  if (!parse_ws_url(wsUrl, &host, &port, &path, &secure)) {
    if (on_state_) on_state_("connect_failed:invalid_ws_url");
    return false;
  }

  session_ = WinHttpOpen(L"remote60/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                         WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session_) return false;

  connection_ = WinHttpConnect(session_, host.c_str(), port, 0);
  if (!connection_) return false;

  const DWORD openFlags = secure ? WINHTTP_FLAG_SECURE : 0;
  request_ = WinHttpOpenRequest(connection_, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, openFlags);
  if (!request_) return false;

  if (secure) {
    if (const char* insecure = std::getenv("REMOTE60_SIGNALING_TLS_INSECURE")) {
      if (std::string(insecure) == "1") {
        DWORD secFlags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(request_, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
      }
    }
  }

  if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) return false;
  if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return false;
  if (!WinHttpReceiveResponse(request_, nullptr)) return false;

  websocket_ = WinHttpWebSocketCompleteUpgrade(request_, 0);
  if (!websocket_) return false;

  WinHttpCloseHandle(request_);
  request_ = nullptr;

  running_ = true;
  disconnected_notified_ = false;
  recv_thread_ = std::thread([this]() { recv_loop(); });
  if (on_state_) on_state_("connected:ws");
  return true;
}

void WsRtcSignalingClient::disconnect() {
  running_ = false;

  if (websocket_) {
    WinHttpWebSocketClose(websocket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
  }

  if (recv_thread_.joinable()) recv_thread_.join();

  if (websocket_) {
    WinHttpCloseHandle(websocket_);
    websocket_ = nullptr;
  }
  if (request_) {
    WinHttpCloseHandle(request_);
    request_ = nullptr;
  }
  if (connection_) {
    WinHttpCloseHandle(connection_);
    connection_ = nullptr;
  }
  if (session_) {
    WinHttpCloseHandle(session_);
    session_ = nullptr;
  }

  if (!disconnected_notified_.exchange(true)) {
    if (on_state_) on_state_("disconnected:ws");
  }
}

bool WsRtcSignalingClient::register_role(const std::string& role) {
  if (!signaling::is_valid_role(role)) return false;
  std::string json = "{\"type\":\"register\",\"role\":\"" + json_escape(role) + "\"";
  if (const char* token = std::getenv("REMOTE60_SIGNALING_TOKEN")) {
    const std::string authToken(token);
    if (!authToken.empty()) json += ",\"authToken\":\"" + json_escape(authToken) + "\"";
  }
  json += "}";
  return send_raw_json(json);
}

bool WsRtcSignalingClient::send_raw_json(const std::string& json) {
  if (!websocket_) return false;
  std::lock_guard<std::mutex> lk(send_mu_);
  const auto status = WinHttpWebSocketSend(websocket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                           reinterpret_cast<PVOID>(const_cast<char*>(json.data())),
                                           static_cast<DWORD>(json.size()));
  return status == NO_ERROR;
}

bool WsRtcSignalingClient::send_message(const signaling::Message& message) {
  std::string json = "{\"type\":\"" + std::string(signaling::to_string(message.type)) + "\"";
  if (!message.role.empty()) json += ",\"role\":\"" + json_escape(message.role) + "\"";
  if (!message.sdp.empty()) json += ",\"sdp\":\"" + json_escape(message.sdp) + "\"";
  if (!message.candidate.empty()) json += ",\"candidate\":\"" + json_escape(message.candidate) + "\"";
  if (!message.candidateMid.empty()) json += ",\"candidateMid\":\"" + json_escape(message.candidateMid) + "\"";
  if (message.candidateMLineIndex.has_value()) json += ",\"candidateMLineIndex\":" + std::to_string(*message.candidateMLineIndex);
  if (!message.requestId.empty()) json += ",\"requestId\":\"" + json_escape(message.requestId) + "\"";
  json += "}";
  return send_raw_json(json);
}

void WsRtcSignalingClient::set_on_message(OnMessage cb) { on_message_ = std::move(cb); }

void WsRtcSignalingClient::set_on_state(OnState cb) { on_state_ = std::move(cb); }

void WsRtcSignalingClient::recv_loop() {
  std::array<char, 8192> buffer{};
  std::string assembled;
  while (running_) {
    DWORD bytes_read = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE;
    const DWORD status = WinHttpWebSocketReceive(websocket_, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, &type);
    if (status != NO_ERROR) break;

    if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;

    if ((type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
         type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) && bytes_read > 0) {
      assembled.append(buffer.data(), buffer.data() + bytes_read);
      if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
        if (on_message_) on_message_(parse_message(assembled));
        assembled.clear();
      }
    }
  }

  running_ = false;
  if (!disconnected_notified_.exchange(true)) {
    if (on_state_) on_state_("disconnected:ws");
  }
}

signaling::Message WsRtcSignalingClient::parse_message(const std::string& json) const {
  signaling::Message m;
  m.type = signaling::message_type_from_string(find_json_string(json, "type"));
  m.role = find_json_string(json, "role");
  m.from = find_json_string(json, "from");
  m.sdp = find_json_string(json, "sdp");
  m.candidate = find_json_string(json, "candidate");
  m.candidateMid = find_json_string(json, "candidateMid");
  m.candidateMLineIndex = find_json_int(json, "candidateMLineIndex");
  m.reason = find_json_string(json, "reason");
  m.code = find_json_string(json, "code");
  m.requestId = find_json_string(json, "requestId");
  m.session = find_json_string(json, "session");

  const auto pos = json.find("\"peerOnline\"");
  if (pos != std::string::npos) {
    const auto t = json.find("true", pos);
    const auto f = json.find("false", pos);
    if (t != std::string::npos && (f == std::string::npos || t < f)) m.peerOnline = true;
    if (f != std::string::npos && (t == std::string::npos || f < t)) m.peerOnline = false;
  }

  return m;
}

std::string WsRtcSignalingClient::json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

}  // namespace remote60::common
