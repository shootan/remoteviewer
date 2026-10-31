R = 'C:/work/gnlink-wt/feat-apk-0233/apps/native_poc/src/'


def edit(name, reps):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:90], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


edit('native_video_client_session.hpp', [
    ('''  ClientHeldInput heldInput_;
''', '''  ClientHeldInput heldInput_;
  // keepalive-r2 e r2: what became of each X button's press on this connection (refused / dropped / sent); under
  // parkMu_, reset with the connection (client_mouse_xbutton_fence.hpp)
  ClientMouseXPressLedger mouseXLedger_;
'''),
])
# the include: where client_park_state.hpp is included
p = R + 'native_video_client_session.hpp'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '#include "client_park_state.hpp"' + nl
assert s.count(a) == 1
s = s.replace(a, a + '#include "client_mouse_xbutton_fence.hpp"' + nl)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok include')

edit('native_video_client_session.cpp', [
    ('''  // keepalive-r2 e: no X edge, no X bit to a host without kCaptureFlagMouseXButtonsV1 (or not known yet) -- the
  // JNI input and the macro playback both come here (client_mouse_xbutton_fence.hpp; the PC: fence_mouse_for_host).
  if (!FenceMouseXButtonsForHost(HostTakesMouseXButtons(role.known, role.flags), kind, keyCode, &buttons)) {''',
     '''  // keepalive-r2 e: no X edge, no X bit to a host without kCaptureFlagMouseXButtonsV1 (or not known yet) -- the
  // JNI input and the macro playback both come here (client_mouse_xbutton_fence.hpp; the PC: fence_mouse_for_host).
  // r2: a press refused here stays refused to its up (its up and its held bit too), whatever the next Pong says.
  if (!mouseXLedger_.AdmitQueued(HostTakesMouseXButtons(role.known, role.flags), kind, keyCode, &buttons)) {'''),
    ('''      if (!FenceMouseXButtonsAction(HostTakesMouseXButtons(role.known, role.flags), &action)) {
        mouseXButtonsHeldBack_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }''', '''      // r2: per press -- a down dropped here takes its up and its held bit with it; a sent down's owed release
      // goes only while the host takes X.
      bool xAdmitted = true;
      {
        std::lock_guard<std::mutex> lk(parkMu_);
        xAdmitted = mouseXLedger_.AdmitSendAction(HostTakesMouseXButtons(role.known, role.flags), &action);
      }
      if (!xAdmitted) {
        mouseXButtonsHeldBack_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }'''),
    ('''        heldInput_.OnSent(action);
''', '''        heldInput_.OnSent(action);
        mouseXLedger_.OnSentAction(action);  // r2: a sent X down owes its release; a sent X up ends it
'''),
    ('''    park_.End();
    heldInput_.Reset();
''', '''    park_.End();
    heldInput_.Reset();
    mouseXLedger_.Reset();  // r2: no press record carried into another connection
'''),
])
