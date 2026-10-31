p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease.hpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''// Activation is a CONNECT MODE of ClientSessionController, not another session type: ActivationArgs() hands the
// lease's socket to the session (ClientSessionConnectArgs::preparedUdpSocket, which the session then owns) with a
// handshake that sends StandbyActivate instead of the Hello and returns only when this activation's own Pong
// (Granted) AND the HelloAck have arrived, in either order (StandbyEntryClient). Everything after is the session's
// ordinary path. Until the session ends the lease does not touch the socket.
//
// Limit (said, not hidden): when the session ends -- or a failed activation closes the socket -- the lease opens a
// NEW socket and keeps the entry by Ping (the host follows an entry's endpoint by its secret, §2.2). Directly that is
// enough; through a relay the lease is bound to the old client endpoint, so the Pings die and the entry is re-admitted
// with a new capability (bounded backoff). Keeping the socket across the session is ③'s.''',
'''// Activation is a CONNECT MODE of ClientSessionController, not another session type: ActivationArgs() hands the
// session a DUPLICATE handle of the lease's socket (ClientSessionConnectArgs::preparedUdpSocket, which the session owns
// and closes) with a handshake that sends StandbyActivate instead of the Hello and returns only when this activation's
// own Pong (Granted) AND the HelloAck have arrived, in either order (StandbyEntryClient). Everything after is the
// session's ordinary path. The lease keeps its own handle and does not read it until the session is over; then it goes
// on with the SAME socket -- the same endpoint, which a relay lease is bound to (§2.7) -- and the entry by Ping.''')

rep('''  bool ActivationArgs(ClientSessionConnectArgs* args) {
    if (client_.state() != State::Parked || socket_ == kInvalidSocket || port_ == 0) return false;''',
'''  bool ActivationArgs(ClientSessionConnectArgs* args) {
    if (client_.state() != State::Parked || socket_ == kInvalidSocket || port_ == 0) return false;
    const SocketHandle sessionHandle = Duplicate(socket_);
    if (sessionHandle == kInvalidSocket) {
      events_.push_back("socket duplicate failed");
      return false;
    }''')
rep('''    args->preparedUdpSocket = socket_;
    socket_ = kInvalidSocket;  // the session's now
    handedOff_ = true;''', '''    args->preparedUdpSocket = sessionHandle;  // the session's handle; the lease keeps its own
    handedOff_ = true;''')
rep('''  // The session this lease activated ended (or its Connect failed): a new socket, and the entry goes on by Ping.
  void SessionOver() {
    handedOff_ = false;
    if (client_.state() == State::Active) client_.SessionEnded(now_());
    std::string error;
    if (!Reopen(&error)) events_.push_back("reopen failed: " + error);
  }''', '''  // The session this lease activated ended (or its Connect failed; the session closed its handle): the entry goes on
  // by Ping on the lease's same socket.
  void SessionOver() {
    handedOff_ = false;
    if (client_.state() == State::Active) client_.SessionEnded(now_());
    if (socket_ == kInvalidSocket) {
      std::string error;
      if (!Reopen(&error)) events_.push_back("reopen failed: " + error);
    }
  }''')
rep('''  bool Aim(sockaddr_in* to) const {''', '''  // Another handle to the same socket (the same local endpoint); closing one leaves the other.
  static SocketHandle Duplicate(SocketHandle s) {
#if defined(_WIN32)
    WSAPROTOCOL_INFOW info{};
    if (WSADuplicateSocketW(s, GetCurrentProcessId(), &info) != 0) return kInvalidSocket;
    return WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &info, 0, WSA_FLAG_OVERLAPPED);
#else
    return dup(s);
#endif
  }

  bool Aim(sockaddr_in* to) const {''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

e = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_admission_e2e_test.cpp'
t = open(e, 'rb').read().decode('utf-8')
c2 = '\r\n' in t
t = t.replace('\r\n', '\n')
for old, new in [
    ('''  check("A parks again on a new socket: its entry answers its Pings", a.state() == StandbyLease::State::Parked &&''',
     '''  check("A parks again on its lease's own socket: its entry answers its Pings", a.state() == StandbyLease::State::Parked &&'''),
    ('''    // the stated limit: A's new socket is not the endpoint its relay lease is bound to -- its Pings die there, the
    // entry is lost (never kept as ready) and re-admitted with a NEW standby connect from the new socket
    const bool readmitted = pump_until(ra, [&] { return ra.lostCount() >= 1 && ra.state() == StandbyLease::State::Parked; }, 45000);
    check("through the relay, A's lease after its session: lost on the new socket, then re-admitted and parked (bounded)",
          readmitted, last_events(ra));''',
     '''    // the lease kept its socket (the session had a duplicate handle): the SAME endpoint its relay lease is bound to --
    // the entry goes on by Ping through the relay, no loss, no new standby connect
    const int connectsBefore = observeSeq;
    pump_until(ra, [] { return false; }, 20000);
    check("through the relay, A's lease after its session: still parked on the same socket -- no loss, no new connect",
          ra.state() == StandbyLease::State::Parked && ra.lostCount() == 0 && observeSeq == connectsBefore, last_events(ra));
    remote60::native_poc::ClientSessionConnectArgs rargsA2;
    rargsA2.encodedFrameSink = &rsinkA;
    rargsA2.controlIntervalMs = 200;
    const uint64_t rframes = rsinkA.frames();
    const bool rupA2 = ra.ActivationArgs(&rargsA2) && rca.Connect(rargsA2) &&
                       wait_until([&] { return rca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
    check("...and takes the session back through the same relay lease (video)", rupA2 &&
          wait_until([&] { return rsinkA.frames() > rframes; }, 10000), rca.Snapshot().lastError + " | " + last_events(ra));
    rca.Disconnect();'''),
]:
    assert t.count(old) == 1, (t.count(old), old[:60])
    t = t.replace(old, new)
if c2:
    t = t.replace('\n', '\r\n')
open(e, 'wb').write(t.encode('utf-8'))
print('ok')
