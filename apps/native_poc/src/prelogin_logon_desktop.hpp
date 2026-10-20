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

#include <atomic>
#include <functional>
#include <iostream>
#include <string>

#include "stoppable_worker.hpp"

namespace remote60::native_poc::prelogin {

inline constexpr wchar_t kLogonDesktopName[] = L"Winlogon";
inline constexpr int kLeftLogonDesktopExit = 14;
// r10 T2: before login only DXGI, bound to the logon desktop, captures. When it cannot go on -- the input
// desktop is not the one served, or duplication failed -- there is no other backend to fall back to (WGC
// and GDI are not bound to a desktop): the worker stops serving and leaves.
inline constexpr int kPreloginCaptureStoppedExit = 15;

// prelogin-field D3: what the worker runs right before it ends itself at once (its log sink's drain, when
// one is installed -- prelogin_worker_log.hpp). Null: nothing.
inline void (*&PreloginBeforeExitHook())() {
  static void (*hook)() = nullptr;
  return hook;
}

[[noreturn]] inline void PreloginStopServing(const std::string& why) {
  std::cout << "[native-video-host][prelogin] capture cannot continue on the logon desktop (" << why
            << "): no other capture before login -- stop serving and leave (r10 T2)" << std::endl;
  std::cerr.flush();
  if (PreloginBeforeExitHook()) PreloginBeforeExitHook()();
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

// r17 R16-1: ONE permanent "this worker has left" for the whole process. Whichever prelogin path actually
// sees an input desktop other than the one served -- the watch, or the control path's fresh look before an
// input -- latches it, and the first to latch runs the leave handler (the product's: exit 14). The
// credential receiver refuses once it is set, so a challenge issued before is never used after, even when
// the desktop has come back to the logon one before the process ended.
inline std::atomic<bool>& PreloginLeftLatch() {
  static std::atomic<bool> left{false};
  return left;
}
inline std::function<void(const std::wstring&)>& PreloginLeaveHandler() {
  static std::function<void(const std::wstring&)> handler;
  return handler;
}
// r18 R17-1: the desktop this worker serves, for the paths that look at the input desktop without the
// capture state at hand (the credential receiver's target / environment). Set once at startup in --prelogin
// (host_startup_config); empty otherwise -- then nothing is latched.
inline std::wstring& PreloginServedDesktop() {
  static std::wstring served;
  return served;
}

// A prelogin path saw `name` (empty = could not be told: not an observation of another desktop).
inline void PreloginSawDesktop(const std::wstring& name, const std::wstring& served) {
  if (served.empty() || name.empty() || _wcsicmp(name.c_str(), served.c_str()) == 0) return;
  if (!PreloginLeftLatch().exchange(true, std::memory_order_acq_rel)) {
    if (PreloginLeaveHandler()) PreloginLeaveHandler()(name);
  }
}

// r17 R16-1: the control path's look right before an input (`now` = the input desktop asked afresh): refused
// unless it is the served desktop and this worker has not left; seeing another desktop latches the leave.
inline bool PreloginInputRefused(const std::wstring& now, const std::wstring& served) {
  PreloginSawDesktop(now, served);
  return now.empty() || _wcsicmp(now.c_str(), served.c_str()) != 0 ||
         PreloginLeftLatch().load(std::memory_order_acquire);
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

// The worker's watch, as its owner runs it (r12 L1: an owned StoppableWorker, not a detached for(;;) over
// the capture state). Every `periodMs` the input desktop is read and judged; `serving` says the verdict to
// the input / capture paths; `onFirstServing` once; `onLeft(name)` when the desktop was another (the
// product's leaves the process; a returning one ends the watch). Stopped by its owner: the wait wakes and
// the loop returns before the owner's Stop() does -- before what it writes is destroyed.
template <typename DesktopName, typename OnServing, typename OnLeft>
void RunLogonDesktopWatch(StoppableWorker& w, const std::wstring& servedDesktop, std::atomic<bool>& serving,
                          DesktopName desktopName, OnServing onFirstServing, OnLeft onLeft, uint32_t periodMs = 100) {
  LogonDesktopGuard guard(servedDesktop);
  bool announced = false;
  while (!w.Stopping()) {
    const std::wstring name = desktopName();
    // r17 R16-1: another path (the control path) may have seen it first -- the same permanent leave.
    const bool now = guard.Observe(name) && !PreloginLeftLatch().load(std::memory_order_acquire);
    serving.store(now, std::memory_order_release);
    if (PreloginLeftLatch().load(std::memory_order_acquire) && !guard.left()) return;  // its handler has run
    if (now && !announced) {
      onFirstServing();
      announced = true;
    }
    if (guard.left()) {
      if (!PreloginLeftLatch().exchange(true, std::memory_order_acq_rel)) onLeft(name);  // r17: one latch
      return;
    }
    if (!w.Wait(periodMs)) return;
  }
}

}  // namespace remote60::native_poc::prelogin
