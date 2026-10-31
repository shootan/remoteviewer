p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_admission_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''#include "standby_runner.hpp"
''', '''#include "standby_runner.hpp"
#include "standby_directory.hpp"
''')
rep('''    int observeSeq = 0;
    std::string lastRelayState;
    // The capability through the lease's OWN socket: observe from it, then a standby connect -- the relay binds the
    // lease to this endpoint. Aim at the relay only when THIS connect reserved one.
    auto relayCap = [&](remote60::native_poc::SocketHandle s, remote60::native_poc::StandbyCapability* out) {
      const std::string obs = "prod-standby-" + std::to_string(++observeSeq);
      if (!observe_from(s, srv.udpPort, obs)) return false;
      remote60::native_poc::DirectoryConnectTarget t;
      std::string e;
      if (!remote60::native_poc::directory_connect(url, session, hostId, obs, &t, &e, nullptr, /*standby=*/true)) return false;
      lastRelayState = t.standbyRelay;
      if (t.standbyRelay != "reserved" || t.relayPort == 0) return false;
      out->token = t.punchToken;
      out->ip = t.relayIp;
      out->port = t.relayPort;
      return true;
    };''', '''    int observeSeq = 0;
    std::string lastRelayState;
    // The PRODUCT capability (standby_directory.hpp): observe from the lease's own socket, a standby connect, aim at the
    // relay when this connect reserved one.
    remote60::native_poc::StandbyDirectoryConfig dirCfg;
    dirCfg.url = url;
    dirCfg.session = [&] { return session; };
    dirCfg.hostId = hostId;
    dirCfg.observeHost = "127.0.0.1";
    dirCfg.observePort = srv.udpPort;
    dirCfg.freshToken = [&] { return "prod-standby-" + std::to_string(++observeSeq); };
    dirCfg.note = [&](const std::string& what) { lastRelayState = what; };
    auto relayCap = remote60::native_poc::standby_directory_capability(dirCfg);''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
