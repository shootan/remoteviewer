import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ======================================================================= host: one bounded retry
edit('apps/native_poc/src/directory_client.hpp', [
 ('''  bool Heartbeat(std::vector<PunchTarget>* outPunch);''',
  '''  bool Heartbeat(std::vector<PunchTarget>* outPunch);
  // One attempt, reporting the status and the server's error name so the caller can tell a
  // missing observation apart from every other refusal. Heartbeat() is the policy on top.
  bool HeartbeatAttempt(std::vector<PunchTarget>* outPunch, uint32_t* outStatus,
                        std::string* outServerError);'''),
])

edit('apps/native_poc/src/directory_client.cpp', [
 ('''bool HostAgent::Heartbeat(std::vector<PunchTarget>* outPunch) {
  std::ostringstream body;''',
  '''bool HostAgent::Heartbeat(std::vector<PunchTarget>* outPunch) {
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
  std::ostringstream body;'''),
 ('''  uint32_t status = 0;
  std::string resp;
  if (!HttpPostJson("/api/host/heartbeat", body.str(), &status, &resp)) {
    SetStatus("directory unreachable");
    return false;
  }
  if (status == 401) {''',
  '''  uint32_t status = 0;
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
  if (status == 401) {'''),
])

# ==================================================================== viewer: one bounded retry
edit('apps/native_poc/src/directory_session_client.hpp', [
 ('''bool directory_connect(const std::string& url, const std::string& sessionToken,''',
  '''/**
 * `outStatus` carries the http status when the exchange happened at all, so a caller can tell a
 * 409 -- the directory has no address observation for this client, which is repairable here by
 * sending one -- from every other refusal, which is not.
 */
bool directory_connect(const std::string& url, const std::string& sessionToken,'''),
])

edit('apps/native_poc/src/directory_session_client.cpp', [
 ('''  if (!http_request(host, port, secure, "POST", "/api/connect", sessionToken, body.str(), &status,
                    &response)) {
    if (outError) *outError = "cannot reach the server";
    return false;
  }
  if (status != 200) {''',
  '''  if (!http_request(host, port, secure, "POST", "/api/connect", sessionToken, body.str(), &status,
                    &response)) {
    if (outError) *outError = "cannot reach the server";
    return false;
  }
  if (outStatus) *outStatus = status;
  if (status != 200) {'''),
])
