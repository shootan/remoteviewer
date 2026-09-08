#include "update_http.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <vector>

namespace remote60::native_poc::update {
namespace {

std::string lower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(::tolower(c)); });
  return out;
}

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(need), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), need);
  return out;
}

/** Closes whatever it was given, so no early return leaks a WinHTTP handle. */
struct WinHttpHandle {
  HINTERNET h = nullptr;
  ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
};

void set_error(std::string* error, const std::string& text) {
  if (error) *error = text;
}

}  // namespace

const char* fetch_status_name(FetchStatus s) {
  switch (s) {
    case FetchStatus::Ok: return "Ok";
    case FetchStatus::BadUrl: return "BadUrl";
    case FetchStatus::ConnectFailed: return "ConnectFailed";
    case FetchStatus::HttpError: return "HttpError";
    case FetchStatus::TooLarge: return "TooLarge";
    case FetchStatus::WriteFailed: return "WriteFailed";
  }
  return "?";
}

bool parse_https_url(const std::string& url, HttpsUrl* out, std::string* error) {
  if (!out) return false;
  const std::string trimmed = url;
  const std::string lowered = lower(trimmed);

  if (lowered.rfind("http://", 0) == 0) {
    // Named separately from "unsupported scheme" because this is the mistake most likely to be
    // made on purpose by someone testing against a local server, and it must not be quietly
    // tolerated: the artifact behind this URL runs with administrator rights.
    set_error(error, "http:// is refused; the update path requires https");
    return false;
  }
  if (lowered.rfind("https://", 0) != 0) {
    set_error(error, "only https:// URLs are accepted");
    return false;
  }

  std::string rest = trimmed.substr(8);
  if (rest.empty()) {
    set_error(error, "no host");
    return false;
  }
  // Credentials in the URL are refused rather than parsed. They have no business in an update
  // endpoint and they are a classic way to make a URL read as one host while resolving another.
  const size_t at = rest.find('@');
  const size_t firstSlash = rest.find('/');
  if (at != std::string::npos && (firstSlash == std::string::npos || at < firstSlash)) {
    set_error(error, "credentials in the URL are not accepted");
    return false;
  }

  std::string hostPort = rest;
  std::string path = "/";
  if (firstSlash != std::string::npos) {
    hostPort = rest.substr(0, firstSlash);
    path = rest.substr(firstSlash);
  }
  if (hostPort.empty()) {
    set_error(error, "no host");
    return false;
  }

  uint16_t port = 443;
  const size_t colon = hostPort.rfind(':');
  if (colon != std::string::npos) {
    const std::string portText = hostPort.substr(colon + 1);
    if (portText.empty() || portText.find_first_not_of("0123456789") != std::string::npos) {
      set_error(error, "port is not a number");
      return false;
    }
    const unsigned long value = std::strtoul(portText.c_str(), nullptr, 10);
    if (value == 0 || value > 65535) {
      set_error(error, "port out of range");
      return false;
    }
    port = static_cast<uint16_t>(value);
    hostPort = hostPort.substr(0, colon);
  }
  if (hostPort.empty()) {
    set_error(error, "no host");
    return false;
  }

  out->host = widen(hostPort);
  out->port = port;
  out->path = widen(path);
  return true;
}

bool redirect_is_allowed(const std::string& from, const std::string& to, std::string* error) {
  HttpsUrl parsedFrom;
  if (!parse_https_url(from, &parsedFrom, error)) return false;
  HttpsUrl parsedTo;
  std::string toError;
  if (!parse_https_url(to, &parsedTo, &toError)) {
    // The message matters here: "redirected to cleartext" is a different event from "redirected
    // somewhere unparseable", and only the first one is an attack shape worth naming.
    set_error(error, "redirect target refused: " + toError);
    return false;
  }
  return true;
}

namespace {

/**
 * Opens a session, connects, and sends the request. Shared by both fetch entry points so the TLS
 * posture is written once and cannot drift between them.
 */
FetchStatus open_request(const std::string& url, WinHttpHandle* session, WinHttpHandle* connect,
                         WinHttpHandle* request, std::string* error) {
  HttpsUrl parsed;
  if (!parse_https_url(url, &parsed, error)) return FetchStatus::BadUrl;

  session->h = WinHttpOpen(L"GNLinkUpdater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session->h) {
    set_error(error, "WinHttpOpen failed");
    return FetchStatus::ConnectFailed;
  }

  // TLS 1.2 and above. Older protocols are not merely discouraged here; an update artifact is
  // executed with administrator rights and there is no reason to negotiate down for it.
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
  protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
  WinHttpSetOption(session->h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

  // Redirects are followed only while they stay on https. WinHttp's DISALLOW_HTTPS_TO_HTTP policy
  // is the same rule redirect_is_allowed() states, enforced by the stack itself.
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(session->h, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                   sizeof(redirectPolicy));

  // NOTE: WINHTTP_OPTION_SECURITY_FLAGS is deliberately never set. Setting it is how certificate
  // validation gets turned off, and there is no configuration of this client that does so.

  connect->h = WinHttpConnect(session->h, parsed.host.c_str(), parsed.port, 0);
  if (!connect->h) {
    set_error(error, "WinHttpConnect failed");
    return FetchStatus::ConnectFailed;
  }

  request->h = WinHttpOpenRequest(connect->h, L"GET", parsed.path.c_str(), nullptr,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE);
  if (!request->h) {
    set_error(error, "WinHttpOpenRequest failed");
    return FetchStatus::ConnectFailed;
  }

  if (!WinHttpSendRequest(request->h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                          0, 0)) {
    const DWORD err = GetLastError();
    // ERROR_WINHTTP_SECURE_FAILURE is the one worth naming: it means the certificate did not
    // check out, and the request stopped there rather than continuing.
    set_error(error, err == ERROR_WINHTTP_SECURE_FAILURE
                         ? "TLS certificate validation failed"
                         : "send failed (" + std::to_string(err) + ")");
    return FetchStatus::ConnectFailed;
  }
  if (!WinHttpReceiveResponse(request->h, nullptr)) {
    set_error(error, "no response (" + std::to_string(GetLastError()) + ")");
    return FetchStatus::ConnectFailed;
  }

  DWORD statusCode = 0;
  DWORD size = sizeof(statusCode);
  if (!WinHttpQueryHeaders(request->h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size,
                           WINHTTP_NO_HEADER_INDEX)) {
    set_error(error, "could not read the status code");
    return FetchStatus::HttpError;
  }
  if (statusCode != 200) {
    set_error(error, "HTTP " + std::to_string(statusCode));
    return FetchStatus::HttpError;
  }
  return FetchStatus::Ok;
}

}  // namespace

FetchStatus https_get_text(const std::string& url, size_t maxBytes, std::string* out,
                           std::string* error) {
  if (!out) return FetchStatus::BadUrl;
  out->clear();

  WinHttpHandle session, connect, request;
  const FetchStatus opened = open_request(url, &session, &connect, &request, error);
  if (opened != FetchStatus::Ok) return opened;

  std::vector<char> buffer(16 * 1024);
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
      set_error(error, "read failed");
      out->clear();
      return FetchStatus::ConnectFailed;
    }
    if (read == 0) break;
    if (out->size() + read > maxBytes) {
      // Aborted, not truncated. A truncated manifest would fail its signature check anyway, and
      // stopping here says why instead of leaving a confusing signature failure.
      set_error(error, "response exceeded " + std::to_string(maxBytes) + " bytes");
      out->clear();
      return FetchStatus::TooLarge;
    }
    out->append(buffer.data(), read);
  }
  return FetchStatus::Ok;
}

FetchStatus https_get_file(const std::string& url, const std::wstring& destPath, uint64_t maxBytes,
                           std::string* error) {
  WinHttpHandle session, connect, request;
  const FetchStatus opened = open_request(url, &session, &connect, &request, error);
  if (opened != FetchStatus::Ok) return opened;

  HANDLE file = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    set_error(error, "could not create the destination file");
    return FetchStatus::WriteFailed;
  }

  FetchStatus result = FetchStatus::Ok;
  uint64_t total = 0;
  std::vector<char> buffer(64 * 1024);
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
      set_error(error, "read failed");
      result = FetchStatus::ConnectFailed;
      break;
    }
    if (read == 0) break;
    total += read;
    if (total > maxBytes) {
      set_error(error, "artifact exceeded " + std::to_string(maxBytes) + " bytes");
      result = FetchStatus::TooLarge;
      break;
    }
    DWORD written = 0;
    if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
      set_error(error, "write failed");
      result = FetchStatus::WriteFailed;
      break;
    }
  }
  CloseHandle(file);

  if (result != FetchStatus::Ok) {
    // Nothing downstream may find a partial file and take it for a finished download.
    DeleteFileW(destPath.c_str());
  }
  return result;
}

}  // namespace remote60::native_poc::update
