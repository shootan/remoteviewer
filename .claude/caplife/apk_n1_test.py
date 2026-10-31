S = 'C:/work/gnlink-wt/apk-prelogin-select/apps/native_poc/'
p = S + 'src/client_session_prelogin_select_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep('''// P7: a new connection.''', '''// P8 (apk-0232 r2, Codex N1): a queued pick A turned local in the sink and held there (test hook) -- meanwhile the
// user cancels it and picks B -- then A goes on. B, completed by its own IDR, must show selected with ITS generation;
// A's late turn must not take B's tracking. And: A cancelled with no new pick completes nothing afterwards.
void p8_stale_turn() {
  std::printf("\\n--- P8: a queued pick turned local late does not take a newer pick's tracking ---\\n");
  for (const bool pickB : {true, false}) {
    FakeHost h;
    h.pongFlags = kPrelogin;
    h.holdPongs = true;
    if (!h.Start()) return check("P8: fake host starts", false);
    n::ClientSessionController c;
    GateSink sink;
    check(pickB ? "P8: the session connects (Pongs held)" : "P8 (cancel only): the session connects", connect_to(c, h, &sink));
    std::atomic<bool> entered{false}, release{false};
    c.SetTestAfterLocalSinkBeginHook([&](uint64_t tag) {
      if (tag != 81) return;
      entered = true;
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
      while (!release.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
    sink.Prepare(81);
    c.RequestDesktopMode();  // queued: the role is not known yet
    h.holdPongs = false;
    const bool held = wait_until([&] { return entered.load(); }, 4000);
    check(pickB ? "P8: A was turned local in the sink and is held there" : "P8 (cancel only): A held after its sink turn", held);
    std::thread ui;
    if (pickB) {
      // the user: back out of A, pick B (prepare + desktop pick) -- on the UI's own thread
      sink.Prepare(82);
      ui = std::thread([&] { c.RequestDesktopMode(); });
    } else {
      sink.Abort();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    release = true;
    if (ui.joinable()) ui.join();
    c.SetTestAfterLocalSinkBeginHook(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    sink.NewDecoder();
    sink.Feed(910, true);
    if (pickB) {
      const bool acked = wait_until([&] { return verdict(status_of(c)) == Verdict::Acked; }, 2500);
      check("P8: B, completed by its own IDR, shows selected with ITS generation (A's late turn took nothing of B's)",
            acked && c.WindowPanelSnapshotCopy().lastSelectStreamGeneration == 910 && sink.ready() == 82,
            status_of(c) + " gen=" + std::to_string(c.WindowPanelSnapshotCopy().lastSelectStreamGeneration));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      check("P8 (cancel only): A, cancelled, completes nothing afterwards", verdict(status_of(c)) != Verdict::Acked && sink.ready() == 0,
            status_of(c));
    }
    check(pickB ? "P8: no WindowSelect reached the logon-screen host" : "P8 (cancel only): no WindowSelect on the wire",
          h.Count(MessageType::ControlWindowSelect) == 0);
    c.Disconnect();
    h.Stop();
  }
}

// P7: a new connection.''')
rep('''  p7_reconnect();
''', '''  p7_reconnect();
  p8_stale_turn();
''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

q = S + 'CMakeLists.txt'
t = open(q, 'rb').read().decode('utf-8')
crlf2 = '\r\n' in t
t = t.replace('\r\n', '\n')
o = '''target_include_directories(remote60_client_session_prelogin_select_test PRIVATE src)
target_link_libraries(remote60_client_session_prelogin_select_test PRIVATE common ws2_32)
'''
assert t.count(o) == 1
t = t.replace(o, o + '''# apk-0232 r2: the N1 latch (SetTestAfterLocalSinkBeginHook) -- this test only; the session in every product target
# compiles without it.
target_compile_definitions(remote60_client_session_prelogin_select_test PRIVATE REMOTE60_CLIENT_SESSION_TEST_HOOKS=1)
''')
if crlf2:
    t = t.replace('\n', '\r\n')
open(q, 'wb').write(t.encode('utf-8'))
print('ok')
