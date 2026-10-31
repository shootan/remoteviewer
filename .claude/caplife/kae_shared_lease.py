SRC = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def patch(name, reps):
    p = SRC + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:70], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


patch('standby_lease.hpp', [
("""    args->preparedUdpSocket = sessionHandle;  // the session's handle; the lease keeps its own
""", """    args->preparedUdpSocket = sessionHandle;  // the session's handle; the lease keeps its own
    args->preparedSocketShared = true;        // ② e: never shut down by the session (POSIX: it is this socket too)
"""),
("""    if (op != op_ || !handedOff_ || handshaking_.load(std::memory_order_acquire)) return false;
    if (sessionHandle) close_socket(sessionHandle);
    handedOff_ = false;
    return true;""", """    if (op != op_ || !handedOff_ || handshaking_.load(std::memory_order_acquire)) return false;
    if (sessionHandle) close_socket(sessionHandle);
    handedOff_ = false;
    DissolvePeer(socket_);
    return true;"""),
("""    handedOff_ = false;
    if (client_.state() == State::Active) client_.SessionEnded(now_());""", """    handedOff_ = false;
    DissolvePeer(socket_);  // the session connect()ed its duplicate to its peer: that stays on the shared socket
    if (client_.state() == State::Active) client_.SessionEnded(now_());"""),
("""  // Another handle to the same socket (the same local endpoint); closing one leaves the other.""",
 """  // ② e (AVD ke_r3): the session connect()s its duplicate to its peer, and the association is the SOCKET's (POSIX
  // dup, a WSADuplicateSocket duplicate): dissolved when the lease takes its socket back, so the lease's own datagrams go
  // where IT aims them again -- an observe to the directory, a new target. connect(AF_UNSPEC) on POSIX, connect to
  // INADDR_ANY:0 on Windows.
  static void DissolvePeer(SocketHandle s) {
    if (s == kInvalidSocket) return;
#if defined(_WIN32)
    sockaddr_in any{};
    any.sin_family = AF_INET;
    (void)connect(s, reinterpret_cast<const sockaddr*>(&any), sizeof(any));
#else
    sockaddr none{};
    none.sa_family = AF_UNSPEC;
    (void)connect(s, &none, sizeof(none));
#endif
  }

  // Another handle to the same socket (the same local endpoint); closing one leaves the other."""),
])

patch('standby_lease_test.cpp', [
("""// The connect thread of a session: runs the handshake of `args` as ClientSessionController::Connect would.""",
 """// A UDP endpoint on loopback that answers every datagram with itself (a directory's observe port, for ObserveFrom).
class Echo {
 public:
  ~Echo() { Stop(); }
  bool Start() {
    s_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (bind(s_, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) return false;
    socklen_t l = sizeof(a);
    getsockname(s_, reinterpret_cast<sockaddr*>(&a), &l);
    port = ntohs(a.sin_port);
    t_ = std::thread([this] {
      while (!stop_) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s_, &rd);
        timeval tv{0, 20000};
        if (select(static_cast<int>(s_) + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
        char buf[256];
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        const int n = static_cast<int>(recvfrom(s_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl));
        if (n > 0) {
          ++seen;
          sendto(s_, buf, n, 0, reinterpret_cast<const sockaddr*>(&from), sizeof(from));
        }
      }
    });
    return true;
  }
  void Stop() {
    stop_ = true;
    if (t_.joinable()) t_.join();
    n::close_socket(&s_);
  }
  uint16_t port = 0;
  std::atomic<int> seen{0};

 private:
  n::SocketHandle s_ = n::kInvalidSocket;
  std::thread t_;
  std::atomic<bool> stop_{false};
};

// ② e (AVD ke_r3): the session connect()s the duplicate it got; the lease's socket must not stay tied to that peer.
void shared_socket_test(FakeHost& host) {
  std::printf("\\n--- ② e: the activation's socket is shared; the session's connect() does not stay on the lease ---\\n");
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
  Echo directory;
  check("parked at the fake host; an observe endpoint up", park(lease) && directory.Start());
  n::ClientSessionConnectArgs args;
  uint64_t op = 0;
  check("activation arguments say the socket is shared (the session never shuts it down)",
        lease.ActivationArgs(&args, nullptr, &op) && args.preparedSocketShared);
  // what ClientSessionController::ConnectUdpVideo does with a prepared socket: connect() it to the peer
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_port = htons(host.port);
  inet_pton(AF_INET, "127.0.0.1", &peer.sin_addr);
  const bool connected = connect(args.preparedUdpSocket, reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) == 0;
  n::close_socket(&args.preparedUdpSocket);  // the session ended (its descriptor closed, not shut down)
  check("the session's connect() on its duplicate; its descriptor closed; SessionOver", connected && lease.SessionOver(op));
  const int seen0 = directory.seen;
  check("...an observe to ANOTHER address from the lease's socket works (the association is dissolved)",
        lease.ObserveFrom("127.0.0.1", directory.port, "obs-shared") && directory.seen > seen0,
        "seen=" + std::to_string(directory.seen - seen0));
  const int pings = host.pings;
  const uint64_t until = mono_ms() + 7000;
  while (mono_ms() < until && host.pings == pings) lease.Pump(50);
  check("...and the lease pings its own target again", host.pings > pings && lease.state() == State::Parked);
}

// The connect thread of a session: runs the handshake of `args` as ClientSessionController::Connect would."""),
("""  handoff_priority_test(host);
""", """  handoff_priority_test(host);
  shared_socket_test(host);
"""),
])
print('ok')
