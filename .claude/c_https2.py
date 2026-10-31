import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert old in d, '%s pair %d' % (path, i)
        assert d.count(old) == 1, '%s pair %d not unique' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

CR = chr(13) + chr(10)

# ============================================ the transport takes headers verbatim, like the
# raw path does, so the two carry the same things
edit('apps/native_poc/src/winhttp_transport.hpp', [
 (''' * `authToken` is sent as a bearer header when non-empty. `body` is sent when non-empty, with the
 * caller's content type.''',
  ''' * `extraHeaders` is appended verbatim and must already be CRLF terminated -- the same contract
 * the raw-socket path has, so a caller can be moved between them without its headers changing
 * shape. `body` is sent when non-empty, with the caller's content type.'''),
 ('''bool http_exchange(const std::string& host, uint16_t port, bool secure, const char* method,
                   const std::string& path, const std::string& authToken,
                   const std::string& body, const char* contentType, uint32_t timeoutMs,
                   HttpResult* out);''',
  '''bool http_exchange(const std::string& host, uint16_t port, bool secure, const char* method,
                   const std::string& path, const std::string& extraHeaders,
                   const std::string& body, const char* contentType, uint32_t timeoutMs,
                   HttpResult* out);'''),
])

edit('apps/native_poc/src/winhttp_transport.cpp', [
 ('''bool http_exchange(const std::string& host, uint16_t port, bool secure, const char* method,
                   const std::string& path, const std::string& authToken,
                   const std::string& body, const char* contentType, uint32_t timeoutMs,
                   HttpResult* out) {''',
  '''bool http_exchange(const std::string& host, uint16_t port, bool secure, const char* method,
                   const std::string& path, const std::string& extraHeaders,
                   const std::string& body, const char* contentType, uint32_t timeoutMs,
                   HttpResult* out) {'''),
 ('''  std::wstring headers;
  if (!body.empty() && contentType) {
    headers += L"Content-Type: ";
    headers += widen(contentType);
    headers += L"\\r\\n";
  }
  if (!authToken.empty()) {
    headers += L"Authorization: Bearer ";
    headers += widen(authToken);
    headers += L"\\r\\n";
  }''',
  '''  std::wstring headers;
  if (!body.empty() && contentType) {
    headers += L"Content-Type: ";
    headers += widen(contentType);
    headers += L"\\r\\n";
  }
  headers += widen(extraHeaders);'''),
])

# ============================================================ directory_client: dispatch on secure
edit('apps/native_poc/src/directory_client.cpp', [
 ('#include "poc_protocol.hpp"',
  '#include "poc_protocol.hpp"\n#include "winhttp_transport.hpp"'),
 ('#include <cstdio>', '#include <cstdio>\n#include <cstring>'),
 ('''bool http_post(const std::string& httpHost_, uint16_t httpPort_, const std::string& path,
               const std::string& contentType, const std::string& extraHeaders,
               const std::string& body, uint32_t* outStatus, std::string* outResponse) {
  sockaddr_in addr{};''',
  '''bool http_post(const std::string& httpHost_, uint16_t httpPort_, bool secure,
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
  sockaddr_in addr{};'''),
])

# ================================================================= session client: same dispatch
edit('apps/native_poc/src/directory_session_client.cpp', [
 ('''bool http_request(const std::string& host, uint16_t port, const char* method,
                  const std::string& path, const std::string& bearer, const std::string& body,
                  uint32_t* outStatus, std::string* outResponse) {
  sockaddr_in addr{};''',
  '''bool http_request(const std::string& host, uint16_t port, bool secure, const char* method,
                  const std::string& path, const std::string& bearer, const std::string& body,
                  uint32_t* outStatus, std::string* outResponse) {
  if (secure) {
    // Same split as the host agent's: TLS on WinHTTP, cleartext on the socket that already
    // works. The bearer header is written here in the shape the raw path writes it, so the two
    // send the same request rather than two requests that resemble each other.
    net::HttpResult result;
    const std::string headers =
        bearer.empty() ? std::string() : "Authorization: Bearer " + bearer + "''' + CR + '''";
    const bool ok = net::http_exchange(host, port, true, method, path, headers, body,
                                       "application/json", kHttpTimeoutMs, &result);
    if (outStatus) *outStatus = result.status;
    if (outResponse) *outResponse = result.body;
    return ok;
  }
  sockaddr_in addr{};'''),
 ('''bool split_url(const std::string& url, std::string* host, uint16_t* port, std::string* error) {
  return directory::parse_directory_url(url, host, port, error);
}''',
  '''bool split_url(const std::string& url, std::string* host, uint16_t* port, bool* secure,
               std::string* error) {
  return directory::parse_directory_url(url, host, port, error, secure);
}'''),
 ('''  std::string host;
  uint16_t port = 0;
  if (!split_url(url, &host, &port, outError)) return false;

  uint32_t status = 0;
  std::string response;
  if (!http_request(host, port, "GET", "/healthz", {}, {}, &status, &response)) {''',
  '''  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!split_url(url, &host, &port, &secure, outError)) return false;

  uint32_t status = 0;
  std::string response;
  if (!http_request(host, port, secure, "GET", "/healthz", {}, {}, &status, &response)) {'''),
 ('''  std::string host;
  uint16_t port = 0;
  if (!split_url(url, &host, &port, outError)) return false;

  std::ostringstream body;
  body << "{\\"id\\":\\"" << json_escape(accountId) << "\\",\\"pw\\":\\"" << json_escape(password) << "\\"}";

  uint32_t status = 0;
  std::string response;
  if (!http_request(host, port, "POST", "/api/login", {}, body.str(), &status, &response)) {''',
  '''  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!split_url(url, &host, &port, &secure, outError)) return false;

  std::ostringstream body;
  body << "{\\"id\\":\\"" << json_escape(accountId) << "\\",\\"pw\\":\\"" << json_escape(password) << "\\"}";

  uint32_t status = 0;
  std::string response;
  if (!http_request(host, port, secure, "POST", "/api/login", {}, body.str(), &status, &response)) {'''),
])
