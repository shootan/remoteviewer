// See viewer_input_forward.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_input_forward.hpp"

#include "viewer_common.hpp"
#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {

void enqueue_control_input_message(ViewerState& ctx, const QueuedControlInputMessage& msg) {
  ctx.control.inputQueue.Enqueue(msg);
}

// Host-side IME opt-in: on only when the user set the env AND the host advertised support. Default
// off => the existing VK/composed-text path is completely unchanged (zero regression). (Codex #366.)
bool host_ime_optin() {
  static const bool optIn = [] {
    const char* v = std::getenv("REMOTE60_HOST_IME");
    return v && (v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y');
  }();
  return optIn;
}
// Keys route as physical scans only once negotiation reached Active (local HIMC detached AND the
// host has aligned its IME to English). Before that, imeMode==Disabled and the legacy client-IME
// path runs unchanged -- so English/Korean during the brief connect-time negotiation still work via
// the local IME, and the switch to physical is atomic (Codex Edge 4). The env opt-in and the v2
// capability are folded into imeMode: the control thread only enters Active when both hold.
bool host_ime_mode(ViewerState& ctx) {
  return ctx.session.imeMode.load(std::memory_order_acquire) == 2;
}

// Host-side IME: forward one raw physical key (scan code) so the host IME composes live. Not
// coalesced (type != ControlInputEvent), so keys never merge like moves do.
bool enqueue_physical_key(ViewerState& ctx, bool down, uint16_t vk, uint16_t scan, bool extended,
                          bool repeat, bool makeOnly) {
  if (kInputPolicyForceBlock) return false;
  if (!ctx.session.inputEnabled.load()) return false;
  QueuedControlInputMessage msg{};
  msg.type = MessageType::ControlPhysicalKey;
  msg.physicalKey.header.magic = remote60::native_poc::kMagic;
  msg.physicalKey.header.type = static_cast<uint16_t>(MessageType::ControlPhysicalKey);
  msg.physicalKey.header.size = static_cast<uint16_t>(sizeof(ControlPhysicalKeyMessage));
  msg.physicalKey.seq = ctx.control.inputQueue.NextSequence();
  msg.physicalKey.down = down ? 1 : 0;
  msg.physicalKey.vk = vk;
  msg.physicalKey.scanCode = scan;
  msg.physicalKey.flags =
      (extended ? 0x1u : 0u) | (repeat ? 0x2u : 0u) | (makeOnly ? 0x4u : 0u);
  msg.generatedUs = qpc_now_us();
  enqueue_control_input_message(ctx, msg);
  return true;
}

void enqueue_input_text_units(ViewerState& ctx, const uint16_t* text, size_t count) {
  if (kInputPolicyForceBlock) return;
  if (!ctx.session.inputEnabled.load()) return;
  // Chunking and sequencing are the shared enqueue_control_input_text (F-09), the same code the
  // Android session runs.
  (void)remote60::native_poc::enqueue_control_input_text(ctx.control.inputQueue, text, count,
                                                         qpc_now_us());
}

bool local_hotkey_modifiers_active() {
  return (GetKeyState(VK_CONTROL) < 0) && (GetKeyState(VK_MENU) < 0);
}

/**
 * Whether this virtual key should be forwarded as a key event.
 *
 * Keys that produce a character are excluded, because their character arrives through the
 * text path -- forwarding both injected every printable twice: Korean once as composed text
 * and once as the raw letter the host's English layout makes of the same key. That was the
 * "type 11, get 22" session, with an English echo trailing every Hangul syllable.
 */
bool key_event_should_forward(WPARAM vk) {
  // Only a key the IME is composing with stays off the key path -- its result arrives instead
  // through WM_IME_COMPOSITION as committed text. Everything else -- letters, digits, space,
  // symbols, function keys, Enter, Tab, Backspace -- goes as a key event.
  //
  // The earlier attempt gated this on "does the key produce a character" via MapVirtualKey,
  // which pushed digits, space, and symbols onto the text path. That path shares the IME
  // result-suppression counter, and once a Korean syllable committed, the counter was off by
  // enough to swallow the next few printables -- the "type it several times before it lands"
  // report. Routing every non-composed key as a key event keeps them clear of that counter
  // entirely; only Hangul, which genuinely needs composition, takes the text path.
  return vk != VK_PROCESSKEY;
}

/** Decides for a down event and records the answer for the matching up. */
bool forward_key_down(ViewerState& ctx, WPARAM vk) {
  const bool forward = key_event_should_forward(vk);
  if (vk < 256) ctx.input.forwardedKeyDown[vk].store(forward, std::memory_order_relaxed);
  return forward;
}

bool forward_key_up(ViewerState& ctx, WPARAM vk) {
  if (vk < 256) return ctx.input.forwardedKeyDown[vk].exchange(false, std::memory_order_relaxed);
  return key_event_should_forward(vk);
}

bool send_ime_result_text(ViewerState& ctx, HWND hwnd, LPARAM imeFlags) {
  if ((imeFlags & GCS_RESULTSTR) == 0) return false;
  HIMC imc = ImmGetContext(hwnd);
  if (!imc) return false;
  const LONG bytes = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
  if (bytes <= 0) {
    ImmReleaseContext(hwnd, imc);
    return false;
  }
  std::vector<uint16_t> text(static_cast<size_t>(bytes) / sizeof(uint16_t));
  const LONG copied = ImmGetCompositionStringW(imc, GCS_RESULTSTR, text.data(), bytes);
  ImmReleaseContext(hwnd, imc);
  if (copied <= 0 || text.empty()) return false;
  enqueue_input_text_units(ctx, text.data(), text.size());
  return true;
}

void release_mouse_capture_if_idle(ViewerState& ctx, HWND hwnd) {
  if ((ctx.input.mouseButtons.load(std::memory_order_relaxed) & 0x7u) == 0 && GetCapture() == hwnd) {
    ReleaseCapture();
  }
}

void enqueue_release_for_pressed_mouse_buttons(ViewerState& ctx) {
  const uint16_t buttons = ctx.input.mouseButtons.exchange(0, std::memory_order_acq_rel);
  if ((buttons & 0x7u) == 0) return;
  const int32_t vx = ctx.input.lastVideoX.load(std::memory_order_relaxed);
  const int32_t vy = ctx.input.lastVideoY.load(std::memory_order_relaxed);
  if ((buttons & 0x4u) != 0) enqueue_input_event(ctx, 3, vx, vy, 0, VK_MBUTTON);
  if ((buttons & 0x2u) != 0) enqueue_input_event(ctx, 3, vx, vy, 0, VK_RBUTTON);
  if ((buttons & 0x1u) != 0) enqueue_input_event(ctx, 3, vx, vy, 0, VK_LBUTTON);
}

// Release every key this client has an outstanding down for.
//
// A key-up only arrives if this window still has focus when the key is released. Alt, the Win
// key, and Alt+Tab are all intercepted by the local Windows and steal focus as they do it, so
// their down reaches the host and their up never does -- the host is left holding a modifier
// nobody is pressing, and because it is a real SendInput state it survives the client being
// closed and reopened. Sending the up for everything held, the moment focus is lost, is what
// keeps a modifier from latching on the host.
void enqueue_release_for_pressed_keys(ViewerState& ctx) {
  for (int vk = 0; vk < 256; ++vk) {
    if (ctx.input.forwardedKeyDown[vk].exchange(false, std::memory_order_relaxed)) {
      enqueue_input_event(ctx, 6, 0, 0, 0, static_cast<uint32_t>(vk));
    }
  }
}

void enqueue_input_event(ViewerState& ctx, uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode) {
  if (kInputPolicyForceBlock) return;
  if (!ctx.session.inputEnabled.load()) return;
  // The message is the shared make_control_input_event (F-09); only the live button state and
  // the macro tap are this side's.
  const QueuedControlInputMessage msg = remote60::native_poc::make_control_input_event(
      ctx.control.inputQueue, kind, ctx.input.mouseButtons.load(), x, y, wheelDelta, keyCode,
      qpc_now_us());
  // Recording taps the send path, so the macro sees exactly what the host will see -- the
  // engine keeps pointer actions and drops keys on its own.
  if (ctx.input.macro.IsRecording()) {
    ctx.input.macro.RecordEvent(msg.inputEvent, GetTickCount64());
  }
  enqueue_control_input_message(ctx, msg);
}

/** A replayed step carries its own recorded button state instead of today's live one. */
void enqueue_macro_step(ViewerState& ctx, const remote60::native_poc::MacroStep& step) {
  if (kInputPolicyForceBlock) return;
  if (!ctx.session.inputEnabled.load()) return;
  enqueue_control_input_message(
      ctx, remote60::native_poc::make_control_input_event(ctx.control.inputQueue, step.kind,
                                                          step.buttons, step.x, step.y,
                                                          step.wheelDelta, step.keyCode,
                                                          qpc_now_us()));
}

void toggle_macro_window(ViewerState& ctx, HWND owner) {
  remote60::native_poc::MacroWindowHooks hooks;
  hooks.macro = &ctx.input.macro;
  // The macro window is destroyed at shutdown, before ctx goes away.
  hooks.sendStep = [&ctx](const remote60::native_poc::MacroStep& step) { enqueue_macro_step(ctx, step); };
  remote60::native_poc::macro_window_toggle(GetModuleHandleW(nullptr), owner, hooks);
  if (owner) InvalidateRect(owner, nullptr, FALSE);
}

}  // namespace remote60::native_poc::viewer
