// The branch for a helper that will not die, actually executed.
//
// It cannot be reached honestly. TerminateProcess does not fail for a process this one started, so
// the confirmation always succeeds, the branch never runs, and removing it breaks no test -- which
// is the same as saying nothing covers it. The failure it guards against is also the kind nobody
// wants to discover in the field: a helper still out there, and the host cheerfully starting more.
//
// So this target links host_thumbnail_kill_fault.cpp in place of host_thumbnail_kill.cpp. The one
// OS call at the boundary reports failure and does not terminate anything; every other line is the
// real product. The helper genuinely stays alive, the handles are genuinely held, the next request
// is genuinely refused -- and then the test kills the helper itself and checks that the host
// notices and resumes.
//
// The substitution is at link time, so the shipping binaries contain none of it. shipped_helper
// _gate_test reads GNLinkStream and GNLinkCapture and fails if the fault file's marker is in
// either.
//
// What this starts: one copy of THIS EXECUTABLE, named GNLinkCapture.exe in a scratch directory,
// which sleeps. No host, no sockets, no screen capture, no firewall prompt.

#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "host_thumbnail_budget.hpp"
#include "host_thumbnail_helper.hpp"
#include "time_utils.hpp"

#include <tlhelp32.h>

namespace {

using remote60::native_poc::capture_thumbnail_isolated;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::ThumbnailBudget;
using remote60::native_poc::ThumbnailCaptureResult;
using remote60::native_poc::ThumbnailOutcome;
using remote60::native_poc::ThumbnailTargetKey;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

const char* name_of(ThumbnailOutcome o) {
  switch (o) {
    case ThumbnailOutcome::Ok: return "Ok";
    case ThumbnailOutcome::Failed: return "Failed";
    case ThumbnailOutcome::TimedOut: return "TimedOut";
    case ThumbnailOutcome::Canceled: return "Canceled";
  }
  return "?";
}

std::wstring self_path() {
  std::wstring path(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  path.resize(n);
  return path;
}

std::wstring directory_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}

/** Helpers this test started, found by image path rather than by name. */
std::vector<DWORD> helpers_in(const std::wstring& dir) {
  std::vector<DWORD> pids;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return pids;
  PROCESSENTRY32W e{};
  e.dwSize = sizeof(e);
  if (Process32FirstW(snap, &e)) {
    do {
      if (_wcsicmp(e.szExeFile, L"GNLinkCapture.exe") != 0) continue;
      HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
      if (!h) continue;
      wchar_t image[MAX_PATH]{};
      DWORD size = MAX_PATH;
      // By path: the user's installed GNLinkCapture is none of this test's business.
      if (QueryFullProcessImageNameW(h, 0, image, &size) &&
          _wcsnicmp(image, dir.c_str(), dir.size()) == 0) {
        pids.push_back(e.th32ProcessID);
      }
      CloseHandle(h);
    } while (Process32NextW(snap, &e));
  }
  CloseHandle(snap);
  return pids;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // Helper mode: this executable, copied over GNLinkCapture.exe, answering nothing.
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {
      Sleep(120000);
      return 0;
    }
  }

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"remote60_lingering_" +
                           std::to_wstring(GetCurrentProcessId()) + L"\\";
  CreateDirectoryW(dir.substr(0, dir.size() - 1).c_str(), nullptr);

  const std::wstring me = self_path();
  const bool staged = CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE) != FALSE &&
                      CopyFileW(me.c_str(), (dir + L"probe.exe").c_str(), FALSE) != FALSE;
  check("a never-answering helper could be staged", staged);
  if (!staged) {
    std::printf("\nRESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures + 1);
    return 1;
  }

  // The capture resolves its helper beside the running executable, so the work happens in a child
  // started from the scratch directory. This process only orchestrates.
  const std::wstring probe = dir + L"probe.exe";
  const std::wstring resultFile = dir + L"result.txt";

  // Run the probe in-process instead: capture_thumbnail_isolated looks beside THIS executable, and
  // this executable is not in the scratch directory. So the probe has to be the staged copy.
  std::wstring cmd = L"\"" + probe + L"\" --lingering-probe \"" + resultFile + L"\"";
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');

  // The probe mode, handled by the staged copy.
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::wstring(argv[i]) == L"--lingering-probe") {
      const std::wstring out = argv[i + 1];
      // First capture: the helper never answers, so this times out. Teardown then calls the
      // fault-injected kill, which does not terminate and reports failure -- the lingering branch.
      const ThumbnailCaptureResult first =
          capture_thumbnail_isolated(nullptr, 256, 160, 400 * 1000);
      // Second: must be refused without starting anything.
      const ThumbnailCaptureResult second =
          capture_thumbnail_isolated(nullptr, 256, 160, 400 * 1000);
      const std::wstring myDir = directory_of(self_path());
      const std::vector<DWORD> alive = helpers_in(myDir);

      FILE* f = nullptr;
      if (_wfopen_s(&f, out.c_str(), L"w") != 0 || !f) return 2;
      std::fprintf(f, "%s %llu %llu %llu %d %s %s %llu %zu\n", name_of(first.outcome),
                   static_cast<unsigned long long>(first.spawnUs),
                   static_cast<unsigned long long>(first.waitUs),
                   static_cast<unsigned long long>(first.teardownUs),
                   first.helperLingering ? 1 : 0, second.detail.c_str(),
                   name_of(second.outcome),
                   static_cast<unsigned long long>(second.elapsedUs), alive.size());
      std::fclose(f);
      // Leave the lingering helper for the parent to clean up: it is the evidence.
      return 0;
    }
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const bool ran = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
  check("the probe started", ran);
  if (ran) {
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }

  char firstOutcome[32]{}, secondDetail[64]{}, secondOutcome[32]{};
  unsigned long long spawn = 0, wait = 0, teardown = 0, secondElapsed = 0;
  int lingering = 0;
  size_t aliveCount = 0;
  bool read = false;
  {
    FILE* f = nullptr;
    if (_wfopen_s(&f, resultFile.c_str(), L"r") == 0 && f) {
      read = std::fscanf(f, "%31s %llu %llu %llu %d %63s %31s %llu %zu", firstOutcome, &spawn,
                         &wait, &teardown, &lingering, secondDetail, secondOutcome,
                         &secondElapsed, &aliveCount) == 9;
      std::fclose(f);
    }
  }
  check("the probe reported", read);

  if (read) {
    check("the first capture times out", std::string(firstOutcome) == "TimedOut", firstOutcome);
    check("...and the kill is reported as unconfirmed", lingering == 1,
          "this is the branch that cannot be reached without the fault TU");
    std::printf("  stages: spawn %lluus  wait %lluus  teardown %lluus  total %lluus\n", spawn,
                wait, teardown, spawn + wait + teardown);

    // The worst case, measured rather than reasoned about. The deadline bounds the WAIT; a kill
    // that is never confirmed costs the whole of kKillConfirmMs on top. Two budgets that add, and
    // the header says six seconds because of this -- one second of wait plus five of confirmation.
    // Here the wait is 400ms, so the same arithmetic lands near five and a half.
    constexpr unsigned long long kKillConfirmUs = 5ull * 1000 * 1000;
    check("an unconfirmed kill really does spend the confirmation window",
          teardown >= kKillConfirmUs - 200 * 1000,
          std::to_string(teardown / 1000) + "ms teardown, kKillConfirmMs is 5000ms");
    check("...so the worst case is deadline PLUS that, not the deadline",
          spawn + wait + teardown > kKillConfirmUs,
          std::to_string((spawn + wait + teardown) / 1000) + "ms total for a 400ms deadline");

    check("the next request is refused as thumb_helper_lingering",
          std::string(secondDetail) == "thumb_helper_lingering", secondDetail);
    check("...as Canceled, so the window is not charged for it",
          std::string(secondOutcome) == "Canceled", secondOutcome);
    check("...immediately, without spending a deadline", secondElapsed < 100 * 1000,
          std::to_string(secondElapsed) + "us");
    check("...and without starting a second helper", aliveCount == 1,
          std::to_string(aliveCount) + " helper(s) alive after two requests");

    // The whole chain, in one place. A refusal is ours, not the window's -- so recording it the
    // way the control session does must leave the window eligible. Three in a row is the case
    // that matters: with two dispatchers it takes no time at all to reach, and if each one cost an
    // attempt the window would be silent for a minute over a resource conflict it had no part in.
    //
    // The outcome fed in is the one the product returned, parsed back from the probe, rather than
    // a constant typed here. If the refusal ever goes back to Failed this check fails.
    {
      const ThumbnailOutcome refused = std::string(secondOutcome) == "Canceled"
                                           ? ThumbnailOutcome::Canceled
                                       : std::string(secondOutcome) == "TimedOut"
                                           ? ThumbnailOutcome::TimedOut
                                       : std::string(secondOutcome) == "Ok"
                                           ? ThumbnailOutcome::Ok
                                           : ThumbnailOutcome::Failed;
      ThumbnailBudget budget;
      ThumbnailTargetKey key;
      key.windowId = 0x4242;
      key.ownerPid = 1234;
      key.processCreatedUs = 5678;
      uint64_t now = 0;
      bool allowedThroughout = true;
      for (int i = 0; i < 3; ++i) {
        if (!budget.Allow(key, now)) allowedThroughout = false;
        budget.Record(key, refused, now);
        now += 10 * 1000;  // faster than any retry interval, on purpose
      }
      check("three refusals in a row leave the window still allowed",
            allowedThroughout && budget.Allow(key, now),
            "a resource conflict must not cost the window its three attempts");
      check("...and put it in no cooldown", !budget.InCooldown(key, now),
            std::to_string(budget.ConsecutiveFailures(key)) + " failures recorded");
    }
  }

  // Clean up the helper the fault injection refused to kill. By pid and by path.
  const std::vector<DWORD> left = helpers_in(dir);
  for (DWORD pid : left) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (h) {
      TerminateProcess(h, 1);
      WaitForSingleObject(h, 5000);
      CloseHandle(h);
    }
  }
  check("the lingering helper could be cleaned up by this test", helpers_in(dir).empty(),
        std::to_string(helpers_in(dir).size()) + " left");

  for (int i = 0; i < 30; ++i) {
    DeleteFileW((dir + L"GNLinkCapture.exe").c_str());
    DeleteFileW((dir + L"probe.exe").c_str());
    DeleteFileW(resultFile.c_str());
    if (RemoveDirectoryW(dir.substr(0, dir.size() - 1).c_str())) break;
    Sleep(100);
  }
  check("the scratch directory is cleaned up",
        GetFileAttributesW(dir.substr(0, dir.size() - 1).c_str()) == INVALID_FILE_ATTRIBUTES);

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  ("
            << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
