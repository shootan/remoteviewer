p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease.hpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


i = s.index("  // ② e (AVD ke_r3): the session connect()s its duplicate to its peer")
j = s.index("  // Another handle to the same socket (the same local endpoint); closing one leaves the other.")
s = s[:i] + """  // ② e (AVD ke_r3): the session connect()s its duplicate to its peer, and the association is the SOCKET's (a POSIX
  // dup, and a WSADuplicateSocket duplicate alike): when the lease takes its socket back it is dissolved, so the lease's
  // own datagrams go where IT aims them again. POSIX: connect(AF_UNSPEC). Windows cannot un-connect a UDP socket
  // (INADDR_ANY:0 -> WSAEADDRNOTAVAIL, AF_UNSPEC -> WSAEAFNOSUPPORT, measured): false -- the socket stays tied to that
  // peer, which is still the lease's own target (its Pings go on); before it aims anywhere else (an observation for a
  // new capability) the lease opens a fresh socket (ObserveFrom), as a new capability's lease is a new one anyway.
  static bool DissolvePeer(SocketHandle s) {
    if (s == kInvalidSocket) return true;
#if defined(_WIN32)
    return false;
#else
    sockaddr none{};
    none.sa_family = AF_UNSPEC;
    return connect(s, &none, sizeof(none)) == 0;
#endif
  }

""" + s[j:]

rep("""    if (sessionHandle) close_socket(sessionHandle);
    handedOff_ = false;
    DissolvePeer(socket_);
    return true;""", """    if (sessionHandle) close_socket(sessionHandle);
    handedOff_ = false;  // never connected: the session's worker (which connect()s it) did not start
    return true;""")
rep("""    DissolvePeer(socket_);  // the session connect()ed its duplicate to its peer: that stays on the shared socket
""", """    // the session connect()ed its duplicate to its peer: that stays on the shared socket unless dissolved
    if (!DissolvePeer(socket_)) peerTied_ = true;
""")
rep("""      --intents_;
      if (socket_ == kInvalidSocket || handedOff_) {
        idle_.notify_all();
        return false;
      }
      observing_ = true;
      s = socket_;""", """      --intents_;
      if (socket_ == kInvalidSocket || handedOff_) {
        idle_.notify_all();
        return false;
      }
      if (peerTied_) {
        // still tied to a session's peer (Windows): a fresh socket for the new capability (no read is running: the
        // pump starts none while an intent waits, and the one in flight has ended)
        std::string error;
        peerTied_ = false;
        if (!Reopen(&error)) {
          Note("reopen failed: " + error);
          idle_.notify_all();
          return false;
        }
        Note("socket reopened (tied to a session's peer)");
      }
      observing_ = true;
      s = socket_;""")
rep("""  bool observing_ = false;        // ObserveFrom reads socket_ (outside the lock)
""", """  bool observing_ = false;        // ObserveFrom reads socket_ (outside the lock)
  bool peerTied_ = false;         // a session's connect() could not be dissolved: reopen before aiming elsewhere
""")
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
assert 'EXPERIMENT' not in s
print('ok')
