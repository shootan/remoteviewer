import os
base = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src' + os.sep

def edit(name, pairs):
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    for a, c in pairs:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, c)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

# R3: the fence is a gate: releases first (input still on), then input off + fenced, then close. The drop list covers
# IME, deferred picks / restore / unsendable selects; the two payload-owning posts are freed, not applied.
edit('viewer_window_proc.cpp', [(
'''  // ka-pc-impl r2 (Codex N4): fenced by its owner -- nothing typed, clicked or copied here goes on to the host.
  if (ctx.session.ownerFenced &&
      ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
       msg == WM_INPUT || msg == WM_CLIPBOARDUPDATE || msg == kMsgPushClipboardNow || msg == kMsgApplyClipboard)) {
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  switch (msg) {
    case kMsgOwnerFence:
      if (!ctx.session.ownerFenced) {
        ctx.session.ownerFenced = true;
        std::cout << "[native-video-client] owner fence: input and clipboard cut, closing the session\\n";
        cancel_pastes(ctx, hwnd, PasteFailure::Silent, "owner fence");
        RemoveClipboardFormatListener(hwnd);
        release_all_physical(ctx);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;''',
'''  // ka-pc-impl r2/r3 (Codex N4, R3): fenced by its owner. The gate itself is inputEnabled (cleared by the fence):
  // every input path -- keys, mouse, IME results, text, macro -- enqueues only while it is on. What is dropped
  // here besides is what would START something: a typed or clicked message, an IME message, a clipboard change, a
  // deferred / restored / refused pick. The three posts that carry a payload are freed, not applied.
  if (ctx.session.ownerFenced) {
    if (msg == kMsgApplyClipboard) {
      std::unique_ptr<RemoteTextApply> owned(reinterpret_cast<RemoteTextApply*>(lp));
      return 0;
    }
    if (msg == kMsgApplyWindowList) {
      std::unique_ptr<ControlWindowListMessage> owned(reinterpret_cast<ControlWindowListMessage*>(lp));
      return 0;
    }
    if (msg == kMsgDecideRemoteFiles) {
      std::unique_ptr<RemoteFilesDecide> owned(reinterpret_cast<RemoteFilesDecide*>(lp));
      return 0;
    }
    if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
        msg == WM_INPUT || (msg >= WM_IME_STARTCOMPOSITION && msg <= WM_IME_KEYLAST) ||
        (msg >= WM_IME_SETCONTEXT && msg <= WM_IME_KEYUP) || msg == WM_CLIPBOARDUPDATE ||
        msg == kMsgPushClipboardNow || msg == kMsgRunDeferredPick || msg == kMsgUnsendableSelect ||
        msg == kMsgRestoreStep) {
      return DefWindowProcW(hwnd, msg, wp, lp);
    }
  }
  switch (msg) {
    case kMsgOwnerFence:
      if (!ctx.session.ownerFenced) {
        // 1. Whatever is held is released FIRST, while input is still on -- the same set the control channel's
        //    resume sends: every modifier up, every key this window sent down, every mouse button, the scan keys
        //    of host-IME routing, the mouse capture. (Turning input off first would drop these very releases.)
        cancel_pastes(ctx, hwnd, PasteFailure::Silent, "owner fence");
        int modifierUps = 0;
        if (!kInputPolicyForceBlock) {
          modifierUps = enqueue_release_all_modifiers(ctx);
          enqueue_release_for_pressed_keys(ctx);
          enqueue_release_for_pressed_mouse_buttons(ctx);
          release_all_physical(ctx);
          release_mouse_capture_if_idle(ctx, hwnd);
        }
        ctx.picker.CancelPress();
        ctx.input.macro.StopPlayback();
        ctx.input.macro.StopRecording();
        // 2. Then the gate: nothing more is sent from this window, and nothing new starts.
        ctx.session.inputEnabled = false;
        ctx.session.ownerFenced = true;
        RemoveClipboardFormatListener(hwnd);
        std::cout << "[native-video-client] owner fence: held input released (modifierUps=" << modifierUps
                  << "), input and clipboard cut, closing the session\\n";
        // 3. The ordinary teardown.
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;''')])

# R3: an ordinary viewer whose cancel was signalled before it began never makes its window; the watcher starts
# before the window; a cancel during window / decoder set-up ends the session before the connect.
edit('native_video_client_main.cpp', [(
'''  // ka-pc-impl r2 (Codex N4): ended before it began (a Release / Shutdown that reached the managed viewer before
  // this session's window existed) -- no window, no decoder, nothing of the old owner's comes up.
  if (ctx.connectCancelled.load(std::memory_order_acquire)) return 0;
  if (const int rc = create_window_and_toolbar(ctx)) return rc;
  if (const int rc = init_decoder(ctx)) return rc;
''', '''  // ka-pc-impl r2/r3 (Codex N4, R3): ended before it began -- the managed viewer's Release / Shutdown before this
  // session's window existed, or (ordinary) the shell's cancel already signalled when this process started (its
  // owner signed out while it was starting). No window, no decoder, nothing of the old owner's comes up.
  HANDLE cancelEvent = nullptr;
  if (!ctx.args.cancelEventHandle.empty()) {
    cancelEvent = viewer_cancel_handle_from_arg(
        std::wstring(ctx.args.cancelEventHandle.begin(), ctx.args.cancelEventHandle.end()));
    if (!cancelEvent) {
      // One line, and then on with the connection. A viewer that cannot be called off is
      // the viewer this product shipped until now; refusing to connect over it would turn a
      // missing convenience into a broken session.
      std::cerr << "[native-video-client] --cancel-event is not a handle this process holds;"
                << " connecting anyway, this session cannot be called off" << std::endl;
    }
  }
  if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
    std::cerr << "[native-video-client] called off before the session began; nothing is opened\\n";
    return 0;
  }
  if (ctx.connectCancelled.load(std::memory_order_acquire)) return 0;
  // The watcher runs from here -- before the window -- so a cancel during the set-up below is seen.
  (void)viewer_start_cancel_watcher(cancelEvent, &ctx.connectCancelled,
                                    &ctx.connectCancelledByOwner, &ctx.cancelWatcherStop,
                                    &ctx.cancelWatcher);
  if (const int rc = create_window_and_toolbar(ctx)) {
    viewer_stop_cancel_watcher(&ctx.cancelWatcherStop, &ctx.cancelWatcher);
    return rc;
  }
  if (const int rc = init_decoder(ctx)) {
    viewer_stop_cancel_watcher(&ctx.cancelWatcherStop, &ctx.cancelWatcher);
    return rc;
  }
  if (ctx.connectCancelled.load(std::memory_order_acquire)) {
    // Called off while its window and decoder were being made: closed before any connect (run_session closes the
    // window and drains it).
    std::cerr << "[native-video-client] called off during set-up; the session is not started\\n";
    viewer_stop_cancel_watcher(&ctx.cancelWatcherStop, &ctx.cancelWatcher);
    if (ctx.dec.mfStarted) MFShutdown();
    return 0;
  }
'''), (
'''  // The handle the shell created and let this process inherit, named on the command line by
  // value. Nothing is opened here: an unnamed event has no name to open, which is the point --
  // the earlier named one could be signalled by any process in the session that knew a pid.
  //
  // The check that follows asks only whether the number is a valid handle in this process. It
  // does not prove the handle is an event, nor that it came from the shell -- it rules out
  // waiting on a number that is nothing, which is the case that actually turns up.
  //
  // The value is ASCII digits, so it converts without needing the viewer's wide-string
  // helpers, which live in the shell rather than here.
  HANDLE cancelEvent = nullptr;
  if (!ctx.args.cancelEventHandle.empty()) {
    cancelEvent = viewer_cancel_handle_from_arg(
        std::wstring(ctx.args.cancelEventHandle.begin(), ctx.args.cancelEventHandle.end()));
    if (!cancelEvent) {
      // One line, and then on with the connection. A viewer that cannot be called off is
      // the viewer this product shipped until now; refusing to connect over it would turn a
      // missing convenience into a broken session.
      std::cerr << "[native-video-client] --cancel-event is not a handle this process holds;"
                << " connecting anyway, this session cannot be called off" << std::endl;
    }
  }
  (void)viewer_start_cancel_watcher(cancelEvent, &ctx.connectCancelled,
                                    &ctx.connectCancelledByOwner, &ctx.cancelWatcherStop,
                                    &ctx.cancelWatcher);
''', '''  // The handle the shell created and let this process inherit, named on the command line by
  // value (read at the top of this function). Nothing is opened: an unnamed event has no name to open,
  // which is the point -- the earlier named one could be signalled by any process in the session that knew a pid.
  // The check there asks only whether the number is a valid handle in this process. It does not prove the handle
  // is an event, nor that it came from the shell -- it rules out waiting on a number that is nothing.
''')])
print('ok')
