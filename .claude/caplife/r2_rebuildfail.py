src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'
NL = chr(92) + 'n'

def edit(name, pairs):
    p = src + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

# ---- host: the fence line carries its QPC time; a test-only seam fails the secure-side rebuilds
edit('host_stage_backend.cpp', [
    ('''    std::cout << "[native-video-host][desktop] edge fence desktopGen="
              << capture.desktopGeneration.load(std::memory_order_acquire) << "''' + NL + '''";''',
     '''    std::cout << "[native-video-host][desktop] edge fence desktopGen="
              << capture.desktopGeneration.load(std::memory_order_acquire) << " atUs=" << qpc_now_us() << "''' + NL + '''";'''),
    ('''        ++backend.desktopRebuildAttempts;
        powerKeepalive.SetStreaming(true, true);
        const bool ok = restart_capture_session(hx);''',
     '''        ++backend.desktopRebuildAttempts;
        powerKeepalive.SetStreaming(true, true);
        bool seamFail = false;
#ifdef REMOTE60_STREAM_TEST_SEAM
        // TEST BUILD ONLY (capture-lifecycle r2): GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL=1 -- every rebuild for an
        // odd desktop generation (the secure side of a round trip from Default) fails, as a rebuild that cannot
        // open its capture would: what crosses an edge whose rebuild never comes.
        {
          const char* v = std::getenv("GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL");
          seamFail = v && std::strcmp(v, "1") == 0 && (dgen % 2) == 1;
        }
#endif
        const bool ok = !seamFail && restart_capture_session(hx);'''),
])

# ---- UA e2e: --rebuild-fails
edit('host_capture_lifecycle_ua_e2e_test.cpp', [
    ('''//   remote60_host_capture_lifecycle_ua_e2e_test   (REMOTE60_ALLOW_HOST_E2E=1; exit 77 = skipped)''',
     '''//   --rebuild-fails (r2, Codex N1 (b)): GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL=1 -- the rebuild after the edge to
//               the secure desktop fails every attempt. Nothing of the attachment made before the edge may
//               reach the viewer (its frames are refused at adoption), the attempts are bounded, and the next
//               edge's rebuild brings the picture back. Instead of the phases above.
//
//   remote60_host_capture_lifecycle_ua_e2e_test   (REMOTE60_ALLOW_HOST_E2E=1; exit 77 = skipped)'''),
    ('''int wmain(int, wchar_t**) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);''',
     '''int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  bool rebuildFails = false;
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--rebuild-fails") rebuildFails = true;
  }'''),
    ('''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT", secureEventName.c_str());''',
     '''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT", secureEventName.c_str());
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL", rebuildFails ? L"1" : nullptr);'''),
    ('''  // ------------------------------------------------------------------ UA-1 (WGC): the shared session
  SeenLog phoneSeen;
  DecodingSink sink;
  sink.log = &phoneSeen;
  if (launched) {''',
     '''  // ------------------------------------------------------------------ r2: an edge whose rebuild fails
  if (launched && rebuildFails) {
    std::cout << "''' + NL + '''--- r2 (N1 b): the rebuild after the edge fails; nothing of the old attachment crosses ---''' + NL + '''";
    SeenLog seen;
    DecodingSink rs;
    rs.log = &seen;
    ClientSessionController phone;
    ClientSessionConnectArgs a;
    a.host = "127.0.0.1";
    a.videoPort = hostPort;
    a.controlPort = hostPort;
    a.requireUdpHello = true;
    a.requireTcpControl = false;
    a.controlOverUdp = true;
    a.controlIntervalMs = 200;
    a.udpHandshakeTimeoutMs = 20000;
    a.encodedFrameSink = &rs;
    const bool up = phone.Connect(a) &&
                    wait_until([&] { return phone.Snapshot().state == ClientSessionState::Connected; }, 25000);
    check("the shared session connects", up, phone.Snapshot().status);
    check("...and decodes the desktop", wait_until([&] { return rs.decoded > 0; }, 15000));
    wait_until([&] { return phone.HostAnswersDesktopBackendStatus(); }, 5000);
    check("it asks for WGC (a capture that keeps giving frames of this live desktop), and the host runs it",
          phone.RequestDesktopCaptureBackend(2) && host_says("desktop-backend-applied seq=", 8000));
    Sleep(2000);
    check("before the edge, the picture comes (frames in 1 s)", [&] {
      const uint64_t t = qpc_now_us();
      Sleep(1000);
      return seen.CountSince(t) >= 2;
    }());
    SetEvent(secure);
    check("the host saw the input desktop go secure", host_says("input desktop Default -> Winlogon", 3000));
    const uint64_t gen = last_edge_gen("input desktop Default -> Winlogon");
    check("THE MEDIA FENCE CAME AT THE EDGE, before any rebuild (\\"edge fence desktopGen=G\\")",
          host_says("edge fence desktopGen=" + std::to_string(gen) + " atUs=", 2000) &&
              host_log().find("edge fence desktopGen=" + std::to_string(gen)) <
                  host_log().find("rebuild desktopGen=" + std::to_string(gen) + " attempt=1"),
          "desktopGen=" + std::to_string(gen));
    uint64_t fenceUs = 0;
    {
      const std::string log = host_log();
      const size_t at = log.find("edge fence desktopGen=" + std::to_string(gen) + " atUs=");
      if (at != std::string::npos) fenceUs = std::strtoull(log.c_str() + log.find("atUs=", at) + 5, nullptr, 10);
    }
    const bool gaveUp = host_says("rebuild gave up desktopGen=" + std::to_string(gen), 6000);
    check("the rebuild failed, BOUNDED: 3 attempts, then it gave up (no loop)",
          gaveUp && host_count("rebuild desktopGen=" + std::to_string(gen) + " attempt=3 ok=0") == 1 &&
              host_count("rebuild desktopGen=" + std::to_string(gen) + " attempt=4") == 0);
    check("the old attachment's frames were REFUSED at adoption (\\"publish refused reason=provenance\\")",
          host_count("publish refused reason=provenance") >= 1);
    Sleep(1500);
    // An AU whose first datagram left before the fence finishes (the sender's contract, P11) -- 500 ms for it.
    const uint64_t quietFrom = fenceUs + 500000ULL;
    const size_t after = seen.CountSince(quietFrom);
    check("NOTHING OF THE OLD ATTACHMENT REACHES THE VIEWER after the edge (0 pictures decoded from fence+500 ms)",
          fenceUs != 0 && after == 0, std::to_string(after) + " pictures; fenceUs=" + std::to_string(fenceUs));
    check("the session is still up (no reconnect)", phone.Snapshot().state == ClientSessionState::Connected);
    const uint64_t tBack = qpc_now_us();
    ResetEvent(secure);
    check("the host saw the input desktop come back to Default", host_says("input desktop Winlogon -> Default", 3000));
    const uint64_t backGen = last_edge_gen("input desktop Winlogon -> Default");
    check("...and THAT edge's rebuild succeeded",
          host_says("rebuild desktopGen=" + std::to_string(backGen) + " attempt=1 ok=1", 4000));
    const bool resumed = wait_until([&] { return seen.RealCapturedAtOrAfter(tBack); }, 5000);
    check("THE PICTURE COMES BACK on the same connection (a real capture after the return, decoded)", resumed);
    phone.Disconnect();
  }

  // ------------------------------------------------------------------ UA-1 (WGC): the shared session
  SeenLog phoneSeen;
  DecodingSink sink;
  sink.log = &phoneSeen;
  if (launched && !rebuildFails) {'''),
    ('''  if (launched && host_alive()) {
    std::cout << "''' + NL + '''--- UA-2: the PC viewer connects next (it asks for no backend) ---''' + NL + '''";''',
     '''  if (launched && !rebuildFails && host_alive()) {
    std::cout << "''' + NL + '''--- UA-2: the PC viewer connects next (it asks for no backend) ---''' + NL + '''";'''),
])
print('ok')
