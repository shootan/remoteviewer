// prelogin-ui r7 (H6): what a viewer may send, when it does not know yet what the host is, and at the
// moment an input leaves its queue.
//
// The gate (prelogin_input_gate.hpp) the PC viewer and the shared session (Android) both ask:
//   * EffectiveHostFlags: before this connection's first pong the role is unknown -- treated as the logon
//     screen (0x2000), whatever the flags word holds;
//   * ViewerMaySendAction: the same rules on a queued ControlOutboundAction, asked right before it is sent
//     (an input queued while the host looked logged-in is held back if the host is at the logon screen
//     by then). Non-input actions always pass.
// Pure; the end-to-end halves are in viewer_prelogin_login_e2e_test (first pong delayed).

#include <windows.h>

#include <cstdio>
#include <string>

#include "native_video_client_shared_core.hpp"
#include "prelogin_input_gate.hpp"

namespace pl = remote60::native_poc::prelogin;
using remote60::native_poc::ControlOutboundAction;
using remote60::native_poc::ControlOutboundActionKind;
using remote60::native_poc::kCaptureFlagPreloginV1;
using remote60::native_poc::kCaptureFlagLockCredV1;
using remote60::native_poc::kCaptureFlagSecureDesktopActive;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
}
ControlOutboundAction key(uint16_t kind, uint32_t vk) {
  ControlOutboundAction a;
  a.kind = ControlOutboundActionKind::InputEvent;
  a.inputEvent.kind = kind;
  a.inputEvent.keyCode = vk;
  return a;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the viewer's input gate: an unknown role, and the last look before sending (prelogin-ui r7, H6)\n\n");
  check("H6: role unknown, flags 0: treated as the logon screen", pl::HostAtLogonScreen(pl::EffectiveHostFlags(false, 0)));
  check("role known, flags 0 (logged in): not the logon screen", !pl::HostAtLogonScreen(pl::EffectiveHostFlags(true, 0)));
  check("role known, 0x2000: the logon screen", pl::HostAtLogonScreen(pl::EffectiveHostFlags(true, kCaptureFlagPreloginV1)));
  check("the other bits are kept", (pl::EffectiveHostFlags(false, 0x8000u) & 0x8000u) != 0);

  const uint32_t unknown = pl::EffectiveHostFlags(false, 0);
  const uint32_t logon = pl::EffectiveHostFlags(true, kCaptureFlagPreloginV1);
  const uint32_t desktop = pl::EffectiveHostFlags(true, 0);
  check("H6: before the first pong a character key does not leave", !pl::ViewerMaySendAction(unknown, key(5, 'Q')));
  // lockscreen-cred r1 (Codex A6): a key UP goes -- it only releases a key whose down reached the host
  // (the viewer sends an up only for a down it sent); holding it back would leave that key stuck.
  check("lockscreen-cred: ...but its key up does (a release is never held back)", pl::ViewerMaySendAction(unknown, key(6, 'Q')));
  check("lockscreen-cred: ...also at a known logon screen", pl::ViewerMaySendAction(
                                                                pl::EffectiveHostFlags(true, kCaptureFlagPreloginV1), key(6, 'Q')));
  ControlOutboundAction text;
  text.kind = ControlOutboundActionKind::InputText;
  check("H6: ...nor text", !pl::ViewerMaySendAction(unknown, text));
  ControlOutboundAction phys;
  phys.kind = ControlOutboundActionKind::PhysicalKey;
  phys.physicalKey.vk = 'Q';
  check("H6: ...nor a host-IME physical key", !pl::ViewerMaySendAction(unknown, phys));
  check("...while a navigation key does (Tab)", pl::ViewerMaySendAction(unknown, key(5, VK_TAB)));
  check("...and the pointer does (move)", pl::ViewerMaySendAction(unknown, key(1, 0)));
  ControlOutboundAction ping;
  ping.kind = ControlOutboundActionKind::Ping;
  check("...and non-input actions always do (the ping that learns the role)", pl::ViewerMaySendAction(unknown, ping));
  check("H6: queued while logged in, the host at the logon screen by the send: held back",
        pl::ViewerMaySendAction(desktop, key(5, 'Q')) && !pl::ViewerMaySendAction(logon, key(5, 'Q')));
  check("logged in, known: characters, text and physical keys go",
        pl::ViewerMaySendAction(desktop, key(5, 'Q')) && pl::ViewerMaySendAction(desktop, text) &&
            pl::ViewerMaySendAction(desktop, phys));
  // lockscreen-cred r1 (Codex A5): the lock screen of a logged-on host (0x40000) is a logon screen here too.
  const uint32_t lockScreen = pl::EffectiveHostFlags(true, kCaptureFlagLockCredV1 | kCaptureFlagSecureDesktopActive);
  check("lockscreen-cred: lock screen (0x40000): a character, text and a physical key are held back",
        pl::HostAtLogonScreen(lockScreen) && !pl::ViewerMaySendAction(lockScreen, key(5, 'Q')) &&
            !pl::ViewerMaySendAction(lockScreen, text) && !pl::ViewerMaySendAction(lockScreen, phys));
  check("lockscreen-cred: ...Enter, Tab and the pointer go", pl::ViewerMaySendAction(lockScreen, key(5, VK_RETURN)) &&
                                                                 pl::ViewerMaySendAction(lockScreen, key(5, VK_TAB)) &&
                                                                 pl::ViewerMaySendAction(lockScreen, key(1, 0)));
  const uint32_t uac = pl::EffectiveHostFlags(true, kCaptureFlagSecureDesktopActive);
  check("lockscreen-cred: a UAC prompt alone (secure desktop, no 0x40000): characters and text still go",
        !pl::HostAtLogonScreen(uac) && pl::ViewerMaySendAction(uac, key(5, 'Q')) && pl::ViewerMaySendAction(uac, text));
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
