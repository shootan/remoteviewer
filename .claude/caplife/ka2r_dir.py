base = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'

def edit(rel, pairs):
    p = base + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (rel, s.count(old), old[:80])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('directory_session_client.hpp', [
    ('''  std::string hostPublicIp;
  uint16_t hostPublicUdpPort = 0;
};''', '''  std::string hostPublicIp;
  uint16_t hostPublicUdpPort = 0;
  // keepalive-r2 (2), a standby connect only (contract §2.7): "reserved" (this connect has its own relay lease) or
  // why not ("host-full", "not-eligible", "relay-disabled", ...); empty = the server did not say (an older server:
  // no standby relay). The relay's address when one is offered (the candidate of kind "relay").
  std::string standbyRelay;
  std::string relayIp;
  uint16_t relayPort = 0;
};'''),
    ('''bool directory_connect(const std::string& url, const std::string& sessionToken,
                       const std::string& hostId, const std::string& observeToken,
                       DirectoryConnectTarget* outTarget, std::string* outError,
                       uint32_t* outStatus = nullptr);''', '''bool directory_connect(const std::string& url, const std::string& sessionToken,
                       const std::string& hostId, const std::string& observeToken,
                       DirectoryConnectTarget* outTarget, std::string* outError,
                       uint32_t* outStatus = nullptr, bool standby = false);

/**
 * keepalive-r2 (2) (contract §2.7, Codex decision 2): whether this directory supports standby, read from /api/hosts
 * (no side effect). An older server says nothing: *outSupported false. Missing, a timeout or an error is NOT support --
 * a client never learns it from a first mutating connect.
 */
bool directory_standby_support(const std::string& url, const std::string& sessionToken, bool* outSupported,
                               bool* outRelay, std::string* outError);'''),
])

edit('directory_session_client.cpp', [
    ('''bool directory_connect(const std::string& url, const std::string& sessionToken,
                       const std::string& hostId, const std::string& observeToken,
                       DirectoryConnectTarget* outTarget, std::string* outError,
                       uint32_t* outStatus) {''', '''bool directory_standby_support(const std::string& url, const std::string& sessionToken, bool* outSupported,
                               bool* outRelay, std::string* outError) {
  if (outSupported) *outSupported = false;
  if (outRelay) *outRelay = false;
  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!split_url(url, &host, &port, &secure, outError)) return false;
  uint32_t status = 0;
  std::string response;
  if (!http_request(host, port, secure, "GET", "/api/hosts", sessionToken, {}, &status, &response)) {
    if (outError) *outError = "cannot reach the server";
    return false;
  }
  if (status != 200) {
    if (outError) *outError = error_from_response(status, response);
    return false;
  }
  // {"hosts":[...],"standby":{"supported":true,"relay":true}} -- the object after the host list
  const size_t at = response.find("\\"standby\\":{");
  if (at == std::string::npos) return true;  // an older server: no standby
  const size_t end = response.find('}', at);
  const std::string object = response.substr(at, end == std::string::npos ? std::string::npos : end - at + 1);
  bool supported = false, relay = false;
  json_get_bool(object, "supported", &supported);
  json_get_bool(object, "relay", &relay);
  if (outSupported) *outSupported = supported;
  if (outRelay) *outRelay = supported && relay;
  return true;
}

bool directory_connect(const std::string& url, const std::string& sessionToken,
                       const std::string& hostId, const std::string& observeToken,
                       DirectoryConnectTarget* outTarget, std::string* outError,
                       uint32_t* outStatus, bool standby) {'''),
    ('''  body << "{\\"hostId\\":\\"" << json_escape(hostId) << "\\",\\"observeToken\\":\\""
       << json_escape(observeToken) << "\\"}";''', '''  body << "{\\"hostId\\":\\"" << json_escape(hostId) << "\\",\\"observeToken\\":\\""
       << json_escape(observeToken) << "\\"" << (standby ? ",\\"standby\\":true" : "") << "}";'''),
    ('''  json_get_string(response, "connectId", &outTarget->connectId);  // pc2-connect-diag''',
     '''  json_get_string(response, "connectId", &outTarget->connectId);  // pc2-connect-diag
  json_get_string(response, "standbyRelay", &outTarget->standbyRelay);  // keepalive-r2 (2)'''),
    ('''    std::string kind;
    json_get_string(object, "kind", &kind);''', '''    std::string kind;
    json_get_string(object, "kind", &kind);
    if (kind == "relay") {  // keepalive-r2 (2): where a standby lease goes when one is reserved
      outTarget->relayIp = candidate.ip;
      outTarget->relayPort = candidate.port;
    }'''),
])
print('ok')
