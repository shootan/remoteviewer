// The isolated preview path, end to end, including the case the deadline exists for.
//
// capture_thumbnail_isolated finds its helper beside the running executable -- GNLinkCapture.exe
// in the same directory. That is what makes this testable without a single test hook in the
// product: copy this test into a scratch directory, put something named GNLinkCapture.exe next to
// it, and the production code resolves the helper to whatever was placed there.
//
// Two directories, therefore two cases. One holds the real GNLinkCapture, which answers, and the
// path produces pixels. The other holds a copy of this test, which when invoked as a helper never
// signals -- the stall the deadline is for, injected at the process boundary rather than faked
// inside the capture. Nothing is stubbed: it is the real CreateProcessW, the real mapping, the
// real wait.
//
// This is the negative control the WM_PRINT fixture could not be. A window that ignores WM_PRINT
// does not block this capture at all on this OS (PW_RENDERFULLCONTENT is served from DWM,
// measured in thumbnail_deadline_test), so the only honest way to exercise a missed deadline is
// to stall something we own.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "host_thumbnail_helper.hpp"
#include "time_utils.hpp"

namespace {

using remote60::native_poc::capture_thumbnail_isolated;
using remote60::native_poc::ThumbnailCaptureResult;
using remote60::native_poc::ThumbnailOutcome;
using remote60::native_poc::qpc_now_us;

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

std::wstring scratch_dir(const wchar_t* tag) {
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  std::wstring dir = std::wstring(temp) + L"remote60_thumb_e2e_" + tag + L"_" +
                     std::to_wstring(GetCurrentProcessId());
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\";
}

/** Run `probe.exe --probe-out <file>` in `dir` and read back what it recorded. */
bool run_probe_in(const std::wstring& dir, uint64_t deadlineUs, std::string* outcomeOut,
                  uint64_t* elapsedOut, uint32_t* widthOut, DWORD* helperSurvivors) {
  const std::wstring probe = dir + L"probe.exe";
  const std::wstring resultFile = dir + L"result.txt";
  DeleteFileW(resultFile.c_str());

  std::wstring cmd = L"\"" + probe + L"\" --probe-out \"" + resultFile + L"\" --deadline-us " +
                     std::to_wstring(deadlineUs);
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
    return false;
  }
  // Generous: the probe is expected to finish on its own well before this. If it does not, the
  // deadline inside it did not work, which is the thing being measured.
  const DWORD waited = WaitForSingleObject(pi.hProcess, 30000);
  if (waited != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (waited != WAIT_OBJECT_0) return false;

  FILE* f = nullptr;
  if (_wfopen_s(&f, resultFile.c_str(), L"r") != 0 || !f) return false;
  char outcome[64]{};
  unsigned long long elapsed = 0;
  unsigned width = 0;
  unsigned long survivors = 0;
  const int read = std::fscanf(f, "%63s %llu %u %lu", outcome, &elapsed, &width, &survivors);
  std::fclose(f);
  if (read != 4) return false;
  *outcomeOut = outcome;
  *elapsedOut = elapsed;
  *widthOut = width;
  *helperSurvivors = survivors;
  return true;
}

/** Count processes named GNLinkCapture.exe that were started from `dir`. */
DWORD helpers_from(const std::wstring& dir);

/** Probe mode: one real capture through the production path, result written to a file. */
int run_probe(const std::wstring& outFile, uint64_t deadlineUs) {
  // nullptr = the whole desktop. The real helper can always satisfy that; the stalling one never
  // satisfies anything, which is the difference the two directories are for.
  const ThumbnailCaptureResult result =
      capture_thumbnail_isolated(nullptr, 256, 160, deadlineUs);
  // Counted after the call returns: whatever the outcome, no helper may still be running.
  const DWORD survivors = helpers_from(directory_of(self_path()));

  FILE* f = nullptr;
  if (_wfopen_s(&f, outFile.c_str(), L"w") != 0 || !f) return 2;
  std::fprintf(f, "%s %llu %u %lu\n", name_of(result.outcome),
               static_cast<unsigned long long>(result.elapsedUs), result.width,
               static_cast<unsigned long>(survivors));
  std::fclose(f);
  return 0;
}

bool copy_file(const std::wstring& from, const std::wstring& to) {
  return CopyFileW(from.c_str(), to.c_str(), FALSE) != FALSE;
}

void remove_dir(const std::wstring& dir) {
  DeleteFileW((dir + L"GNLinkCapture.exe").c_str());
  DeleteFileW((dir + L"probe.exe").c_str());
  DeleteFileW((dir + L"result.txt").c_str());
  std::wstring trimmed = dir;
  if (!trimmed.empty() && trimmed.back() == L'\\') trimmed.pop_back();
  RemoveDirectoryW(trimmed.c_str());
}

}  // namespace

#include <tlhelp32.h>

namespace {
DWORD helpers_from(const std::wstring& dir) {
  DWORD count = 0;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, L"GNLinkCapture.exe") != 0) continue;
      HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
      if (!h) continue;
      wchar_t image[MAX_PATH]{};
      DWORD size = MAX_PATH;
      // Only ones started from the scratch directory. The user's installed GNLinkCapture is none
      // of this test's business and must never be counted, let alone touched.
      if (QueryFullProcessImageNameW(h, 0, image, &size) &&
          _wcsnicmp(image, dir.c_str(), dir.size()) == 0) {
        ++count;
      }
      CloseHandle(h);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return count;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // --- helper mode: never answer -------------------------------------------
  //
  // Reached only when this executable has been copied over GNLinkCapture.exe. It takes the real
  // arguments and does nothing with them: no mapping written, no done event set. The host's
  // deadline is the only thing that ends it.
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {
      Sleep(120000);
      return 0;
    }
  }

  // --- probe mode ----------------------------------------------------------
  std::wstring outFile;
  uint64_t deadlineUs = 1000 * 1000;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::wstring(argv[i]) == L"--probe-out") outFile = argv[i + 1];
    if (std::wstring(argv[i]) == L"--deadline-us") deadlineUs = std::wcstoull(argv[i + 1], nullptr, 10);
  }
  if (!outFile.empty()) return run_probe(outFile, deadlineUs);

  // --- orchestration -------------------------------------------------------
  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const std::wstring realHelper = myDir + L"GNLinkCapture.exe";

  // ---------------------------------------------------------------- the real helper
  {
    const std::wstring dir = scratch_dir(L"real");
    const bool staged = copy_file(me, dir + L"probe.exe") &&
                        copy_file(realHelper, dir + L"GNLinkCapture.exe");
    check("the real GNLinkCapture could be staged beside a probe", staged,
          staged ? std::string() : "is remote60_gdi_capture_worker built?");
    if (staged) {
      std::string outcome;
      uint64_t elapsed = 0;
      uint32_t width = 0;
      DWORD survivors = 0;
      const bool ran = run_probe_in(dir, 1000 * 1000, &outcome, &elapsed, &width, &survivors);
      check("a capture through the real helper completes", ran && outcome == "Ok",
            ran ? outcome : "probe did not report");
      check("...and brings back pixels", width > 0, std::to_string(width) + "px wide");
      check("...well inside the deadline", ran && elapsed < 1000 * 1000,
            std::to_string(elapsed / 1000) + "ms");
      check("...leaving no helper behind", ran && survivors == 0,
            std::to_string(survivors) + " still running");
    }
    remove_dir(dir);
  }

  // ---------------------------------------------------------------- a helper that never answers
  {
    const std::wstring dir = scratch_dir(L"stall");
    // The fake helper IS this executable: invoked with --thumbnail it sleeps for two minutes.
    const bool staged = copy_file(me, dir + L"probe.exe") &&
                        copy_file(me, dir + L"GNLinkCapture.exe");
    check("a stalling helper could be staged", staged);
    if (staged) {
      std::string outcome;
      uint64_t elapsed = 0;
      uint32_t width = 0;
      DWORD survivors = 0;
      const uint64_t deadline = 400 * 1000;
      const uint64_t before = qpc_now_us();
      const bool ran = run_probe_in(dir, deadline, &outcome, &elapsed, &width, &survivors);
      const uint64_t wall = qpc_now_us() - before;

      check("a helper that never answers is reported as TimedOut",
            ran && outcome == "TimedOut", ran ? outcome : "probe did not report");
      check("...at the deadline, not after the helper's two minute sleep",
            ran && elapsed < deadline * 4,
            std::to_string(elapsed / 1000) + "ms against a " + std::to_string(deadline / 1000) +
                "ms deadline");
      check("...and no pixels are claimed", width == 0, std::to_string(width));
      check("...and the stalled helper is gone afterwards", ran && survivors == 0,
            std::to_string(survivors) + " still running");
      check("...so the whole call cost the caller seconds, not minutes", wall < 30ull * 1000 * 1000,
            std::to_string(wall / 1000) + "ms end to end");
    }
    remove_dir(dir);
  }

  // ---------------------------------------------------------------- no helper at all
  {
    const std::wstring dir = scratch_dir(L"missing");
    const bool staged = copy_file(me, dir + L"probe.exe");
    check("a probe with no helper beside it could be staged", staged);
    if (staged) {
      std::string outcome;
      uint64_t elapsed = 0;
      uint32_t width = 0;
      DWORD survivors = 0;
      const bool ran = run_probe_in(dir, 1000 * 1000, &outcome, &elapsed, &width, &survivors);
      check("a missing helper is a failure, not a wait", ran && outcome == "Failed",
            ran ? outcome : "probe did not report");
      check("...reported immediately", ran && elapsed < 1000 * 1000,
            std::to_string(elapsed / 1000) + "ms");
    }
    remove_dir(dir);
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  ("
            << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
