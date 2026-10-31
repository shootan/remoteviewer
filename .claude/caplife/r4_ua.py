p = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\host_capture_lifecycle_ua_e2e_test.cpp'
NL = chr(92) + 'n'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''//               phases above.
//
//   remote60_host_capture_lifecycle_ua_e2e_test''',
    '''//               phases above.
//   --fullframe-fails (r4, Codex N1-a FullFrame): a still WGC desktop (GNLINK_STREAM_TEST_WGC_STILL=1: each WGC
//               attachment hands on its first frame only) whose full-frame reacquire -- the real WGC restart --
//               fails after its detach. (C) one failure, then the restart works: the same episode's next ask
//               brings the picture back. (A) it keeps failing: the first try and its retries are the
//               episode's asks -- 3 restarts in all, then none (not the generic retry, not a new kick),
//               no picture. (B) a new session (a new source) gets a new bounded budget and the picture back.
//
//   remote60_host_capture_lifecycle_ua_e2e_test''')

rep('''    if (std::wstring(argv[i]) == L"--rebuild-fails") rebuildFails = true;''',
    '''    if (std::wstring(argv[i]) == L"--rebuild-fails") rebuildFails = true;
    if (std::wstring(argv[i]) == L"--fullframe-fails") fullFrameFails = true;''')
rep('''  bool rebuildFails = false;''', '''  bool rebuildFails = false;
  bool fullFrameFails = false;  // r4''')
rep('''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_FAIL_RESTART_FILE", rebuildFails ? failRestartPath.c_str() : nullptr);''',
    '''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_FAIL_RESTART_FILE",
                            (rebuildFails || fullFrameFails) ? failRestartPath.c_str() : nullptr);
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_WGC_STILL", fullFrameFails ? L"1" : nullptr);''')

block = '''  // ------------------------------------------------------------------ r4: a full-frame reacquire whose restart fails
  if (launched && fullFrameFails) {
    std::cout << "''' + NL + '''--- r4 (N1-a FullFrame): the full-frame WGC restart fails after its detach ---''' + NL + '''";
    SeenLog seen;
    DecodingSink fs;
    fs.log = &seen;
    const auto connect = [&](ClientSessionController& phone) {
      ClientSessionConnectArgs a;
      a.host = "127.0.0.1";
      a.videoPort = hostPort;
      a.controlPort = hostPort;
      a.requireUdpHello = true;
      a.requireTcpControl = false;
      a.controlOverUdp = true;
      a.controlIntervalMs = 200;
      a.udpHandshakeTimeoutMs = 20000;
      a.encodedFrameSink = &fs;
      return phone.Connect(a) &&
             wait_until([&] { return phone.Snapshot().state == ClientSessionState::Connected; }, 25000);
    };
    const auto injected = [&] { return host_count("capture restart failed (injected)"); };
    const auto restarts = [&] { return host_count("full-frame wgc restart ok="); };
    // A picker-like off/on: frames that arrive while off clear the cache; the still WGC gives none after.
    const auto off_on = [&](ClientSessionController& phone) {
      phone.RequestStreamActive(false);
      Sleep(2000);
      phone.RequestStreamActive(true);
    };
    {
      ClientSessionController phone;
      check("the shared session connects", connect(phone), phone.Snapshot().status);
      wait_until([&] { return phone.HostAnswersDesktopBackendStatus(); }, 5000);
      check("it asks for WGC and the host runs it",
            phone.RequestDesktopCaptureBackend(2) && host_says("desktop-backend-applied seq=", 8000) &&
                host_log().find("requested=wgc active=wgc") != std::string::npos);
      check("...its first WGC frame is decoded (the still WGC gives nothing after it)",
            wait_until([&] { return fs.decoded > 0; }, 8000));
      Sleep(1500);

      // (C) one failure, then the restart works: the same episode's next ask brings the picture back.
      set_fail_restart(true);
      const size_t injC = injected();
      const size_t restC = restarts();
      const uint64_t tC = qpc_now_us();
      off_on(phone);
      check("(C) the full-frame WGC restart was asked and FAILED AFTER ITS DETACH",
            wait_until([&] { return injected() > injC; }, 4000) && host_says("full-frame wgc restart ok=0", 1000));
      set_fail_restart(false);
      check("(C) THE SAME EPISODE'S NEXT ASK RESTARTED IT (ok=1) -- no generic retry needed",
            host_says("full-frame wgc restart ok=1", 4000) && host_count("capture retry recovered") == 0);
      check("(C) ...and the picture comes back (a real capture after the pick, decoded)",
            wait_until([&] { return seen.RealCapturedAtOrAfter(tC); }, 5000));
      check("(C) ...within the episode's bound (2 restarts: the failed one + the one that worked)",
            restarts() - restC == 2, std::to_string(restarts() - restC) + " restarts");
      Sleep(1500);

      // (A) it keeps failing: 3 restarts in all for the episode, then none.
      set_fail_restart(true);
      const size_t injA = injected();
      const size_t restA = restarts();
      const size_t recA = host_count("capture retry recovered");
      off_on(phone);
      const uint64_t tA = qpc_now_us();
      const bool three = wait_until([&] { return restarts() - restA >= 3; }, 8000);
      Sleep(12000);  // past the generic retry's backoff (250 ms << 5 = 8 s) and several kicks
      const size_t restN = restarts() - restA;
      const size_t injN = injected() - injA;
      check("(A) THE EPISODE'S RESTARTS STOP AT 3 (first try + its retries), NONE AFTER (12 s: no generic retry, no kick)",
            three && restN == 3 && injN == 3 && host_count("capture retry recovered") == recA &&
                host_count("full-frame ask=4/") == 0,
            std::to_string(restN) + " full-frame restarts, " + std::to_string(injN) + " restarts tried");
      const size_t pics = seen.CountSince(tA + 500000ULL);
      check("(A) no picture meanwhile (nothing captured, nothing re-served)", pics == 0, std::to_string(pics) + " pictures");
      check("(A) the session is still up", phone.Snapshot().state == ClientSessionState::Connected);
      set_fail_restart(false);
      phone.Disconnect();
    }
    Sleep(1500);
    // (B) a new session is a new source: a new bounded budget, and the picture back.
    {
      ClientSessionController phone;
      const uint64_t tB = qpc_now_us();
      check("(B) a new session connects", connect(phone), phone.Snapshot().status);
      check("(B) A NEW SOURCE GETS ITS PICTURE BACK (a real capture after it, decoded)",
            wait_until([&] { return seen.RealCapturedAtOrAfter(tB); }, 10000));
      check("(B) ...with no 4th ask in any episode", host_count("full-frame ask=4/") == 0);
      phone.Disconnect();
    }
  }

'''
anchor = '  // ------------------------------------------------------------------ UA-1 (WGC): the shared session'
rep(anchor, block + anchor)
rep('''  if (launched && !rebuildFails) {''', '''  if (launched && !rebuildFails && !fullFrameFails) {''')
rep('''  if (launched && !rebuildFails && host_alive()) {''', '''  if (launched && !rebuildFails && !fullFrameFails && host_alive()) {''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
