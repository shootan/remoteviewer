S = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'

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

edit('directory_client.hpp', [
    ('''  struct PunchTarget {
    uint32_t ipv4NetworkOrder = 0;
    uint16_t port = 0;
    std::string punchToken;
  };''', '''  struct PunchTarget {
    uint32_t ipv4NetworkOrder = 0;
    uint16_t port = 0;
    std::string punchToken;
    // keepalive-r2 r4 (Codex N1): a relay lease's own host-facing socket, not a peer. The punch toward it carries
    // this proof (from this heartbeat only) so the relay pins THIS host's tuple and nobody else's; such a target
    // is no capability -- it is never authorised as a peer.
    std::string hostProof;
  };'''),
])

edit('directory_client.cpp', [
    ('''        std::string punchToken;
        if (json_get_string(entry, "ip", &ip) && json_get_u32(entry, "port", &port) &&
            json_get_string(entry, "punchToken", &punchToken) && punchToken.size() == 32 && port &&
            port <= 65535) {
          in_addr parsed{};
          if (inet_pton(AF_INET, ip.c_str(), &parsed) == 1) {
            outPunch->push_back({parsed.s_addr, static_cast<uint16_t>(port), punchToken});
          }
        }''', '''        std::string punchToken;
        std::string hostProof;
        if (json_get_string(entry, "ip", &ip) && json_get_u32(entry, "port", &port) &&
            json_get_string(entry, "punchToken", &punchToken) && punchToken.size() == 32 && port &&
            port <= 65535) {
          // keepalive-r2 r4: an optional relay host-facing proof (32 hex); anything else is not one.
          if (!json_get_string(entry, "hostProof", &hostProof) || hostProof.size() != 32) hostProof.clear();
          in_addr parsed{};
          if (inet_pton(AF_INET, ip.c_str(), &parsed) == 1) {
            outPunch->push_back({parsed.s_addr, static_cast<uint16_t>(port), punchToken, hostProof});
          }
        }'''),
    ('''    for (const auto& target : targets) {
      authorizedPeers_.push_back(
          AuthorizedPeer{target, now + std::chrono::seconds(30)});
    }''', '''    for (const auto& target : targets) {
      if (!target.hostProof.empty()) continue;  // keepalive-r2 r4: a relay socket of ours, not a peer
      authorizedPeers_.push_back(
          AuthorizedPeer{target, now + std::chrono::seconds(30)});
    }'''),
    ('''  UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
  for (int i = 0; i < kPunchPackets && running_.load(); ++i) {
    for (const auto& t : targets) {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_addr.s_addr = t.ipv4NetworkOrder;
      to.sin_port = htons(t.port);
      send_(&packet, sizeof(packet), to);
    }''', '''  UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
  for (int i = 0; i < kPunchPackets && running_.load(); ++i) {
    for (const auto& t : targets) {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_addr.s_addr = t.ipv4NetworkOrder;
      to.sin_port = htons(t.port);
      // keepalive-r2 r4 (Codex N1): toward a relay lease's host-facing socket, the punch carries that lease's
      // host proof -- what the relay pins this host's tuple by. Every other punch stays empty (a client's token
      // is never put on the wire here).
      UdpHelloPacket p = packet;
      if (!t.hostProof.empty()) std::memcpy(p.authToken, t.hostProof.data(), (std::min)(t.hostProof.size(), sizeof(p.authToken) - 1));
      send_(&p, sizeof(p), to);
    }'''),
])
print('ok')
