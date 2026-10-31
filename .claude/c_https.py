import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert old in d, '%s pair %d' % (path, i)
        assert d.count(old) == 1, '%s pair %d not unique' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# =============================================================== the parser, one scheme answer
edit('apps/native_poc/src/directory_client.hpp', [
 ('''/** Splits http://host[:port] into its parts; rejects https, which is not supported yet. */
bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError);''',
  '''/**
 * Splits http://host[:port] or https://host[:port] into its parts.
 *
 * `outSecure` is how the caller learns which one it was, and it is the only place that answer
 * should come from: whoever dials has to use the same answer the parser used to pick the default
 * port, or the two disagree about a url neither of them rejected. The scheme is compared without
 * case, which is what a scheme is -- `HTTP://` used to be read as no scheme at all.
 *
 * The default port follows the scheme: 80 for http, 443 for https.
 */
bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError, bool* outSecure = nullptr);'''),
 ('''bool http_post(const std::string& host, uint16_t port, const std::string& path,''',
  '''bool http_post(const std::string& host, uint16_t port, bool secure, const std::string& path,'''),
])

edit('apps/native_poc/src/directory_client.cpp', [
 # --- one case-insensitive scheme comparison, used by both branches
 ('''bool directory_url_is_secure(const std::string& url) {
  size_t at = 0;
  while (at < url.size() && isspace(static_cast<unsigned char>(url[at]))) ++at;
  const std::string scheme = "https://";
  if (url.size() - at < scheme.size()) return false;
  for (size_t i = 0; i < scheme.size(); ++i) {
    if (tolower(static_cast<unsigned char>(url[at + i])) != scheme[i]) return false;
  }
  return true;
}''',
  '''namespace {

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
}'''),
 ('''bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError) {
  std::string rest = trim(url);
  if (directory_url_is_secure(rest)) {
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
  uint16_t port = 80;''',
  '''bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
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
  uint16_t port = secure ? 443 : 80;'''),
 # --- HostAgent: the scheme comes back from the parse, not from a second look at the url
 ('''  // The same answer the parser uses, from the same function. It decides what an absent
  // advertisement means, and a second opinion here would be a second opinion about whether to
  // encrypt.
  httpSecure_ = directory_url_is_secure(cfg_.url);
  if (!parse_directory_url(cfg_.url, &httpHost_, &httpPort_, outError)) return false;''',
  '''  // Straight out of the parse. It decides what an absent advertisement means and whether the
  // socket is a TLS one, and asking a second time is how those come to differ.
  if (!parse_directory_url(cfg_.url, &httpHost_, &httpPort_, outError, &httpSecure_)) return false;'''),
 # --- the http helpers dispatch on it
 ('''/** The json flavour every directory call uses. */
bool post_json(const std::string& httpHost_, uint16_t httpPort_, const std::string& path,
               const std::string& body, uint32_t* outStatus, std::string* outResponse) {
  return http_post(httpHost_, httpPort_, path, "application/json", std::string(), body, outStatus,
                   outResponse);
}''',
  '''/** The json flavour every directory call uses. */
bool post_json(const std::string& httpHost_, uint16_t httpPort_, bool secure,
               const std::string& path, const std::string& body, uint32_t* outStatus,
               std::string* outResponse) {
  return http_post(httpHost_, httpPort_, secure, path, "application/json", std::string(), body,
                   outStatus, outResponse);
}'''),
 ('''bool HostAgent::HttpPostJson(const std::string& path, const std::string& body, uint32_t* outStatus,
                             std::string* outResponse) {
  return post_json(httpHost_, httpPort_, path, body, outStatus, outResponse);
}''',
  '''bool HostAgent::HttpPostJson(const std::string& path, const std::string& body, uint32_t* outStatus,
                             std::string* outResponse) {
  return post_json(httpHost_, httpPort_, httpSecure_, path, body, outStatus, outResponse);
}'''),
 ('''  std::string host;
  uint16_t port = 0;
  if (!parse_directory_url(url, &host, &port, outError)) return false;

  std::ostringstream body;
  body << "{\\"id\\":\\"" << json_escape(accountId) << "\\","
       << "\\"pw\\":\\"" << json_escape(password) << "\\","
       << "\\"hostName\\":\\"" << json_escape(hostName) << "\\","
       << "\\"machineId\\":\\"" << json_escape(machineId) << "\\"}";

  uint32_t status = 0;
  std::string resp;
  if (!post_json(host, port, "/api/host/register", body.str(), &status, &resp)) {''',
  '''  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!parse_directory_url(url, &host, &port, outError, &secure)) return false;

  std::ostringstream body;
  body << "{\\"id\\":\\"" << json_escape(accountId) << "\\","
       << "\\"pw\\":\\"" << json_escape(password) << "\\","
       << "\\"hostName\\":\\"" << json_escape(hostName) << "\\","
       << "\\"machineId\\":\\"" << json_escape(machineId) << "\\"}";

  uint32_t status = 0;
  std::string resp;
  if (!post_json(host, port, secure, "/api/host/register", body.str(), &status, &resp)) {'''),
 ('''  std::string host;
  uint16_t port = 0;
  if (!parse_directory_url(url, &host, &port, outError)) return false;

  std::ostringstream body;
  body << "{\\"id\\":\\"" << json_escape(accountId) << "\\","
       << "\\"pw\\":\\"" << json_escape(password) << "\\","
       << "\\"signupKey\\":\\"" << json_escape(signupKey) << "\\"}";

  uint32_t status = 0;
  std::string resp;
  if (!post_json(host, port, "/api/signup", body.str(), &status, &resp)) {''',
  '''  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!parse_directory_url(url, &host, &port, outError, &secure)) return false;

  std::ostringstream body;
  body << "{\\"id\\":\\"" << json_escape(accountId) << "\\","
       << "\\"pw\\":\\"" << json_escape(password) << "\\","
       << "\\"signupKey\\":\\"" << json_escape(signupKey) << "\\"}";

  uint32_t status = 0;
  std::string resp;
  if (!post_json(host, port, secure, "/api/signup", body.str(), &status, &resp)) {'''),
])
