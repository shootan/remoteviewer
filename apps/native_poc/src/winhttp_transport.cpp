#include "winhttp_transport.hpp"

#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace remote60::native_poc::net {
namespace {

/** Closes a WinHTTP handle on the way out, so no early return leaks one. */
struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
};

std::wstring widen(const std::string& text) {
  if (text.empty()) return {};
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(size <= 0 ? 0 : static_cast<size_t>(size), L'\0');
  if (size > 0) {
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
  }
  return out;
}

/**
 * The TLS posture, in one place.
 *
 * Written here rather than at each call site so the two callers cannot drift apart on it -- which
 * is the whole reason the transport is shared. Nothing in this function is conditional on who is
 * calling: an update artifact and a directory login are both things an attacker would like to sit
 * in the middle of.
 */
void apply_tls_posture(HINTERNET session) {
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
  protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
  WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

  // A redirect that leaves https is refused by the stack itself. Following one would hand the
  // next request -- carrying the session token -- to a cleartext hop.
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                   sizeof(redirectPolicy));

  // WINHTTP_OPTION_SECURITY_FLAGS is deliberately never set here, and there is no argument to
  // this file that would set it. That option is how certificate validation gets switched off, and
  // the way it ends up switched off is someone adding a parameter for it "for testing".
}

}  // namespace

bool http_exchange(const std::string& host, uint16_t port, bool secure, const char* method,
                   const std::string& path, const std::string& extraHeaders,
                   const std::string& body, const char* contentType, uint32_t timeoutMs,
                   HttpResult* out) {
  if (!out) return false;
  *out = HttpResult{};
  if (host.empty() || !method || path.empty()) {
    out->error = "malformed request";
    return false;
  }

  Handle session;
  session.h = WinHttpOpen(L"GNLink/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session.h) {
    out->error = "WinHttpOpen failed";
    return false;
  }
  if (secure) apply_tls_posture(session.h);
  WinHttpSetTimeouts(session.h, static_cast<int>(timeoutMs), static_cast<int>(timeoutMs),
                     static_cast<int>(timeoutMs), static_cast<int>(timeoutMs));

  Handle connect;
  connect.h = WinHttpConnect(session.h, widen(host).c_str(), port, 0);
  if (!connect.h) {
    out->error = "cannot reach the server";
    return false;
  }

  Handle request;
  request.h = WinHttpOpenRequest(connect.h, widen(method).c_str(), widen(path).c_str(), nullptr,
                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 secure ? WINHTTP_FLAG_SECURE : 0);
  if (!request.h) {
    out->error = "cannot open the request";
    return false;
  }

  std::wstring headers;
  if (!body.empty() && contentType) {
    headers += L"Content-Type: ";
    headers += widen(contentType);
    headers += L"\r\n";
  }
  headers += widen(extraHeaders);

  const bool sent = WinHttpSendRequest(
      request.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
      headers.empty() ? 0 : static_cast<DWORD>(-1),
      body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
      static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) != FALSE;
  if (!sent) {
    const DWORD err = GetLastError();
    // Named, because "could not connect" and "the certificate did not check out" call for
    // completely different things from whoever reads it.
    out->error = err == ERROR_WINHTTP_SECURE_FAILURE
                     ? "TLS certificate validation failed"
                     : "cannot reach the server (" + std::to_string(err) + ")";
    return false;
  }
  if (!WinHttpReceiveResponse(request.h, nullptr)) {
    out->error = "no response (" + std::to_string(GetLastError()) + ")";
    return false;
  }

  DWORD statusCode = 0;
  DWORD size = sizeof(statusCode);
  if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size,
                           WINHTTP_NO_HEADER_INDEX)) {
    out->error = "could not read the status code";
    return false;
  }
  out->status = statusCode;

  // The body is read whatever the status: the server's own error text is what the caller shows,
  // and throwing it away on a non-200 would leave "something went wrong" and nothing else.
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.h, &available) || available == 0) break;
    std::vector<char> chunk(available);
    DWORD read = 0;
    if (!WinHttpReadData(request.h, chunk.data(), available, &read) || read == 0) break;
    out->body.append(chunk.data(), read);
    // A server that never stops talking must not be allowed to grow this without limit -- and a
    // truncated body handed back as if it were the whole one is worse than no body at all.
    if (out->body.size() > kMaxHttpResponseBytes) {
      // The status goes too. A false return means the exchange did not happen as far as the
      // caller is concerned, and leaving a 200 behind it invites code that checks the status
      // without checking the return -- the socket path leaves 0 here, so this one does too.
      const std::string tooBig = "the response is larger than " +
                                 std::to_string(kMaxHttpResponseBytes) + " bytes";
      *out = HttpResult{};
      out->error = tooBig;
      return false;
    }
  }

  out->sent = true;
  return true;
}

}  // namespace remote60::native_poc::net
