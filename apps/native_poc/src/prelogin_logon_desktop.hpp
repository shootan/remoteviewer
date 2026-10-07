#pragma once

// The SYSTEM pre-login worker serves the LOGON desktop only (prelogin-ui r9, W2).
//
// The worker (GNLinkStream --prelogin, SYSTEM, the console session) captures and injects into the input
// desktop. That is the logon screen's "Winlogon" desktop -- until a user logs on: then the input desktop
// becomes that user's "Default", and a worker still running (a handoff that did not complete) would be
// a SYSTEM process capturing and typing into the user's desktop. So:
//   * the worker watches the input desktop; the first time it is anything but the logon desktop the
//     worker stops serving and leaves (exit code kLeftLogonDesktopExit) -- for good: a logon screen that
//     comes back later (lock, sign-out) is the user's Host's, or a new worker's;
//   * until it has seen the logon desktop name, nothing is served (an input desktop it cannot open is
//     not taken for the logon one);
//   * the capture worker does not attach to any other desktop (capture_backend_dxgi requiredInputDesktop).
// A test build may name the desktop it serves (GNLINK_STREAM_TEST_LOGON_DESKTOP, host_startup_config) --
// its worker runs in a test user's session, where there is no logon desktop to serve.

#include <windows.h>

#include <iostream>
#include <string>

namespace remote60::native_poc::prelogin {

inline constexpr wchar_t kLogonDesktopName[] = L"Winlogon";
inline constexpr int kLeftLogonDesktopExit = 14;
// r10 T2: before login only DXGI, bound to the logon desktop, captures. When it cannot go on -- the input
// desktop is not the one served, or duplication failed -- there is no other backend to fall back to (WGC
// and GDI are not bound to a desktop): the worker stops serving and leaves.
inline constexpr int kPreloginCaptureStoppedExit = 15;

[[noreturn]] inline void PreloginStopServing(const std::string& why) {
  std::cout << "[native-video-host][prelogin] capture cannot continue on the logon desktop (" << why
            << "): no other capture before login -- stop serving and leave (r10 T2)" << std::endl;
  std::cerr.flush();
  TerminateProcess(GetCurrentProcess(), kPreloginCaptureStoppedExit);
  for (;;) Sleep(1000);
}

// The input desktop's name now; empty when it cannot be opened.
inline std::wstring InputDesktopName() {
  std::wstring name;
  if (HDESK d = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS)) {
    wchar_t buf[64] = {};
    DWORD needed = 0;
    if (GetUserObjectInformationW(d, UOI_NAME, buf, sizeof(buf), &needed)) name = buf;
    CloseDesktop(d);
  }
  return name;
}

// The decision over successive observations. Pure.
class LogonDesktopGuard {
 public:
  explicit LogonDesktopGuard(std::wstring serving) : serving_(std::move(serving)) {}
  // True while the worker may serve. Once the input desktop was seen to be another, never again.
  bool Observe(const std::wstring& inputDesktop) {
    if (left_) return false;
    if (inputDesktop.empty()) return false;  // unknown: not served, not "left" either
    if (_wcsicmp(inputDesktop.c_str(), serving_.c_str()) != 0) {
      left_ = true;
      return false;
    }
    return true;
  }
  bool left() const { return left_; }
  const std::wstring& serving() const { return serving_; }

 private:
  std::wstring serving_;
  bool left_ = false;
};

}  // namespace remote60::native_poc::prelogin
