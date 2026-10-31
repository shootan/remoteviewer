import io

# ================================================================== request carries what the server said
p = 'apps/native_poc/src/directory_session_bootstrap.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  uint16_t directoryUdpPort = 0;'''
new = '''  uint16_t directoryUdpPort = 0;
  /**
   * What the directory said about its observe endpoint, when it said anything.
   *
   * Filled from the login response. Left default it means "nothing was said", which is not the
   * same as "use the default" -- on https there is no safe default and the attempt stops with a
   * reason instead of dialling 443 + 1.
   */
  directory::ObserveEndpoint advertised;'''
assert old in d, 'request field'
d = d.replace(old, new, 1)
if '#include "directory_client.hpp"' not in d:
    d = d.replace('namespace remote60::native_poc {',
                  '#include "directory_client.hpp"\n\nnamespace remote60::native_poc {', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== the bootstrap applies the rule
p = 'apps/native_poc/src/directory_session_bootstrap.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  if (request.directoryUdpPort == 0 && directoryHttpPort == 65535) {
    // httpPort + 1 would wrap to 0 in a uint16_t and dial nothing. A url on the last port has
    // to say its observe port explicitly.
    if (outError) *outError = "directory http port 65535 leaves no room for the observe port";
    return false;
  }
  const uint16_t observePort = request.directoryUdpPort != 0
                                   ? request.directoryUdpPort
                                   : static_cast<uint16_t>(directoryHttpPort + 1);'''
new = '''  // The rule lives in one place and this uses it, rather than adding one above the http port and
  // hoping. A pinned port still wins -- that is the caller's decision -- and otherwise it is what
  // the directory advertised, or the documented http default, or nothing at all.
  //
  // Nothing at all is the https case with a server that has not been told its observe port. It
  // used to become 444 here, which nothing answers; the viewer then returned before it ever
  // called /api/connect, so it never learned the relay address either. Failing with a reason is
  // the difference between "this server needs configuring" and a connection that just does not
  // work.
  bool directorySecure = false;
  {
    std::string lowered = request.url;
    for (char& c : lowered) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    while (!lowered.empty() && isspace(static_cast<unsigned char>(lowered.front()))) {
      lowered.erase(lowered.begin());
    }
    directorySecure = lowered.rfind("https://", 0) == 0;
  }
  const uint16_t observePort =
      request.directoryUdpPort != 0
          ? request.directoryUdpPort
          : directory::observe_port_for(request.advertised, directoryHttpPort, directorySecure);
  if (observePort == 0) {
    if (outError) {
      *outError = directorySecure
                      ? "this directory has not said where to send address observations; the "
                        "server needs REMOTE60_DIR_OBSERVE_PORT set (or use an http url)"
                      : "directory http port 65535 leaves no room for the observe port";
    }
    return false;
  }
  if (request.advertised.hostRejected && outError) {
    // Not fatal: the directory host is used instead. Recorded so a server-side mistake is
    // findable rather than showing up as a timeout.
    *outError = "note: the directory advertised an unusable observe host; using the directory "
                "host instead";
  }
  const std::string& observeHost =
      request.advertised.host.empty() ? directoryHost : request.advertised.host;'''
assert old in d, 'observe port'
d = d.replace(old, new, 1)

old = '''  if (!rendezvous.Observe(directoryHost, observePort, observeToken, &observed, outError)) {'''
new = '''  if (!rendezvous.Observe(observeHost, observePort, observeToken, &observed, outError)) {'''
assert old in d, 'observe call'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== login carries it out
p = 'apps/native_poc/src/directory_session_client.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''bool directory_login(const std::string& url, const std::string& accountId,'''
i = d.find(old)
assert i != -1, 'login decl'
end = d.find(';', i)
decl = d[i:end]
assert 'outError' in decl
d = d[:end] + ',\n                     directory::ObserveEndpoint* outObserve' + d[end:]
if '#include "directory_client.hpp"' not in d:
    d = d.replace('namespace remote60::native_poc {',
                  '#include "directory_client.hpp"\n\nnamespace remote60::native_poc {', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('viewer request, bootstrap rule, login signature')
