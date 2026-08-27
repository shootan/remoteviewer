// See viewer_input_forward.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_input_forward.hpp"

#include "viewer_common.hpp"
#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

void enqueue_control_input_message(const QueuedControlInputMessage& msg) {
  gInputQueueState.Enqueue(msg);
}

void enqueue_input_text_units(const uint16_t* text, size_t count) {
  if (kInputPolicyForceBlock) return;
  if (!gSession.inputEnabled.load()) return;
  if (!text || count == 0) return;
  size_t offset = 0;
  while (offset < count) {
    const size_t remaining = count - offset;
    const size_t chunk = std::min<size_t>(remaining, remote60::native_poc::kControlInputTextMaxUtf16);
    QueuedControlInputMessage msg{};
    msg.type = MessageType::ControlInputText;
    msg.inputText.header.magic = remote60::native_poc::kMagic;
    msg.inputText.header.type = static_cast<uint16_t>(MessageType::ControlInputText);
    msg.inputText.header.size = static_cast<uint16_t>(sizeof(msg.inputText));
    msg.inputText.seq = gInputQueueState.NextSequence();
    msg.inputText.utf16Count = static_cast<uint16_t>(chunk);
    std::memcpy(msg.inputText.utf16, text + offset, chunk * sizeof(uint16_t));
    msg.inputText.clientSendQpcUs = qpc_now_us();
    enqueue_control_input_message(msg);
    offset += chunk;
  }
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
bool forward_key_down(WPARAM vk) {
  const bool forward = key_event_should_forward(vk);
  if (vk < 256) gForwardedKeyDown[vk].store(forward, std::memory_order_relaxed);
  return forward;
}

bool forward_key_up(WPARAM vk) {
  if (vk < 256) return gForwardedKeyDown[vk].exchange(false, std::memory_order_relaxed);
  return key_event_should_forward(vk);
}

bool send_ime_result_text(HWND hwnd, LPARAM imeFlags) {
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
  enqueue_input_text_units(text.data(), text.size());
  return true;
}

void release_mouse_capture_if_idle(HWND hwnd) {
  if ((gMouseButtons.load(std::memory_order_relaxed) & 0x7u) == 0 && GetCapture() == hwnd) {
    ReleaseCapture();
  }
}

void enqueue_release_for_pressed_mouse_buttons() {
  const uint16_t buttons = gMouseButtons.exchange(0, std::memory_order_acq_rel);
  if ((buttons & 0x7u) == 0) return;
  const int32_t vx = gLastInputVideoX.load(std::memory_order_relaxed);
  const int32_t vy = gLastInputVideoY.load(std::memory_order_relaxed);
  if ((buttons & 0x4u) != 0) enqueue_input_event(3, vx, vy, 0, VK_MBUTTON);
  if ((buttons & 0x2u) != 0) enqueue_input_event(3, vx, vy, 0, VK_RBUTTON);
  if ((buttons & 0x1u) != 0) enqueue_input_event(3, vx, vy, 0, VK_LBUTTON);
}

// Release every key this client has an outstanding down for.
//
// A key-up only arrives if this window still has focus when the key is released. Alt, the Win
// key, and Alt+Tab are all intercepted by the local Windows and steal focus as they do it, so
// their down reaches the host and their up never does -- the host is left holding a modifier
// nobody is pressing, and because it is a real SendInput state it survives the client being
// closed and reopened. Sending the up for everything held, the moment focus is lost, is what
// keeps a modifier from latching on the host.
void enqueue_release_for_pressed_keys() {
  for (int vk = 0; vk < 256; ++vk) {
    if (gForwardedKeyDown[vk].exchange(false, std::memory_order_relaxed)) {
      enqueue_input_event(6, 0, 0, 0, static_cast<uint32_t>(vk));
    }
  }
}

uint32_t coord_to_permille(int coord, int extent) {
  if (extent <= 1) return 5000;
  const int clamped = std::clamp(coord, 0, extent - 1);
  const uint64_t numerator = static_cast<uint64_t>(clamped) * 10000ULL +
                             static_cast<uint64_t>((extent - 1) / 2);
  return static_cast<uint32_t>(numerator / static_cast<uint64_t>(extent - 1));
}

void enqueue_input_event(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode) {
  if (kInputPolicyForceBlock) return;
  if (!gSession.inputEnabled.load()) return;
  QueuedControlInputMessage msg{};
  msg.type = MessageType::ControlInputEvent;
  msg.inputEvent.header.magic = remote60::native_poc::kMagic;
  msg.inputEvent.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  msg.inputEvent.header.size = static_cast<uint16_t>(sizeof(msg.inputEvent));
  msg.inputEvent.seq = gInputQueueState.NextSequence();
  msg.inputEvent.kind = kind;
  msg.inputEvent.buttons = gMouseButtons.load();
  msg.inputEvent.x = x;
  msg.inputEvent.y = y;
  msg.inputEvent.wheelDelta = wheelDelta;
  msg.inputEvent.keyCode = keyCode;
  msg.inputEvent.clientSendQpcUs = qpc_now_us();
  // Recording taps the send path, so the macro sees exactly what the host will see -- the
  // engine keeps pointer actions and drops keys on its own.
  if (gInputMacro.IsRecording()) {
    gInputMacro.RecordEvent(msg.inputEvent, GetTickCount64());
  }
  enqueue_control_input_message(msg);
}

/** A replayed step carries its own recorded button state instead of today's live one. */
void enqueue_macro_step(const remote60::native_poc::MacroStep& step) {
  if (kInputPolicyForceBlock) return;
  if (!gSession.inputEnabled.load()) return;
  QueuedControlInputMessage msg{};
  msg.type = MessageType::ControlInputEvent;
  msg.inputEvent.header.magic = remote60::native_poc::kMagic;
  msg.inputEvent.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  msg.inputEvent.header.size = static_cast<uint16_t>(sizeof(msg.inputEvent));
  msg.inputEvent.seq = gInputQueueState.NextSequence();
  msg.inputEvent.kind = step.kind;
  msg.inputEvent.buttons = step.buttons;
  msg.inputEvent.x = step.x;
  msg.inputEvent.y = step.y;
  msg.inputEvent.wheelDelta = step.wheelDelta;
  msg.inputEvent.keyCode = step.keyCode;
  msg.inputEvent.clientSendQpcUs = qpc_now_us();
  enqueue_control_input_message(msg);
}

void toggle_macro_window(HWND owner) {
  remote60::native_poc::MacroWindowHooks hooks;
  hooks.macro = &gInputMacro;
  hooks.sendStep = [](const remote60::native_poc::MacroStep& step) { enqueue_macro_step(step); };
  remote60::native_poc::macro_window_toggle(GetModuleHandleW(nullptr), owner, hooks);
  if (owner) InvalidateRect(owner, nullptr, FALSE);
}

}  // namespace remote60::native_poc::viewer
