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

edit('viewer_constants.hpp', [(
'''constexpr UINT kMsgUnsendableSelect = WM_APP + 25;
''', '''constexpr UINT kMsgUnsendableSelect = WM_APP + 25;
// ka-pc-impl r2 (Codex N4): the shell that started this viewer is ending it (sign-out, account change, its exit, a
// kept PC released). On the UI thread: input and the clipboard are cut at once -- held keys and buttons released,
// the clipboard listener off, pastes cancelled, no further input forwarded -- and then the window closes through
// its ordinary WM_CLOSE teardown.
constexpr UINT kMsgOwnerFence = WM_APP + 26;
''')])

edit('viewer_session_state.hpp', [(
'''  std::atomic<bool> pickerReadySaid{false};
  std::atomic<bool> presentedSaid{false};
''', '''  std::atomic<bool> pickerReadySaid{false};
  std::atomic<bool> presentedSaid{false};
  // ka-pc-impl r2 (Codex N4): set on the UI thread by kMsgOwnerFence; from then on no input or clipboard reaches the
  // host from this window (the window proc drops them) while the session closes.
  bool ownerFenced = false;
''')])

edit('viewer_window_proc.cpp', [(
'''  ViewerState& ctx = *state;
  ctx.session.uiHeartbeatUs.store(qpc_now_us(), std::memory_order_relaxed);
  switch (msg) {
    case WM_CLOSE:''', '''  ViewerState& ctx = *state;
  ctx.session.uiHeartbeatUs.store(qpc_now_us(), std::memory_order_relaxed);
  // ka-pc-impl r2 (Codex N4): fenced by its owner -- nothing typed, clicked or copied here goes on to the host.
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
      return 0;
    case WM_CLOSE:''')])

edit('viewer_context.hpp', [(
'''  remote60::native_poc::ClientSessionConnectArgs* standby = nullptr;
  bool standbyTaken = false;
''', '''  remote60::native_poc::ClientSessionConnectArgs* standby = nullptr;
  bool standbyTaken = false;
  // ka-pc-impl r2 (Codex N6): the session really connected -- handshake and control done, "connected host=" said.
  // Not the same as standbyTaken (the socket was taken): a handshake that failed after the take is not a connect.
  bool sessionConnected = false;
''')])

edit('viewer_startup.cpp', [(
'''  std::cout << "[native-video-client] connected host=" << ctx.resolvedArgs.host''',
'''  ctx.sessionConnected = true;
  std::cout << "[native-video-client] connected host=" << ctx.resolvedArgs.host''')])
print('ok')
