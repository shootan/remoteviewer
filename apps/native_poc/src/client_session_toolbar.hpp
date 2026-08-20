#pragma once

// The toolbar that sits over the session window.
//
// It has to be its own top-level window rather than something painted into the video window,
// because the video is presented through a flip-model DXGI swap chain: every Present replaces
// the entire client area, and anything GDI drew there -- including the buttons that used to
// live in the overlay -- is gone before the user can see it, let alone click it. A separate
// owned window is composited by the window manager instead, so it survives every frame.
//
// It owns no session state: the caller pushes what should be shown and receives clicks back.

#include <cstdint>
#include <functional>
#include <vector>

#include <windows.h>

namespace remote60::native_poc {

struct SessionToolbarCallbacks {
  std::function<void()> onTargets;  // back to the capture-target picker
  std::function<void()> onMacro;    // show/hide the macro window
  std::function<void(uint32_t monitorId)> onMonitor;
};

struct SessionToolbarMonitor {
  uint32_t id = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool primary = false;
};

struct SessionToolbarState {
  bool connected = false;
  bool inputOn = false;
  bool macroOpen = false;
  bool relay = false;  // the billed path, so it is worth saying out loud
  uint32_t fps = 0;
  uint32_t selectedMonitorId = 0;
  std::vector<SessionToolbarMonitor> monitors;
};

/** Creates the toolbar for `owner`. Later calls are ignored. */
bool session_toolbar_create(HWND owner, SessionToolbarCallbacks callbacks);

/** Shown only while the session view is up: the picker draws its own header. */
void session_toolbar_set_visible(bool visible);

/**
 * The video window's mouse position, forwarded on every move.
 *
 * The bar is a window of its own, so every pixel it covers is a pixel of the remote desktop
 * that cannot be clicked. It therefore hides itself entirely and comes back when the mouse
 * dwells in the top-center band -- and detecting that dwell is the video window's job,
 * because a hidden window receives no mouse events of its own.
 */
void session_toolbar_notify_mouse(int x, int y, int clientWidth);

void session_toolbar_update(const SessionToolbarState& state);

/** Re-anchors to the owner after it moved, resized, or changed show state. */
void session_toolbar_follow_owner();

void session_toolbar_destroy();

}  // namespace remote60::native_poc
