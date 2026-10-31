// TEST ONLY: e2e runs that use this logon session's clipboard station take their turn.
//
// A Medium process cannot name a window station, so every "private" station a test creates is the
// logon session's one unnamed station -- one clipboard shared by every test on this PC that makes
// one (clip_image_winsta_test proves it). Two such runs at once (another worktree, the verifier)
// take each other's clipboard and both fail for reasons that are not the product's. Asking people
// not to overlap is a rule that breaks again; this makes the runs queue by themselves.
//
// Local\ is the logon session's namespace -- exactly the scope of the shared station. The ORCHESTRATOR
// takes it; the child modes a test starts (consumers, --child) must not, or they would wait on
// their own parent. Not linked into any product executable.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace remote60::native_poc::e2e {

// Exit code of a run that could not get its turn: not a pass, not a product failure.
constexpr int kStationBusyExit = 3;

class StationLock {
 public:
  StationLock() = default;
  StationLock(const StationLock&) = delete;
  StationLock& operator=(const StationLock&) = delete;
  ~StationLock() {
    if (held_) ReleaseMutex(mutex_);
    if (mutex_) CloseHandle(mutex_);
  }

  // Waits for the turn (REMOTE60_E2E_STATION_WAIT_SEC, default 1800). false = not taken: the caller
  // reports INVALID and exits kStationBusyExit without touching the clipboard.
  bool Acquire(const char* test) {
    DWORD waitSec = 1800;
    if (const char* v = std::getenv("REMOTE60_E2E_STATION_WAIT_SEC")) {
      const long n = std::strtol(v, nullptr, 10);
      if (n >= 0) waitSec = static_cast<DWORD>(n);
    }
    mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\GNLinkE2EClipboardStation");
    if (!mutex_) {
      std::printf("station lock: CreateMutex failed err=%lu\n", GetLastError());
      return false;
    }
    const ULONGLONG t0 = GetTickCount64();
    DWORD r = WaitForSingleObject(mutex_, 0);
    if (r == WAIT_TIMEOUT) {
      std::printf("station lock: another clipboard e2e holds the station; %s waits (up to %lu s)\n", test,
                  static_cast<unsigned long>(waitSec));
      r = WaitForSingleObject(mutex_, waitSec * 1000);
    }
    const unsigned long waitedMs = static_cast<unsigned long>(GetTickCount64() - t0);
    if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) {
      held_ = true;
      // Abandoned: the previous holder died without releasing. The turn is ours; the station may
      // still hold what it left, which the test's own foreign-writer check will see.
      std::printf("station lock: taken after %lu ms%s\n", waitedMs, r == WAIT_ABANDONED ? " (previous holder died)" : "");
      return true;
    }
    std::printf("station lock: not taken after %lu ms (r=%lu)\n", waitedMs, static_cast<unsigned long>(r));
    return false;
  }

  // The standard refusal: printed as the run's last line.
  static int Busy(const char* test) {
    std::printf("\nRESULT: INVALID  (%s did not get the clipboard station; nothing was run)\n", test);
    return kStationBusyExit;
  }

 private:
  HANDLE mutex_ = nullptr;
  bool held_ = false;
};

}  // namespace remote60::native_poc::e2e
