#pragma once

// Input forwarding of the viewer: mouse/key/IME events become ControlInputEvent/Text messages.
//
// Role:    enqueue_input_event / enqueue_input_text_units / enqueue_macro_step (queue for the control
//          thread), the key-forwarding memory (forward_key_down/up, key_event_should_forward,
//          enqueue_release_for_pressed_keys), IME result text, mouse capture release,
//          toggle_macro_window.
// Thread:  UI (WndProc) produces; the control thread drains ctx.control.inputQueue. The macro window replays
//          through enqueue_macro_step on the UI thread.
// Input:   virtual keys, video coordinates, UTF-16 text, macro steps.
// Output:  QueuedControlInputMessage entries; macro recording taps enqueue_input_event.
// Callers: WndProc, the toolbar/macro callbacks in main().
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-9).

#include "viewer_common.hpp"
#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {

void enqueue_control_input_message(ViewerState& ctx, const QueuedControlInputMessage& msg);

void enqueue_input_text_units(ViewerState& ctx, const uint16_t* text, size_t count);

bool local_hotkey_modifiers_active();

/**
 * Whether this virtual key should be forwarded as a key event.
 *
 * Keys that produce a character are excluded, because their character arrives through the
 * text path -- forwarding both injected every printable twice: Korean once as composed text
 * and once as the raw letter the host's English layout makes of the same key. That was the
 * "type 11, get 22" session, with an English echo trailing every Hangul syllable.
 */
bool key_event_should_forward(WPARAM vk);

/** Decides for a down event and records the answer for the matching up. */
bool forward_key_down(ViewerState& ctx, WPARAM vk);

bool forward_key_up(ViewerState& ctx, WPARAM vk);

bool send_ime_result_text(ViewerState& ctx, HWND hwnd, LPARAM imeFlags);

void release_mouse_capture_if_idle(ViewerState& ctx, HWND hwnd);

void enqueue_release_for_pressed_mouse_buttons(ViewerState& ctx);

// Release every key this client has an outstanding down for.
//
// A key-up only arrives if this window still has focus when the key is released. Alt, the Win
// key, and Alt+Tab are all intercepted by the local Windows and steal focus as they do it, so
// their down reaches the host and their up never does -- the host is left holding a modifier
// nobody is pressing, and because it is a real SendInput state it survives the client being
// closed and reopened. Sending the up for everything held, the moment focus is lost, is what
// keeps a modifier from latching on the host.
void enqueue_release_for_pressed_keys(ViewerState& ctx);

void enqueue_input_event(ViewerState& ctx, uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode);
// Host-side IME opt-in (env REMOTE60_HOST_IME): whether the user asked for the host-IME path at all.
bool host_ime_optin();
// Host-side IME active: true when raw physical keys should be forwarded instead of VK/composed text
// (imeMode == Active). See viewer_session_state ImeMode.
bool host_ime_mode(ViewerState& ctx);
// `makeOnly` marks a dedicated Hangul(scan 0xF2)/Hanja(0xF1) toggle key, which hardware reports as a
// make with no break. It is sent as a single down pulse, never tracked as held, and never released.
bool enqueue_physical_key(ViewerState& ctx, bool down, uint16_t vk, uint16_t scan, bool extended,
                          bool repeat, bool makeOnly = false);

/** A replayed step carries its own recorded button state instead of today's live one. */
void enqueue_macro_step(ViewerState& ctx, const remote60::native_poc::MacroStep& step);

void toggle_macro_window(ViewerState& ctx, HWND owner);

}  // namespace remote60::native_poc::viewer
