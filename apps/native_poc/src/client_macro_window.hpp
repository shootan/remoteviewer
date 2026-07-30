#pragma once

// The macro tool window for the Windows client: record, review the captured actions, edit or
// drop individual ones, replay with humanised timing, and keep named macros on disk.
//
// Lives in its own top-level window rather than an overlay because, unlike the phone, the
// desktop has room for both: the window sits beside the video and stays up while recording,
// so pause and stop are always one click away.

#include <windows.h>

#include <functional>

#include "input_macro.hpp"

namespace remote60::native_poc {

struct MacroWindowHooks {
  InputMacro* macro = nullptr;
  /** Sends one replayed step to the host, carrying the step's own buttons and key code. */
  std::function<void(const MacroStep&)> sendStep;
};

/** Shows the window (creating it on first use) or hides it if it is up. UI thread only. */
void macro_window_toggle(HINSTANCE instance, HWND owner, const MacroWindowHooks& hooks);

bool macro_window_visible();

/** Tears the window down; safe to call when it was never created. */
void macro_window_destroy();

}  // namespace remote60::native_poc
