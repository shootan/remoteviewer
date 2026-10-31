p = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\host_capture_lifecycle_ua_e2e_test.cpp'
NL = chr(92) + 'n'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''//   --rebuild-fails (r2, Codex N1 (b)): GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL=1 -- the rebuild after the edge to
//               the secure desktop fails every attempt. Nothing of the attachment made before the edge may
//               reach the viewer (its frames are refused at adoption), the attempts are bounded, and the next
//               edge's rebuild brings the picture back. Instead of the phases above.''',
    '''//   --rebuild-fails (r2/r3, Codex N1 (b) / N1-a): the rebuild after the edge to the secure desktop fails AFTER
//               its detach, as a real one does (GNLINK_STREAM_TEST_FAIL_RESTART_FILE: while that file exists every
//               capture restart fails right after DetachCaptureSession). (1) one failure, the file goes: the
//               SAME desktop generation's 2nd attempt succeeds and the picture comes back. (2) the file stays:
//               3 attempts, then nothing -- not the rebuild, not the host's generic retry (4th attempt 0) -- and
//               no picture; the next edge (file gone) rebuilds and the picture comes back. Instead of the
//               phases above.''')

rep('''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL", rebuildFails ? L"1" : nullptr);''',
    '''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_FAIL_RESTART_FILE", rebuildFails ? failRestartPath.c_str() : nullptr);''')

rep('''  const std::wstring hostLogPath = dir + L"host.log";''',
    '''  const std::wstring hostLogPath = dir + L"host.log";
  // r3: while this file exists, every capture restart of the test host fails after its detach (--rebuild-fails).
  const std::wstring failRestartPath = dir + L"fail-restart.flag";
  const auto set_fail_restart = [&](bool on) {
    if (on) {
      HANDLE f = CreateFileW(failRestartPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    } else {
      DeleteFileW(failRestartPath.c_str());
    }
  };''')

start = s.index('  // ------------------------------------------------------------------ r2: an edge whose rebuild fails')
end = s.index('  // ------------------------------------------------------------------ UA-1 (WGC): the shared session')
block = '''  // ------------------------------------------------------------------ r2/r3: an edge whose rebuild fails
  if (launched && rebuildFails) {
    std::cout << "''' + NL + '''--- r3 (N1-a): the rebuild after the edge fails after its detach ---''' + NL + '''";
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
    const auto injected = [&] { return host_count("capture restart failed (injected)"); };

    // (1) one failure after the detach, then the restart works again: the same generation's 2nd attempt.
    set_fail_restart(true);
    SetEvent(secure);
    check("(1) the host saw the input desktop go secure", host_says("input desktop Default -> Winlogon", 3000));
    const uint64_t gen1 = last_edge_gen("input desktop Default -> Winlogon");
    const std::string r1 = "rebuild desktopGen=" + std::to_string(gen1);
    check("(1) THE MEDIA FENCE CAME AT THE EDGE, before any rebuild",
          host_says("edge fence desktopGen=" + std::to_string(gen1) + " atUs=", 2000) &&
              host_log().find("edge fence desktopGen=" + std::to_string(gen1)) < host_log().find(r1 + " attempt=1"));
    check("(1) the 1st attempt failed AFTER its detach (the injected restart failure)",
          host_says(r1 + " attempt=1 ok=0", 3000) && injected() >= 1, std::to_string(injected()) + " injected");
    set_fail_restart(false);
    check("(1) THE SAME GENERATION'S 2nd ATTEMPT SUCCEEDED (a failed detach is not 'rebuilt')",
          host_says(r1 + " attempt=2 ok=1", 4000));
    const uint64_t t1 = qpc_now_us();
    check("(1) ...and the picture comes back on the same connection",
          wait_until([&] { return seen.RealCapturedAtOrAfter(t1); }, 5000));
    ResetEvent(secure);
    check("(1) back to Default: rebuilt", host_says("input desktop Winlogon -> Default", 3000) &&
                                            host_says("rebuild desktopGen=" + std::to_string(gen1 + 1) + " attempt=1 ok=1", 4000));
    Sleep(2000);

    // (2) the restart keeps failing: 3 attempts, then nothing more -- the generic retry does not take over.
    set_fail_restart(true);
    const size_t injectedBefore = injected();
    const size_t recoveredBefore = host_count("capture retry recovered");
    SetEvent(secure);
    check("(2) the host saw the input desktop go secure", host_says("input desktop Default -> Winlogon", 3000));
    const uint64_t gen2 = last_edge_gen("input desktop Default -> Winlogon");
    const std::string r2 = "rebuild desktopGen=" + std::to_string(gen2);
    uint64_t fenceUs = 0;
    if (host_says("edge fence desktopGen=" + std::to_string(gen2) + " atUs=", 2000)) {
      const std::string log = host_log();
      const size_t at = log.find("edge fence desktopGen=" + std::to_string(gen2) + " atUs=");
      fenceUs = std::strtoull(log.c_str() + log.find("atUs=", at) + 5, nullptr, 10);
    }
    check("(2) 3 attempts failed after their detach, then the rebuild gave up",
          host_says("rebuild gave up desktopGen=" + std::to_string(gen2), 8000) &&
              host_count(r2 + " attempt=3 ok=0") == 1,
          std::to_string(injected() - injectedBefore) + " injected");
    Sleep(10000);  // past the generic retry's backoff (250 ms << 5 = 8 s)
    check("(2) NOTHING MORE FOR 10 s: no 4th attempt, the generic retry did not take over (3 restarts in all)",
          host_count(r2 + " attempt=4") == 0 && injected() - injectedBefore == 3 &&
              host_count("capture retry recovered") == recoveredBefore,
          std::to_string(injected() - injectedBefore) + " restarts tried");
    const size_t after = fenceUs != 0 ? seen.CountSince(fenceUs + 500000ULL) : 1;
    check("(2) NOTHING OF THE OLD ATTACHMENT REACHES THE VIEWER after the edge (0 pictures from fence+500 ms)",
          fenceUs != 0 && after == 0, std::to_string(after) + " pictures; fenceUs=" + std::to_string(fenceUs));
    check("(2) the session is still up (no reconnect)", phone.Snapshot().state == ClientSessionState::Connected);
    set_fail_restart(false);
    const uint64_t tBack = qpc_now_us();
    ResetEvent(secure);
    check("(2) back to Default (file gone): THAT edge's rebuild succeeded",
          host_says("input desktop Winlogon -> Default", 3000) &&
              host_says("rebuild desktopGen=" + std::to_string(gen2 + 1) + " attempt=1 ok=1", 4000));
    check("(2) THE PICTURE COMES BACK on the same connection",
          wait_until([&] { return seen.RealCapturedAtOrAfter(tBack); }, 5000));
    phone.Disconnect();
  }

'''
s = s[:start] + block + s[end:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
