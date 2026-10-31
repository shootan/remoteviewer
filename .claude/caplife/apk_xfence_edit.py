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
    ('''  uint64_t PreloginInputHeldBack() const { return preloginInputHeldBack_.load(std::memory_order_relaxed); }
''', '''  uint64_t PreloginInputHeldBack() const { return preloginInputHeldBack_.load(std::memory_order_relaxed); }
  // keepalive-r2 e (client_mouse_xbutton_fence.hpp): X edges not sent to a host without kCaptureFlagMouseXButtonsV1
  // (refused when queued, or dropped right before sending).
  uint64_t MouseXButtonsHeldBack() const { return mouseXButtonsHeldBack_.load(std::memory_order_relaxed); }
'''),
    ('''  std::atomic<uint64_t> preloginInputHeldBack_{0};  // r5 G8: inputs not sent to a host at the logon screen
''', '''  std::atomic<uint64_t> preloginInputHeldBack_{0};  // r5 G8: inputs not sent to a host at the logon screen
  std::atomic<uint64_t> mouseXButtonsHeldBack_{0};  // keepalive-r2 e: X edges not sent to a host without X
'''),
])

edit('native_video_client_session.cpp', [
    ('''#include "prelogin_input_gate.hpp"
''', '''#include "prelogin_input_gate.hpp"
#include "client_mouse_xbutton_fence.hpp"
'''),
    ('''  if (!prelogin::ViewerMaySendInputEvent(prelogin::EffectiveHostFlags(role.known, role.flags), kind, keyCode)) {
    preloginInputHeldBack_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  inputQueue_.Enqueue(''', '''  if (!prelogin::ViewerMaySendInputEvent(prelogin::EffectiveHostFlags(role.known, role.flags), kind, keyCode)) {
    preloginInputHeldBack_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  // keepalive-r2 e: no X edge, no X bit to a host without kCaptureFlagMouseXButtonsV1 (or not known yet) -- the
  // JNI input and the macro playback both come here (client_mouse_xbutton_fence.hpp; the PC: fence_mouse_for_host).
  if (!FenceMouseXButtonsForHost(HostTakesMouseXButtons(role.known, role.flags), kind, keyCode, &buttons)) {
    mouseXButtonsHeldBack_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  inputQueue_.Enqueue('''),
    ('''      if (!prelogin::ViewerMaySendAction(prelogin::EffectiveHostFlags(role.known, role.flags), action)) {
        preloginInputHeldBack_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
''', '''      if (!prelogin::ViewerMaySendAction(prelogin::EffectiveHostFlags(role.known, role.flags), action)) {
        preloginInputHeldBack_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      // keepalive-r2 e: the same X fence on whatever reached the queue another way (a release the park or the
      // held state made, an entry the queue kept through DropAllButReleases); a refused edge is never OnSent.
      if (!FenceMouseXButtonsAction(HostTakesMouseXButtons(role.known, role.flags), &action)) {
        mouseXButtonsHeldBack_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
'''),
])
