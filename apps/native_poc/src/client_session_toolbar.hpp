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
#include <string>
#include <vector>

#include <windows.h>

namespace remote60::native_poc {

struct SessionToolbarCallbacks {
  std::function<void()> onTargets;  // back to the capture-target picker
  std::function<void()> onMacro;    // show/hide the macro window
  /**
   * Chords the local OS would otherwise eat, sent to the HOST instead.
   *
   * Win+D and Alt+Tab never reach the remote machine by being typed: Windows acts on them here,
   * on the viewer's own desktop. A button is the only way to aim them at the other end.
   */
  std::function<void()> onShowDesktop;   // Win+D on the host
  std::function<void()> onSwitchWindow;  // Alt+Tab on the host
  std::function<void(uint32_t monitorId)> onMonitor;
  /**
   * Where this window says what it did with a click.
   *
   * "대상 선택 does nothing" has several possible meanings and the toolbar can distinguish three
   * of them on its own: the press was never recorded, the pointer left the button before release,
   * or the callback was invoked and the rest of the chain is where it stopped. Without this they
   * all look the same from outside -- two of the branches below `return 0` in silence.
   *
   * Optional. Diagnostics only; it must not decide anything.
   */
  std::function<void(const std::string& line)> onLog;
};

struct SessionToolbarMonitor {
  uint32_t id = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool primary = false;
};

/**
 * How the session is doing, as one dot.
 *
 * Three states because three is what a colour can carry at a glance, and because the useful
 * question is not a number: is it fine, is it struggling, or has it stopped.
 */
enum class SessionHealth {
  Good,    // green
  Slow,    // amber: still moving, but late
  Broken,  // red: nothing is arriving
};

/**
 * What the dot is decided from. Every field is something the viewer already measures.
 *
 * Pure inputs rather than a pointer to session state so the rules can be tested without a session
 * -- the thresholds are a judgement call and a judgement call that cannot be tested is a guess.
 */
struct SessionHealthInputs {
  bool connected = false;         // the control channel is up
  bool streaming = false;         // a picture is expected (not sitting in the picker)
  bool haveRtt = false;           // a pong has measured one
  uint64_t rttUs = 0;
  uint64_t sinceLastFrameUs = 0;  // since the last frame reached the screen
  bool congested = false;         // the receiver says it is recovering or congested
};

// A round trip past this is "late" rather than "fine"; measured sessions on this project sit well
// under it, and interactive work starts to feel wrong above it.
constexpr uint64_t kHealthSlowRttUs = 150000;  // 150 ms
// No picture for this long, while a picture is expected, is a stop rather than a stutter. Chosen
// at the low end of the 2-3 s the field logs show, so the dot turns red while the user is still
// wondering, not after they have given up.
constexpr uint64_t kHealthBrokenFrameGapUs = 2500000;  // 2.5 s

/**
 * The dot's colour, from what the viewer measured.
 *
 * Broken outranks Slow: a session with nothing arriving is not merely late, and saying "slow"
 * about a dead link is the kind of reassurance that wastes somebody's afternoon.
 */
SessionHealth evaluate_session_health(const SessionHealthInputs& in);

struct SessionToolbarState {
  bool connected = false;
  bool inputOn = false;
  bool macroOpen = false;
  SessionHealth health = SessionHealth::Good;  // the dot beside the frame rate
  bool relay = false;  // the billed path, so it is worth saying out loud
  /**
   * False until something has decided `relay`.
   *
   * Kept separate rather than folded into `relay` because a bool cannot hold three answers, and
   * the third one -- "nobody has said yet" -- is the one the user sees first.
   */
  bool pathKnown = false;
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

/**
 * The one line the bar draws for a given state.
 *
 * Pulled out so it can be asserted without a window, and so the assertion runs the function the
 * product runs rather than a copy of its rules. The bar calls this with its own state.
 */
std::wstring session_toolbar_status_text(const SessionToolbarState& state);

/** Re-anchors to the owner after it moved, resized, or changed show state. */
void session_toolbar_follow_owner();

void session_toolbar_destroy();

}  // namespace remote60::native_poc
