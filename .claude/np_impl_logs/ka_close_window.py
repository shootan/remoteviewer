p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\native_video_client_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('''int run_session(ViewerContext& ctx) {
  if (const int rc = create_window_and_toolbar(ctx)) return rc;''', '''int run_session_body(ViewerContext& ctx) {
  if (const int rc = create_window_and_toolbar(ctx)) return rc;''')
rep('''}  // namespace

int main(int argc, char** argv) {''', '''// ka-pc-impl r1: a session that ended without its window's X (--seconds, the host gone, a takeover, a failed
// connect) left its window: in a process that then ended that did not matter. A process that runs another session
// (the managed standby viewer, the re-entry test) must not keep a window whose context is gone, nor the quit it
// posts. So on EVERY way out of a session its window is destroyed and its messages drained up to that quit.
void close_session_window(ViewerContext& ctx) {
  if (ctx.session.hwnd && IsWindow(ctx.session.hwnd)) DestroyWindow(ctx.session.hwnd);
  ctx.session.hwnd = nullptr;
  MSG m;
  while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
    if (m.message == WM_QUIT) break;
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}

int run_session(ViewerContext& ctx) {
  const int rc = run_session_body(ctx);
  close_session_window(ctx);
  return rc;
}

}  // namespace

int main(int argc, char** argv) {''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\viewer_managed_standby.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')
rep('''    const int rc = run(*ctx);
    over.store(true);
    watcher.join();
    close_session_window(*ctx);''', '''    const int rc = run(*ctx);  // run_session closes and drains the session's window on every way out
    over.store(true);
    watcher.join();
    close_session_window(*ctx);  // a second, harmless pass (nothing left after run_session)''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
