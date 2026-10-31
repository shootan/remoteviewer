S = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = S + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('host_standby_admission_e2e_test.cpp', [
    # 1. the relay stand-in: per-client host-facing sockets only (the shared mode and its pre-approval lastActivated
    #    routing are gone -- Codex S4: that mock was no evidence for (b), and (b) is not part of R2)
    ('''// A relay stand-in. per-client mode: each client gets its own host-facing socket (the routing a relay
// needs for several entries -- the host then tells them apart by address too). shared mode: every client
// goes out of ONE host-facing socket (today's single relay lease: one address for all), and host->client
// standby replies are routed back by entry id / device id, everything else to the last activated client.
struct RelayMock {
  bool shared = false;
  SOCKET front = INVALID_SOCKET;  // client-facing''',
     '''// A relay stand-in for decision (a): each client lease gets its OWN host-facing socket, so the host tells the
// clients apart by address as well as by entry. It routes host->client by the socket a datagram arrived on and
// reads nothing in it (r3: the shared-socket mode, whose replies were routed by the last StandbyActivate SEEN --
// before the host had accepted it -- is gone with option (b), Codex S4).
struct RelayMock {
  SOCKET front = INVALID_SOCKET;  // client-facing'''),
    ('''    SOCKET up = INVALID_SOCKET;
    uint8_t device = 0;
    uint64_t entryId = 0;
  };
  std::vector<Leg> legs;
  SOCKET sharedUp = INVALID_SOCKET;
  sockaddr_in lastActivated{};
  std::mutex mu;

  bool Start(const sockaddr_in& host, bool sharedMode) {
    shared = sharedMode;
    hostAddr = host;
    front = raw_open("127.0.0.1");
    if (front == INVALID_SOCKET) return false;
    int len = sizeof(frontAddr);
    getsockname(front, reinterpret_cast<sockaddr*>(&frontAddr), &len);
    if (shared) sharedUp = raw_open("127.0.0.1");
    DWORD to = 5;
    setsockopt(front, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    if (shared) setsockopt(sharedUp, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    running = true;''',
     '''    SOCKET up = INVALID_SOCKET;
  };
  std::vector<Leg> legs;
  std::mutex mu;

  bool Start(const sockaddr_in& host) {
    hostAddr = host;
    front = raw_open("127.0.0.1");
    if (front == INVALID_SOCKET) return false;
    int len = sizeof(frontAddr);
    getsockname(front, reinterpret_cast<sockaddr*>(&frontAddr), &len);
    DWORD to = 5;
    setsockopt(front, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    running = true;'''),
    ('''          leg->client = from;
          if (!shared) {
            leg->up = raw_open("127.0.0.1");
            DWORD to = 5;
            setsockopt(leg->up, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
          }
        }
        uint16_t k = 0;
        std::memcpy(&k, buf + 4, 2);
        if (n == static_cast<int>(sizeof(UdpStandbyPacket))) {
          UdpStandbyPacket p{};
          std::memcpy(&p, buf, sizeof(p));
          if (k == static_cast<uint16_t>(UdpPacketKind::StandbyHello)) leg->device = p.deviceId[0];
          if (p.entryId) leg->entryId = p.entryId;
          if (k == static_cast<uint16_t>(UdpPacketKind::StandbyActivate)) lastActivated = leg->client;
        }
        sendto(shared ? sharedUp : leg->up, reinterpret_cast<const char*>(buf), n, 0,
               reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
      }
      // host -> clients
      std::vector<std::pair<SOCKET, sockaddr_in>> ups;
      {
        std::lock_guard<std::mutex> lock(mu);
        if (shared) {
          ups.push_back({sharedUp, sockaddr_in{}});
        } else {
          for (Leg& l : legs) ups.push_back({l.up, l.client});
        }
      }
      for (auto& up : ups) {
        for (int i = 0; i < 64; ++i) {
          const int m = recv(up.first, reinterpret_cast<char*>(buf), sizeof(buf), 0);
          if (m <= 0) break;
          sockaddr_in dest = up.second;
          if (shared) {
            std::lock_guard<std::mutex> lock(mu);
            dest = lastActivated;
            if (m == static_cast<int>(sizeof(UdpStandbyPacket))) {
              UdpStandbyPacket p{};
              std::memcpy(&p, buf, sizeof(p));
              for (Leg& l : legs) {
                if ((p.entryId && l.entryId == p.entryId) ||
                    (!l.entryId && p.kind == static_cast<uint16_t>(UdpPacketKind::StandbyAck) && p.entryId != 0 &&
                     legs.size() > 0 && &l == &legs.back()))
                  dest = l.client;
              }
            }
          }
          if (dest.sin_port != 0)
            sendto(front, reinterpret_cast<const char*>(buf), m, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
        }
      }''',
     '''          leg->client = from;
          leg->up = raw_open("127.0.0.1");
          DWORD to = 5;
          setsockopt(leg->up, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
        }
        sendto(leg->up, reinterpret_cast<const char*>(buf), n, 0, reinterpret_cast<const sockaddr*>(&hostAddr),
               sizeof(hostAddr));
      }
      // host -> clients: by the leg's own socket, nothing read from the datagram
      std::vector<std::pair<SOCKET, sockaddr_in>> ups;
      {
        std::lock_guard<std::mutex> lock(mu);
        for (Leg& l : legs) ups.push_back({l.up, l.client});
      }
      for (auto& up : ups) {
        for (int i = 0; i < 64; ++i) {
          const int m = recv(up.first, reinterpret_cast<char*>(buf), sizeof(buf), 0);
          if (m <= 0) break;
          sendto(front, reinterpret_cast<const char*>(buf), m, 0, reinterpret_cast<const sockaddr*>(&up.second),
                 sizeof(up.second));
        }
      }'''),
    ('''    for (Leg& l : legs)
      if (l.up != INVALID_SOCKET) closesocket(l.up);
    if (sharedUp != INVALID_SOCKET) closesocket(sharedUp);
    if (front != INVALID_SOCKET) closesocket(front);''',
     '''    for (Leg& l : legs)
      if (l.up != INVALID_SOCKET) closesocket(l.up);
    if (front != INVALID_SOCKET) closesocket(front);'''),
    # 2. raw senders for the rest of the session packets
    ('''// A loopback UDP socket bound to `bindIp` (an ephemeral port), with a short receive timeout.''',
     '''// r3 S4: the rest of a session's packets, raw -- what a parked client behind a relay could send.
void raw_send_ctrl_nack(SOCKET s, const sockaddr_in& host, uint32_t streamId, uint32_t seq) {
  remote60::native_poc::UdpControlAckPacket a{};
  a.kind = static_cast<uint16_t>(UdpPacketKind::ControlNack);
  a.streamId = streamId;
  a.messageSeq = seq;
  a.missingCount = 1;
  a.missing[0] = 0;
  sendto(s, reinterpret_cast<const char*>(&a), sizeof(a), 0, reinterpret_cast<const sockaddr*>(&host), sizeof(host));
}
void raw_send_video_nack(SOCKET s, const sockaddr_in& host, uint32_t seq) {
  remote60::native_poc::UdpVideoNackPacket n{};
  n.seq = seq;
  n.missingCount = 1;
  n.missing[0] = 0;
  sendto(s, reinterpret_cast<const char*>(&n), sizeof(n), 0, reinterpret_cast<const sockaddr*>(&host), sizeof(host));
}
void raw_send_resume(SOCKET s, const sockaddr_in& host, uint32_t resumeId) {
  remote60::native_poc::UdpControlResumePacket r{};
  r.streamId = remote60::native_poc::kUdpControlStreamClientToHost;
  r.resumeId = resumeId;
  sendto(s, reinterpret_cast<const char*>(&r), sizeof(r), 0, reinterpret_cast<const sockaddr*>(&host), sizeof(host));
}
// A bulk-channel-shaped datagram (control chunk header, a bulk stream id): the host's bulk receivers sit behind
// the same adopted-peer gate as control.
void raw_send_bulk(SOCKET s, const sockaddr_in& host, uint32_t messageSeq) {
  remote60::native_poc::UdpControlChunkHeader head{};
  head.magic = remote60::native_poc::kMagic;
  head.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::ControlData);
  head.size = static_cast<uint16_t>(sizeof(head));
  head.streamId = 0x42554C4Bu;  // not a control stream
  head.messageSeq = messageSeq;
  head.totalSize = 16;
  head.fragCount = 1;
  head.fragSize = 16;
  uint8_t dg[sizeof(head) + 16] = {};
  std::memcpy(dg, &head, sizeof(head));
  sendto(s, reinterpret_cast<const char*>(dg), sizeof(dg), 0, reinterpret_cast<const sockaddr*>(&host), sizeof(host));
}
// Drains `s` for `ms` and counts what the host sent it that is NOT a Standby* reply.
int count_non_standby_rx(SOCKET s, int ms) {
  int n = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    uint8_t buf[2048];
    const int m = recv(s, reinterpret_cast<char*>(buf), sizeof(buf), 0);
    if (m < 6) continue;
    uint16_t k = 0;
    std::memcpy(&k, buf + 4, 2);
    if (k < static_cast<uint16_t>(UdpPacketKind::StandbyHello) || k > static_cast<uint16_t>(UdpPacketKind::StandbyRelease)) ++n;
  }
  return n;
}

// A loopback UDP socket bound to `bindIp` (an ephemeral port), with a short receive timeout.'''),
    # 3. the relay section: one mode; S4 extended to every session packet + what C receives + B's reverse path
    ('''  for (const bool shared : {false}) {
    std::printf''', '''  {
    std::printf'''),
    ('''    check("the relay stand-in starts", relay.Start(hostAddr, shared));''',
     '''    check("the relay stand-in starts", relay.Start(hostAddr));'''),
    ('''    std::printf("  -- r3 S4: C, parked behind the relay, sends session packets while B is active --\\n");
    const size_t epochsB = count_of(host.log(), "session epoch=");
    for (int i = 0; i < 5; ++i) raw_send_window_select(c, relay.frontAddr, 70 + i, 0xC4C4000000000000ull + i);
    for (int i = 0; i < 5; ++i) raw_send_ctrl_ack(c, relay.frontAddr, 1, 100 + i);''',
     '''    std::printf("  -- r3 S4: C, parked behind the relay, sends session packets while B is active --\\n");
    const size_t epochsB = count_of(host.log(), "session epoch=");
    const size_t endedB = count_of(host.log(), "control session ended");
    count_non_standby_rx(c, 200);  // whatever was in flight before
    for (int i = 0; i < 5; ++i) raw_send_window_select(c, relay.frontAddr, 70 + i, 0xC4C4000000000000ull + i);
    for (int i = 0; i < 5; ++i) raw_send_ctrl_ack(c, relay.frontAddr, 1, 100 + i);
    for (int i = 0; i < 5; ++i) raw_send_ctrl_nack(c, relay.frontAddr, 2, 1 + i);
    for (int i = 0; i < 5; ++i) raw_send_video_nack(c, relay.frontAddr, 1 + i);
    for (int i = 0; i < 3; ++i) raw_send_resume(c, relay.frontAddr, 0xC5C50000u + i);
    for (int i = 0; i < 5; ++i) raw_send_bulk(c, relay.frontAddr, 1 + i);'''),
    ('''    check("S4: ...B keeps the session: no new epoch, its control still works",
          count_of(host.log(), "session epoch=") == epochsB && select_seen(host, b, relay.frontAddr, 2, 0xB2B2000000000002ull));''',
     '''    check("S4: C's control-NACK / video-NACK / resume / bulk get NOTHING back on C's leg (no retransmit, no ResumeAck)",
          count_non_standby_rx(c, 1500) == 0);
    check("S4: ...C's resume is ignored as from another peer (no reply, no state)",
          host.log().find("resume ignored id=" + std::to_string(0xC5C50000u)) != std::string::npos, host.tail());
    check("S4: ...B keeps the session: no new epoch, no control-session end, its control still works",
          count_of(host.log(), "session epoch=") == epochsB && count_of(host.log(), "control session ended") == endedB &&
              select_seen(host, b, relay.frontAddr, 2, 0xB2B2000000000002ull));
    uint32_t bSeq = 0;
    check("S4: ...and the host still sends B's control data back down B's own leg",
          raw_wait_ctrl_data(b, remote60::native_poc::kUdpControlStreamHostToClient, 0, 3000, &bSeq));'''),
])
print('ok')
