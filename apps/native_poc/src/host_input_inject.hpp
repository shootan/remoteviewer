#pragma once

// Host-side input injection: turns ControlInputEvent / ControlInputText messages from the viewer
// into Win32 input on this machine.
//
// Role:    two delivery paths -- window mode (PostMessage at the captured window's resolved child,
//          without stealing focus) and desktop mode (real cursor via SetCursorPos + SendInput,
//          real keystrokes to the focused window) -- plus the secure-desktop probe that tells the
//          control thread when neither path can work (lock screen / UAC) and it must fall back to
//          the SYSTEM broker. Failures are classified by the API they died in (InputFailStage).
// Thread:  control thread only (one event at a time). DesktopInputState carries the last resolved
//          target + synthetic keyboard state under its own mutex. interactive_desktop_is_default()
//          keeps a 250ms cache in function-local atomics; the *_uncached variant is for callers
//          that must not trust a stale answer.
// Input:   ControlInputEventMessage / ControlInputTextMessage, the capture target HWND, the
//          client's input domain (the size its coordinates are expressed in).
// Output:  InputInjectResult + optional resolved-target description / fail stage + GetLastError.
// Callers: native_video_host_main.cpp (control session InputEvent/InputText handlers, secure
//          desktop checks in the main loop and static-screen bootstrap).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-1). Definitions
// live in host_input_inject.cpp; the Win32 helper functions it uses internally stay private to that
// file. Behavior is byte-identical.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "host_window_enum.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

enum class InputInjectionMode : uint8_t {
  Disabled = 0,
  BackgroundMessage = 1,
};

InputInjectionMode parse_input_injection_mode(std::string raw);
const char* input_injection_mode_name(InputInjectionMode mode);

struct DesktopInputState {
  std::mutex mu;
  HWND lastHwnd = nullptr;
  POINT lastScreenPt{};
  bool hasLastScreenPt = false;
  BYTE keyState[256] = {};
};

// True while the interactive desktop is the ordinary one the user works on (see the definition
// for why the secure desktop -- lock screen, UAC, Ctrl+Alt+Del -- has to be detected here).
// The uncached variant performs the OpenInputDesktop query every call; the cached one answers
// from a 250ms cache because it runs per input event.
bool interactive_desktop_is_default_uncached();
bool interactive_desktop_is_default();

enum class InputInjectResult : uint8_t {
  Injected = 0,
  IgnoredMove = 1,
  NoTarget = 2,
  Unsupported = 3,
  Failed = 4,
};

// Which API a failed injection died in. "Failed" alone hid the 14:51 field freeze for two
// sessions: the log said inject-fail on a CoreWindow target and everyone (including review)
// assumed UWP message rejection -- but the kind=1 desktop path never posts a message at all;
// its only failing API is SetCursorPos. Stage + captured error make the next one one-glance.
enum class InputFailStage : uint8_t {
  None = 0,
  MapPoint,
  ResolveTarget,
  SetCursorPos,
  SendInputMouse,
  SendInputKey,
  PostMessage,
};
const char* input_fail_stage_name(InputFailStage s);

InputInjectResult inject_background_input_event(const ControlInputEventMessage& input,
                                                const CaptureWindowCriteria& explicitCriteria,
                                                const std::atomic<uint64_t>& captureTargetHwnd,
                                                bool desktopMode,
                                                uint32_t inputDomainW,
                                                uint32_t inputDomainH,
                                                DesktopInputState* desktopInputState,
                                                std::string* resolvedTargetOut = nullptr,
                                                InputFailStage* failStageOut = nullptr,
                                                DWORD* failErrorOut = nullptr);

InputInjectResult apply_input_text_message(const ControlInputTextMessage& text,
                                           const std::atomic<uint64_t>& captureTargetHwnd,
                                           bool desktopMode,
                                           DesktopInputState* desktopInputState,
                                           std::string* resolvedTargetOut = nullptr);

}  // namespace remote60::native_poc
