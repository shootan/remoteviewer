p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_admission_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''#include "standby_lease.hpp"
''', '''#include "standby_lease.hpp"
#include "directory_session_client.hpp"
''')

# direct provider: the host's address
rep('''  size_t nextToken = 0;
  auto cap = [&]() { return nextToken < tokens.size() ? tokens[nextToken++] : std::string(); };''',
    '''  size_t nextToken = 0;
  auto cap = [&](remote60::native_poc::SocketHandle, remote60::native_poc::StandbyCapability* out) {
    if (nextToken >= tokens.size()) return false;
    out->token = tokens[nextToken++];
    out->ip = "127.0.0.1";
    out->port = mediaPort;
    return true;
  };''')
rep('''  check("A's and B's leases open (one socket each)", a.Open("127.0.0.1", mediaPort, &err) && b.Open("127.0.0.1", mediaPort, &err), err);''',
    '''  check("A's and B's leases open (one socket each)", a.Open(&err) && b.Open(&err), err);''')

# the old host: its own provider (its own port)
rep('''    MemGenStore storeC;
    GenerationAllocator gensC(storeC, randA);
    StandbyLease c(gensC, features, cap, mono_ms);
    check("a lease opens at the older host", c.Open("127.0.0.1", port2, &err), err);''',
    '''    MemGenStore storeC;
    GenerationAllocator gensC(storeC, randA);
    auto capOld = [&](remote60::native_poc::SocketHandle, remote60::native_poc::StandbyCapability* out) {
      out->token = tokens[tokens.size() - 1];
      out->ip = "127.0.0.1";
      out->port = port2;
      return true;
    };
    StandbyLease c(gensC, features, capOld, mono_ms);
    check("a lease opens at the older host", c.Open(&err), err);''')

# the real relay, when --real-directory
rep('''  if (!oldHost.empty()) {
    std::printf("\\n[product client at an OLDER host: Unsupported, no Hello]\\n");''', r'''  if (realDirectory) {
    std::printf("\n[product client through the REAL server.js relay: support from /api/hosts, standby connects, a lease each]\n");
    wchar_t nodeExe[MAX_PATH] = {};
    const bool haveNode = SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, nodeExe, nullptr) > 0;
    const std::string repo = REMOTE60_TEST_REPO_ROOT;
    const std::wstring serverJs = std::wstring(repo.begin(), repo.end()) + L"/apps/directory/server.js";
    NodeServer srv;
    check("the real directory server starts (relay on, loopback)", haveNode && srv.Start(nodeExe, serverJs));
    SpawnedHost rh;
    const uint16_t rPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    const std::string url = "http://127.0.0.1:" + std::to_string(srv.httpPort);
    check("a real GNLinkStream starts against it", StartHost(&rh, url, rPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "realdir"));
    check("the host is online", rh.WaitFor("directory online", 30000), rh.tail());
    std::string session, lerr;
    check("a client signs in (the product's directory_login)",
          remote60::native_poc::directory_login(url, "tester", "e2e-pass-1234", &session, &lerr), lerr);
    std::vector<remote60::native_poc::DirectoryHostEntry> hosts;
    remote60::native_poc::directory_list_hosts(url, session, &hosts, &lerr);
    const std::string hostId = hosts.empty() ? std::string() : hosts.front().hostId;
    bool supported = false, relay = false;
    check("standby support is read from /api/hosts (no side effect): supported, with a relay",
          remote60::native_poc::directory_standby_support(url, session, &supported, &relay, &lerr) && supported && relay, lerr);
    int observeSeq = 0;
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
    };
    MemGenStore rsA, rsB;
    GenerationAllocator rgA(rsA, randA), rgB(rsB, randB);
    StandbyLease ra(rgA, features, relayCap, mono_ms), rb(rgB, features, relayCap, mono_ms);
    check("two leases open", ra.Open(&err) && rb.Open(&err), err);
    ra.Start();
    rb.Start();
    check("each standby connect reserved its own relay lease", lastRelayState == "reserved", lastRelayState);
    const bool rpa = pump_until(ra, [&] { return ra.state() == StandbyLease::State::Parked; }, 30000);
    const bool rpb = pump_until(rb, [&] { return rb.state() == StandbyLease::State::Parked; }, 30000);
    check("A and B park THROUGH the relay, each its own entry", rpa && rpb && ra.entryId() != rb.entryId(),
          last_events(ra) + " | " + last_events(rb) + " target=" + ra.target());
    check("...two relay leases bound and ready, no session", count_of(srv.log(), "standby ready") >= 2 &&
          count_of(rh.log(), "first session") == 0, rh.tail());
    NullSink rsinkA, rsinkB;
    remote60::native_poc::ClientSessionController rca, rcb;
    remote60::native_poc::ClientSessionConnectArgs rargsA;
    rargsA.encodedFrameSink = &rsinkA;
    rargsA.controlIntervalMs = 200;
    const bool rupA = ra.ActivationArgs(&rargsA) && rca.Connect(rargsA) &&
                      wait_until([&] { return rca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
    check("A's session through its relay lease, by activation", rupA && rh.WaitFor("first session by activation", 3000),
          rca.Snapshot().lastError + " | " + last_events(ra));
    check("...video through the relay", wait_until([&] { return rsinkA.frames() > 0; }, 10000));
    check("...the relay marks A's lease active", srv.WaitFor("standby active", 3000));
    pump_until(rb, [] { return false; }, 8000);
    check("B pings through its own lease: parked, A untouched", rb.state() == StandbyLease::State::Parked &&
          rca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected, last_events(rb));
    remote60::native_poc::ClientSessionConnectArgs rargsB;
    rargsB.encodedFrameSink = &rsinkB;
    rargsB.controlIntervalMs = 200;
    const bool rupB = rb.ActivationArgs(&rargsB) && rcb.Connect(rargsB) &&
                      wait_until([&] { return rcb.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
    check("B takes over through ITS relay lease (a new epoch, video)", rupB && rh.WaitFor("(standby activation", 3000) &&
          wait_until([&] { return rsinkB.frames() > 0; }, 10000), rcb.Snapshot().lastError + " | " + last_events(rb));
    wait_until([&] { return rca.Snapshot().state != remote60::native_poc::ClientSessionState::Connected; }, 25000);
    rca.Disconnect();
    ra.SessionOver();
    // the stated limit: A's new socket is not the endpoint its relay lease is bound to -- its Pings die there, the
    // entry is lost (never kept as ready) and re-admitted with a NEW standby connect from the new socket
    const bool readmitted = pump_until(ra, [&] { return ra.lostCount() >= 1 && ra.state() == StandbyLease::State::Parked; }, 45000);
    check("through the relay, A's lease after its session: lost on the new socket, then re-admitted and parked (bounded)",
          readmitted, last_events(ra));
    rcb.Disconnect();
    rh.Stop();
    srv.Stop();
  }

  if (!oldHost.empty()) {
    std::printf("\n[product client at an OLDER host: Unsupported, no Hello]\n");''')

rep('''int run_product_client(const std::vector<std::string>& tokens, const std::wstring& oldHost) {''',
    '''int run_product_client(const std::vector<std::string>& tokens, const std::wstring& oldHost, bool realDirectory) {''')
rep('''    if (std::wstring(argv[i]) == L"--product-client") return run_product_client(tokens, oldHost);''',
    '''    if (std::wstring(argv[i]) == L"--product-client") return run_product_client(tokens, oldHost, realDirectory);''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
