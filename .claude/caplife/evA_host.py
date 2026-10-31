p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/native_poc/src/host_control_session.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''  bool firstPongDelayed = false;
#endif
  // Keys and buttons held from an earlier client are not this one's;''', '''  bool firstPongDelayed = false;
  // TEST BUILD ONLY (apk-0232 evidence A, Codex codex_apk0232_evidence_design.md): GNLINK_STREAM_TEST_PRELOGIN_ROLE=1
  // makes this ordinary (non --prelogin) host ADVERTISE the logon-screen role (kCaptureFlagPreloginV1) to a manually
  // connected viewer -- so the real APK's local desktop selection runs -- while it holds the prelogin control
  // allowlist as a TRIPWIRE: a window list / select / thumbnail (anything the logon screen refuses) or an effectful
  // logon-screen message (credential, unlock, reboot, Ctrl+Alt+Del) is logged FORBIDDEN and the link is closed, never
  // answered. Nothing of the real worker changes: --prelogin, its directory authentication, its capture. Default off.
  const bool testPreloginRole = [] {
    wchar_t v[8] = {};
    return GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_PRELOGIN_ROLE", v, 8) > 0 && v[0] == L'1';
  }();
  if (testPreloginRole) {
    std::cout << "[native-video-host][test-seam] prelogin-role advertised (test build; allowlist tripwire on)\\n";
  }
#endif
  // Keys and buttons held from an earlier client are not this one's;''')

rep('''    if (args.preloginMode && !remote60::native_poc::prelogin::IsControlAllowedInPrelogin(type)) {
      std::cout << "[native-video-host][control][prelogin] rejected control type="
                << static_cast<unsigned>(header.type) << "\\n";
      break;
    }
''', '''    if (args.preloginMode && !remote60::native_poc::prelogin::IsControlAllowedInPrelogin(type)) {
      std::cout << "[native-video-host][control][prelogin] rejected control type="
                << static_cast<unsigned>(header.type) << "\\n";
      break;
    }
#ifdef REMOTE60_STREAM_TEST_SEAM
    if (testPreloginRole) {
      const bool effectful = type == MessageType::ControlUnlockChallengeRequest ||
                             type == MessageType::ControlUnlockSealedRequest ||
                             type == MessageType::ControlUnlockStatusRequest ||
                             type == MessageType::ControlRebootRequest ||
                             type == MessageType::ControlPreloginControl;
      if (!remote60::native_poc::prelogin::IsControlAllowedInPrelogin(type) || effectful) {
        std::cout << "[native-video-host][test-seam] prelogin-role FORBIDDEN control type="
                  << static_cast<unsigned>(header.type) << " (closing the link)\\n";
        break;
      }
    }
#endif
''')

rep('''      if (args.preloginMode) {
        // Serving the pre-login secure desktop right now (prelogin-service r3 R1: the momentary signal).''',
    '''#ifdef REMOTE60_STREAM_TEST_SEAM
      // apk-0232 evidence A: the role only (the momentary bit) -- not durable-enabled, not SAS
      if (testPreloginRole) pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagPreloginV1;
#endif
      if (args.preloginMode) {
        // Serving the pre-login secure desktop right now (prelogin-service r3 R1: the momentary signal).''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

g = 'C:/work/gnlink-wt/apk-prelogin-select/automation/gnlink_check_fixed_server.py'
t = open(g, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in t else '\n'
t = t.replace('\r\n', '\n')
old = "    'GNLINK_VIEWER_TEST_SEAL_FAIL',\n]"
assert t.count(old) == 1
t = t.replace(old, "    'GNLINK_VIEWER_TEST_SEAL_FAIL',\n    'GNLINK_STREAM_TEST_PRELOGIN_ROLE',  # apk-0232 evidence A: the test host's logon-screen role\n]")
open(g, 'wb').write(t.replace('\n', nl).encode('utf-8'))
print('ok')
