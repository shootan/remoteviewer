SRC = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def patch(name, reps):
    p = SRC + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b, n in reps:
        assert s.count(a) == n, (name, a[:70], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


patch('native_video_client_session.hpp', [
("""  SocketHandle preparedUdpSocket = kInvalidSocket;
""", """  SocketHandle preparedUdpSocket = kInvalidSocket;
  // keepalive-r2 ② e: preparedUdpSocket is a DUPLICATE of a socket someone else keeps using (a standby lease's own,
  // StandbyLease::ActivationArgs). The session then never shuts it down: on POSIX shutdown() acts on the shared socket
  // itself (not on this descriptor), so the lease would lose its endpoint for good (AVD ke_r3: the next activation and
  // every observe after it went nowhere). Its reader leaves on its own receive tick; the descriptor is only closed.
  bool preparedSocketShared = false;
""", 1),
("""  SocketHandle udpVideoSocket_ = kInvalidSocket;
""", """  SocketHandle udpVideoSocket_ = kInvalidSocket;
  bool udpVideoSocketShared_ = false;  // ClientSessionConnectArgs::preparedSocketShared (under mu_)
  void ShutdownVideoSocketLocked();
""", 1),
])

patch('native_video_client_session.cpp', [
("""    shutdown_socket(&tcpControlSocket_);
    shutdown_socket(&udpVideoSocket_);
  }
  if (workerThread_.joinable()) {""", """    shutdown_socket(&tcpControlSocket_);
    ShutdownVideoSocketLocked();
  }
  if (workerThread_.joinable()) {""", 1),
("""  snapshot_.controlLoopActive = false;
  shutdown_socket(&tcpControlSocket_);
  shutdown_socket(&udpVideoSocket_);
}
""", """  snapshot_.controlLoopActive = false;
  shutdown_socket(&tcpControlSocket_);
  ShutdownVideoSocketLocked();
}

// Wakes the video reader by shutting its socket down -- unless that socket is shared with a standby lease
// (preparedSocketShared): then it is left alone (the reader sees stopRequested_ on its next receive tick, <= 100 ms) and
// only this descriptor is closed later (CloseSocketsUnlocked, after the join).
void ClientSessionController::ShutdownVideoSocketLocked() {
  if (udpVideoSocketShared_) return;
  shutdown_socket(&udpVideoSocket_);
}
""", 1),
("""  udpVideoSocket_ = connected;
  snapshot_.transport.udpVideoReady = true;
  return true;
}""", """  udpVideoSocket_ = connected;
  udpVideoSocketShared_ = args.preparedUdpSocket != kInvalidSocket && args.preparedSocketShared;
  snapshot_.transport.udpVideoReady = true;
  return true;
}""", 1),
("""  close_socket(&udpVideoSocket_);
""", """  close_socket(&udpVideoSocket_);
  udpVideoSocketShared_ = false;
""", 1),
])
print('ok')
