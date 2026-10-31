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

# the shared rule: X1/X2 mouse ups are releases too
edit('native_video_client_shared_core.cpp', [(
'''    if (msg.inputEvent.kind == 3 && (msg.inputEvent.keyCode == 1 || msg.inputEvent.keyCode == 2 ||
                                   msg.inputEvent.keyCode == 4)) return 257 + msg.inputEvent.keyCode;''',
'''    // ka-pc-impl r7 (Codex N1): every mouse button's up, the X pair (VK_XBUTTON1=5, VK_XBUTTON2=6) included --
    // enqueue_release_for_pressed_mouse_buttons makes those ups, and a release-only drain must keep them. (Whether an
    // X edge may go to a host at all is the viewer's own fence, enqueue_input_event; it is not decided here.)
    if (msg.inputEvent.kind == 3 && (msg.inputEvent.keyCode == 1 || msg.inputEvent.keyCode == 2 ||
                                   msg.inputEvent.keyCode == 4 || msg.inputEvent.keyCode == 5 ||
                                   msg.inputEvent.keyCode == 6)) return 257 + msg.inputEvent.keyCode;'''), (
'''}  // namespace
''', '''}  // namespace

uint32_t control_input_release_identity(const QueuedControlInputMessage& msg) { return release_identity(msg); }
''')])
edit('native_video_client_shared_core.hpp', [(
'''class ClientInputQueue {''', '''// ka-pc-impl r7 (Codex N1): the ONE release rule -- nonzero for a key up, a mouse button up (left, right, middle, X1,
// X2) and a physical key up; what DropAllButReleases keeps and what an owner fence's release-only drain may send.
uint32_t control_input_release_identity(const QueuedControlInputMessage& msg);

class ClientInputQueue {''')])

# the gate asks the shared rule (no copy of it)
edit('viewer_owner_fence.hpp', [(
'''// The shared core's release rule (native_video_client_shared_core.cpp release_identity), on an outbound action: a key
// up, a mouse button up (left/right/middle), a physical key up.
inline bool action_is_release(const ControlOutboundAction& a) {
  if (a.kind == ControlOutboundActionKind::InputEvent && a.inputEvent.keyCode <= 255) {
    if (a.inputEvent.kind == 6) return true;
    if (a.inputEvent.kind == 3 && (a.inputEvent.keyCode == 1 || a.inputEvent.keyCode == 2 || a.inputEvent.keyCode == 4))
      return true;
  }
  return a.kind == ControlOutboundActionKind::PhysicalKey && !a.physicalKey.down && a.physicalKey.vk <= 255 &&
         a.physicalKey.scanCode <= 255;
}''', '''// r7 (Codex N1): the shared core's ONE release rule (control_input_release_identity), asked about an outbound action --
// a key up, a mouse button up (left, right, middle, X1, X2), a physical key up. No second copy of the rule.
inline bool action_is_release(const ControlOutboundAction& a) {
  QueuedControlInputMessage m{};
  if (a.kind == ControlOutboundActionKind::InputEvent) {
    m.type = MessageType::ControlInputEvent;
    m.inputEvent = a.inputEvent;
  } else if (a.kind == ControlOutboundActionKind::PhysicalKey) {
    m.type = MessageType::ControlPhysicalKey;
    m.physicalKey = a.physicalKey;
  } else {
    return false;
  }
  return control_input_release_identity(m) != 0;
}''')])
print('ok')
