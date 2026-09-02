// See host_input_inject.hpp for the module summary. Bodies below are moved verbatim from
// native_video_host_main.cpp (host split refactor Phase 0-1); no logic change. The helpers in the
// anonymous namespace were file-private in the host before and stay private here.

#include <windows.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>

#include "host_input_inject.hpp"
#include "host_string_util.hpp"
#include "host_window_enum.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

namespace {

WPARAM mouse_button_wparam(uint16_t buttons) {
  WPARAM wp = 0;
  if ((buttons & 0x1u) != 0) wp |= MK_LBUTTON;
  if ((buttons & 0x2u) != 0) wp |= MK_RBUTTON;
  if ((buttons & 0x4u) != 0) wp |= MK_MBUTTON;
  return wp;
}

uint16_t mouse_vk_to_mask(uint32_t vk) {
  switch (vk) {
    case VK_LBUTTON:
      return 0x1u;
    case VK_RBUTTON:
      return 0x2u;
    case VK_MBUTTON:
      return 0x4u;
    default:
      return 0x0u;
  }
}

UINT mouse_vk_to_message(uint16_t kind, uint32_t vk) {
  if (kind == 2) {
    if (vk == VK_RBUTTON) return WM_RBUTTONDOWN;
    if (vk == VK_MBUTTON) return WM_MBUTTONDOWN;
    return WM_LBUTTONDOWN;
  }
  if (kind == 3) {
    if (vk == VK_RBUTTON) return WM_RBUTTONUP;
    if (vk == VK_MBUTTON) return WM_MBUTTONUP;
    return WM_LBUTTONUP;
  }
  return 0;
}

bool is_extended_vk(uint32_t vk) {
  switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT:  // Page Up / Page Down
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_NUMLOCK: case VK_SNAPSHOT: case VK_CANCEL:  // Print Screen / Break
    case VK_DIVIDE:   // Numpad /
    case VK_RCONTROL: case VK_RMENU:  // Right Ctrl / Right Alt
    case VK_RETURN:   // Numpad Enter shares VK_RETURN but scan code differs
      return true;
    default:
      return false;
  }
}

LPARAM key_event_lparam(uint32_t keyCode, bool keyUp) {
  const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(keyCode), MAPVK_VK_TO_VSC);
  DWORD lp = 1u | ((scanCode & 0xFFu) << 16);
  if (is_extended_vk(keyCode)) lp |= (1u << 24);  // extended key flag
  if (keyUp) lp |= (1u << 30) | (1u << 31);
  return static_cast<LPARAM>(lp);
}

void set_keyboard_state_flag(BYTE* keyboardState, uint32_t keyCode, bool down) {
  if (!keyboardState || keyCode >= 256) return;
  if (down) {
    keyboardState[keyCode] = static_cast<BYTE>(keyboardState[keyCode] | 0x80u);
  } else {
    keyboardState[keyCode] = static_cast<BYTE>(keyboardState[keyCode] & ~0x80u);
  }
}

void update_synthetic_keyboard_state(BYTE* keyboardState, uint32_t keyCode, bool keyUp) {
  if (!keyboardState || keyCode >= 256) return;
  const bool down = !keyUp;
  set_keyboard_state_flag(keyboardState, keyCode, down);
  switch (keyCode) {
    case VK_SHIFT:
      set_keyboard_state_flag(keyboardState, VK_SHIFT, down);
      set_keyboard_state_flag(keyboardState, VK_LSHIFT, down);
      set_keyboard_state_flag(keyboardState, VK_RSHIFT, down);
      break;
    case VK_LSHIFT:
    case VK_RSHIFT:
      set_keyboard_state_flag(keyboardState, VK_SHIFT, down);
      break;
    case VK_CONTROL:
      set_keyboard_state_flag(keyboardState, VK_CONTROL, down);
      set_keyboard_state_flag(keyboardState, VK_LCONTROL, down);
      set_keyboard_state_flag(keyboardState, VK_RCONTROL, down);
      break;
    case VK_LCONTROL:
    case VK_RCONTROL:
      set_keyboard_state_flag(keyboardState, VK_CONTROL, down);
      break;
    case VK_MENU:
      set_keyboard_state_flag(keyboardState, VK_MENU, down);
      set_keyboard_state_flag(keyboardState, VK_LMENU, down);
      set_keyboard_state_flag(keyboardState, VK_RMENU, down);
      break;
    case VK_LMENU:
    case VK_RMENU:
      set_keyboard_state_flag(keyboardState, VK_MENU, down);
      break;
    case VK_CAPITAL:
      if (down) {
        keyboardState[VK_CAPITAL] = static_cast<BYTE>(keyboardState[VK_CAPITAL] ^ 0x01u);
      }
      break;
    default:
      break;
  }
}

bool keycode_to_unicode_char(uint32_t keyCode, const BYTE* keyboardState, wchar_t* outCh) {
  if (!outCh) return false;
  *outCh = 0;
  BYTE localKeyboardState[256] = {};
  if (keyboardState) {
    std::memcpy(localKeyboardState, keyboardState, sizeof(localKeyboardState));
  } else if (!GetKeyboardState(localKeyboardState)) {
    std::memset(localKeyboardState, 0, sizeof(localKeyboardState));
  }
  const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(keyCode), MAPVK_VK_TO_VSC);
  WCHAR buffer[8] = {};
  const int rc = ToUnicodeEx(static_cast<UINT>(keyCode), scanCode, localKeyboardState, buffer,
                             static_cast<int>(std::size(buffer)), 0, GetKeyboardLayout(0));
  if (rc == 1 && buffer[0] >= 0x20) {
    *outCh = buffer[0];
    return true;
  }
  switch (keyCode) {
    case VK_SPACE:
      *outCh = L' ';
      return true;
    case VK_RETURN:
      *outCh = L'\r';
      return true;
    case VK_TAB:
      *outCh = L'\t';
      return true;
    case VK_BACK:
      *outCh = L'\b';
      return true;
    default:
      return false;
  }
}

int scale_input_coord(int32_t coord, uint32_t inputExtent, int outputExtent) {
  if (outputExtent <= 1) return 0;
  if (inputExtent <= 1) return std::clamp<int>(coord, 0, outputExtent - 1);
  const int32_t clamped = std::clamp<int32_t>(coord, 0, static_cast<int32_t>(inputExtent - 1));
  const uint64_t numerator = static_cast<uint64_t>(clamped) * static_cast<uint64_t>(outputExtent - 1) +
                             static_cast<uint64_t>((inputExtent - 1) / 2);
  return static_cast<int>(numerator / static_cast<uint64_t>(inputExtent - 1));
}

bool make_scaled_client_lparam(HWND hwnd, int32_t x, int32_t y, uint32_t inputW, uint32_t inputH,
                               LPARAM* outLp, POINT* outClientPt = nullptr) {
  if (!outLp || !hwnd || !IsWindow(hwnd)) return false;
  RECT rc{};
  if (!GetClientRect(hwnd, &rc)) return false;
  const int w = std::max<int>(1, rc.right - rc.left);
  const int h = std::max<int>(1, rc.bottom - rc.top);
  const int cx = scale_input_coord(x, inputW, w);
  const int cy = scale_input_coord(y, inputH, h);
  if (outClientPt) {
    outClientPt->x = cx;
    outClientPt->y = cy;
  }
  *outLp = MAKELPARAM(cx, cy);
  return true;
}

HWND choose_desktop_seed_window(const POINT& screenPt, DesktopInputState* state) {
  CaptureWindowInfo topLevel{};
  if (find_top_level_window_at_point(screenPt, &topLevel) && topLevel.hwnd && IsWindow(topLevel.hwnd)) {
    return topLevel.hwnd;
  }
  if (state) {
    std::lock_guard<std::mutex> lk(state->mu);
    if (state->lastHwnd && IsWindow(state->lastHwnd)) return state->lastHwnd;
  }
  const HWND pointHwnd = WindowFromPoint(screenPt);
  return (pointHwnd && IsWindow(pointHwnd)) ? pointHwnd : nullptr;
}

HWND choose_text_target_window(HWND fallbackHwnd, DesktopInputState* state) {
  if (state) {
    std::lock_guard<std::mutex> lk(state->mu);
    if (state->lastHwnd && IsWindow(state->lastHwnd)) return state->lastHwnd;
  }
  if (fallbackHwnd && IsWindow(fallbackHwnd)) return fallbackHwnd;
  const HWND foreground = GetForegroundWindow();
  return (foreground && IsWindow(foreground)) ? foreground : nullptr;
}

bool resolve_message_target_at_screen_point(HWND seedHwnd, const POINT& screenPt, HWND* outHwnd,
                                            POINT* outClientPt) {
  if (!outHwnd || !outClientPt) return false;
  HWND hwnd = seedHwnd;
  if (!hwnd || !IsWindow(hwnd)) hwnd = WindowFromPoint(screenPt);
  if (!hwnd || !IsWindow(hwnd)) return false;

  POINT clientPt = screenPt;
  if (!ScreenToClient(hwnd, &clientPt)) return false;

  HWND target = hwnd;
  POINT targetClient = clientPt;
  for (int depth = 0; depth < 16; ++depth) {
    HWND child = ChildWindowFromPointEx(target, targetClient,
                                        CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
    if (!child || child == target) break;
    POINT nextClient = screenPt;
    if (!ScreenToClient(child, &nextClient)) break;
    target = child;
    targetClient = nextClient;
  }

  *outHwnd = target;
  *outClientPt = targetClient;
  return true;
}

bool map_input_to_primary_monitor_point(int32_t x, int32_t y, uint32_t inputW, uint32_t inputH,
                                        POINT* outPt) {
  if (!outPt) return false;
  HMONITOR primaryMon = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO monInfo{};
  monInfo.cbSize = sizeof(monInfo);
  if (!GetMonitorInfo(primaryMon, &monInfo)) {
    monInfo.rcMonitor.left = 0;
    monInfo.rcMonitor.top = 0;
    monInfo.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
    monInfo.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
  }
  const int monW = std::max<int>(1, monInfo.rcMonitor.right - monInfo.rcMonitor.left);
  const int monH = std::max<int>(1, monInfo.rcMonitor.bottom - monInfo.rcMonitor.top);
  outPt->x = monInfo.rcMonitor.left + scale_input_coord(x, inputW, monW);
  outPt->y = monInfo.rcMonitor.top + scale_input_coord(y, inputH, monH);
  return true;
}

void remember_input_target(DesktopInputState* state, HWND hwnd, const POINT& screenPt) {
  if (!state) return;
  std::lock_guard<std::mutex> lk(state->mu);
  state->lastHwnd = hwnd;
  state->lastScreenPt = screenPt;
  state->hasLastScreenPt = true;
}

bool resolve_desktop_input_target(const POINT& screenPt, DesktopInputState* state, HWND* outHwnd = nullptr,
                                  POINT* outClientPt = nullptr, std::string* resolvedTargetOut = nullptr) {
  const HWND seed = choose_desktop_seed_window(screenPt, state);
  HWND resolved = nullptr;
  POINT clientPt{};
  if (!resolve_message_target_at_screen_point(seed, screenPt, &resolved, &clientPt)) return false;
  remember_input_target(state, resolved, screenPt);
  if (outHwnd) *outHwnd = resolved;
  if (outClientPt) *outClientPt = clientPt;
  if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(resolved);
  return true;
}

DWORD mouse_vk_to_sendinput_flag(uint16_t kind, uint32_t vk) {
  if (kind == 2) {
    if (vk == VK_RBUTTON) return MOUSEEVENTF_RIGHTDOWN;
    if (vk == VK_MBUTTON) return MOUSEEVENTF_MIDDLEDOWN;
    return MOUSEEVENTF_LEFTDOWN;
  }
  if (kind == 3) {
    if (vk == VK_RBUTTON) return MOUSEEVENTF_RIGHTUP;
    if (vk == VK_MBUTTON) return MOUSEEVENTF_MIDDLEUP;
    return MOUSEEVENTF_LEFTUP;
  }
  return 0;
}

bool send_desktop_mouse_input(DWORD flags, DWORD mouseData = 0) {
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = flags;
  in.mi.mouseData = mouseData;
  return SendInput(1, &in, sizeof(INPUT)) == 1;
}


// Real keyboard events for the focused window.
//
// Chrome, Electron apps and anything UWP-backed ignore a synthetic WM_CHAR/WM_KEYDOWN that
// was PostMessage'd at them: they route keyboard through their own focus manager and consult
// real key state, which a posted message never establishes. Desktop mode already drives the
// real cursor with SendInput, so the keyboard has to travel the same way to reach them.
bool send_desktop_unicode_char(wchar_t ch) {
  INPUT in[2]{};
  for (int i = 0; i < 2; ++i) {
    in[i].type = INPUT_KEYBOARD;
    in[i].ki.wVk = 0;
    in[i].ki.wScan = static_cast<WORD>(ch);
    in[i].ki.dwFlags = KEYEVENTF_UNICODE | (i == 1 ? KEYEVENTF_KEYUP : 0);
  }
  return SendInput(2, in, sizeof(INPUT)) == 2;
}

// G7: keep the host IME out of the way of injected keystrokes. English letters travel as VK key
// events through SendInput, which the host IME composes -- so when the host IME sits in Korean /
// native mode, injected English is eaten and never appears (the user had to click the host tray
// toggle to unstick it, and the client-side Han/Yeong never reached the host). Korean does not need
// the host IME at all: the PC client composes it locally and sends the finished text, injected as
// Unicode (KEYEVENTF_UNICODE) which bypasses the IME entirely. So the correct state for a remote
// session is host IME OFF -- English VK lands, Korean still arrives via the text path. Force the
// focused control's IME to alphanumeric on keydown, throttled and env-gated (set
// REMOTE60_NATIVE_IME_NEUTRALIZE_OFF=1 to disable). Low regression risk: it can only fail to help
// English; it cannot break the Unicode-injected Korean. (history #349)
void ensure_foreground_ime_alphanumeric() {
  static std::atomic<bool> enabledInit{false};
  static std::atomic<bool> enabled{true};
  if (!enabledInit.exchange(true)) {
    char buf[8]{};
    const DWORD n = GetEnvironmentVariableA("REMOTE60_NATIVE_IME_NEUTRALIZE_OFF", buf, sizeof(buf));
    if (n > 0 && (buf[0] == '1' || buf[0] == 't' || buf[0] == 'T' || buf[0] == 'y' || buf[0] == 'Y')) {
      enabled.store(false, std::memory_order_relaxed);
    }
  }
  if (!enabled.load(std::memory_order_relaxed)) return;

  static std::atomic<uint64_t> lastUs{0};
  const uint64_t nowUs = qpc_now_us();
  const uint64_t last = lastUs.load(std::memory_order_relaxed);
  if (last != 0 && nowUs - last < 250000ULL) return;  // at most ~4x/s: this is a syscall dance
  lastUs.store(nowUs, std::memory_order_relaxed);

  const HWND fg = GetForegroundWindow();
  if (!fg) return;
  const DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
  const DWORD myThread = GetCurrentThreadId();
  const bool attached =
      (fgThread != 0 && fgThread != myThread && AttachThreadInput(myThread, fgThread, TRUE));
  const HWND focus = GetFocus();  // the actual focused control; needs the attach to read cross-thread
  const HWND target = focus ? focus : fg;
  const HIMC imc = ImmGetContext(target);
  if (imc) {
    DWORD conv = 0;
    DWORD sentence = 0;
    if (ImmGetConversionStatus(imc, &conv, &sentence) && (conv & IME_CMODE_NATIVE)) {
      (void)ImmSetConversionStatus(imc, conv & ~(IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE), sentence);
    }
    (void)ImmReleaseContext(target, imc);
  }
  if (attached) AttachThreadInput(myThread, fgThread, FALSE);
}

bool send_desktop_virtual_key(uint32_t vk, bool keyUp) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = static_cast<WORD>(vk);
  in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
  in.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
  // Extended keys need the flag or the target sees the numpad twin instead.
  switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE: case VK_RCONTROL: case VK_RMENU:
      in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
      break;
    default:
      break;
  }
  return SendInput(1, &in, sizeof(INPUT)) == 1;
}

}  // namespace

InputInjectionMode parse_input_injection_mode(std::string raw) {
  raw = ascii_lower(trim_ascii(raw));
  if (raw.empty() || raw == "none" || raw == "disabled" || raw == "off") {
    return InputInjectionMode::Disabled;
  }
  if (raw == "background_message") {
    return InputInjectionMode::BackgroundMessage;
  }
  return InputInjectionMode::Disabled;
}

const char* input_injection_mode_name(InputInjectionMode mode) {
  switch (mode) {
    case InputInjectionMode::Disabled:
      return "disabled";
    case InputInjectionMode::BackgroundMessage:
      return "background_message";
    default:
      return "unknown";
  }
}

// True while the interactive desktop is the ordinary one the user works on.
//
// The elevated host can inject into that desktop itself, including elevated windows such as the
// taskbar. What it cannot reach is the secure desktop -- the lock screen, the UAC prompt,
// Ctrl+Alt+Del -- which is a separate desktop only a SYSTEM process can drive. Routing
// everything through the SYSTEM agent instead is worse than it sounds: the agent reports nothing
// back, so when its injection does not take effect the host still counts every event as
// delivered and the session looks alive while no click ever lands.
//
// Checked on a short cache because it runs per input event.
// Fresh (uncached) query of whether the input desktop is the ordinary "Default" one. A locked
// workstation, a UAC prompt, or any other secure desktop switches the input desktop to Winlogon,
// which this user-session process cannot open. Split out from the cached wrapper below so callers
// that must not trust a stale flag -- the static-screen bootstrap, which is about to paint pixels
// -- can force a live check.
bool interactive_desktop_is_default_uncached() {
  bool isDefault = false;
  HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (desktop) {
    wchar_t name[64]{};
    DWORD needed = 0;
    if (GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed)) {
      isDefault = _wcsicmp(name, L"Default") == 0;
    }
    CloseDesktop(desktop);
  }
  // A desktop this process cannot even open is the secure one.
  return isDefault;
}

bool interactive_desktop_is_default() {
  static std::atomic<uint64_t> lastCheckUs{0};
  static std::atomic<bool> cached{true};
  const uint64_t nowUs = qpc_now_us();
  const uint64_t last = lastCheckUs.load(std::memory_order_relaxed);
  if (last != 0 && nowUs - last < 250000ULL) return cached.load(std::memory_order_relaxed);

  const bool isDefault = interactive_desktop_is_default_uncached();
  cached.store(isDefault, std::memory_order_relaxed);
  lastCheckUs.store(nowUs, std::memory_order_relaxed);
  return isDefault;
}

const char* input_fail_stage_name(InputFailStage s) {
  switch (s) {
    case InputFailStage::MapPoint: return "map_point";
    case InputFailStage::ResolveTarget: return "resolve_target";
    case InputFailStage::SetCursorPos: return "set_cursor_pos";
    case InputFailStage::SendInputMouse: return "sendinput_mouse";
    case InputFailStage::SendInputKey: return "sendinput_key";
    case InputFailStage::PostMessage: return "post_message";
    default: return "none";
  }
}

InputInjectResult inject_background_input_event(const ControlInputEventMessage& input,
                                                const CaptureWindowCriteria& explicitCriteria,
                                                const std::atomic<uint64_t>& captureTargetHwnd,
                                                bool desktopMode,
                                                uint32_t inputDomainW,
                                                uint32_t inputDomainH,
                                                DesktopInputState* desktopInputState,
                                                std::string* resolvedTargetOut,
                                                InputFailStage* failStageOut,
                                                DWORD* failErrorOut) {
  // SetLastError(ERROR_SUCCESS) before each stamped API, capture immediately on failure: per the
  // SendInput contract a UIPI block can return with no error at all, so a preserved 0 next to a
  // stage is itself the signal (stage set, error 0 = swallowed, not skipped).
  auto fail = [&](InputFailStage stage) {
    if (failStageOut) *failStageOut = stage;
    if (failErrorOut) *failErrorOut = GetLastError();
    return InputInjectResult::Failed;
  };
  // For non-Win32 validation/mapping failures, whose GetLastError would be a stale value from some
  // earlier API. Pass the meaning explicitly instead of reporting a lie.
  auto failVal = [&](InputFailStage stage, DWORD err) {
    if (failStageOut) *failStageOut = stage;
    if (failErrorOut) *failErrorOut = err;
    return InputInjectResult::Failed;
  };
  if (inputDomainW == 0 || inputDomainH == 0) return failVal(InputFailStage::MapPoint, ERROR_INVALID_PARAMETER);

  CaptureWindowInfo explicitTarget{};
  const bool explicitTargetEnabled = explicitCriteria.enabled();
  if (explicitTargetEnabled) {
    if (!find_capture_window_input_target(explicitCriteria, &explicitTarget)) return InputInjectResult::NoTarget;
    if (!explicitTarget.hwnd || !IsWindow(explicitTarget.hwnd) || IsIconic(explicitTarget.hwnd)) {
      return InputInjectResult::NoTarget;
    }
    desktopMode = false;
  }

  if (!desktopMode) {
    HWND targetHwnd = explicitTargetEnabled ? explicitTarget.hwnd
                                            : reinterpret_cast<HWND>(static_cast<uintptr_t>(
                                                  captureTargetHwnd.load(std::memory_order_acquire)));
    if (!targetHwnd || !IsWindow(targetHwnd)) return InputInjectResult::NoTarget;
    POINT rootClientPt{};
    LPARAM rootLp = 0;
    if (!make_scaled_client_lparam(targetHwnd, input.x, input.y, inputDomainW, inputDomainH, &rootLp, &rootClientPt)) {
      return failVal(InputFailStage::MapPoint, ERROR_INVALID_PARAMETER);
    }
    POINT screenPt = rootClientPt;
    SetLastError(ERROR_SUCCESS);
    if (!ClientToScreen(targetHwnd, &screenPt)) return fail(InputFailStage::MapPoint);
    HWND resolvedTargetHwnd = targetHwnd;
    POINT resolvedClientPt = rootClientPt;
    if (!resolve_message_target_at_screen_point(targetHwnd, screenPt, &resolvedTargetHwnd, &resolvedClientPt)) {
      return InputInjectResult::NoTarget;
    }
    if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(resolvedTargetHwnd);
    remember_input_target(desktopInputState, resolvedTargetHwnd, screenPt);
    if (input.kind == 1) {
      // Button-less moves used to be dropped, on the assumption that a finger not touching
      // cannot hover. The on-screen mouse breaks that assumption: it exists precisely to place
      // the pointer before pressing anything, and dropping those moves made it do nothing at
      // all. WM_MOUSEMOVE with no buttons is what a real mouse sends while hovering.
      const WPARAM wp = mouse_button_wparam(static_cast<uint16_t>(input.buttons & 0x7u));
      const LPARAM lp = MAKELPARAM(static_cast<short>(resolvedClientPt.x), static_cast<short>(resolvedClientPt.y));
      SetLastError(ERROR_SUCCESS);
      return PostMessageW(resolvedTargetHwnd, WM_MOUSEMOVE, wp, lp) ? InputInjectResult::Injected
                                                                    : fail(InputFailStage::PostMessage);
    }
    if (input.kind == 2 || input.kind == 3) {
      const UINT msg = mouse_vk_to_message(input.kind, input.keyCode);
      if (msg == 0) return InputInjectResult::Unsupported;
      uint16_t buttons = static_cast<uint16_t>(input.buttons & 0x7u);
      const uint16_t eventMask = mouse_vk_to_mask(input.keyCode);
      if (input.kind == 2) {
        buttons = static_cast<uint16_t>(buttons | eventMask);
      } else if (input.kind == 3) {
        buttons = static_cast<uint16_t>(buttons & static_cast<uint16_t>(~eventMask));
      }
      const WPARAM wp = mouse_button_wparam(buttons);
      const LPARAM lp = MAKELPARAM(static_cast<short>(resolvedClientPt.x), static_cast<short>(resolvedClientPt.y));
      // Sequential, not one combined bool: a failing PostMessage must have its own error captured
      // before the next call overwrites it.
      SetLastError(ERROR_SUCCESS);
      if (!PostMessageW(resolvedTargetHwnd, WM_MOUSEMOVE, wp, lp)) return fail(InputFailStage::PostMessage);
      SetLastError(ERROR_SUCCESS);
      if (!PostMessageW(resolvedTargetHwnd, msg, wp, lp)) return fail(InputFailStage::PostMessage);
      return InputInjectResult::Injected;
    }
    if (input.kind == 4) {
      const WPARAM wp =
          MAKEWPARAM(mouse_button_wparam(static_cast<uint16_t>(input.buttons & 0x7u)),
                     static_cast<WORD>(static_cast<SHORT>(input.wheelDelta)));
      const LPARAM screenLp =
          MAKELPARAM(static_cast<short>(screenPt.x), static_cast<short>(screenPt.y));
      SetLastError(ERROR_SUCCESS);
      return PostMessageW(resolvedTargetHwnd, WM_MOUSEWHEEL, wp, screenLp) ? InputInjectResult::Injected
                                                                           : fail(InputFailStage::PostMessage);
    }
    if (input.kind == 5 || input.kind == 6) {
      HWND keyTargetHwnd = choose_text_target_window(targetHwnd, desktopInputState);
      if (!keyTargetHwnd || !IsWindow(keyTargetHwnd)) return InputInjectResult::NoTarget;
      if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(keyTargetHwnd);
      const bool keyUp = (input.kind == 6);
      // Same reasoning as the text path: a posted WM_KEYDOWN never reaches Chrome and
      // friends, and a modifier posted this way does not establish real key state, so
      // Ctrl+C could never work. Use real keystrokes whenever the target holds focus.
      const HWND foreground = GetForegroundWindow();
      const bool targetHasFocus =
          foreground && (foreground == keyTargetHwnd ||
                         GetAncestor(keyTargetHwnd, GA_ROOT) == foreground);
      InputFailStage keyStage = InputFailStage::PostMessage;
      bool ok = false;
      SetLastError(ERROR_SUCCESS);
      if (targetHasFocus) {
        keyStage = InputFailStage::SendInputKey;
        ok = send_desktop_virtual_key(input.keyCode, keyUp);
      } else {
        const UINT msg = keyUp ? WM_KEYUP : WM_KEYDOWN;
        const LPARAM lp = key_event_lparam(input.keyCode, keyUp);
        ok = PostMessageW(keyTargetHwnd, msg, static_cast<WPARAM>(input.keyCode), lp) != 0;
      }
      if (!ok) return fail(keyStage);
      if (desktopInputState) {
        std::lock_guard<std::mutex> lk(desktopInputState->mu);
        update_synthetic_keyboard_state(desktopInputState->keyState, input.keyCode, keyUp);
      }
      return InputInjectResult::Injected;
    }
    return InputInjectResult::Unsupported;
  }

  POINT screenPt{};
  if (!map_input_to_primary_monitor_point(input.x, input.y, inputDomainW, inputDomainH, &screenPt)) {
    return failVal(InputFailStage::MapPoint, ERROR_INVALID_PARAMETER);
  }
  resolve_desktop_input_target(screenPt, desktopInputState, nullptr, nullptr, resolvedTargetOut);

  if (input.kind == 1) {
    // A move with no button held is honoured here, unlike window mode. Desktop mode drives the
    // real cursor, and the client only sends these when it means to place the pointer: the
    // on-screen mouse positions before clicking, and scroll mode positions before scrolling.
    // Dropping them made both silently do nothing.
    SetLastError(ERROR_SUCCESS);
    return SetCursorPos(screenPt.x, screenPt.y) ? InputInjectResult::Injected
                                                : fail(InputFailStage::SetCursorPos);
  }
  if (input.kind == 2 || input.kind == 3) {
    const DWORD mouseFlag = mouse_vk_to_sendinput_flag(input.kind, input.keyCode);
    if (mouseFlag == 0) return InputInjectResult::Unsupported;
    SetLastError(ERROR_SUCCESS);
    if (!SetCursorPos(screenPt.x, screenPt.y)) return fail(InputFailStage::SetCursorPos);
    SetLastError(ERROR_SUCCESS);
    return send_desktop_mouse_input(mouseFlag) ? InputInjectResult::Injected
                                               : fail(InputFailStage::SendInputMouse);
  }
  if (input.kind == 4) {
    SetLastError(ERROR_SUCCESS);
    if (!SetCursorPos(screenPt.x, screenPt.y)) return fail(InputFailStage::SetCursorPos);
    SetLastError(ERROR_SUCCESS);
    return send_desktop_mouse_input(MOUSEEVENTF_WHEEL,
                                    static_cast<DWORD>(static_cast<SHORT>(input.wheelDelta)))
               ? InputInjectResult::Injected
               : fail(InputFailStage::SendInputMouse);
  }
  if (input.kind == 5 || input.kind == 6) {
    // Desktop mode drives the real cursor, so keyboard goes to the real focus too.
    const bool keyUp = (input.kind == 6);
    if (resolvedTargetOut) {
      HWND focusHwnd = GetForegroundWindow();
      *resolvedTargetOut = focusHwnd ? describe_input_target(focusHwnd) : std::string("sendinput");
    }
    // G7: before an injected keydown lands, make sure the host IME is not sitting in Korean mode
    // and about to eat an English letter. Keydown only (the up carries no character), throttled.
    if (!keyUp) ensure_foreground_ime_alphanumeric();
    SetLastError(ERROR_SUCCESS);
    if (!send_desktop_virtual_key(input.keyCode, keyUp)) return fail(InputFailStage::SendInputKey);
    if (desktopInputState) {
      std::lock_guard<std::mutex> lk(desktopInputState->mu);
      update_synthetic_keyboard_state(desktopInputState->keyState, input.keyCode, keyUp);
    }
    return InputInjectResult::Injected;
  }
  return InputInjectResult::Unsupported;
}

InputInjectResult apply_input_text_message(const ControlInputTextMessage& text,
                                           const std::atomic<uint64_t>& captureTargetHwnd,
                                           bool desktopMode,
                                           DesktopInputState* desktopInputState,
                                           std::string* resolvedTargetOut) {
  if (text.utf16Count == 0 || text.utf16Count > remote60::native_poc::kControlInputTextMaxUtf16) {
    return InputInjectResult::Unsupported;
  }
  if (desktopMode) {
    // Real keystrokes to the focused window; WM_CHAR posted at a top-level window is ignored
    // by Chrome and other apps that own their own input routing.
    if (resolvedTargetOut) {
      HWND focusHwnd = GetForegroundWindow();
      *resolvedTargetOut = focusHwnd ? describe_input_target(focusHwnd) : std::string("sendinput");
    }
    for (uint16_t i = 0; i < text.utf16Count; ++i) {
      const uint16_t ch = text.utf16[i];
      if (ch == 0) continue;
      if (!send_desktop_unicode_char(static_cast<wchar_t>(ch))) return InputInjectResult::Failed;
    }
    return InputInjectResult::Injected;
  }

  HWND fallbackHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(
      captureTargetHwnd.load(std::memory_order_acquire)));
  HWND targetHwnd = choose_text_target_window(fallbackHwnd, desktopInputState);
  if (!targetHwnd || !IsWindow(targetHwnd)) return InputInjectResult::NoTarget;
  if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(targetHwnd);
  // Window mode targets a specific window without stealing focus, so posted messages are the
  // deliberate mechanism. If that window already holds focus, real keystrokes reach far more
  // applications, so prefer them.
  const HWND foreground = GetForegroundWindow();
  const bool targetHasFocus =
      foreground && (foreground == targetHwnd || IsChild(foreground, targetHwnd) ||
                     GetAncestor(targetHwnd, GA_ROOT) == foreground);
  for (uint16_t i = 0; i < text.utf16Count; ++i) {
    const uint16_t ch = text.utf16[i];
    if (ch == 0) continue;
    if (targetHasFocus) {
      if (!send_desktop_unicode_char(static_cast<wchar_t>(ch))) return InputInjectResult::Failed;
    } else if (!PostMessageW(targetHwnd, WM_CHAR, static_cast<WPARAM>(ch), 1)) {
      return InputInjectResult::Failed;
    }
  }
  return InputInjectResult::Injected;
}

}  // namespace remote60::native_poc
