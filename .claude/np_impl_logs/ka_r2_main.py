p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\native_video_client_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:90])
    s = s.replace(a, c)

rep('''int run_session_body(ViewerContext& ctx) {
  if (const int rc = create_window_and_toolbar(ctx)) return rc;''', '''int run_session_body(ViewerContext& ctx) {
  // ka-pc-impl r2 (Codex N4): ended before it began (a Release / Shutdown that reached the managed viewer before
  // this session's window existed) -- no window, no decoder, nothing of the old owner's comes up.
  if (ctx.connectCancelled.load(std::memory_order_acquire)) return 0;
  if (const int rc = create_window_and_toolbar(ctx)) return rc;''')

rep('''  // Connect is over, one way or the other. The watcher has nothing left to interrupt, and the
  // session that follows is torn down through the ordinary path.
  viewer_stop_cancel_watcher(&ctx.cancelWatcherStop, &ctx.cancelWatcher);
  // A cancel that arrives from here on reaches nothing: the session is established and this
  // viewer runs until it is closed. That is unchanged (a connected session is not killed from
  // outside), but it is now SAID when it happens, instead of the shell's "asked=1" standing as
  // the only record of a request that had no effect. (RV-07)
  if (cancelEvent) {
    std::thread([cancelEvent]() {
      if (WaitForSingleObject(cancelEvent, INFINITE) == WAIT_OBJECT_0) {
        std::cerr << "[native-video-client] cancel requested after the session was established:"
                  << " no effect (session established)" << std::endl;
      }
    }).detach();
  }

  attach_control_tunnel_and_log(ctx);
  try {
    connect_control(ctx);
    start_receiver(ctx);
    run_message_pump(ctx);
  } catch (...) {
    ctx.session.recoveryExitCode.store(43);
  }

  shutdown_viewer(ctx);''', '''  // Connect is over, one way or the other. The watcher has nothing left to interrupt, and the
  // session that follows is torn down through the ordinary path.
  viewer_stop_cancel_watcher(&ctx.cancelWatcherStop, &ctx.cancelWatcher);
  // ka-pc-impl r2 (Codex N4): called off while the connect was finishing (the managed viewer's Release / Shutdown,
  // or the shell's cancel) -- the session is not started at all; its socket and decoder go through the ordinary
  // teardown.
  if (ctx.connectCancelled.load(std::memory_order_acquire)) {
    std::cerr << "[native-video-client] connect called off as it finished; the session is not started\\n";
    shutdown_viewer(ctx);
    return 0;
  }
  // ka-pc-impl r2 (Codex N4): a cancel from the shell that started this viewer, AFTER the session was established
  // -- its owner is ending it (sign-out, account change, the shell's exit, a replacement). It used to reach
  // nothing ("no effect"), and the shell then ended the process 3 s later with no teardown at all. Now it is the
  // owner fence: the UI thread cuts input and the clipboard and closes the window through WM_CLOSE (the same
  // teardown as the X). The watch ends with this session (joined, never detached: it posts to this session's window).
  HANDLE fenceStop = cancelEvent ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : nullptr;
  std::thread fenceWatch;
  if (cancelEvent && fenceStop) {
    fenceWatch = std::thread([cancelEvent, fenceStop, hwnd = ctx.session.hwnd]() {
      HANDLE both[2] = {fenceStop, cancelEvent};
      if (WaitForMultipleObjects(2, both, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        std::cerr << "[native-video-client] cancel requested after the session was established:"
                  << " owner fence, closing" << std::endl;
        if (hwnd) PostMessageW(hwnd, kMsgOwnerFence, 0, 0);
      }
    });
  }

  attach_control_tunnel_and_log(ctx);
  try {
    connect_control(ctx);
    start_receiver(ctx);
    run_message_pump(ctx);
  } catch (...) {
    ctx.session.recoveryExitCode.store(43);
  }

  if (fenceWatch.joinable()) {
    SetEvent(fenceStop);
    fenceWatch.join();
  }
  if (fenceStop) CloseHandle(fenceStop);
  shutdown_viewer(ctx);''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
