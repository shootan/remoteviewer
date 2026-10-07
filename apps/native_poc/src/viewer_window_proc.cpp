// See viewer_window_proc.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_window_proc.hpp"

#include <imm.h>
#pragma comment(lib, "imm32.lib")

#include <random>
#include <set>
#include <sstream>

#include "clipboard_win32.hpp"
#include "viewer_clip_transfer_bar.hpp"
#include "viewer_common.hpp"
#include "mouse_button_map.hpp"  // after viewer_common.hpp: it owns the windows.h configuration
#include "viewer_cursor_overlay.hpp"
#include "viewer_gdi_util.hpp"
#include "viewer_state.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_paste_gate.hpp"
#include "viewer_paste_ui.hpp"
#include "viewer_unlock.hpp"
#include "viewer_layout.hpp"
#include "viewer_log.hpp"
#include "viewer_nv12_renderer.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"
#include "viewer_present.hpp"
#include "viewer_session_watchdog.hpp"

#include <iostream>

namespace remote60::native_poc::viewer {

namespace {
// Host-side IME state (UI thread only). Detaching the local IME makes keys arrive as raw VK (no
// VK_PROCESSKEY) with no local composition -- the host IME composes instead. (Codex #370.)
HIMC gPrevHimc = nullptr;
bool gImeDetached = false;
std::set<uint16_t> gPhysicalDown;  // pressed physical keys: scan | (E0 ? 0x100 : 0)

void ensure_local_ime_off(HWND hwnd) {
  if (gImeDetached) return;
  gPrevHimc = ImmAssociateContext(hwnd, nullptr);  // keep the previous context so we can restore it
  gImeDetached = true;
}
void restore_local_ime(HWND hwnd) {
  if (!gImeDetached) return;
  ImmAssociateContext(hwnd, gPrevHimc);
  gImeDetached = false;
  gPrevHimc = nullptr;
}
// Release every physical key still held (focus loss / mode change / destroy), so a modifier can't
// strand on the host. (Codex #370 BLOCKER 6.)
void release_all_physical(ViewerState& ctx) {
  for (auto it = gPhysicalDown.begin(); it != gPhysicalDown.end();) {
    const uint16_t key = *it;
    if (enqueue_physical_key(ctx, false, 0, static_cast<uint16_t>(key & 0xff), (key & 0x100) != 0,
                             false)) {
      it = gPhysicalDown.erase(it);  // released; forget it
    } else {
      ++it;  // enqueue rejected (input disabled): keep it for a later retry, do not lose the edge
    }
  }
}
// One physical key transition: detach IME on the first down, track pressed state, forward the scan.
// A key-up is only forwarded for a key we actually sent as down (Codex #370): a spurious SYS key-up
// (e.g. after a swallowed hotkey down) must not be injected on the host.
void forward_physical(ViewerState& ctx, HWND hwnd, WPARAM wp, LPARAM lp, bool down) {
  const uint16_t scan = static_cast<uint16_t>((lp >> 16) & 0xff);
  const bool ext = (lp & (1 << 24)) != 0;
  const uint16_t key = static_cast<uint16_t>(scan | (ext ? 0x100 : 0));
  // Dedicated Hangul(0xF2)/Hanja(0xF1) keys: hardware emits a make with no break, so they must be
  // sent as a one-shot down pulse and never tracked -- tracking them would strand a phantom "held"
  // key that the host then tries to release with a break that never physically happens. Identify by
  // the actual scan code, NOT the VK: a Right-Alt/Right-Ctrl remapped to VK_HANGUL/VK_HANJA keeps a
  // normal make/break scan (0x38/0x1D +E0) and must stay on the tracked path. (Codex Edge 1.)
  const bool makeOnly = (scan == 0xF1 || scan == 0xF2);
  if (makeOnly) {
    if (down) {  // up (if the stack even sends one) is ignored: nothing to release
      ensure_local_ime_off(hwnd);
      (void)enqueue_physical_key(ctx, true, static_cast<uint16_t>(wp), scan, ext,
                                 (lp & (1 << 30)) != 0, /*makeOnly=*/true);
    }
    return;
  }
  if (down) {
    ensure_local_ime_off(hwnd);
    // Track only keys we actually enqueued, so a down dropped while input is disabled cannot leave a
    // stale entry whose up gets sent later. (Codex 3rd review.)
    if (enqueue_physical_key(ctx, true, static_cast<uint16_t>(wp), scan, ext, (lp & (1 << 30)) != 0)) {
      gPhysicalDown.insert(key);
    }
  } else if (gPhysicalDown.count(key) > 0) {
    // Only forget the key once the up is actually accepted; otherwise a disabled-input window would
    // clear the set while the host still holds the key down. (Codex 4th review.)
    if (enqueue_physical_key(ctx, false, static_cast<uint16_t>(wp), scan, ext, false)) {
      gPhysicalDown.erase(key);
    }
  }
}
}  // namespace

// WM_RBUTTONDOWN / WM_RBUTTONUP / WM_MBUTTONDOWN / WM_MBUTTONUP: identical apart from the button bit
// (2 = right, 4 = middle) and the virtual key the host receives.
// Whether the touch path's mouse-suppression window is still open, said out loud the first time
// it swallows something.
//
// Touch generates synthetic mouse messages after the pointer messages, and this 300ms window is
// what stops them being handled twice. The cost is that during those 300ms a real mouse click is
// dropped before it reaches the picker, in silence -- the branches below are plain `return 0`.
// One line per window, not per message: a suppressed drag is hundreds of moves.
bool mouse_suppressed(ViewerState& ctx, const char* what) {
  const uint64_t until = ctx.input.suppressMouseUntilUs.load(std::memory_order_relaxed);
  const uint64_t now = qpc_now_us();
  if (now >= until) return false;
  ctx.input.suppressedMouseCount.fetch_add(1, std::memory_order_relaxed);
  if (ctx.input.suppressReportedForUs.exchange(until, std::memory_order_relaxed) != until) {
    std::cout << "[native-video-client][picker] mouse " << what
              << " ignored: touch suppresses the mouse for another " << ((until - now) / 1000)
              << "ms\n";
  }
  return true;
}

// A press forwarded to the host owns its release even outside the video or over local UI.
// Coordinate rejection must never strand the remote button. UI-only presses have no bit set.
bool release_forwarded_button(ViewerState& ctx, HWND hwnd, uint16_t bit, uint32_t vk, int x, int y) {
  if ((ctx.input.mouseButtons.load(std::memory_order_relaxed) & bit) == 0) return false;
  int32_t vx = ctx.input.lastVideoX.load(std::memory_order_relaxed);
  int32_t vy = ctx.input.lastVideoY.load(std::memory_order_relaxed);
  (void)map_client_point_to_video_coords(ctx, hwnd, x, y, &vx, &vy);
  ctx.input.mouseButtons.fetch_and(static_cast<uint16_t>(~bit));
  enqueue_input_event(ctx, 3, vx, vy, 0, vk);
  release_mouse_capture_if_idle(ctx, hwnd);
  return true;
}

LRESULT on_secondary_button(ViewerState& ctx, HWND hwnd, bool down, uint16_t buttonBit, uint32_t vk, int x, int y) {
  if (!down && release_forwarded_button(ctx, hwnd, buttonBit, vk, x, y)) return 0;
  if (mouse_suppressed(ctx, "secondary button")) return 0;
  if (point_in_toggle_button(ctx, hwnd, x, y)) return 0;
  if (point_in_macro_button(ctx, hwnd, x, y)) return 0;
  if (ctx.picker.visible.load(std::memory_order_relaxed)) return 0;
  if (point_in_panel_ui(ctx, hwnd, x, y)) return 0;
  if (kInputPolicyForceBlock) return 0;
  int32_t vx = 0;
  int32_t vy = 0;
  if (!map_client_point_to_video_coords(ctx, hwnd, x, y, &vx, &vy)) return 0;
  if (down) {
    SetCapture(hwnd);
    ctx.input.mouseButtons.fetch_or(buttonBit);
    enqueue_input_event(ctx, 2, vx, vy, 0, vk);
  } else {
    ctx.input.mouseButtons.fetch_and(static_cast<uint16_t>(~buttonBit));
    enqueue_input_event(ctx, 3, vx, vy, 0, vk);
    release_mouse_capture_if_idle(ctx, hwnd);
  }
  return 0;
}

// The picker's DOWN (mouse WM_LBUTTONDOWN and touch WM_POINTERDOWN): remember which target (if any)
// this press started on; the UP handler only selects when it ends on the same one. A press on empty
// picker space latches "none", and so does a press within the first 300ms after the picker appeared
// -- the gesture must START after the picker is stable, or a long-press begun against the old
// screen could still select (PickerState::PressTarget).
void picker_press(ViewerState& ctx, HWND hwnd, const ClientLayout& layout, int x, int y) {
  uint64_t pressedId = kPickerPressNone;
  uint64_t hitId = 0;
  if (point_in_rect(layout.desktopButtonRect, x, y)) {
    pressedId = 0;
  } else if (try_hit_window_list_item(ctx, hwnd, x, y, &hitId)) {
    pressedId = hitId;
  }
  ctx.picker.PressTarget(pressedId, qpc_now_us());
}

// The picker's UP (mouse WM_LBUTTONUP and touch WM_POINTERUP). Consumes the press latch first --
// any UP ends the gesture. Refresh needs no latch; selecting needs a picker that has been up for a
// moment (a click begun before it appeared must not land on a card) AND a DOWN that started on the
// same target (PickerState::SelectAllowed). Desktop is an explicit WindowSelect(0) even when desktop
// is already the selected target: one clean restart with a fresh generation, so the first-frame
// gate has something to wait on.
void picker_release(ViewerState& ctx, HWND hwnd, const ClientLayout& layout, int x, int y, const char* source) {
  const uint64_t pressedId = ctx.picker.ReleaseTarget();
  if (point_in_rect(layout.refreshButtonRect, x, y)) {
    queue_window_list_request(ctx, "window_list_request pending");
    InvalidateRect(hwnd, nullptr, FALSE);
    return;
  }
  const uint64_t nowUs = qpc_now_us();
  const uint64_t shownAgeMs = ctx.picker.ShownAgeMs(nowUs);
  if (point_in_rect(layout.desktopButtonRect, x, y)) {
    if (ctx.picker.SelectAllowed(pressedId, 0, nowUs) &&
        begin_pc_target_selection(ctx, 0, "desktop_select_requested")) {
      std::cout << "[native-video-client][picker] select source=" << source << " x=" << x << " y=" << y
                << " id=0 shownAgeMs=" << shownAgeMs << "\n";
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return;
  }
  uint64_t hitWindowId = 0;
  if (try_hit_window_list_item(ctx, hwnd, x, y, &hitWindowId)) {
    if (ctx.picker.SelectAllowed(pressedId, hitWindowId, nowUs) &&
        begin_pc_target_selection(ctx, hitWindowId, "window_select_requested")) {
      std::cout << "[native-video-client][picker] select source=" << source << " x=" << x << " y=" << y
                << " id=" << hitWindowId << " shownAgeMs=" << shownAgeMs << "\n";
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  }
}

// The Ctrl+Alt local hotkeys of WM_KEYDOWN: F5 refresh the window list, F9 capture overview,
// [ ] bitrate down/up, ; ' keyint down/up. True when consumed.
bool on_local_hotkey(ViewerState& ctx, HWND hwnd, WPARAM wp) {
  if (local_hotkey_modifiers_active() && wp == 'U') {  // Ctrl+Alt+U: unlock the host lock screen
    if (!has_unlock_password(0)) {
      std::wstring pw;
      if (!prompt_unlock_password(hwnd, &pw)) return true;  // cancelled
      save_unlock_password(0, pw);
      if (!pw.empty()) SecureZeroMemory(&pw[0], pw.size() * sizeof(wchar_t));
    }
    ctx.session.unlockRequested.store(true, std::memory_order_release);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_F5) {
    queue_window_list_request(ctx, "window_list_request pending");
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_F9) {
    request_capture_overview_mode(ctx);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_4) {  // [
    apply_runtime_tune_delta(ctx, -1, 0);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_6) {  // ]
    apply_runtime_tune_delta(ctx, 1, 0);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_1) {  // ;
    apply_runtime_tune_delta(ctx, 0, -1);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_7) {  // '
    apply_runtime_tune_delta(ctx, 0, 1);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- paste on demand (t-y4wj64jw)
//
// A copy on this PC changes nothing on the remote PC. Ctrl+V / Shift+Insert in this window sends the
// clipboard as it is at that moment, and the paste key follows only once the remote PC has said the
// content is on ITS clipboard. Until then the V / Insert is held here (never in the input queue,
// which drops what waits over 2 s), the modifiers that already reached the host are released at
// once, and the key sequence is rebuilt from scratch when the answer comes. A failure sends no key:
// the remote PC's older clipboard is never pasted in the user's name.

namespace {

PasteGate gPaste;
LatestCopy gLatestCopy;

// What a gesture found on this PC's clipboard, read on this thread at the moment of the gesture.
struct PasteSnapshot {
  PasteFormat format = PasteFormat::None;
  uint32_t revision = 0;  // GetClipboardSequenceNumber at the gesture
  std::u16string text;
  uint64_t hash = 0;
  remote60::native_poc::ClipSnapshot image;
  std::vector<std::wstring> paths;  // never logged
};
PasteSnapshot gWaitingSnap;  // the gesture waiting behind the pending one

// r4: the host's copy generation when this PC's clipboard last changed, and the paste whose check
// (one fresh poll) is outstanding with the snapshot it will send if the host has not copied since.
uint64_t gGenAtLocalCopy = 0;
bool gGenAtLocalValid = false;
uint64_t gLocalCopySeq = 0;  // r5: the clipboard sequence of the last copy here (its baseline poll's token)
bool gLocalBasePending = false;  // r6: that copy's baseline poll is still on its way

void publish_local_copy_state(ViewerState& ctx, bool pending) {
  gLocalBasePending = pending;
  ctx.control.clipboard.localCopySeq.store(gLocalCopySeq, std::memory_order_release);
}
uint64_t gProbeId = 0;
PasteSnapshot gProbeSnap;

// Modifiers released on the host at a gesture while still held here. The next ordinary key that goes
// out first puts them back down, so Ctrl kept held after a paste still makes Ctrl+A a Ctrl+A.
constexpr uint8_t kModCtrl = 1;
constexpr uint8_t kModShift = 2;
uint8_t gReleasedMods = 0;

// The bar's line after a paste that did not happen (5 s), and what Retry repeats.
struct PasteLine {
  std::wstring text;
  bool retry = false;
  bool failed = false;
  uint64_t untilUs = 0;
};
PasteLine gPasteLine;
struct PasteGesture {
  PasteKey key = PasteKey::None;
  uint16_t scan = 0;
  bool ext = false;
};
PasteGesture gLastFailed;

void paste_log(const std::string& line) { std::cout << "[native-video-client][paste] " << line << "\n"; }

uint64_t new_paste_id() {
  static std::mt19937_64 rng{static_cast<uint64_t>(std::random_device{}()) ^ qpc_now_us()};
  uint64_t v = 0;
  while (v == 0) v = rng();
  return v;
}

PasteMods local_paste_mods() {
  PasteMods m;
  m.ctrl = GetKeyState(VK_CONTROL) < 0;
  m.shift = GetKeyState(VK_SHIFT) < 0;
  m.alt = GetKeyState(VK_MENU) < 0;
  m.win = GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;
  return m;
}

bool is_modifier_vk(WPARAM wp) {
  switch (wp) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
      return true;
    default:
      return false;
  }
}

uint16_t lp_scan(LPARAM lp) { return static_cast<uint16_t>((lp >> 16) & 0xff); }
bool lp_ext(LPARAM lp) { return (lp & (1 << 24)) != 0; }

// The feature applies: input goes to the host, clipboard sync is on, and the host has clipboard sync.
bool paste_feature_on(ViewerState& ctx) {
  const auto& clip = ctx.control.clipboard;
  return ctx.session.inputEnabled.load() && clip.enabled.load(std::memory_order_relaxed) &&
         clip.hostSupports.load(std::memory_order_relaxed);
}
uint64_t paste_conn_gen(ViewerState& ctx) { return ctx.control.clipboard.connGen.load(std::memory_order_acquire); }
uint64_t paste_target_gen(ViewerState& ctx) { return ctx.sel.epoch.load(std::memory_order_acquire); }

bool paste_retryable(PasteFailure f) {
  switch (f) {
    case PasteFailure::Timeout:
    case PasteFailure::HostWrite:
    case PasteFailure::Refused:
    case PasteFailure::HelperUnavailable:
    case PasteFailure::Link:
    case PasteFailure::ReadFailed:
      return true;
    default:
      return false;
  }
}

void show_paste_failure(PasteFailure f, PasteKey key, uint16_t scan, bool ext) {
  const std::wstring text = paste_failure_text(f);
  if (text.empty()) return;
  gPasteLine.text = text;
  gPasteLine.failed = f != PasteFailure::Cancelled && f != PasteFailure::Busy;
  gPasteLine.retry = paste_retryable(f);
  gPasteLine.untilUs = qpc_now_us() + remote60::native_poc::kClipBarResultUs;
  gLastFailed = PasteGesture{key, scan, ext};
  remote60::native_poc::clip_transfer_bar_refresh();
}

// Whether the down of this key reached the host (so its repeats and its up follow it).
bool key_forwarded(ViewerState& ctx, WPARAM wp, LPARAM lp) {
  if (host_ime_mode(ctx)) return gPhysicalDown.count(static_cast<uint16_t>(lp_scan(lp) | (lp_ext(lp) ? 0x100 : 0))) > 0;
  return wp < 256 && ctx.input.forwardedKeyDown[wp].load(std::memory_order_relaxed);
}

// The modifiers that already reached the host go up NOW: they must not stay held on the remote PC
// for as long as the paste takes. The user's own release later finds nothing to release and is
// dropped by the trackers, as for any key the host never saw.
void release_paste_modifiers(ViewerState& ctx) {
  struct Vk {
    UINT vk;
    uint8_t bit;
  };
  for (const Vk& k : {Vk{VK_CONTROL, kModCtrl}, Vk{VK_LCONTROL, kModCtrl}, Vk{VK_RCONTROL, kModCtrl},
                      Vk{VK_SHIFT, kModShift}, Vk{VK_LSHIFT, kModShift}, Vk{VK_RSHIFT, kModShift}}) {
    if (forward_key_up(ctx, k.vk)) {
      enqueue_input_event(ctx, 6, 0, 0, 0, k.vk);
      gReleasedMods |= k.bit;
    }
  }
  struct Scan {
    uint16_t key;
    UINT vk;
    uint8_t bit;
  };
  for (const Scan& s : {Scan{0x1D, VK_CONTROL, kModCtrl}, Scan{0x11D, VK_CONTROL, kModCtrl},
                        Scan{0x2A, VK_SHIFT, kModShift}, Scan{0x36, VK_SHIFT, kModShift}}) {
    if (gPhysicalDown.count(s.key) == 0) continue;
    if (enqueue_physical_key(ctx, false, static_cast<uint16_t>(s.vk), static_cast<uint16_t>(s.key & 0xff),
                             (s.key & 0x100) != 0, false)) {
      gPhysicalDown.erase(s.key);
      gReleasedMods |= s.bit;
    }
  }
}

uint8_t local_mod_mask() {
  return static_cast<uint8_t>((GetKeyState(VK_CONTROL) < 0 ? kHeldCtrl : 0) | (GetKeyState(VK_SHIFT) < 0 ? kHeldShift : 0));
}

// Whether the host holds this modifier down as far as this viewer sent it (either key of the pair).
bool host_holds_mod(ViewerState& ctx, uint8_t bit) {
  if (host_ime_mode(ctx)) {
    return bit == kHeldCtrl ? (gPhysicalDown.count(0x1D) || gPhysicalDown.count(0x11D))
                            : (gPhysicalDown.count(0x2A) || gPhysicalDown.count(0x36));
  }
  const auto& down = ctx.input.forwardedKeyDown;
  return bit == kHeldCtrl ? (down[VK_CONTROL].load() || down[VK_LCONTROL].load() || down[VK_RCONTROL].load())
                          : (down[VK_SHIFT].load() || down[VK_LSHIFT].load() || down[VK_RSHIFT].load());
}

// Puts the host's Ctrl / Shift where `wanted` says. For keys replayed after a paste: each goes out
// with the modifiers that were held when it was typed, not with whatever is held now.
void sync_host_modifiers(ViewerState& ctx, uint8_t wanted) {
  for (const uint8_t bit : {kHeldCtrl, kHeldShift}) {
    const bool want = (wanted & bit) != 0;
    const bool holds = host_holds_mod(ctx, bit);
    if (want && !holds) {
      const UINT vk = bit == kHeldCtrl ? VK_CONTROL : VK_SHIFT;
      const uint16_t scan = bit == kHeldCtrl ? 0x1D : 0x2A;
      if (host_ime_mode(ctx)) {
        if (enqueue_physical_key(ctx, true, static_cast<uint16_t>(vk), scan, false, false)) gPhysicalDown.insert(scan);
      } else if (forward_key_down(ctx, vk)) {
        enqueue_input_event(ctx, 5, 0, 0, 0, vk);
      }
    } else if (!want && holds) {
      for (const UINT vk : bit == kHeldCtrl ? std::initializer_list<UINT>{VK_CONTROL, VK_LCONTROL, VK_RCONTROL}
                                            : std::initializer_list<UINT>{VK_SHIFT, VK_LSHIFT, VK_RSHIFT}) {
        if (forward_key_up(ctx, vk)) enqueue_input_event(ctx, 6, 0, 0, 0, vk);
      }
      for (const uint16_t key : bit == kHeldCtrl ? std::initializer_list<uint16_t>{0x1D, 0x11D}
                                                 : std::initializer_list<uint16_t>{0x2A, 0x36}) {
        if (gPhysicalDown.count(key) &&
            enqueue_physical_key(ctx, false, static_cast<uint16_t>(bit == kHeldCtrl ? VK_CONTROL : VK_SHIFT),
                                 static_cast<uint16_t>(key & 0xff), (key & 0x100) != 0, false)) {
          gPhysicalDown.erase(key);
        }
      }
    }
    gReleasedMods = static_cast<uint8_t>(gReleasedMods & ~bit);
  }
}

// Before an ordinary key goes out: a modifier released at a gesture and still held here goes back
// down first, so the host sees the combination the user is typing.
void resync_released_modifiers(ViewerState& ctx) {
  struct Mod {
    uint8_t bit;
    UINT vk;
    uint16_t scan;
  };
  for (const Mod& m : {Mod{kModCtrl, VK_CONTROL, 0x1D}, Mod{kModShift, VK_SHIFT, 0x2A}}) {
    if ((gReleasedMods & m.bit) == 0) continue;
    gReleasedMods = static_cast<uint8_t>(gReleasedMods & ~m.bit);
    if (GetKeyState(m.vk) >= 0) continue;  // let go meanwhile: nothing to put back
    if (host_ime_mode(ctx)) {
      if (enqueue_physical_key(ctx, true, static_cast<uint16_t>(m.vk), m.scan, false, false)) gPhysicalDown.insert(m.scan);
    } else if (forward_key_down(ctx, m.vk)) {
      enqueue_input_event(ctx, 5, 0, 0, 0, m.vk);
    }
  }
}

// One key edge on its way to the host, by whichever path this session uses -- the forwarding the
// window procedure always did, plus the modifier bookkeeping above.
// `replay`: a key held during a paste, sent afterwards -- its modifiers were put in place by the
// caller from what was held when it was typed, so the live keyboard is not consulted.
void forward_key(ViewerState& ctx, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool replay = false) {
  const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
  if (down && !replay && !is_modifier_vk(wp)) resync_released_modifiers(ctx);
  if (!down) {
    if (wp == VK_CONTROL || wp == VK_LCONTROL || wp == VK_RCONTROL) gReleasedMods &= static_cast<uint8_t>(~kModCtrl);
    if (wp == VK_SHIFT || wp == VK_LSHIFT || wp == VK_RSHIFT) gReleasedMods &= static_cast<uint8_t>(~kModShift);
  }
  if (host_ime_mode(ctx)) {
    forward_physical(ctx, hwnd, wp, lp, down);
    return;
  }
  if (down) {
    if (forward_key_down(ctx, wp)) enqueue_input_event(ctx, 5, 0, 0, 0, static_cast<uint32_t>(wp));
  } else if (forward_key_up(ctx, wp)) {
    enqueue_input_event(ctx, 6, 0, 0, 0, static_cast<uint32_t>(wp));
  }
}

// The paste itself, complete and balanced, on the path this session sends keys by.
void send_paste_chord(ViewerState& ctx, const PasteGate::Ticket& t) {
  const bool physical = host_ime_mode(ctx);
  for (const PasteStep& s : paste_chord(t.key, t.scan, t.ext)) {
    if (physical) {
      (void)enqueue_physical_key(ctx, s.down, s.vk, s.scan, s.ext, false);
    } else {
      enqueue_input_event(ctx, s.down ? 5 : 6, 0, 0, 0, s.vk);
    }
  }
}

bool local_clipboard_pasteable() {
  static const UINT kPng = RegisterClipboardFormatW(L"PNG");
  return IsClipboardFormatAvailable(CF_HDROP) || IsClipboardFormatAvailable(CF_UNICODETEXT) ||
         IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) ||
         IsClipboardFormatAvailable(CF_BITMAP) || (kPng && IsClipboardFormatAvailable(kPng));
}

// The clipboard as it is now, in the order a copy is read: files, an image, text (Q3: nothing is
// substituted -- an image that cannot go is a failure, not its text).
PasteFailure read_paste_snapshot(ViewerState& ctx, HWND hwnd, PasteSnapshot* s) {
  s->revision = GetClipboardSequenceNumber();
  std::vector<std::wstring> paths;
  if (!remote60::native_poc::clipboard_read_file_paths(hwnd, remote60::native_poc::file_copy::kMaxFiles + 1, &paths)) {
    return PasteFailure::ReadFailed;
  }
  if (!paths.empty()) {
    if (!ctx.control.fileCopy.Usable()) return PasteFailure::Refused;  // the remote PC takes no files
    s->format = PasteFormat::Files;
    s->paths = std::move(paths);
    return PasteFailure::None;
  }
  if (remote60::native_poc::clip_image_available()) {
    remote60::native_poc::ClipSnapshot snap;
    const auto r = remote60::native_poc::clip_image_read_snapshot(hwnd, &snap);
    if (r == remote60::native_poc::ClipSnapshotResult::Ok) {
      if (!ctx.control.clipImage.Usable()) return PasteFailure::Refused;  // the remote PC takes no images
      s->format = PasteFormat::Image;
      s->image = std::move(snap);
      return PasteFailure::None;
    }
    if (r == remote60::native_poc::ClipSnapshotResult::TooLarge) return PasteFailure::TooLarge;
    if (r != remote60::native_poc::ClipSnapshotResult::NoImage) return PasteFailure::ReadFailed;
  }
  std::wstring wide;
  if (!remote60::native_poc::clipboard_read_unicode_text(hwnd, &wide)) return PasteFailure::ReadFailed;
  if (wide.empty()) return PasteFailure::Empty;
  s->text = remote60::native_poc::wide_to_u16(wide);
  if (s->text.size() > remote60::native_poc::kClipboardTextMaxUtf16) return PasteFailure::TooLarge;
  s->hash = remote60::native_poc::clipboard_fnv1a(s->text);
  s->format = PasteFormat::Text;
  return PasteFailure::None;
}

const char* paste_format_name(PasteFormat f) {
  switch (f) {
    case PasteFormat::Text: return "text";
    case PasteFormat::Image: return "image";
    case PasteFormat::Files: return "files";
    default: return "none";
  }
}

void dispatch_paste(ViewerState& ctx, const PasteGate::Ticket& t, PasteSnapshot snap) {
  std::ostringstream os;
  os << "send id=" << std::hex << t.id << std::dec << " format=" << paste_format_name(t.format)
     << " revision=" << snap.revision;
  switch (t.format) {
    case PasteFormat::Text: {
      os << " utf16Count=" << snap.text.size();
      auto& clip = ctx.control.clipboard;
      std::lock_guard<std::mutex> lock(clip.mu);
      clip.havePasteText = true;
      clip.pasteTextId = t.id;
      clip.pasteTextRevision = snap.revision;
      clip.pasteTextHash = snap.hash;
      clip.pasteText = std::move(snap.text);
      break;
    }
    case PasteFormat::Image:
      ctx.control.clipImage.SubmitSnapshotForPaste(std::move(snap.image), t.id);
      break;
    case PasteFormat::Files:
      os << " files=" << snap.paths.size();
      ctx.control.fileCopy.SubmitLocalFilesForPaste(snap.paths, snap.revision, t.id);
      break;
    default:
      break;
  }
  paste_log(os.str());
}

// A paste given up here: its request is withdrawn where it still can be, so a late write does not
// follow (a text not yet sent is dropped; an image transfer is cancelled; a file answer is ignored).
void abandon_paste(ViewerState& ctx, const PasteGate::Ticket& t) {
  if (t.id != 0 && t.id == gProbeId) {
    gProbeId = 0;
    gProbeSnap = PasteSnapshot{};
    auto& clip = ctx.control.clipboard;
    std::lock_guard<std::mutex> lock(clip.mu);
    if (clip.probeRequested && clip.probeId == t.id) clip.probeRequested = false;
    return;  // nothing of it was sent
  }
  switch (t.format) {
    case PasteFormat::Text: {
      auto& clip = ctx.control.clipboard;
      std::lock_guard<std::mutex> lock(clip.mu);
      if (clip.havePasteText && clip.pasteTextId == t.id) {
        clip.havePasteText = false;
        clip.pasteText.clear();
      }
      break;
    }
    case PasteFormat::Image:
      ctx.control.clipImage.AbandonPaste(t.id);
      break;
    case PasteFormat::Files:
      ctx.control.fileCopy.AbandonPaste(t.id);
      break;
    default:
      break;
  }
}

// The order of a copy here and a copy on the host (r5, F1). What the viewer can know is the host's
// copy generation at a moment it asked; it cannot see when the host's copy happened. So:
//   * each copy here asks for one poll right after it; the generation that poll returns is the
//     copy's BASELINE -- a copy on the host counted by then is taken to be OLDER than this copy;
//   * a later, higher generation is a copy on the host made AFTER this copy: the host's is the newest;
//   * until the baseline is back (one round trip and whatever control work is ahead of it), the
//     order is not known, and this PC's copy wins: a paste in that window sends this PC's copy, and
//     the generation its own check returns becomes the baseline.
// Never a baseline from a generation merely observed earlier: that one may predate a host copy
// that happened before this copy (the r4 defect).
bool remote_copied_since_local(uint64_t gen, bool known) {
  if (!known || !gGenAtLocalValid) return false;  // older host, or the order not known yet: this PC's
  return gen > gGenAtLocalCopy;
}

// The send of a paste whose route is Send: first one fresh poll (the check), then -- only if the
// host has not copied since this PC's copy -- the request. The keys stay held meanwhile.
void begin_send(ViewerState& ctx, const PasteGate::Ticket& t, PasteSnapshot snap) {
  gProbeId = t.id;
  gProbeSnap = std::move(snap);
  auto& clip = ctx.control.clipboard;
  std::lock_guard<std::mutex> lock(clip.mu);
  clip.probeRequested = true;
  clip.probeId = t.id;
}

// r6 (C3): after a failure, timeout or cancel, the held key edges that were dropped. A down that
// already went out (sent between two pastes) is held on the host: its up is sent now, or that key
// stays down there. Downs never sent, and ups of keys the host does not hold, go nowhere.
void release_dropped_keys(ViewerState& ctx, HWND hwnd) {
  size_t released = 0;
  for (const HeldKey& k : gPaste.TakeDropped()) {
    const bool up = k.msg == WM_KEYUP || k.msg == WM_SYSKEYUP;
    if (!up || !key_forwarded(ctx, static_cast<WPARAM>(k.wp), static_cast<LPARAM>(k.lp))) continue;
    forward_key(ctx, hwnd, k.msg, static_cast<WPARAM>(k.wp), static_cast<LPARAM>(k.lp), /*replay=*/true);
    ++released;
  }
  if (released) paste_log("released " + std::to_string(released) + " key(s) whose down had already gone out");
}

void after_paste_settled(ViewerState& ctx, HWND hwnd) {
  PasteGate::Ticket next;
  if (gPaste.Promote(qpc_now_us(), &next)) {
    begin_send(ctx, next, std::move(gWaitingSnap));
    gWaitingSnap = PasteSnapshot{};
  }
  if (!gPaste.Pending()) KillTimer(hwnd, kPasteTimerId);
  remote60::native_poc::clip_transfer_bar_refresh();
}

void cancel_pastes(ViewerState& ctx, HWND hwnd, PasteFailure why, const char* reason) {
  if (!gPaste.Pending() && !gPaste.HasWaiting()) return;
  const std::vector<PasteGate::Ticket> ended = gPaste.CancelAll();
  for (const PasteGate::Ticket& t : ended) abandon_paste(ctx, t);
  release_dropped_keys(ctx, hwnd);
  gWaitingSnap = PasteSnapshot{};
  KillTimer(hwnd, kPasteTimerId);
  paste_log(std::string("cancelled (") + reason + ") ended=" + std::to_string(ended.size()) + " -- no paste key sent");
  if (!ended.empty() && why != PasteFailure::Silent) show_paste_failure(why, ended[0].key, ended[0].scan, ended[0].ext);
  remote60::native_poc::clip_transfer_bar_refresh();
}

void start_paste_gesture(ViewerState& ctx, HWND hwnd, PasteKey key, uint16_t scan, bool ext) {
  release_paste_modifiers(ctx);
  PasteSnapshot snap;
  const PasteFailure readFailure = read_paste_snapshot(ctx, hwnd, &snap);
  if (readFailure != PasteFailure::None) {
    paste_log("not sent: this PC's clipboard " + std::to_string(static_cast<int>(readFailure)) + " -- no paste key sent");
    show_paste_failure(readFailure, key, scan, ext);
    return;
  }
  PasteGate::Ticket t;
  t.id = new_paste_id();
  t.connGen = paste_conn_gen(ctx);
  t.targetGen = paste_target_gen(ctx);
  t.key = key;
  t.scan = scan;
  t.ext = ext;
  t.format = snap.format;
  t.baseGen = gGenAtLocalCopy;
  t.baseValid = gGenAtLocalValid;
  t.localSeq = gLocalCopySeq;
  t.budgetUs = snap.format == PasteFormat::Text    ? kPasteTextDeadlineUs
               : snap.format == PasteFormat::Files ? kPasteFilesDeadlineUs
                                                   : 0;
  switch (gPaste.Admit(t, qpc_now_us())) {
    case PasteGate::Admitted::Started:
      gPasteLine = PasteLine{};
      begin_send(ctx, t, std::move(snap));
      SetTimer(hwnd, kPasteTimerId, kPasteTimerMs, nullptr);
      remote60::native_poc::clip_transfer_bar_refresh();
      break;
    case PasteGate::Admitted::Waiting:
      gWaitingSnap = std::move(snap);
      paste_log("waiting behind the pending paste");
      break;
    case PasteGate::Admitted::Dropped:
      paste_log("dropped: a paste is pending and another is already waiting");
      break;
  }
}

void on_paste_answer(ViewerState& ctx, HWND hwnd, const PasteAnswer& a) {
  if (a.probe && a.baseline) {
    // The poll right after a copy here: its generation marks that copy -- if it is still the last one.
    if (a.id == gLocalCopySeq && a.connGen == paste_conn_gen(ctx) && !gGenAtLocalValid) {
      if (a.genKnown) {
        gGenAtLocalCopy = a.hostCopyGen;
        gGenAtLocalValid = true;
        paste_log("baseline for this PC's copy: host copyGen=" + std::to_string(a.hostCopyGen));
      }
      publish_local_copy_state(ctx, /*pending=*/false);
    }
    return;
  }
  if (a.probe) {
    // The check before sending. Only the paste it was asked for, still pending, takes it.
    if (a.id == 0 || a.id != gProbeId || !gPaste.Pending() || gPaste.pending().id != a.id) {
      paste_log("check answer ignored (not the pending paste)");
      return;
    }
    gProbeId = 0;
    PasteSnapshot snap = std::move(gProbeSnap);
    gProbeSnap = PasteSnapshot{};
    if (a.failure != PasteFailure::None) {
      PasteAnswer fail = a;
      fail.probe = false;
      on_paste_answer(ctx, hwnd, fail);  // the link went: the paste fails as any other would
      return;
    }
    // Compared with the baseline of the copy THIS paste took, fixed at its gesture -- a copy made
    // here since then does not move it. No baseline yet: the order is unknown and this PC's copy
    // wins; this check's value becomes the baseline if that copy is still the last one here.
    const PasteGate::Ticket& tk = gPaste.pending();
    // r6 (C1): this answer may change what the viewer thinks of THIS PC's copy only while the copy
    // this paste took is still the last one here, on the connection it was asked on. Otherwise it
    // decides this paste alone.
    const bool current = tk.localSeq == gLocalCopySeq && tk.connGen == paste_conn_gen(ctx) &&
                         a.connGen == paste_conn_gen(ctx);
    bool hostNewer = false;
    if (a.genKnown) {
      if (tk.baseValid) {
        hostNewer = a.hostCopyGen > tk.baseGen;
      } else if (current && !gGenAtLocalValid) {
        gGenAtLocalCopy = a.hostCopyGen;
        gGenAtLocalValid = true;
        publish_local_copy_state(ctx, /*pending=*/false);
      }
    }
    if (hostNewer) {
      // A copy made on the host after this PC's: the host pastes its own. Nothing is sent; the key
      // goes as the paste would have, rebuilt, now.
      if (current) gLatestCopy.NoteRemoteCopy();
      paste_log("pass-through (check): remote copy on the host (any format) copyGen=" + std::to_string(a.hostCopyGen) +
                " > " + std::to_string(tk.baseGen) + " -- nothing sent");
      PasteAnswer own = a;
      own.probe = false;
      own.applied = true;
      on_paste_answer(ctx, hwnd, own);
      return;
    }
    dispatch_paste(ctx, gPaste.pending(), std::move(snap));
    return;
  }
  PasteGate::Ticket done;
  const PasteGate::Outcome o = gPaste.OnAnswer(a.id, a.applied, paste_conn_gen(ctx), paste_target_gen(ctx), &done);
  std::ostringstream os;
  os << "answer id=" << std::hex << a.id << std::dec << " format=" << paste_format_name(a.format)
     << " applied=" << (a.applied ? 1 : 0) << " failure=" << static_cast<int>(a.failure) << " detail=0x" << std::hex
     << a.detail << std::dec;
  if (o == PasteGate::Outcome::Ignored) {
    paste_log(os.str() + " ignored (not the pending paste)");
    return;
  }
  const uint64_t ms = (qpc_now_us() - done.startedUs) / 1000;
  if (o == PasteGate::Outcome::Inject) {
    send_paste_chord(ctx, done);
    // Keys typed while waiting go after the paste, in the order they were typed. With another paste
    // waiting, only those typed BEFORE it go now (r5, F2: paste A, Enter, paste B is A, Enter, B);
    // the ones typed after it wait for it.
    size_t flushed = 0;
    for (const HeldKey& k : gPaste.TakeHeldBeforeWaiting()) {
      const bool keyDown = k.msg == WM_KEYDOWN || k.msg == WM_SYSKEYDOWN;
      if (keyDown && !is_modifier_vk(static_cast<WPARAM>(k.wp))) sync_host_modifiers(ctx, k.mods);
      forward_key(ctx, hwnd, k.msg, static_cast<WPARAM>(k.wp), static_cast<LPARAM>(k.lp), /*replay=*/true);
      ++flushed;
    }
    // Then the modifiers as they are on this keyboard now.
    if (flushed) sync_host_modifiers(ctx, local_mod_mask());
    paste_log(os.str() + " -> paste key sent ms=" + std::to_string(ms) + " heldKeysSent=" + std::to_string(flushed));
  } else {
    // Applied but on another connection or target: the key would land where the user did not paste.
    const PasteFailure f = a.applied ? PasteFailure::Silent : a.failure;
    paste_log(os.str() + " -> no paste key sent ms=" + std::to_string(ms) +
              (a.applied ? " (connection or target changed)" : ""));
    show_paste_failure(f, done.key, done.scan, done.ext);
    release_dropped_keys(ctx, hwnd);
  }
  after_paste_settled(ctx, hwnd);
}

void on_paste_timer(ViewerState& ctx, HWND hwnd) {
  PasteGate::Ticket expired;
  if (gPaste.OnTick(qpc_now_us(), &expired)) {
    abandon_paste(ctx, expired);
    paste_log("timed out id=" + std::to_string(expired.id) + " format=" + paste_format_name(expired.format) +
              " -- no paste key sent");
    show_paste_failure(PasteFailure::Timeout, expired.key, expired.scan, expired.ext);
    release_dropped_keys(ctx, hwnd);
    after_paste_settled(ctx, hwnd);
  }
  if (!gPaste.Pending()) KillTimer(hwnd, kPasteTimerId);
}

// What a paste gesture leads to, for a key press and for the bar's Retry alike.
bool run_paste_gesture(ViewerState& ctx, HWND hwnd, PasteKey key, uint16_t scan, bool ext, const char* source) {
  // r4: a copy already seen on the host after this PC's (the last poll) makes the host's the newest.
  if (gLatestCopy.LocalIsLatest() &&
      remote_copied_since_local(ctx.control.clipboard.hostCopyGen.load(std::memory_order_acquire),
                                ctx.control.clipboard.hostCopyGenKnown.load(std::memory_order_acquire))) {
    gLatestCopy.NoteRemoteCopy();
    paste_log("remote copy on the host (any format): the remote PC's clipboard is the newest");
  }
  const PasteRoute route = route_paste(paste_feature_on(ctx),
                                       ctx.control.clipboard.hostPasteOnDemand.load(std::memory_order_acquire),
                                       gLatestCopy.LocalIsLatest());
  switch (route) {
    case PasteRoute::Forward:
      return false;
    case PasteRoute::PassThrough:
      paste_log(std::string("pass-through (") + source + "): the remote PC's copy is the newest");
      return false;
    case PasteRoute::UpdateNeeded:
      paste_log(std::string("update needed (") + source + "): the host does not confirm a paste -- no paste key sent");
      show_paste_failure(PasteFailure::UpdateNeeded, key, scan, ext);
      return true;
    case PasteRoute::Send:
      start_paste_gesture(ctx, hwnd, key, scan, ext);
      return true;
  }
  return false;
}

// True when paste on demand took the key: held behind a pending paste, swallowed, or a gesture.
bool paste_intercept(ViewerState& ctx, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
  const bool repeat = down && (lp & (1 << 30)) != 0;
  const bool mod = is_modifier_vk(wp);
  if (gPaste.Pending()) {
    if (down && wp == VK_ESCAPE) {
      cancel_pastes(ctx, hwnd, PasteFailure::Cancelled, "escape");
      return true;
    }
    if (down) {
      if (!mod) {
        const PasteKey k = classify_paste_key(static_cast<uint32_t>(wp), local_paste_mods());
        if (k != PasteKey::None) {
          if (!repeat) start_paste_gesture(ctx, hwnd, k, lp_scan(lp), lp_ext(lp));
          return true;  // a repeat of the held paste key is not another paste
        }
      }
      if (!gPaste.Hold(HeldKey{msg, static_cast<uint64_t>(wp), static_cast<int64_t>(lp), local_mod_mask()})) {
        cancel_pastes(ctx, hwnd, PasteFailure::Cancelled, "too many keys while waiting");
      }
      return true;
    }
    // An up is held only when its down is; any other release goes now -- releases are never delayed.
    if (gPaste.HoldsDown(static_cast<uint64_t>(wp))) {
      if (!gPaste.Hold(HeldKey{msg, static_cast<uint64_t>(wp), static_cast<int64_t>(lp), local_mod_mask()})) {
        // r7 (D3): this up is the edge that did not fit. The paste is cancelled, and the up goes on
        // like any release: to the host if it holds the key (its down went out before), else nowhere.
        cancel_pastes(ctx, hwnd, PasteFailure::Cancelled, "too many keys while waiting");
        return false;
      }
      return true;
    }
    return false;
  }
  if (!down || mod) return false;
  const PasteMods mods = local_paste_mods();
  const PasteKey k = classify_paste_key(static_cast<uint32_t>(wp), mods);
  if (k == PasteKey::None) {
    if (!repeat && paste_feature_on(ctx) && is_copy_key(static_cast<uint32_t>(wp), mods)) {
      gLatestCopy.NoteRemoteCopy();
      paste_log("copy key sent to the remote PC: its clipboard is the newest copy");
    }
    return false;
  }
  // A held paste key's repeats follow its first press: forwarded if that went, swallowed if not.
  if (repeat) return !key_forwarded(ctx, wp, lp);
  return run_paste_gesture(ctx, hwnd, k, lp_scan(lp), lp_ext(lp), "key");
}

LRESULT on_key_message(ViewerState& ctx, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
  if (down && on_local_hotkey(ctx, hwnd, wp)) return 0;
  if (kInputPolicyForceBlock) return 0;
  if (paste_intercept(ctx, hwnd, msg, wp, lp)) return 0;
  forward_key(ctx, hwnd, msg, wp, lp);
  return 0;
}

// r7 (D1): a change of this PC's clipboard that this thread has not handled yet -- a copy here not
// recorded yet. Its notification is in this queue: the change has closed the clipboard, which is when
// the system posts it.
bool clipboard_change_unhandled(HWND hwnd) {
  MSG m;
  return PeekMessageW(&m, hwnd, WM_CLIPBOARDUPDATE, WM_CLIPBOARDUPDATE, PM_NOREMOVE) != FALSE;
}

// The remote PC's text, written here only if it is newer than this PC's copy by the order rule
// (r5 F1, r6 C2) -- decided on this thread, which records the copies here, against the copy state as
// it is now (r7 D1). The last check is made with the clipboard held, when nothing else can change it:
// a change whose notification is still queued (a copy here not recorded yet) puts the text back behind
// it, to be decided again once that copy is.
void apply_remote_text(ViewerState& ctx, HWND hwnd, std::unique_ptr<RemoteTextApply> item) {
  auto& clip = ctx.control.clipboard;
  if (item->connGen != paste_conn_gen(ctx)) {
    paste_log("remote text not written here: it came on an earlier connection");
    return;
  }
  if (item->hasCopyGen && (gLocalBasePending || (gGenAtLocalValid && item->copyGen <= gGenAtLocalCopy))) {
    paste_log("remote text not written here: older than this PC's copy (copyGen=" + std::to_string(item->copyGen) + ")");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(clip.mu);
    if (clip.core.CheckRemoteData(item->text, item->hash) != remote60::native_poc::ClipboardRemoteDecision::Apply) return;
  }
  bool refused = false;
  const bool written = remote60::native_poc::clipboard_set_unicode_text_if(
      hwnd, remote60::native_poc::u16_to_wide(item->text), [hwnd] { return !clipboard_change_unhandled(hwnd); }, &refused);
  if (refused) {
    if (item->deferrals >= 8) {
      paste_log("remote text not written here: this PC's clipboard kept changing");
      return;
    }
    ++item->deferrals;
    RemoteTextApply* again = item.release();
    if (!PostMessageW(hwnd, kMsgApplyClipboard, 0, reinterpret_cast<LPARAM>(again))) delete again;
    return;
  }
  if (!written) return;
  clip.ownWriteSeq.store(GetClipboardSequenceNumber(), std::memory_order_relaxed);
  {
    // Recorded now, before the notification the write provokes is handled (queued, this thread).
    std::lock_guard<std::mutex> lock(clip.mu);
    clip.core.NoteApplied(item->hash);
  }
  gLatestCopy.NoteRemoteCopy();  // a copy made on the remote PC: the next Ctrl+V pastes it there
}

// The remote PC's files, published here only if newer than this PC's copy -- the text's rule (r7 D2).
// The clipboard revision decided on is read with the clipboard held, and only when no change here is
// waiting to be handled; the helper then publishes only over that revision (it checks with the
// clipboard held), so a copy made here after this decision is never covered by the late publish.
bool gToldUnorderedFiles = false;
void decide_remote_files(ViewerState& ctx, HWND hwnd, std::unique_ptr<RemoteFilesDecide> d) {
  if (d->connGen != paste_conn_gen(ctx)) {
    paste_log("remote files not published here: they came on an earlier connection");
    return;
  }
  if (d->files.hasCopyGen && (gLocalBasePending || (gGenAtLocalValid && d->files.copyGen <= gGenAtLocalCopy))) {
    paste_log("remote files not published here: older than this PC's copy (copyGen=" + std::to_string(d->files.copyGen) + ")");
    return;
  }
  if (!d->files.hasCopyGen && !gToldUnorderedFiles) {
    gToldUnorderedFiles = true;
    paste_log("this host does not order its file copies (older host): its files are published without the order rule");
  }
  if (!remote60::native_poc::clipboard_open_with_retry(hwnd)) {
    paste_log("remote files not published here: this PC's clipboard is busy");
    return;
  }
  const bool unhandled = clipboard_change_unhandled(hwnd);
  const uint32_t seq = static_cast<uint32_t>(GetClipboardSequenceNumber());
  CloseClipboard();
  if (unhandled) {
    if (d->deferrals >= 8) {
      paste_log("remote files not published here: this PC's clipboard kept changing");
      return;
    }
    ++d->deferrals;
    RemoteFilesDecide* again = d.release();
    if (!PostMessageW(hwnd, kMsgDecideRemoteFiles, 0, reinterpret_cast<LPARAM>(again))) delete again;
    return;
  }
  ctx.control.fileCopy.PublishApproved(std::move(d->files.pub), seq);
}

// A clipboard change on this PC. Nothing is sent; it only decides what the next Ctrl+V pastes.
void note_local_clipboard_change(ViewerState& ctx) {
  auto& clip = ctx.control.clipboard;
  if (GetClipboardSequenceNumber() == clip.ownWriteSeq.load(std::memory_order_relaxed)) return;  // the host's text, written here
  // Files the remote PC copied arrive through THIS PC's clipboard helper: its publish leaves the
  // clipboard owned by the helper's process. That owner -- not the virtual-file formats, which any
  // program can offer (r5, F3) -- is what makes this change the remote PC's copy.
  DWORD owner = 0;
  if (HWND o = GetClipboardOwner()) GetWindowThreadProcessId(o, &owner);
  const DWORD helper = ctx.control.fileCopy.HelperPid();
  if (owner != 0 && helper != 0 && owner == helper) {
    gLatestCopy.NoteRemoteCopy();
    paste_log("remote copy arrived (files, published here by this PC's helper pid " + std::to_string(helper) +
              "): the remote PC's clipboard is the newest");
    return;
  }
  gLatestCopy.NoteLocalCopy();
  // r5 (F1): this copy's baseline is the host generation of a poll made after it, not the last one seen.
  gLocalCopySeq = GetClipboardSequenceNumber();
  gGenAtLocalValid = false;
  publish_local_copy_state(ctx, /*pending=*/true);
  {
    std::lock_guard<std::mutex> lock(clip.mu);
    clip.baselineRequested = true;
    clip.baselineToken = gLocalCopySeq;
  }
}

}  // namespace

remote60::native_poc::ClipPasteBarView paste_bar_view(ViewerState& ctx) {
  remote60::native_poc::ClipPasteBarView v;
  if (gPaste.Pending()) {
    if (gPaste.pending().format == PasteFormat::Image) {
      const auto p = ctx.control.clipImage.GetProgress();
      if (p.active || p.cancelling) return v;  // the image's own line: percent, seconds, Cancel
    }
    v.active = true;
    v.text = L"붙여넣을 내용을 원격 PC에 보내는 중…";
    v.cancel = true;
    return v;
  }
  if (gPasteLine.untilUs > qpc_now_us()) {
    v.active = true;
    v.text = gPasteLine.text;
    v.retry = gPasteLine.retry;
    v.failed = gPasteLine.failed;
  }
  return v;
}

void paste_cancel_from_bar(ViewerState& ctx) {
  if (ctx.session.hwnd) cancel_pastes(ctx, ctx.session.hwnd, PasteFailure::Cancelled, "bar");
}

void paste_retry_from_bar(ViewerState& ctx) {
  if (!ctx.session.hwnd || gLastFailed.key == PasteKey::None || gPaste.Pending()) return;
  gPasteLine = PasteLine{};
  const PasteGesture g = gLastFailed;
  if (!run_paste_gesture(ctx, ctx.session.hwnd, g.key, g.scan, g.ext, "retry")) {
    paste_log("retry not sent: the remote PC's copy is the newest, or the feature is off");
  }
  remote60::native_poc::clip_transfer_bar_refresh();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  // The state arrives with the creation parameters and is pinned to the window for its lifetime
  // (F-17). It is stamped with the handle here, before CreateWindowExW returns, so the messages the
  // creation itself generates already see the window the state describes.
  if (msg == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
    auto* state = create ? static_cast<ViewerState*>(create->lpCreateParams) : nullptr;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    if (state) state->session.hwnd = hwnd;
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  auto* state = reinterpret_cast<ViewerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!state) return DefWindowProcW(hwnd, msg, wp, lp);
  ViewerState& ctx = *state;
  ctx.session.uiHeartbeatUs.store(qpc_now_us(), std::memory_order_relaxed);
  switch (msg) {
    case WM_CLOSE:
      ctx.session.running = false;
      if (ctx.session.sock != INVALID_SOCKET) shutdown(ctx.session.sock, SD_BOTH);
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      cancel_pastes(ctx, hwnd, PasteFailure::Silent, "window destroyed");
      RemoveClipboardFormatListener(hwnd);  // stop hearing clipboard changes (K1)
      release_all_physical(ctx);  // nothing should stay held on the host
      restore_local_ime(hwnd);  // re-attach the IME we detached for host-side IME mode
      remote60::native_poc::session_toolbar_destroy();
      remote60::native_poc::clip_transfer_bar_destroy();
      destroy_cached_gdi_objects(ctx);
      PostQuitMessage(0);
      return 0;
    case WM_CLIPBOARDUPDATE:
      // Paste on demand: a copy here changes nothing on the remote PC. It only makes this PC's
      // clipboard the newest copy, which is what the next Ctrl+V in this window will send.
      note_local_clipboard_change(ctx);
      return 0;
    case kMsgPushClipboardNow:
      // A clipboard session opened. Nothing is pushed any more; what this PC holds is the newest
      // copy if it holds anything that can be pasted (Q2: it was copied before connecting).
      gLatestCopy.ResetForSession(local_clipboard_pasteable());
      gGenAtLocalValid = false;  // this session's first generation is the baseline
      publish_local_copy_state(ctx, /*pending=*/false);
      if (gLatestCopy.LocalIsLatest()) {  // the copy made before connecting gets its baseline now
        gLocalCopySeq = GetClipboardSequenceNumber();
        publish_local_copy_state(ctx, /*pending=*/true);
        std::lock_guard<std::mutex> lock(ctx.control.clipboard.mu);
        ctx.control.clipboard.baselineRequested = true;
        ctx.control.clipboard.baselineToken = gLocalCopySeq;
      }
      paste_log(std::string("session: newest copy is ") + (gLatestCopy.LocalIsLatest() ? "this PC's" : "the remote PC's"));
      return 0;
    case kMsgPasteResult: {
      std::unique_ptr<PasteAnswer> answer(reinterpret_cast<PasteAnswer*>(lp));
      if (answer) on_paste_answer(ctx, hwnd, *answer);
      return 0;
    }
    case kMsgDecideRemoteFiles: {
      std::unique_ptr<RemoteFilesDecide> d(reinterpret_cast<RemoteFilesDecide*>(lp));
      if (d) decide_remote_files(ctx, hwnd, std::move(d));
      return 0;
    }
    case kMsgApplyClipboard: {
      // The control thread handed us the host's clipboard text (heap-allocated) with what decides
      // whether it lands here (r7). Written, it is recorded as applied, so the WM_CLIPBOARDUPDATE the
      // write provokes is recognised as an echo and not sent back.
      std::unique_ptr<RemoteTextApply> item(reinterpret_cast<RemoteTextApply*>(lp));
      if (item) apply_remote_text(ctx, hwnd, std::move(item));
      return 0;
    }
    case kMsgApplyWindowList: {
      // Ownership of the copy arrives with the message (F-07). A message still queued when the
      // window dies is one small leak at exit, which is cheaper than a drain protocol.
      std::unique_ptr<ControlWindowListMessage> msg(reinterpret_cast<ControlWindowListMessage*>(lp));
      if (msg) apply_window_list_snapshot(ctx, *msg);
      return 0;
    }
    case kMsgHostImeActivate: {
      // Host aligned its IME (to EN) and confirmed; flip to physical routing atomically on this
      // thread. Cancel any in-flight local composition first, then detach the local IME so no local
      // composer competes with the host, then arm Active. (Codex Edge 4.)
      if (HIMC imc = ImmGetContext(hwnd)) {
        (void)ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        (void)ImmReleaseContext(hwnd, imc);
      }
      ensure_local_ime_off(hwnd);
      ctx.session.imeReportedOpen.store(static_cast<int>(wp), std::memory_order_relaxed);
      ctx.session.imeMode.store(2, std::memory_order_release);  // Active
      std::cout << "[native-video-client][ime] activated reportedOpen=" << static_cast<int>(wp)
                << " (0=EN 1=KR 2=?)\n";
      return 0;
    }
    case kMsgControlResumed: {
      // The control channel is back on the same session. Nothing about the local key
      // state can be trusted across that gap: a key-up sent while the tunnel was dead is
      // simply gone, and the host is still holding whatever was down when it broke. So
      // the same release-all the window runs when focus leaves, for the same reason --
      // and NOT a replay of anything, which would turn one keystroke into two.
      //
      // Wider than focus loss (RV-01): every modifier gets an up whether or not this client
      // remembers pressing it (the same list as session start), and mouse buttons are released
      // too -- a drag that was under way when the channel died left its button down on the host.
      cancel_pastes(ctx, hwnd, PasteFailure::Silent, "control resumed");
      int heldKeys = 0;
      for (int vk = 0; vk < 256; ++vk) {
        if (ctx.input.forwardedKeyDown[vk].load(std::memory_order_relaxed)) ++heldKeys;
      }
      const unsigned heldButtons = ctx.input.mouseButtons.load(std::memory_order_relaxed) & kMouseWireMask;
      int modifierUps = 0;
      if (!kInputPolicyForceBlock) {
        modifierUps = enqueue_release_all_modifiers(ctx);
        enqueue_release_for_pressed_keys(ctx);
        enqueue_release_for_pressed_mouse_buttons(ctx);
        release_all_physical(ctx);
        release_mouse_capture_if_idle(ctx, hwnd);
      }
      ctx.picker.CancelPress();
      // What was actually done, not what was meant to be.
      std::cout << "[native-video-client][control-resume] release: "
                << (kInputPolicyForceBlock ? "skipped (input blocked by policy)" : "sent")
                << " modifierUps=" << modifierUps << " heldKeysBefore=" << heldKeys
                << " heldButtonsBefore=0x" << std::hex << heldButtons << std::dec << "\n";
      return 0;
    }
    case kMsgHostImeDeactivate: {
      // Leaving host-IME (capability lost / reconnect / shutdown): stop physical routing first, send
      // ups for anything still held on the host, then restore the local IME for the legacy path.
      ctx.session.imeMode.store(0, std::memory_order_release);  // Disabled
      release_all_physical(ctx);
      restore_local_ime(hwnd);
      ctx.session.imeReportedOpen.store(-1, std::memory_order_relaxed);
      std::cout << "[native-video-client][ime] deactivated (legacy client IME restored)\n";
      return 0;
    }
    case kMsgRevealStreamView: {
      // The video thread saw the first frame of a selection and posted this once; CommitReveal
      // revalidates against the live selection state (see viewer_selection_gate.cpp).
      if (ctx.sel.CommitReveal()) {
        // A new stream episode begins here: the per-episode UI-thread state must not carry over
        // from the previous target (F-14). reportedSecure is control-thread state and is left
        // alone -- it is "say it once per process", which is still the right cadence.
        ctx.present.ResetForNewEpisode();
        ctx.session.nextToolbarPushUs = 0;
        // Dropping the picker guard opens both the paint path and the input guard (input handlers
        // early-return while the picker is up); clearing pending re-enables the picker's buttons.
        ctx.picker.visible.store(false, std::memory_order_relaxed);
        clear_pc_target_selection(ctx);
        remote60::native_poc::session_toolbar_set_visible(true);
        push_session_toolbar_state(ctx);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      ctx.sel.ReleaseRevealLatch();
      return 0;
    }
    // The toolbar is a window of its own, so it does not move with this one for free.
    case WM_WINDOWPOSCHANGED: {
      remote60::native_poc::session_toolbar_follow_owner();
      remote60::native_poc::clip_transfer_bar_follow_owner();
      // A resize has to reach the picker. This class sets no CS_HREDRAW/CS_VREDRAW and there is no
      // WM_SIZE handler, so shrinking the window repaints nothing. That was invisible while the
      // picker was GDI drawn into a window the screen was ignoring; now that the picker is
      // presented through the swapchain it shows as the previous size stretched, because DXGI
      // scales the last presented frame until something presents at the new one.
      const WINDOWPOS* pos = reinterpret_cast<const WINDOWPOS*>(lp);
      if (pos && (pos->flags & SWP_NOSIZE) == 0 &&
          ctx.picker.visible.load(std::memory_order_relaxed)) {
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_DPICHANGED: {
      ensure_ui_font(ctx, hwnd);
      const RECT* suggested = reinterpret_cast<const RECT*>(lp);
      if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;
    }
    case WM_MOUSEMOVE:
      // The toolbar hides itself so it stops blocking clicks, which leaves it deaf: a hidden
      // window gets no mouse events, so this window watches for the summoning dwell for it.
      if (!ctx.picker.visible.load(std::memory_order_relaxed)) {
        RECT toolbarZone{};
        GetClientRect(hwnd, &toolbarZone);
        remote60::native_poc::session_toolbar_notify_mouse(GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
                                                           toolbarZone.right);
      }
      if (mouse_suppressed(ctx, "move")) return 0;
      if (point_in_toggle_button(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (ctx.picker.visible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      if ((ctx.input.mouseButtons.load(std::memory_order_relaxed) & kMouseWireMask) == 0) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        ctx.input.moveGeneratedCount.fetch_add(1, std::memory_order_relaxed);  // P0 telemetry (#351)
        enqueue_input_event(ctx, 1, vx, vy, 0, 0);
      }
      return 0;
    case WM_LBUTTONDOWN:
      cancel_pastes(ctx, hwnd, PasteFailure::Cancelled, "mouse");
      if (mouse_suppressed(ctx, "down")) return 0;
      if (point_in_toggle_button(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        ctx.picker.toggleDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (point_in_macro_button(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        ctx.picker.macroButtonDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (ctx.picker.visible.load(std::memory_order_relaxed)) {
        if (ctx.sel.pending.load(std::memory_order_acquire)) {
          ctx.picker.CancelPress();
          return 0;
        }
        picker_press(ctx, hwnd, compute_client_layout(ctx, hwnd), GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
      }
      if (point_in_panel_ui(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      SetFocus(hwnd);
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        ctx.input.mouseButtons.fetch_or(1);
        enqueue_input_event(ctx, 2, vx, vy, 0, VK_LBUTTON);
      }
      return 0;
    case WM_LBUTTONUP: {
      if (release_forwarded_button(ctx, hwnd, 1, VK_LBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (mouse_suppressed(ctx, "up")) return 0;
      const int x = GET_X_LPARAM(lp);
      const int y = GET_Y_LPARAM(lp);
      const ClientLayout layout = compute_client_layout(ctx, hwnd);
      if (ctx.picker.toggleDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.toggleButtonRect, x, y)) {
          set_picker_visible_and_sync_stream(ctx, 
              !ctx.picker.visible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (ctx.picker.macroButtonDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.macroButtonRect, x, y)) {
          toggle_macro_window(ctx, hwnd);
        }
        return 0;
      }
      if (ctx.picker.visible.load(std::memory_order_relaxed)) {
        // A selection already in flight owns the picker until its first frame arrives; ignore
        // further target clicks so a double-click cannot queue a second, racing select. (The
        // latch is dropped either way: any UP ends the gesture.)
        if (ctx.sel.pending.load(std::memory_order_acquire)) {
          ctx.picker.CancelPress();
          return 0;
        }
        picker_release(ctx, hwnd, layout, x, y, "mouse");
        return 0;
      }
      if (point_in_panel_ui(ctx, hwnd, x, y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(ctx, hwnd, x, y, &vx, &vy)) return 0;
        ctx.input.mouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        enqueue_input_event(ctx, 3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(ctx, hwnd);
      }
      return 0;
    }
    case WM_RBUTTONDOWN:
      cancel_pastes(ctx, hwnd, PasteFailure::Cancelled, "mouse");
      return on_secondary_button(ctx, hwnd, true, 2, VK_RBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    case WM_RBUTTONUP:
      return on_secondary_button(ctx, hwnd, false, 2, VK_RBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    case WM_MBUTTONDOWN:
      cancel_pastes(ctx, hwnd, PasteFailure::Cancelled, "mouse");
      return on_secondary_button(ctx, hwnd, true, 4, VK_MBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    case WM_MBUTTONUP:
      return on_secondary_button(ctx, hwnd, false, 4, VK_MBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    // WM_XBUTTONDOWN / UP: the back and forward buttons (mouse-xbutton r1). The same secondary
    // path as right and middle, with two differences the Win32 contract imposes: which of the two
    // buttons it is travels in the HIWORD of wParam, and a handled message is answered TRUE rather
    // than 0. DBLCLK is the second press of a double-click, delivered instead of a DOWN only to a
    // class with CS_DBLCLKS -- this class has none, but a press must not be lost if that changes.
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP: {
      const uint32_t vk = mouse_xbutton_to_vk(GET_XBUTTON_WPARAM(wp));
      const uint16_t bit = mouse_vk_to_wire(vk);
      if (bit == 0) return TRUE;  // neither X1 nor X2: nothing to forward
      const bool down = msg != WM_XBUTTONUP;
      if (down) cancel_pastes(ctx, hwnd, PasteFailure::Cancelled, "mouse");
      if (down && !ctx.session.hostMouseXButtons.load(std::memory_order_acquire)) {
        // The host has not said it takes X buttons: an older host, or no pong yet. Its input
        // path turned an unknown button key into a LEFT click, so nothing is sent -- said once.
        // (An up is left to the ordinary path: a held X bit can only exist against a host that
        // took the down, and the wire fence drops anything else.)
        if (!ctx.input.xButtonRefusalReported.exchange(true, std::memory_order_relaxed)) {
          std::cout << "[native-video-client][input] mouse X button ignored: the host does not "
                       "advertise kCaptureFlagMouseXButtonsV1 (older host, or no pong yet)\n";
        }
        return TRUE;
      }
      (void)on_secondary_button(ctx, hwnd, down, bit, vk, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      return TRUE;
    }
    case WM_MOUSEWHEEL: {
      if (mouse_suppressed(ctx, "wheel")) return 0;
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      const ClientLayout layout = compute_client_layout(ctx, hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) return 0;
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) return 0;
      if (ctx.picker.visible.load(std::memory_order_relaxed)) {
        if (point_in_rect(layout.listRect, p.x, p.y)) {
          const int wheel = GET_WHEEL_DELTA_WPARAM(wp);
          scroll_window_list(ctx, hwnd, (wheel < 0) ? 1 : -1);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_panel_ui(ctx, hwnd, p.x, p.y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(ctx, hwnd, p.x, p.y, &vx, &vy)) return 0;
      enqueue_input_event(ctx, 4, vx, vy, GET_WHEEL_DELTA_WPARAM(wp), 0);
      return 0;
    }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP: {
      UINT32 pointerId = GET_POINTERID_WPARAM(wp);
      POINTER_INPUT_TYPE pointerType = PT_POINTER;
      if (!GetPointerType(pointerId, &pointerType) || pointerType != PT_TOUCH) {
        return DefWindowProcW(hwnd, msg, wp, lp);
      }
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      ctx.input.suppressMouseUntilUs.store(qpc_now_us() + 300000ULL, std::memory_order_relaxed);
      const ClientLayout layout = compute_client_layout(ctx, hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          ctx.picker.toggleDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP && ctx.picker.toggleDown.exchange(false, std::memory_order_relaxed)) {
          set_picker_visible_and_sync_stream(ctx, 
              !ctx.picker.visible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          ctx.picker.macroButtonDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP &&
                   ctx.picker.macroButtonDown.exchange(false, std::memory_order_relaxed)) {
          toggle_macro_window(ctx, hwnd);
        }
        return 0;
      }
      if (ctx.picker.visible.load(std::memory_order_relaxed)) {
        // A selection in flight owns the picker; also clear the latch so a gesture spanning the
        // pending window cannot leave a stale press behind.
        if (ctx.sel.pending.load(std::memory_order_acquire)) {
          ctx.picker.CancelPress();
          return 0;
        }
        if (msg == WM_POINTERDOWN) {
          picker_press(ctx, hwnd, layout, p.x, p.y);
          return 0;
        }
        if (msg == WM_POINTERUP) {
          picker_release(ctx, hwnd, layout, p.x, p.y, "touch");
        }
        return 0;
      }
      if (point_in_panel_ui(ctx, hwnd, p.x, p.y)) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(ctx, hwnd, p.x, p.y, &vx, &vy)) return 0;
      if (msg == WM_POINTERDOWN) {
        cancel_pastes(ctx, hwnd, PasteFailure::Cancelled, "touch");
        if (ctx.input.activeTouchDown.load(std::memory_order_relaxed)) return 0;
        SetFocus(hwnd);
        SetCapture(hwnd);
        ctx.input.activeTouchPointerId.store(pointerId, std::memory_order_relaxed);
        ctx.input.activeTouchDown.store(true, std::memory_order_relaxed);
        ctx.input.mouseButtons.fetch_or(1);
        enqueue_input_event(ctx, 2, vx, vy, 0, VK_LBUTTON);
      } else if (msg == WM_POINTERUPDATE) {
        if (!ctx.input.activeTouchDown.load(std::memory_order_relaxed) ||
            ctx.input.activeTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        enqueue_input_event(ctx, 1, vx, vy, 0, 0);
      } else {
        if (!ctx.input.activeTouchDown.load(std::memory_order_relaxed) ||
            ctx.input.activeTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        ctx.input.mouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        ctx.input.activeTouchDown.store(false, std::memory_order_relaxed);
        ctx.input.activeTouchPointerId.store(0, std::memory_order_relaxed);
        enqueue_input_event(ctx, 3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(ctx, hwnd);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
    case WM_POINTERCAPTURECHANGED:
      enqueue_release_for_pressed_mouse_buttons(ctx);
      ctx.input.activeTouchDown.store(false, std::memory_order_relaxed);
      ctx.input.activeTouchPointerId.store(0, std::memory_order_relaxed);
      // A gesture that lost capture mid-flight must not leave a stale picker press behind: the
      // whole point of the latch is that an UP without its own valid DOWN selects nothing.
      ctx.picker.CancelPress();
      return 0;
    case WM_IME_SETCONTEXT: {
      const LPARAM masked =
          lp & ~(static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 0) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 1) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 2) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 3) |
                 static_cast<LPARAM>(ISC_SHOWUIGUIDELINE));
      return DefWindowProcW(hwnd, msg, wp, masked);
    }
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_CHAR:
      return 0;
    case WM_IME_COMPOSITION:
      if (kInputPolicyForceBlock) return 0;
      if (host_ime_mode(ctx)) return 0;  // host composes; no local composition to forward
      (void)send_ime_result_text(ctx, hwnd, lp);
      return 0;
    // Every key edge, legacy VK and host-IME physical alike, goes through the same admission: the
    // local hotkeys, then paste on demand, then the forwarding this window always did.
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
      return on_key_message(ctx, hwnd, msg, wp, lp);
    case WM_SETFOCUS:
      // Detach the local IME before the first keystroke so no first char is eaten as VK_PROCESSKEY.
      if (host_ime_mode(ctx)) ensure_local_ime_off(hwnd);
      return 0;
    case WM_KILLFOCUS:
      // Focus is about to leave, so no more key-ups will reach this window. Release whatever
      // is held now, before Alt/Win/Alt+Tab strands it on the host. A paste waiting for its answer
      // ends without its key: the key would go to wherever the remote focus is by then.
      cancel_pastes(ctx, hwnd, PasteFailure::Silent, "focus left");
      if (!kInputPolicyForceBlock) {
        enqueue_release_for_pressed_keys(ctx);
        release_all_physical(ctx);
      }
      ctx.picker.CancelPress();
      return 0;
    case WM_CHAR:
      // Ignored on purpose. Every physical key now travels the key-event path, and IME
      // composition results travel the text path from WM_IME_COMPOSITION. Emitting text here
      // too would double every printable -- and it was this handler's IME-suppression
      // bookkeeping, drifting after a Hangul commit, that swallowed digits and space. With the
      // two paths cleanly split, there is nothing left for WM_CHAR to do.
      return 0;
    case WM_SYSCHAR:
      return 0;
    case WM_ERASEBKGND:
      // Avoid background erase flicker between frames.
      return 1;
    case WM_TIMER:
      if (wp == kRenderRetryTimerId) {
        KillTimer(hwnd, kRenderRetryTimerId);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (wp == kPacedPresentTimerId) {
        // One-shot: the held frame's wait is over (F-11).
        KillTimer(hwnd, kPacedPresentTimerId);
        request_video_paint(ctx, hwnd);
        return 0;
      }
      if (wp == kPasteTimerId) {
        on_paste_timer(ctx, hwnd);
        return 0;
      }
      if (wp == kCursorOverlayTimerId) {
        update_cursor_overlay(ctx, hwnd);
        // Safety net, not the primary path: if a repaint request was ever lost, this notices that
        // a newer frame is sitting unpresented and asks again. Correctness lives in
        // request_video_paint; this only bounds the damage to one tick. (Viewer ledger F-20.)
        poll_video_paint_liveness(ctx, hwnd);
        // Once a second: is the recv thread moving, is anything arriving, is the session dead?
        poll_session_liveness(ctx, hwnd);
        return 0;
      }
      break;
    case WM_PAINT:
      return paint_video_frame(ctx, hwnd);
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// UNICODE is not defined for this target, so the generic Win32 names resolve to the ANSI
// entry points. This window is registered and created wide, so every message API it touches
// must be the explicit *W form -- DefWindowProcA on a Unicode window read the wide title as
// ANSI and truncated it to "r", and delivered WM_CHAR as ANSI.
bool create_window(ViewerState& ctx) {
  HINSTANCE inst = GetModuleHandle(nullptr);
  const wchar_t* cls = L"Remote60NativeVideoClient";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // Keep background unmanaged so WM_ERASEBKGND can suppress flicker.
  wc.hbrBackground = nullptr;
  wc.lpszClassName = cls;
  if (!RegisterClassExW(&wc)) return false;

  // The user sees this in the taskbar and in Alt-Tab. "remote60 native video client" is the
  // name of a prototype; set_viewer_window_title() puts the target beside it once one is chosen.
  ctx.session.hwnd = CreateWindowExW(0, cls, L"GNLink",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          static_cast<int>(ctx.session.windowW), static_cast<int>(ctx.session.windowH),
                          nullptr, nullptr, inst, &ctx);  // lpParam: WndProc pins it at WM_NCCREATE
  if (!ctx.session.hwnd) return false;
  // Clipboard text sync (K1): hear local clipboard changes on this window's message queue. Fires
  // only on future changes, never on the current contents, so connecting does not sweep whatever is
  // already on the clipboard to the host.
  AddClipboardFormatListener(ctx.session.hwnd);
  ensure_ui_font(ctx, ctx.session.hwnd);
  // The process is per-monitor DPI aware, so the requested size is physical pixels; rescale
  // to keep the intended logical size on scaled displays.
  if (ctx.ui.dpi != 96) {
    SetWindowPos(ctx.session.hwnd, nullptr, 0, 0, dpi_scale(ctx, static_cast<int>(ctx.session.windowW)),
                 dpi_scale(ctx, static_cast<int>(ctx.session.windowH)), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  ShowWindow(ctx.session.hwnd, SW_SHOW);
  UpdateWindow(ctx.session.hwnd);
  // Remote-cursor overlay cadence: 50ms is enough for a 30Hz feed and costs nothing when hidden.
  SetTimer(ctx.session.hwnd, kCursorOverlayTimerId, 50, nullptr);
  // The session starts on the picker; stamp its shown-time so the select debounce has one uniform
  // contract from the very first gesture instead of a special startup exemption.
  ctx.picker.shownAtUs.store(qpc_now_us(), std::memory_order_relaxed);
  return true;
}

}  // namespace remote60::native_poc::viewer
