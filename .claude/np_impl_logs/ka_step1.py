import os
R = r'C:\work\gnlink-wt\ka-pc\apps\native_poc'

def load(rel):
    b = open(os.path.join(R, rel), 'rb').read().decode('utf-8')
    return b.replace('\r\n', '\n'), ('\r\n' in b), b.startswith('\ufeff')

def save(rel, s, crlf):
    open(os.path.join(R, rel), 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

def rep(s, a, b, cnt=1):
    assert s.count(a) == cnt, (rel_now, s.count(a), a[:90])
    return s.replace(a, b)

# ------------------------------------------------------------ main: run_session
rel_now = r'src\native_video_client_main.cpp'
s, c, _ = load(rel_now)
s = rep(s, """int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);
  apply_latency_priority();
  apply_dpi_awareness();

  ViewerContext ctx;
  load_config(ctx, argc, argv);
  if (const int rc = validate_codec_transport(ctx)) return rc;
  apply_initial_state(ctx);

  remote60::native_poc::WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-client] WSAStartup failed\\n";
    return 1;
  }

  if (const int rc = create_window_and_toolbar(ctx)) return rc;""", """namespace {

// ka-pc-impl r1 (KA ③, step 1): one session, start to end, on a context of its own. What main() did after
// WSAStartup, unchanged -- moved here so a process can run more than one session in turn (the managed standby
// viewer activates its lease again after a park). Everything a session creates (its window, toolbar, decoder,
// MF, threads, sockets) is created and torn down inside; the process-wide state it touches is reset at the
// start of the next one (create_window, session_toolbar_destroy).
int run_session(ViewerContext& ctx) {
  if (const int rc = create_window_and_toolbar(ctx)) return rc;""")
s = rep(s, """  shutdown_viewer(ctx);
  std::cout << "[native-video-client] done\\n";
  return ctx.session.recoveryExitCode.load();
}""", """  shutdown_viewer(ctx);
  std::cout << "[native-video-client] done\\n";
  return ctx.session.recoveryExitCode.load();
}

}  // namespace

int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);
  apply_latency_priority();
  apply_dpi_awareness();

  remote60::native_poc::WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-client] WSAStartup failed\\n";
    return 1;
  }

#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
  // TEST BUILD ONLY (GNLinkViewerKaTest, ka-pc-impl r1 step 1): REMOTE60_VIEWER_TEST_SESSIONS=N runs N sessions
  // one after another in this one process, each on a fresh context -- the re-entry the managed standby viewer
  // needs. The exit code is the last session's; each session's end is logged.
  int sessions = 1;
  if (const char* v = std::getenv("REMOTE60_VIEWER_TEST_SESSIONS")) sessions = (std::max)(1, std::atoi(v));
  int rc = 0;
  for (int i = 1; i <= sessions; ++i) {
    std::cout << "[viewer][ka-test] session " << i << "/" << sessions << " begins\\n";
    auto ctx = std::make_unique<ViewerContext>();
    load_config(*ctx, argc, argv);
    if (const int bad = validate_codec_transport(*ctx)) return bad;
    apply_initial_state(*ctx);
    rc = run_session(*ctx);
    std::cout << "[viewer][ka-test] session " << i << "/" << sessions << " ended rc=" << rc << "\\n";
  }
  return rc;
#else
  ViewerContext ctx;
  load_config(ctx, argc, argv);
  if (const int rc = validate_codec_transport(ctx)) return rc;
  apply_initial_state(ctx);
  return run_session(ctx);
#endif
}""")
if '#include <memory>' not in s:
    s = rep(s, '#include <iostream>\n', '#include <algorithm>\n#include <cstdlib>\n#include <iostream>\n#include <memory>\n')
save(rel_now, s, c)

# ------------------------------------------------------------ window proc: class re-registration, session statics, test seam
rel_now = r'src\viewer_window_proc.cpp'
s, c, _ = load(rel_now)
s = rep(s, """PasteGesture gLastFailed;
""", """PasteGesture gLastFailed;

// ka-pc-impl r1 (KA ③, step 1): this file's statics belong to ONE session (one window). A process that runs
// another session after the first (the managed standby viewer, after a park) starts it from these defaults,
// never from the last session's paste, IME or held keys. Called by create_window, on the UI thread.
void reset_window_session_statics() {
  gPrevHimc = nullptr;
  gImeDetached = false;
  gPhysicalDown.clear();
  gPaste = PasteGate{};
  gLatestCopy = LatestCopy{};
  gWaitingSnap = PasteSnapshot{};
  gGenAtLocalCopy = 0;
  gGenAtLocalValid = false;
  gLocalCopySeq = 0;
  gLocalBasePending = false;
  gProbeId = 0;
  gProbeSnap = PasteSnapshot{};
  gReleasedMods = 0;
  gPasteLine = PasteLine{};
  gLastFailed = PasteGesture{};
}
""")
s = rep(s, """bool create_window(ViewerState& ctx) {
  HINSTANCE inst = GetModuleHandle(nullptr);""", """bool create_window(ViewerState& ctx) {
  reset_window_session_statics();
  HINSTANCE inst = GetModuleHandle(nullptr);""")
s = rep(s, """  wc.lpszClassName = cls;
  if (!RegisterClassExW(&wc)) return false;

  // The user sees this in the taskbar""", """  wc.lpszClassName = cls;
  // A second session in the same process (ka-pc-impl r1) finds the class already registered -- that is fine.
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

  // The user sees this in the taskbar""")
s = rep(s, """  ShowWindow(ctx.session.hwnd, SW_SHOW);
  UpdateWindow(ctx.session.hwnd);
  // Remote-cursor overlay cadence""", """#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
  // TEST BUILD ONLY (GNLinkViewerKaTest): REMOTE60_VIEWER_TEST_WINDOW_X/_Y place the window (off the user's
  // monitors for an isolated run) and it is shown without taking the focus. The product shows it as below.
  if (const char* x = std::getenv("REMOTE60_VIEWER_TEST_WINDOW_X")) {
    const char* y = std::getenv("REMOTE60_VIEWER_TEST_WINDOW_Y");
    SetWindowPos(ctx.session.hwnd, nullptr, std::atoi(x), y ? std::atoi(y) : 0, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  ShowWindow(ctx.session.hwnd, SW_SHOWNOACTIVATE);
#else
  ShowWindow(ctx.session.hwnd, SW_SHOW);
#endif
  UpdateWindow(ctx.session.hwnd);
  // Remote-cursor overlay cadence""")
if '#include <cstdlib>' not in s:
    s = rep(s, '#include <iostream>\n', '#include <cstdlib>\n#include <iostream>\n')
save(rel_now, s, c)

# ------------------------------------------------------------ toolbar: forget the session on destroy
rel_now = r'src\client_session_toolbar.cpp'
s, c, _ = load(rel_now)
s = rep(s, """  g.owner = nullptr;
  g.buttons.clear();
}""", """  g.owner = nullptr;
  g.buttons.clear();
  // ka-pc-impl r1: the bar belongs to one session. A later session in the same process (the managed standby
  // viewer) starts from a bar that is not expanded, not hovered and not holding the previous session's state.
  g = Toolbar{};
  std::lock_guard<std::mutex> lock(gStateMu);
  gPendingState = SessionToolbarState{};
}""")
save(rel_now, s, c)

# ------------------------------------------------------------ CMake: the test viewer
rel_now = r'CMakeLists.txt'
s, c, _ = load(rel_now)
anchor = """# prelogin r21 (E2): RequestWindowList's role gate + park through the product ClientSessionController,"""
s = rep(s, anchor, """# ka-pc-impl r1 (KA ③): the same viewer with REMOTE60_VIEWER_KA_TEST_SEAM -- N sessions in one process
# (REMOTE60_VIEWER_TEST_SESSIONS) and a window placed off the user's monitors, shown without focus
# (REMOTE60_VIEWER_TEST_WINDOW_X/_Y). Same sources, libraries and options as GNLinkViewer; one define. Never shipped.
add_executable(remote60_viewer_ka_test ${_viewer_sources})
foreach(_prop INCLUDE_DIRECTORIES LINK_LIBRARIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
  get_target_property(_value remote60_native_video_client_poc ${_prop})
  if(_value)
    set_property(TARGET remote60_viewer_ka_test PROPERTY ${_prop} "${_value}")
  endif()
endforeach()
target_compile_definitions(remote60_viewer_ka_test PRIVATE REMOTE60_VIEWER_KA_TEST_SEAM=1)
set_target_properties(remote60_viewer_ka_test PROPERTIES OUTPUT_NAME "GNLinkViewerKaTest")

""" + anchor)
save(rel_now, s, c)
print('ok')
