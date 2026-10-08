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
#include <wtsapi32.h>

#include <atomic>
#include <iostream>
#include <string>

#include "stoppable_worker.hpp"

#pragma comment(lib, "wtsapi32.lib")  // WTSGetActiveConsoleSessionId / WTSQueryUserToken (r15 L6)

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

// r15 L6 (limited): the screen saver that can cover the logon screen. With nobody logged on, a secure screen
// saver runs on its own desktop; r14 took that for "a user's desktop" and left (exit 14) -- the service
// started the worker again and again, and the viewer could neither see nor wake the PC. Exact names only,
// compared case-insensitively, no prefix or look-alike: Microsoft's "Desktops" documentation calls it the
// screen-saver desktop ("Screen-saver"; "ScreenSaver" is listed as the other spelling the review named). The
// UOI_NAME a given Windows actually reports is to be recorded on the real machine (G) -- not measured here.
inline constexpr const wchar_t* kScreenSaverDesktopNames[] = {L"Screen-saver", L"ScreenSaver"};

inline bool IsScreenSaverDesktop(const std::wstring& name) {
  for (const wchar_t* n : kScreenSaverDesktopNames) {
    if (_wcsicmp(name.c_str(), n) == 0) return true;
  }
  return false;
}

// The physical console session -- this process's own -- has NOBODY logged on, as the token query says
// authoritatively (ERROR_NO_TOKEN). A failed query, another session at the console or a user: false.
inline bool ConsoleHasNoInteractiveUser() {
  DWORD mine = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &mine)) return false;
  const DWORD console = WTSGetActiveConsoleSessionId();
  if (console == 0xFFFFFFFFu || console != mine) return false;
  HANDLE token = nullptr;
  if (WTSQueryUserToken(console, &token)) {
    CloseHandle(token);
    return false;  // a user
  }
  return GetLastError() == ERROR_NO_TOKEN;
}

// What the worker does on the input desktop `name`: serve it (the logon desktop it serves), serve it for
// WAKING only (the screen saver, with nobody logged on), hold (not served, not left: unknown, or a screen
// saver when someone may be logged on -- the supervisor's handoff decides), or leave (Default, anything
// else -- r9 W2 unchanged). `noInteractiveUser` is asked only for a screen-saver name. Pure.
enum class DesktopServe { Serve, WakeOnly, Hold, Leave };

template <typename NoUser>
DesktopServe DecideLogonDesktop(const std::wstring& name, const std::wstring& served, NoUser noInteractiveUser) {
  if (name.empty()) return DesktopServe::Hold;
  if (_wcsicmp(name.c_str(), served.c_str()) == 0) return DesktopServe::Serve;
  if (IsScreenSaverDesktop(name)) return noInteractiveUser() ? DesktopServe::WakeOnly : DesktopServe::Hold;
  return DesktopServe::Leave;
}

// r15 L6: bumped on every change of the input desktop the worker sees (the watch). What was asked on one
// desktop -- a credential challenge above all -- is not carried to the next, even when the name comes back
// (Winlogon -> screen saver -> Winlogon).
inline std::atomic<uint32_t>& InputDesktopGeneration() {
  static std::atomic<uint32_t> g{0};
  return g;
}

// A wake on the screen saver: the pointer moving, or a navigation key (the pre-login allowlist decides
// which keys); never a click, a wheel, text or a character key.
inline bool IsScreenSaverWakeEvent(uint16_t eventKind) { return eventKind == 1 || eventKind == 5 || eventKind == 6; }

// The decision over successive observations. Pure.
class LogonDesktopGuard {
 public:
  explicit LogonDesktopGuard(std::wstring serving) : serving_(std::move(serving)) {}
  // True while the worker may serve. Once the input desktop was seen to be another, never again.
  bool Observe(const std::wstring& inputDesktop) {
    return Observe(inputDesktop, [] { return false; }) != DesktopServe::Leave && last_ == DesktopServe::Serve;
  }
  // r15 L6: with the screen saver.
  template <typename NoUser>
  DesktopServe Observe(const std::wstring& inputDesktop, NoUser noInteractiveUser) {
    if (left_) return last_ = DesktopServe::Leave;
    last_ = DecideLogonDesktop(inputDesktop, serving_, noInteractiveUser);
    if (last_ == DesktopServe::Leave) left_ = true;
    if (!inputDesktop.empty() && _wcsicmp(inputDesktop.c_str(), lastName_.c_str()) != 0) {
      if (!lastName_.empty()) InputDesktopGeneration().fetch_add(1, std::memory_order_acq_rel);
      lastName_ = inputDesktop;
    }
    return last_;
  }
  bool left() const { return left_; }
  const std::wstring& serving() const { return serving_; }

 private:
  std::wstring serving_;
  std::wstring lastName_;
  DesktopServe last_ = DesktopServe::Hold;
  bool left_ = false;
};

// The worker's watch, as its owner runs it (r12 L1: an owned StoppableWorker, not a detached for(;;) over
// the capture state). Every `periodMs` the input desktop is read and judged; `serving` says the verdict to
// the input / capture paths; `onFirstServing` once; `onLeft(name)` when the desktop was another (the
// product's leaves the process; a returning one ends the watch). Stopped by its owner: the wait wakes and
// the loop returns before the owner's Stop() does -- before what it writes is destroyed.
template <typename DesktopName, typename OnServing, typename OnLeft, typename NoUser>
void RunLogonDesktopWatch(StoppableWorker& w, const std::wstring& servedDesktop, std::atomic<bool>& serving,
                          DesktopName desktopName, OnServing onFirstServing, OnLeft onLeft, NoUser noInteractiveUser,
                          std::atomic<bool>* wakeOnly = nullptr, uint32_t periodMs = 100) {
  LogonDesktopGuard guard(servedDesktop);
  bool announced = false;
  while (!w.Stopping()) {
    const std::wstring name = desktopName();
    const DesktopServe verdict = guard.Observe(name, noInteractiveUser);  // r15 L6
    const bool now = verdict == DesktopServe::Serve || verdict == DesktopServe::WakeOnly;
    if (wakeOnly) wakeOnly->store(verdict == DesktopServe::WakeOnly, std::memory_order_release);
    serving.store(now, std::memory_order_release);
    if (now && !announced) {
      onFirstServing();
      announced = true;
    }
    if (guard.left()) {
      onLeft(name);
      return;
    }
    if (!w.Wait(periodMs)) return;
  }
}

}  // namespace remote60::native_poc::prelogin
