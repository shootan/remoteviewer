import io

p = 'apps/native_poc/src/directory_retry_test.cpp'
d = io.open(p, encoding='utf-8').read()

def sub(old, new):
    global d
    assert d.count(old) == 1, old[:70]
    d = d.replace(old, new, 1)

# ---- the fake directory can answer the probe with whatever the test wants
sub('''  int ObserveProbes() const { return probes_.load(); }''',
    '''  int ObserveProbes() const { return probes_.load(); }

  /** What to answer an OBSERVE probe with. Empty = the real thing (the sender's own address). */
  void ObserveReply(const std::string& json) {
    std::lock_guard<std::mutex> lk(mu_);
    observeReply_ = json;
  }''')

sub('''      ++probes_;
      char ip[INET_ADDRSTRLEN] = {};
      inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
      const std::string reply = std::string("{\\"ip\\":\\"") + ip + "\\",\\"port\\":" +
                                std::to_string(ntohs(from.sin_port)) + "}";''',
    '''      ++probes_;
      char ip[INET_ADDRSTRLEN] = {};
      inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
      std::string scripted;
      {
        std::lock_guard<std::mutex> lk(mu_);
        scripted = observeReply_;
      }
      const std::string reply =
          scripted.empty() ? std::string("{\\"ip\\":\\"") + ip + "\\",\\"port\\":" +
                                 std::to_string(ntohs(from.sin_port)) + "}"
                           : scripted;''')

sub('''  std::atomic<int> probes_{0};''',
    '''  std::atomic<int> probes_{0};
  std::string observeReply_;''')

# ---- RunHost hands the status line back so the caller can see what was published
sub('''void RunHost(FakeDirectory& dir, const char* label, int wantHeartbeats, int wantRegisters) {''',
    '''std::string RunHost(FakeDirectory& dir, const char* label, int wantHeartbeats,
                    int wantRegisters) {''')
sub('''  agent.Stop();
  pumping = false;
  closesocket(media);
  if (pump.joinable()) pump.join();
  DeleteFileA(cfg.cachePath.c_str());
}''',
    '''  const std::string status = agent.StatusLine();
  agent.Stop();
  pumping = false;
  closesocket(media);
  if (pump.joinable()) pump.join();
  DeleteFileA(cfg.cachePath.c_str());
  return status;
}''')

# ---- the new cases
anchor = '''  WSACleanup();'''
added = '''  // ---------------------------------------- a port that is not a port must not become one
  //
  // The reply to the address probe is the host's own public port: it is what gets published, and
  // it decides whether anyone can reach this machine. The reader here took the digits in front of
  // a '.' and had no upper bound -- only zero was refused -- so 65537 became **1** on the way
  // through uint16_t. That is not a value anything downstream rejects. It is a plausible port,
  // and a host nobody can reach looks like a network fault rather than a parse.
  //
  // Asserted through the status line, which is where the observation surfaces: no "public=" means
  // nothing was published, which is the correct outcome for every one of these.
  {
    const char* bad[] = {
        "{\\"ip\\":\\"1.2.3.4\\",\\"port\\":65537}",    // truncated to 1 before
        "{\\"ip\\":\\"1.2.3.4\\",\\"port\\":65536}",    // one past the top
        "{\\"ip\\":\\"1.2.3.4\\",\\"port\\":0}",        // refused before too
        "{\\"ip\\":\\"1.2.3.4\\",\\"port\\":29181.5}",  // read as 29181 before
        "{\\"ip\\":\\"1.2.3.4\\",\\"port\\":-1}",
        "{\\"ip\\":\\"1.2.3.4\\",\\"port\\":\\"29181\\"}",
        "{\\"ip\\":\\"1.2.3.4\\",\\"port\\":99999999999999}",
        "{\\"ip\\":\\"\\",\\"port\\":29181}",           // an address that is not one
    };
    for (const char* reply : bad) {
      FakeDirectory dir;
      dir.Start();
      dir.ObserveReply(reply);
      dir.Script("/api/host/register",
                 {Reply{200, "{\\"hostId\\":\\"h1\\",\\"hostToken\\":\\"" + std::string(32, 'c') + "\\"}"}});
      dir.Script("/api/host/heartbeat", {Reply{200, "{\\"ok\\":true}"}});
      const std::string status = RunHost(dir, "bad-port", 1, 1);
      check((std::string("nothing is published for ") + reply).c_str(),
            status.find("public=") == std::string::npos, status);
      dir.Stop();
    }
  }

  {
    // The boundary that must still work. A rule that refuses 65535 as well would be a different
    // defect wearing the same fix.
    FakeDirectory dir;
    dir.Start();
    dir.ObserveReply("{\\"ip\\":\\"1.2.3.4\\",\\"port\\":65535}");
    dir.Script("/api/host/register",
               {Reply{200, "{\\"hostId\\":\\"h1\\",\\"hostToken\\":\\"" + std::string(32, 'd') + "\\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\\"ok\\":true}"}});
    const std::string status = RunHost(dir, "port-65535", 1, 1);
    check("65535 is a port and is published", status.find("public=1.2.3.4:65535") != std::string::npos,
          status);
    dir.Stop();
  }

  // ---------------------------------------- and the same reply on the viewer's path
  //
  // The viewer and the phone share one parser (directory_rendezvous.cpp), so this covers both.
  // A refused reply must leave the probe unanswered rather than dialling a number it invented.
  {
    FakeDirectory dir;
    dir.Start();
    dir.ObserveReply("{\\"ip\\":\\"1.2.3.4\\",\\"port\\":65537}");
    dir.Script("/api/connect", {Reply{200, candidateBody()}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    const bool opened = directory_session_open(request, &session, &error);
    check("the viewer refuses an out-of-range observed port", !opened, error);
    check("...and never asks to connect on it", dir.Count("/api/connect") == 0,
          std::to_string(dir.Count("/api/connect")));
    dir.Stop();
  }

  {
    FakeDirectory dir;
    dir.Start();
    dir.ObserveReply("{\\"ip\\":\\"1.2.3.4\\",\\"port\\":29181.5}");
    dir.Script("/api/connect", {Reply{200, candidateBody()}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    check("a fractional observed port is not a port on the viewer's side either",
          !directory_session_open(request, &session, &error), error);
    dir.Stop();
  }

''' + anchor
assert d.count(anchor) == 1, 'tail'
d = d.replace(anchor, added, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok retry ports')
