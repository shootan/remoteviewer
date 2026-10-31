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
'''constexpr UINT_PTR kPasteTimerId = 0x714;''',
'''constexpr UINT_PTR kPasteTimerId = 0x714;
// ka-pc-impl r4 (Codex R3): the owner fence waits (bounded) for its releases to reach the host before closing.
constexpr UINT_PTR kOwnerFenceDrainTimerId = 0x715;
constexpr uint64_t kOwnerFenceDrainBudgetMs = 2000;''')])

edit('viewer_session_state.hpp', [(
'''  bool ownerFenced = false;
''', '''  bool ownerFenced = false;
  uint64_t ownerFenceAtMs = 0;  // r4: when the fence was raised (GetTickCount64), for its drain budget
''')])

edit('viewer_window_proc.cpp', [(
'''        // 2. Then the gate: nothing more is sent from this window, and nothing new starts.
        ctx.session.inputEnabled = false;
        ctx.session.ownerFenced = true;
        RemoveClipboardFormatListener(hwnd);
        std::cout << "[native-video-client] owner fence: held input released (modifierUps=" << modifierUps
                  << "), input and clipboard cut, closing the session\\n";
        // 3. The ordinary teardown.
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;''', '''        // 2. Then the gate: nothing NEW is taken from this window, and nothing new starts. (What is already queued
        //    -- the releases above -- still goes: the control thread drains the queue while the session runs.)
        ctx.session.inputEnabled = false;
        ctx.session.ownerFenced = true;
        ctx.session.ownerFenceAtMs = GetTickCount64();
        RemoveClipboardFormatListener(hwnd);
        std::cout << "[native-video-client] owner fence: held input released (modifierUps=" << modifierUps
                  << " queued), input and clipboard cut; waiting for the releases to reach the host\\n";
        // 3. r4 (Codex R3): the teardown only once those releases have LEFT and been ACKNOWLEDGED by the host -- the
        //    input queue empty and the reliable control channel holding nothing unacknowledged -- or, on a link that
        //    is not answering, after a bounded budget. Closing at once could end the control loop with the releases
        //    still queued.
        SetTimer(hwnd, kOwnerFenceDrainTimerId, 20, nullptr);
      }
      return 0;'''), (
'''    case WM_TIMER:
      if (wp == kRenderRetryTimerId) {''', '''    case WM_TIMER:
      if (wp == kOwnerFenceDrainTimerId) {
        const uint64_t waited = GetTickCount64() - ctx.session.ownerFenceAtMs;
        const bool delivered = ctx.control.inputQueue.Empty() && !ctx.control.udpControl.TxPending();
        if (delivered || waited >= kOwnerFenceDrainBudgetMs) {
          KillTimer(hwnd, kOwnerFenceDrainTimerId);
          std::cout << "[native-video-client] owner fence: releases "
                    << (delivered ? "acknowledged by the host" : "NOT confirmed within the budget") << " after "
                    << waited << " ms; closing the session\\n";
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
      }
      if (wp == kRenderRetryTimerId) {''')])
print('ok')
