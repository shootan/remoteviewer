import io

# ================================================================== header: carry it out of register
p = 'apps/native_poc/src/directory_client.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''bool register_host(const std::string& url, const std::string& accountId,
                   const std::string& password, const std::string& hostName,
                   const std::string& machineId, std::string* outHostId,
                   std::string* outHostToken, std::string* outError);'''
new = '''bool register_host(const std::string& url, const std::string& accountId,
                   const std::string& password, const std::string& hostName,
                   const std::string& machineId, std::string* outHostId,
                   std::string* outHostToken, std::string* outError,
                   ObserveEndpoint* outObserve = nullptr);'''
assert old in d, 'register decl'
d = d.replace(old, new, 1)

old = '''  sockaddr_in observeAddr_{};'''
new = '''  sockaddr_in observeAddr_{};
  /**
   * What the server last said about the observe endpoint, and whether the socket is aimed there.
   *
   * Registration happens before the first observation on every cycle, so the answer is available
   * before it is needed. It is re-applied rather than assumed: a cached registration that skipped
   * the exchange would otherwise leave the socket pointed wherever startup guessed.
   */
  ObserveEndpoint observeAdvertised_{};
  bool observeAddrReady_ = false;'''
assert old in d, 'observeAddr member'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== implementation
p = 'apps/native_poc/src/directory_client.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''                   const std::string& machineId, std::string* outHostId,
                   std::string* outHostToken, std::string* outError) {'''
new = '''                   const std::string& machineId, std::string* outHostId,
                   std::string* outHostToken, std::string* outError,
                   ObserveEndpoint* outObserve) {'''
assert old in d, 'register def'
d = d.replace(old, new, 1)

old = '''  if (outHostToken) *outHostToken = token;
  if (outHostId) json_get_string(resp, "hostId", outHostId);
  return true;'''
new = '''  if (outHostToken) *outHostToken = token;
  if (outHostId) json_get_string(resp, "hostId", outHostId);
  // Optional, and absence is not an error: an older server does not send it and the caller has a
  // rule for that.
  if (outObserve) parse_observe_metadata(resp, outObserve);
  return true;'''
assert old in d, 'register out'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- startup no longer guesses alone
old = '''  const uint16_t observePort = cfg_.observeUdpPort ? cfg_.observeUdpPort
                                                   : static_cast<uint16_t>(httpPort_ + 1);
  if (!resolve_ipv4(httpHost_, observePort, &observeAddr_)) {
    if (outError) *outError = "cannot resolve directory host '" + httpHost_ + "'";
    return false;
  }'''
new = '''  // Aimed as far as it can be aimed before talking to anyone. A configured port wins outright; a
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
  }'''
assert old in d, 'startup port'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- apply what the server said
old = '''  hostToken_ = token;
  hostId_ = id;
  SaveCache();'''
new = '''  hostToken_ = token;
  hostId_ = id;
  observeAdvertised_ = advertised;
  if (!ApplyObserveEndpoint()) return false;
  SaveCache();'''
assert old in d, 'register callsite tail'
d = d.replace(old, new, 1)

old = '''  std::string token, id, error;
  if (!register_host(cfg_.url, cfg_.accountId, cfg_.password, cfg_.hostName, machineId_, &id,
                     &token, &error)) {'''
new = '''  std::string token, id, error;
  ObserveEndpoint advertised;
  if (!register_host(cfg_.url, cfg_.accountId, cfg_.password, cfg_.hostName, machineId_, &id,
                     &token, &error, &advertised)) {'''
assert old in d, 'register callsite head'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the aiming itself
old = '''bool parse_observe_metadata(const std::string& json, ObserveEndpoint* out) {'''
new = '''bool HostAgent::ApplyObserveEndpoint() {
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
  const std::string& target = observeAdvertised_.host.empty() ? httpHost_ : observeAdvertised_.host;
  if (!resolve_ipv4(target, port, &observeAddr_)) {
    SetStatus("cannot resolve the observe host '" + target + "'");
    observeAddrReady_ = false;
    return false;
  }
  observeAddrReady_ = true;
  return true;
}

bool parse_observe_metadata(const std::string& json, ObserveEndpoint* out) {'''
assert old in d, 'apply anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- refuse to probe an unaimed socket
old = '''    send_(probe.data(), probe.size(), observeAddr_);'''
new = '''    if (!observeAddrReady_) return false;  // nowhere to send it; the status already says why
    send_(probe.data(), probe.size(), observeAddr_);'''
assert old in d, 'probe send'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('host consumes the advertised endpoint')
