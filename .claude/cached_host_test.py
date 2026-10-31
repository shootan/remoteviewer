# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/directory_retry_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- the harness can seed a cache
old = '''std::string RunHost(FakeDirectory& dir, const char* label, int wantHeartbeats,
                    int wantRegisters) {'''
new = '''/**
 * Writes a host cache so the agent starts as a machine that has registered before.
 *
 * That is the state the field defect lived in: a cached token means EnsureRegistered() returns
 * immediately, so nothing ever tells the host where observations go.
 */
void SeedHostCache(const std::string& path, const std::string& url) {
  remote60::native_poc::directory::HostCache cache;
  cache.directoryUrl = url;
  cache.accountId = "tester";
  cache.machineId = remote60::native_poc::directory::machine_id();
  cache.hostName = "Cached PC";
  cache.hostId = "h-cached";
  cache.hostToken = std::string(32, 'e');
  remote60::native_poc::directory::save_host_cache(path, cache);
}

std::string RunHost(FakeDirectory& dir, const char* label, int wantHeartbeats,
                    int wantRegisters, bool seedCache = false, uint16_t explicitPort = 0) {'''
assert d.count(old) == 1, 'harness head'
d = d.replace(old, new, 1)

old2 = '''  cfg.cachePath = exe_directory() + "\\retry-fixture-" + std::string(label) + ".json";
  cfg.observeUdpPort = dir.udpPort();
  cfg.heartbeatSeconds = 5;'''
new2 = '''  cfg.cachePath = exe_directory() + "\\retry-fixture-" + std::string(label) + ".json";
  // Nothing pinned by default: the point of most of these cases is what the agent works out for
  // itself. A case that pins one says so.
  cfg.observeUdpPort = explicitPort;
  cfg.heartbeatSeconds = 5;
  if (seedCache) {
    SeedHostCache(cfg.cachePath, cfg.url);
    // No password either. Registration is then impossible, so a heartbeat can only happen if the
    // cached token was used -- which is what puts the agent in the state under test.
    cfg.password.clear();
  }'''
assert d.count(old2) == 1, 'cfg'
d = d.replace(old2, new2, 1)

# existing callers keep pinning the port, as they did before
old3 = '''    RunHost(dir, "409-then-200", 2, 1);'''
new3 = '''    RunHost(dir, "409-then-200", 2, 1, false, dir.udpPort());'''
assert d.count(old3) == 1
d = d.replace(old3, new3, 1)
old4 = '''    RunHost(dir, "401", 1, 2);'''
new4 = '''    RunHost(dir, "401", 1, 2, false, dir.udpPort());'''
assert d.count(old4) == 1
d = d.replace(old4, new4, 1)
old5 = '''      const std::string status = RunHost(dir, "bad-port", 1, 1);'''
new5 = '''      const std::string status = RunHost(dir, "bad-port", 1, 1, false, dir.udpPort());'''
assert d.count(old5) == 1
d = d.replace(old5, new5, 1)
old6 = '''    const std::string status = RunHost(dir, "port-65535", 1, 1);'''
new6 = '''    const std::string status = RunHost(dir, "port-65535", 1, 1, false, dir.udpPort());'''
assert d.count(old6) == 1
d = d.replace(old6, new6, 1)

# ---------------------------------------------------------------- the new cases
anchor = '''  WSACleanup();'''
added = '''  // ------------------------------------ a host that resumed from a cache still has to be told
  //
  // The field defect, reproduced without TLS. EnsureRegistered() returns immediately when a token
  // is cached, and the advertisement only ever arrived on the registration response -- so a host
  // that had registered successfully once never learned where observations go again. On https
  // there is no default to fall back to, so it could not observe, could not heartbeat, and never
  // appeared in anyone's list. The better the last run went, the more certainly the next one was
  // stuck.
  //
  // Here the fake directory's observe port is an ephemeral one the OS picked, which is NOT the
  // http port plus one. So the legacy default cannot reach it: a heartbeat proves the agent asked
  // the health route and used the answer.
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/healthz", {Reply{200, "{\\"ok\\":true,\\"observe\\":{\\"port\\":" +
                                           std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\\"ok\\":true}"}});
    dir.Script("/api/host/register", {Reply{500, "{\\"error\\":\\"must not be called\\"}"}});

    const std::string status = RunHost(dir, "cached-advertised", 1, 0, true);
    check("a cached host asks the health route", dir.Count("/healthz") >= 1,
          std::to_string(dir.Count("/healthz")));
    check("...and heartbeats using the advertised port",
          dir.Count("/api/host/heartbeat") >= 1,
          std::to_string(dir.Count("/api/host/heartbeat")));
    check("...without registering again", dir.Count("/api/host/register") == 0,
          std::to_string(dir.Count("/api/host/register")));
    check("...and reports itself online", status.find("public=") != std::string::npos, status);
    dir.Stop();
  }

  {
    // The same, with a directory that says nothing. It must refuse rather than dial something it
    // made up -- and it must not sit in a loop asking. The count is the assertion: a poll would
    // show one request per cycle.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/healthz", {Reply{200, "{\\"ok\\":true}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\\"ok\\":true}"}});
    dir.Script("/api/host/register", {Reply{500, "{\\"error\\":\\"must not be called\\"}"}});

    const std::string status = RunHost(dir, "cached-silent", 99, 99, true);
    check("a silent directory means nothing is published",
          status.find("public=") == std::string::npos, status);
    check("...and no heartbeat is sent", dir.Count("/api/host/heartbeat") == 0,
          std::to_string(dir.Count("/api/host/heartbeat")));
    check("...and the health route is asked, but not on a loop",
          dir.Count("/healthz") >= 1 && dir.Count("/healthz") <= 6,
          std::to_string(dir.Count("/healthz")));
    check("...and the reason is about the server, not a timeout",
          status.find("observations") != std::string::npos, status);
    dir.Stop();
  }

  {
    // A pinned port is the operator's decision and outranks the advertisement -- and the agent
    // must not ask the health route at all when it already has an answer.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/healthz", {Reply{200, "{\\"ok\\":true,\\"observe\\":{\\"port\\":9}}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\\"ok\\":true}"}});
    dir.Script("/api/host/register", {Reply{500, "{\\"error\\":\\"must not be called\\"}"}});

    const std::string status = RunHost(dir, "cached-pinned", 1, 0, true, dir.udpPort());
    check("a pinned port is used and the advertisement is not needed",
          dir.Count("/api/host/heartbeat") >= 1 && status.find("public=") != std::string::npos,
          status);
    check("...so the health route is not asked at all", dir.Count("/healthz") == 0,
          std::to_string(dir.Count("/healthz")));
    dir.Stop();
  }

''' + anchor
assert d.count(anchor) == 1, 'anchor'
d = d.replace(anchor, added, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok test')
