p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease.hpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


# ---- N2: one untie for every way to a new capability
rep("""      if (peerTied_) {
        // still tied to a session's peer: a fresh socket for the new capability (no read is running: the pump starts
        // none while an intent waits, and the one in flight has ended)
        std::string error;
        peerTied_ = false;
        if (!Reopen(&error)) {
          Note("reopen failed: " + error);
          idle_.notify_all();
          return false;
        }
        Note("socket reopened (tied to a session's peer)");
      }
      observing_ = true;""", """      // no read is running here: the pump starts none while an intent waits, and the one in flight has ended
      if (!UntieForNewCapability()) {
        idle_.notify_all();
        return false;
      }
      observing_ = true;""")
rep("""  void NeedCapability() {
    StandbyCapability cap;
    if (socket_ == kInvalidSocket || !capability_ || !capability_(socket_, &cap) || cap.token.empty() || cap.port == 0) {""",
    """  void NeedCapability() {
    StandbyCapability cap;
    // r2 (Codex codex_ka_e_src_r1.md N2): a synchronous CapabilityFn observes from the socket it is given -- never one
    // still tied to a session's peer (its directory answer would not come back). The pump's read in flight, if any, is
    // on the old socket: a Receive on a closed one only fails, and the next read takes the new one.
    if (!UntieForNewCapability()) {
      needsCapability_ = true;
      return;
    }
    if (socket_ == kInvalidSocket || !capability_ || !capability_(socket_, &cap) || cap.token.empty() || cap.port == 0) {""")
rep("""  void Apply(const StandbyEntryClient::Effects& fx) {""", """  // A new capability is a new lease at the relay (or a new admission at the host) anyway: a socket a session tied to its
  // peer is replaced by a fresh one first -- for every way to one (ObserveFrom for an async owner, NeedCapability for a
  // synchronous CapabilityFn). Steady reuse never comes here: the same endpoint stays. Under mu_. false = no socket.
  bool UntieForNewCapability() {
    if (!peerTied_) return true;
    peerTied_ = false;
    std::string error;
    if (!Reopen(&error)) {
      Note("reopen failed: " + error);
      return false;
    }
    Note("socket reopened (tied to a session's peer)");
    return true;
  }

  void Apply(const StandbyEntryClient::Effects& fx) {""")

# ---- D1: the old session's tail, observed (datagrams from the target that are not standby traffic)
rep("""  static bool Receive(SocketHandle s, uint32_t waitMs, const sockaddr_in* want, UdpStandbyPacket* sb, UdpHelloPacket* ack) {""",
    """  // *stray (optional): a datagram from `want` that is neither -- the video / control of a session that used this socket
  // and that the host still sends (r2 D1: the end of a session's tail is observed, never assumed).
  static bool Receive(SocketHandle s, uint32_t waitMs, const sockaddr_in* want, UdpStandbyPacket* sb, UdpHelloPacket* ack,
                      bool* stray = nullptr) {""")
rep("""    if (!want || from.sin_addr.s_addr != want->sin_addr.s_addr || from.sin_port != want->sin_port) return false;
    uint32_t magic = 0;
    uint16_t kind = 0;
    std::memcpy(&magic, buf, 4);
    std::memcpy(&kind, buf + 4, 2);
    if (magic != kMagic) return false;""", """    if (!want || from.sin_addr.s_addr != want->sin_addr.s_addr || from.sin_port != want->sin_port) return false;
    if (stray) *stray = true;  // from the target; cleared below if it is standby / handshake traffic
    uint32_t magic = 0;
    uint16_t kind = 0;
    std::memcpy(&magic, buf, 4);
    std::memcpy(&kind, buf + 4, 2);
    if (magic != kMagic) return false;""")
rep("""      if (!sb) return false;
      std::memcpy(sb, buf, sizeof(*sb));
      return true;
    }""", """      if (stray) *stray = false;
      if (!sb) return false;
      std::memcpy(sb, buf, sizeof(*sb));
      return true;
    }""")
rep("""    if (kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) && n >= static_cast<int>(sizeof(UdpHelloPacket))) {
      if (!ack) return false;""", """    if (kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) && n >= static_cast<int>(sizeof(UdpHelloPacket))) {
      if (stray) *stray = false;
      if (!ack) return false;""")
rep("""      const bool got = Receive(s, 20, aimed ? &want : nullptr, &p, nullptr);
      TestPoint(kTestReadDone);  // outside the lock: a test registers a handoff here, while the read is "in flight"
      std::lock_guard<std::mutex> lock(mu_);
      reading_ = false;
      idle_.notify_all();""", """      bool stray = false;
      const bool got = Receive(s, 20, aimed ? &want : nullptr, &p, nullptr, &stray);
      TestPoint(kTestReadDone);  // outside the lock: a test registers a handoff here, while the read is "in flight"
      std::lock_guard<std::mutex> lock(mu_);
      reading_ = false;
      idle_.notify_all();
      if (stray) {
        ++strays_;
        lastStrayMs_ = now_();
      }""")
rep("""  struct Snapshot {
    State state = State::Idle;
    uint64_t entryId = 0;
    int lost = 0;
    bool handedOff = false;
    bool needsCapability = false;
  };""", """  struct Snapshot {
    State state = State::Idle;
    uint64_t entryId = 0;
    int lost = 0;
    bool handedOff = false;
    bool needsCapability = false;
    // r2 D1: the old session's tail is still arriving (its video / control from the target reached this socket within
    // kTailQuietMs). An observation of what the host still sends -- not a proof that its grant ended when it stops.
    bool tailActive = false;
    uint64_t strays = 0;  // such datagrams so far (the tail's cost, counted apart)
  };
  static constexpr uint64_t kTailQuietMs = 1500;""")
rep("""    s.handedOff = handedOff_;
    s.needsCapability = needsCapability_;
    return s;
  }""", """    s.handedOff = handedOff_;
    s.needsCapability = needsCapability_;
    s.strays = strays_;
    s.tailActive = !handedOff_ && lastStrayMs_ != 0 && now_() - lastStrayMs_ < kTailQuietMs;
    return s;
  }""")
rep("""  bool peerTied_ = false;         // a session connect()ed this socket to its peer: reopen before aiming elsewhere
""", """  bool peerTied_ = false;         // a session connect()ed this socket to its peer: reopen before aiming elsewhere
  uint64_t strays_ = 0;           // datagrams from the target that were not standby traffic (a session's tail)
  uint64_t lastStrayMs_ = 0;
""")
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

r = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_runner.hpp'
t = open(r, 'rb').read().decode('utf-8')
crlf = '\r\n' in t
t = t.replace('\r\n', '\n')
a = """    bool needsCapability = false;  // ② e: the lease waits for a capability the app gets (Observe, then Provide)
  };"""
assert t.count(a) == 1
t = t.replace(a, """    bool needsCapability = false;  // ② e: the lease waits for a capability the app gets (Observe, then Provide)
    bool tailActive = false;       // r2 D1: the old session's datagrams still arrive (StandbyLease::Snapshot)
    uint64_t strays = 0;
  };""")
a = """    s.status.needsCapability = n.needsCapability;"""
assert t.count(a) == 1
t = t.replace(a, a + """
    s.status.tailActive = n.tailActive;
    s.status.strays = n.strays;""")
if crlf:
    t = t.replace('\n', '\r\n')
open(r, 'wb').write(t.encode('utf-8'))
print('ok')
