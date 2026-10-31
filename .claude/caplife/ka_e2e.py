p = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\native_poc\\src\\host_standby_admission_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

# activation helper: the result Pong (entry + generation) comes first, then the HelloAck
rep('''// StandbyActivate until a HelloAck; or a refusal (Pong with a status) -- reported in *refusal.
bool activate(SOCKET s, const sockaddr_in& host, const Entry& e, uint32_t gen, uint32_t* refusal, int tries = 10) {
  for (int i = 0; i < tries; ++i) {
    sb_send(s, host, UdpPacketKind::StandbyActivate, 0, gen, std::string(), &e);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    while (std::chrono::steady_clock::now() < deadline) {
      uint8_t buf[2048];
      sockaddr_in from{};
      int fl = sizeof(from);
      const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
      if (n < 8) continue;
      uint16_t k = 0;
      std::memcpy(&k, buf + 4, 2);
      if (k == static_cast<uint16_t>(UdpPacketKind::HelloAck)) {
        remote60::native_poc::UdpHelloPacket ack{};
        std::memcpy(&ack, buf, sizeof(ack));
        return (ack.features & remote60::native_poc::kUdpFeatureDirectoryAuth) != 0;
      }
      if (k == static_cast<uint16_t>(UdpPacketKind::StandbyPong) && n == static_cast<int>(sizeof(UdpStandbyPacket))) {
        UdpStandbyPacket r{};
        std::memcpy(&r, buf, sizeof(r));
        if (refusal) *refusal = r.status;
        return false;
      }
    }
  }
  return false;
}''',
    '''// StandbyActivate until its result; or a refusal -- reported in *refusal. r3 (Codex decision 2): the result names
// the operation -- a StandbyPong of THIS entry and THIS generation (granted, or refused with a status) -- and a
// granted one is followed by the HelloAck. A Pong of another generation (a late answer to an earlier
// activation) is not this one's result and is skipped.
bool activate(SOCKET s, const sockaddr_in& host, const Entry& e, uint32_t gen, uint32_t* refusal, int tries = 10) {
  for (int i = 0; i < tries; ++i) {
    sb_send(s, host, UdpPacketKind::StandbyActivate, 0, gen, std::string(), &e);
    bool granted = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    while (std::chrono::steady_clock::now() < deadline) {
      uint8_t buf[2048];
      sockaddr_in from{};
      int fl = sizeof(from);
      const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
      if (n < 8) continue;
      uint16_t k = 0;
      std::memcpy(&k, buf + 4, 2);
      if (k == static_cast<uint16_t>(UdpPacketKind::HelloAck) && granted) {
        remote60::native_poc::UdpHelloPacket ack{};
        std::memcpy(&ack, buf, sizeof(ack));
        return (ack.features & remote60::native_poc::kUdpFeatureDirectoryAuth) != 0;
      }
      if (k == static_cast<uint16_t>(UdpPacketKind::StandbyPong) && n == static_cast<int>(sizeof(UdpStandbyPacket))) {
        UdpStandbyPacket r{};
        std::memcpy(&r, buf, sizeof(r));
        if (r.entryId != e.id || r.clientGen != gen) continue;  // not this activation's result
        if (r.status == remote60::native_poc::kUdpStandbyStatusOk &&
            (r.features & remote60::native_poc::kUdpStandbyPongActivationGranted) != 0) {
          granted = true;
          continue;
        }
        if (refusal) *refusal = r.status;
        return false;
      }
    }
  }
  return false;
}''')

# S3 in the direct section: the grant ends with its session
rep('''    std::printf("  -- lease --\\n");
    const Entry lapsing = admit(b, hostAddr, 0xB1, 3, tokens[11]);
    Sleep(remote60::native_poc::kStandbyLeaseMs + 1500);
    check("an entry that stops pinging lapses after its lease (20 s)",
          lapsing.ok && ping(b, hostAddr, lapsing) == remote60::native_poc::kUdpStandbyStatusUnknown);''',
    '''    std::printf("  -- lease --\\n");
    const Entry lapsing = admit(b, hostAddr, 0xB1, 3, tokens[11]);
    Sleep(remote60::native_poc::kStandbyLeaseMs + 1500);
    check("an entry that stops pinging lapses after its lease (20 s)",
          lapsing.ok && ping(b, hostAddr, lapsing) == remote60::native_poc::kUdpStandbyStatusUnknown);

    std::printf("  -- r3 S3: the active grant ends with its session --\\n");
    // A's raw client acknowledges none of the host's control data: its session ends as peer-lost (a real end,
    // not a resume) within seconds -- long before now.
    check("A's session has really ended (its control session ended)",
          host.WaitFor("control session ended", 15000), host.tail());
    refusal = 0;
    const size_t epochsAtEnd = count_of(host.log(), "session epoch=");
    check("S3: A's SAME activation again (gen 2) is refused -- the grant ended with its session, no Ack of it",
          !activate(a, hostAddr, ea, 2, &refusal, 1) && refusal == remote60::native_poc::kUdpStandbyStatusSuperseded &&
              count_of(host.log(), "session epoch=") == epochsAtEnd,
          "refusal=" + std::to_string(refusal));
    check("S3: ...A, parked again, still answers its pings", ping(a, hostAddr, ea) == remote60::native_poc::kUdpStandbyStatusOk);
    check("S3: a NEW activation (gen 3) is a new session",
          activate(a, hostAddr, ea, 3, &refusal) && count_of(host.log(), "session epoch=") == epochsAtEnd + 1);''')

# relay: per-client host-facing sockets only (Codex decision 1: (a)); (b), the shared address, is not this R2
rep('''  for (const bool shared : {false, true}) {
    std::printf("\\n[relay stand-in, %s: A direct active, B and C through the relay]\\n",
                shared ? "SHARED host-facing address (today's single lease)" : "per-client host-facing socket");''',
    '''  // r3 (Codex decision 1 / S4): the relay keeps ONE host-facing socket per client lease -- (a). A shared
  // host-facing address (b) is not part of this R2: behind one, the host's control/ACK/NACK/resume/bulk gate is
  // the address, and a parked client's raw packets would pass it. It is no longer exercised here.
  for (const bool shared : {false}) {
    std::printf("\\n[relay stand-in, per-client host-facing socket: A direct active, B and C through the relay]\\n");''')
rep('''    check("B's control (through the relay) works", select_seen(host, b, relay.frontAddr, 1, 0xB2B2000000000001ull));
    check("C, parked behind the same relay, still answers", ping(c, relay.frontAddr, ec) == 0);
    check("...and A, parked, too", ping(a, hostAddr, ea) == 0);''',
    '''    check("B's control (through the relay) works", select_seen(host, b, relay.frontAddr, 1, 0xB2B2000000000001ull));
    check("C, parked behind the same relay, still answers", ping(c, relay.frontAddr, ec) == 0);
    check("...and A, parked, too", ping(a, hostAddr, ea) == 0);
    std::printf("  -- r3 S4: C, parked behind the relay, sends session packets while B is active --\\n");
    const size_t epochsB = count_of(host.log(), "session epoch=");
    for (int i = 0; i < 5; ++i) raw_send_window_select(c, relay.frontAddr, 70 + i, 0xC4C4000000000000ull + i);
    for (int i = 0; i < 5; ++i) raw_send_ctrl_ack(c, relay.frontAddr, 1, 100 + i);
    Entry cForged = ec;
    std::memset(cForged.secret, 0x5A, 16);
    uint32_t cRefusal = 0;
    const bool forgedTook = activate(c, relay.frontAddr, cForged, 1, &cRefusal, 1);
    uint32_t cStale = 0;
    const bool staleTook = activate(c, relay.frontAddr, ec, 0, &cStale, 1);
    check("S4: C's control data / ACKs through its own relay leg are never processed",
          host.log().find("requestedId=" + std::to_string(0xC4C4000000000000ull)) == std::string::npos);
    check("S4: C's activation with a wrong secret and with gen 0 are refused",
          !forgedTook && cRefusal == remote60::native_poc::kUdpStandbyStatusUnknown && !staleTook &&
              cStale == remote60::native_poc::kUdpStandbyStatusSuperseded,
          "wrong-secret=" + std::to_string(cRefusal) + " gen0=" + std::to_string(cStale));
    check("S4: ...B keeps the session: no new epoch, its control still works",
          count_of(host.log(), "session epoch=") == epochsB && select_seen(host, b, relay.frontAddr, 2, 0xB2B2000000000002ull));''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
