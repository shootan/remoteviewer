p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_managed_e2e_test.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('''    const int back = a.WaitMsg(Type::State, from3, 30000, [](const Message& m) { return m.get("state") == "ready"; });
    check("[" + op + "] ...and the SAME lease is ready again", back >= 0);''',
    '''    // Not "ready" at once: the Host ends the session only after 10 s of a silent control channel, and until then
    // this device's entry still holds it (model A: a Pong is not the grant coming back).
    const uint64_t parkedAt = now_ms();
    const int ret = a.WaitMsg(Type::State, from3, 5000, [](const Message& m) { return m.get("state") == "returning"; });
    check("[" + op + "] ...the lease says \\"returning\\" first, not ready", ret >= 0);
    const int back = a.WaitMsg(Type::State, from3, 40000, [](const Message& m) { return m.get("state") == "ready"; });
    check("[" + op + "] ...and the SAME lease is ready again once the Host can have ended the session", back >= 0 &&
              now_ms() - parkedAt >= 10000,
          "after " + std::to_string(now_ms() - parkedAt) + " ms");''')
rep('''  check("each activation ran on the lease socket, never an ordinary Hello",
        vt.find("standby activation on the lease socket") != std::string::npos &&
            vt.find("[attempt] hello ok") == std::string::npos);''',
    '''  check("each activation ran on the lease socket by the lease's handshake, never an ordinary Hello",
        vt.find("standby activation on the lease socket") != std::string::npos &&
            vt.find("[attempt] activation ok=1") != std::string::npos && vt.find("[attempt] hello ok") == std::string::npos);
  {
    const std::string hl = hostA.Text();
    size_t sessions = 0, p = 0;
    while ((p = hl.find("first session by activation", p)) != std::string::npos) ++sessions, ++p;
    p = 0;
    while ((p = hl.find("(standby activation", p)) != std::string::npos) ++sessions, ++p;
    check("the Host's sessions all came from activations", sessions >= 2 && hl.find("hello admitted") != std::string::npos,
          std::to_string(sessions) + " activation lines");
  }''')
rep('''    a.Send(Type::Release, {{"reason", "test"}});
    const int bye = a.WaitMsg(Type::Bye, 0, 8000);
    check("Release: the viewer says bye and ends", bye >= 0 && a.proc.Exited(8000));''',
    '''    a.Send(Type::Release, {{"reason", "test"}});
    const int bye = a.WaitMsg(Type::Bye, 0, 8000);
    check("Release: the viewer says bye and ends", bye >= 0 && a.proc.Exited(8000));
    Sleep(500);
    const std::string hl = hostA.Text();
    check("...and the Host took the Release (not refused as Live)", hl.find("release refused") == std::string::npos,
          hl.find("release refused") == std::string::npos ? "" : "release refused seen");''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
