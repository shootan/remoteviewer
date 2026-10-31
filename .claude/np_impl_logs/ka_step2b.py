import os
R = r'C:\work\gnlink-wt\ka-pc\apps\native_poc'
rel_now = ''

def load(rel):
    b = open(os.path.join(R, rel), 'rb').read().decode('utf-8')
    return b.replace('\r\n', '\n'), ('\r\n' in b)

def save(rel, s, crlf):
    open(os.path.join(R, rel), 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

def rep(s, a, b, cnt=1):
    assert s.count(a) == cnt, (rel_now, s.count(a), a[:90])
    return s.replace(a, b)

rel_now = r'src\viewer_context.hpp'
s, c = load(rel_now)
s = rep(s, '#include "viewer_video_receiver.hpp"\n', '#include "viewer_video_receiver.hpp"\n#include "native_video_client_session.hpp"\n')
s = rep(s, """  std::thread cancelWatcher;
  std::atomic<bool> cancelWatcherStop{false};
};""", """  std::thread cancelWatcher;
  std::atomic<bool> cancelWatcherStop{false};

  /**
   * ka-pc-impl r1 (KA ③): this session is a standby lease's ACTIVATION (the managed viewer, after a tap). The
   * arguments come from StandbyRunner::TakeActivation and are used once: open_media_socket takes their socket
   * (a duplicate of the lease's) instead of the directory, and connect_media_socket runs their handshake instead
   * of a Hello. `standbyTaken` says the socket was taken -- the caller then owes the lease SessionOver after the
   * session's threads joined; otherwise Abandon. Null: an ordinary session.
   */
  remote60::native_poc::ClientSessionConnectArgs* standby = nullptr;
  bool standbyTaken = false;
};""")
save(rel_now, s, c)

rel_now = r'src\viewer_startup.cpp'
s, c = load(rel_now)
s = rep(s, """int open_media_socket(ViewerContext& ctx) {
  // Reaching the host through the directory replaces the address entirely: the socket comes back
  // already prepared (it is the one the directory observed, so the host is punching towards it)
  // and the capability that follows is what the host authorises the session against.
  ctx.resolvedArgs = ctx.args;""", """int open_media_socket(ViewerContext& ctx) {
  // Reaching the host through the directory replaces the address entirely: the socket comes back
  // already prepared (it is the one the directory observed, so the host is punching towards it)
  // and the capability that follows is what the host authorises the session against.
  ctx.resolvedArgs = ctx.args;
  // ka-pc-impl r1 (KA ③): a standby lease's activation. Its socket is the lease's (duplicated): no directory,
  // no observe, no punch -- the address is the one the lease is admitted at (the Host, or a relay lease).
  if (ctx.standby) {
    if (ctx.standbyTaken || ctx.standby->preparedUdpSocket == INVALID_SOCKET) {
      std::cerr << "[native-video-client] standby activation arguments already used\\n";
      return 3;
    }
    ctx.session.sock = ctx.standby->preparedUdpSocket;
    ctx.standby->preparedUdpSocket = INVALID_SOCKET;  // the session owns this handle now
    ctx.standbyTaken = true;
    ctx.session.sockSharedWithLease = true;
    ctx.resolvedArgs.host = ctx.standby->host;
    ctx.resolvedArgs.port = ctx.standby->videoPort;
    ctx.resolvedArgs.controlPort = 0;  // control rides the media socket, as on the directory path
    ctx.directoryPunchToken.clear();
    std::cout << "[native-video-client] standby activation on the lease socket -> " << ctx.standby->host << ":"
              << ctx.standby->videoPort << "\\n";
    return 0;
  }""")
s = rep(s, """    const bool handshakeOk = remote60::native_poc::udp_hello_handshake(
        ctx.session.sock, hello, &ctx.connectCancelled, &helloError, &ackFeatures,
        &helloStats);""", """    // ka-pc-impl r1: an activation replaces the Hello with the lease's handshake (StandbyActivate, then the
    // current generation's Pong + HelloAck); everything after it is the same.
    const bool handshakeOk =
        (ctx.standby && ctx.standby->udpHandshake)
            ? ctx.standby->udpHandshake(ctx.session.sock, ctx.connectCancelled, &helloError, &ackFeatures)
            : remote60::native_poc::udp_hello_handshake(ctx.session.sock, hello, &ctx.connectCancelled, &helloError,
                                                        &ackFeatures, &helloStats);""")
save(rel_now, s, c)

# main: managed mode -- no retry dialog for an activation; the window does not outlive the session
rel_now = r'src\native_video_client_main.cpp'
s, c = load(rel_now)
s = rep(s, """    std::string reason;
    if (rc == 3) {""", """    // ka-pc-impl r1: an activation's arguments are used once -- no "retry" here. The managed viewer reports the
    // failure to the shell and goes back to standby; a new attempt is a new tap (Codex: no automatic re-activation).
    if (ctx.standby) {
      viewer_stop_cancel_watcher(&ctx.cancelWatcherStop, &ctx.cancelWatcher);
      return rc;
    }

    std::string reason;
    if (rc == 3) {""")
save(rel_now, s, c)
print('ok')
