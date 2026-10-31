# -*- coding: utf-8 -*-
import io

CR = chr(92) + 'r'
LF = chr(92) + 'n'

# ---------------------------------------------------------------- a GET, sharing the same dispatch
p = 'apps/native_poc/src/directory_client.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''bool http_post(const std::string& httpHost_, uint16_t httpPort_, bool secure,
               const std::string& path, const std::string& contentType,
               const std::string& extraHeaders, const std::string& body, uint32_t* outStatus,
               std::string* outResponse) {'''
new = '''/**
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
  req << "GET " << path << " HTTP/1.1''' + CR + LF + '''"
      << "Host: " << host << ":" << port << "''' + CR + LF + '''"
      << "Connection: close''' + CR + LF + CR + LF + '''";
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
  const size_t bodyStart = raw.find("''' + CR + LF + CR + LF + '''");
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
               std::string* outResponse) {'''
assert d.count(old) == 1, 'http_post head'
d = d.replace(old, new, 1)

# the agent uses the local one
old2 = '''  ObserveEndpoint advertised;
  std::string error;
  if (!directory_observe_from_health(cfg_.url, &advertised, &error)) {'''
new2 = '''  ObserveEndpoint advertised;
  std::string error;
  if (!observe_endpoint_from_health(cfg_.url, &advertised, &error)) {'''
assert d.count(old2) == 1, 'call'
d = d.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok cpp')

# ---------------------------------------------------------------- declare it
p = 'apps/native_poc/src/directory_client.hpp'
d = io.open(p, encoding='utf-8').read()
old3 = '''bool http_post(const std::string& host, uint16_t port, bool secure, const std::string& path,'''
new3 = '''/**
 * Asks a directory where address observations go, over its health route.
 *
 * Needs no session, which is the point: a host that resumed from a cached token never registered
 * and so never saw the response that carries the advertisement. Returns false when the server
 * could not be reached (`outError` says why) and also when it simply said nothing -- absence is a
 * documented state, not a failure, and the caller tells them apart by whether `outError` is set.
 */
bool observe_endpoint_from_health(const std::string& url, ObserveEndpoint* out,
                                  std::string* outError);

/** A GET over the same two transports as http_post. */
bool http_get(const std::string& host, uint16_t port, bool secure, const std::string& path,
              uint32_t* outStatus, std::string* outResponse);

bool http_post(const std::string& host, uint16_t port, bool secure, const std::string& path,'''
assert d.count(old3) == 1, 'decl'
d = d.replace(old3, new3, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok hpp')
