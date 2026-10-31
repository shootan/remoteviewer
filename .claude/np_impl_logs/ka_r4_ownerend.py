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

# ---------------------------------------------------------------- viewer: two signals
edit('viewer_args.hpp', [(
'''  std::string cancelEventHandle;
''', '''  std::string cancelEventHandle;
  // ka-pc-impl r4 (Codex decision 1): --owner-end-event, a second unnamed event from the shell. The cancel event
  // calls off a CONNECT (a replacement); it never ends an established session. This one is the owner ending the
  // session (sign-out, account change, the shell closing): the owner fence, then the window's own teardown.
  std::string ownerEndEventHandle;
''')])
edit('viewer_args.cpp', [(
'''    } else if (k == "--cancel-event" && i + 1 < argc) {
      a.cancelEventHandle = trim_ascii(argv[++i]);''', '''    } else if (k == "--cancel-event" && i + 1 < argc) {
      a.cancelEventHandle = trim_ascii(argv[++i]);
    } else if (k == "--owner-end-event" && i + 1 < argc) {
      a.ownerEndEventHandle = trim_ascii(argv[++i]);''')])

edit('native_video_client_main.cpp', [(
'''  if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
    std::cerr << "[native-video-client] called off before the session began; nothing is opened\\n";
    return 0;
  }''', '''  HANDLE ownerEndEvent = nullptr;
  if (!ctx.args.ownerEndEventHandle.empty()) {
    ownerEndEvent = viewer_cancel_handle_from_arg(
        std::wstring(ctx.args.ownerEndEventHandle.begin(), ctx.args.ownerEndEventHandle.end()));
  }
  if ((cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) ||
      (ownerEndEvent && WaitForSingleObject(ownerEndEvent, 0) == WAIT_OBJECT_0)) {
    std::cerr << "[native-video-client] called off before the session began; nothing is opened\\n";
    return 0;
  }'''), (
'''  // ka-pc-impl r2 (Codex N4): a cancel from the shell that started this viewer, AFTER the session was established
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
  }''', '''  // After the session is established the two signals mean different things (ka-pc-impl r4, Codex decision 1):
  //  - the cancel event (a REPLACEMENT -- the user pressed this PC again while the shell still saw it connecting)
  //    reaches nothing: an established session is not closed by it. Said once, as before (RV-07).
  //  - the owner-end event (sign-out, account change, the shell closing) is the owner fence: input cut, held input
  //    released and delivered, then the window's own WM_CLOSE teardown.
  // The watch ends with this session (joined, never detached: it posts to this session's window).
  HANDLE fenceStop = (cancelEvent || ownerEndEvent) ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : nullptr;
  std::thread fenceWatch;
  if (fenceStop) {
    fenceWatch = std::thread([cancelEvent, ownerEndEvent, fenceStop, hwnd = ctx.session.hwnd]() {
      HANDLE cancel = cancelEvent;
      for (;;) {
        HANDLE waits[3];
        DWORD n = 0;
        waits[n++] = fenceStop;
        const DWORD ownerAt = ownerEndEvent ? n : MAXDWORD;
        if (ownerEndEvent) waits[n++] = ownerEndEvent;
        const DWORD cancelAt = cancel ? n : MAXDWORD;
        if (cancel) waits[n++] = cancel;
        const DWORD w = WaitForMultipleObjects(n, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0 + ownerAt) {
          std::cerr << "[native-video-client] the owner ended the session: owner fence, closing" << std::endl;
          if (hwnd) PostMessageW(hwnd, kMsgOwnerFence, 0, 0);
          return;
        }
        if (w == WAIT_OBJECT_0 + cancelAt) {
          std::cerr << "[native-video-client] cancel requested after the session was established:"
                    << " no effect (session established)" << std::endl;
          cancel = nullptr;  // said once; the owner-end event is still watched
          continue;
        }
        return;  // the session ended (fenceStop), or the wait failed
      }
    });
  }''')])
print('viewer ok')
