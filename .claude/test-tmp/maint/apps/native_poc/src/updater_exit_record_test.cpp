// Every GNLinkUpdater run ends with a verdict line, and the working copy's start is recorded.
//
// Ledger 0.0.10 (history #512/#516): updater.log had no "result:" line and was read as success.
// Only the path through effects.run() wrote one; every early return left an explanation and no
// verdict. And the launch of the working copy -- CREATE_BREAKAWAY_FROM_JOB -- logged its failures
// and said nothing when it worked, so a log could not tell "started" from "never got there".
//
// This runs the REAL updater executable, built from the same sources as GNLinkUpdater.exe with
// one difference: an asInvoker manifest (target remote60_updater_asinvoker_test), so a
// non-elevated runner can start it. What that build cannot show: requireAdministrator, UAC, and
// the admin-only working directory under %ProgramFiles% -- none of which these cases reach.
//
// Every case stops BEFORE the update: the credential frame is deliberately not a frame, so the
// working copy exits 6 at the decode. Nothing is fetched, no lock is taken, no process is stopped,
// and every path is under this run's own temp root.
//
// Run: remote60_updater_exit_record_test [path-to-updater.exe]. Prints PASS/FAIL, exit 0 or 1.

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "update_credential_channel.hpp"

using remote60::native_poc::update::CredentialServer;

namespace {

int gFailures = 0;

void check(const std::string& what, bool ok, const std::string& detail) {
  // The detail is usually the whole log; printed only where it explains a failure.
  const bool show = !ok && !detail.empty();
  std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), show ? " -- " : "",
              show ? detail.c_str() : "");
  if (!ok) ++gFailures;
}

std::string narrow(const std::wstring& w) {
  std::string out;
  for (wchar_t c : w) out += (c < 128) ? static_cast<char>(c) : '?';
  return out;
}

std::string read_file(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

void remove_tree(const std::wstring& dir) {
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = fd.cFileName;
      if (name == L"." || name == L"..") continue;
      const std::wstring full = dir + L"\\" + name;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        remove_tree(full);
      } else {
        SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(full.c_str());
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryW(dir.c_str());
}

struct Run {
  bool started = false;
  bool finished = false;
  DWORD exitCode = 0;
  std::string serveError;
};

/** Starts the updater with `args`; if `pipe` is set, serves it `payload` over that channel. */
Run run_updater(const std::wstring& exe, const std::wstring& args, const std::wstring& pipe,
                const std::string& payload) {
  Run r;
  CredentialServer server;
  if (!pipe.empty()) {
    std::string error;
    if (!server.Create(pipe, &error)) {
      r.serveError = "create: " + error;
      return r;
    }
  }
  std::wstring cmd = L"\"" + exe + L"\" " + args;
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      nullptr, &si, &pi)) {
    r.serveError = "CreateProcessW error " + std::to_string(GetLastError());
    return r;
  }
  r.started = true;
  CloseHandle(pi.hThread);
  if (!pipe.empty()) {
    std::string copy = payload;
    std::string error;
    if (!server.Serve(pi.hProcess, copy, 20000, &error)) r.serveError = "serve: " + error;
  }
  if (WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0) {
    r.finished = true;
    GetExitCodeProcess(pi.hProcess, &r.exitCode);
  } else {
    TerminateProcess(pi.hProcess, 0xDEAD);
    WaitForSingleObject(pi.hProcess, 5000);
  }
  CloseHandle(pi.hProcess);
  return r;
}

/** Waits for `needle` to appear in the log -- the working copy writes after the bootstrap exits. */
bool wait_for_log(const std::wstring& log, const std::string& needle, int timeoutMs) {
  for (int waited = 0; waited < timeoutMs; waited += 100) {
    if (contains(read_file(log), needle)) return true;
    Sleep(100);
  }
  return contains(read_file(log), needle);
}

}  // namespace

int main(int argc, char** argv) {
  std::wstring exe;
  if (argc >= 2) {
    const std::string a = argv[1];
    exe.assign(a.begin(), a.end());
  } else {
    exe = GNLINK_UPDATER_ASINVOKER;
  }
  std::printf("updater_exit_record_test: %s\n", narrow(exe).c_str());

  wchar_t tempBuf[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tempBuf);
  const DWORD pid = GetCurrentProcessId();
  const std::wstring root = std::wstring(tempBuf) + L"remote60_updexit_" + std::to_wstring(pid);
  remove_tree(root);
  CreateDirectoryW(root.c_str(), nullptr);
  const std::wstring install = root + L"\\install";
  const std::wstring staging = root + L"\\staging";
  CreateDirectoryW(install.c_str(), nullptr);
  CreateDirectoryW(staging.c_str(), nullptr);

  // Everything the updater requires, all of it this run's own. The url is https and never
  // contacted: every case ends at the credential decode, before any fetch.
  const auto common_args = [&](const std::wstring& work, const std::wstring& log) {
    return L"--install-dir \"" + install + L"\" --staging-dir \"" + staging + L"\" --work-dir \"" +
           work + L"\" --manifest-url \"https://127.0.0.1:9/exit-record-fixture.manifest\"" +
           L" --platform windows --installed-version 0.0.0 --health-log \"" + root +
           L"\\health.log\" --log \"" + log +
           L"\" --service-name GNLinkExitRecordTestService --registry-root "
           L"\"HKCU\\Software\\GNLinkExitRecordTest\"";
  };
  const std::string notAFrame = "exit-record-fixture: not a credential frame";

  // 1. The working copy stops at the decode (exit 6). The explanation was always written; the
  //    verdict was not.
  {
    const std::wstring log = root + L"\\copy.log";
    const std::wstring pipe = L"\\\\.\\pipe\\remote60-updexit-" + std::to_wstring(pid) + L"-copy";
    const Run r = run_updater(exe, common_args(root + L"\\work1", log) +
                                       L" --running-from-copy --credential-pipe \"" + pipe + L"\"",
                              pipe, notAFrame);
    const std::string text = read_file(log);
    check("1 working copy: it ran and exited 6 at the decode",
          r.finished && r.exitCode == 6,
          "started=" + std::to_string(r.started) + " exit=" + std::to_string(r.exitCode) + " " +
              r.serveError);
    check("1 working copy: the reason is logged", contains(text, "did not parse"), text);
    check("1 working copy: and so is a verdict", contains(text, "result: NotRun -- exit 6"), text);
    check("1 working copy: the fixture payload is not echoed", !contains(text, notAFrame), "");
  }

  // 2. The bootstrap refuses its working directory (a file where the directory should be): the
  //    copy is never started, exit 3, and that too ends with a verdict.
  {
    const std::wstring log = root + L"\\refused.log";
    const std::wstring notADir = root + L"\\work-is-a-file";
    { std::ofstream(notADir, std::ios::binary) << "x"; }
    const Run r = run_updater(exe, common_args(notADir, log), L"", "");
    const std::string text = read_file(log);
    check("2 refused work dir: the bootstrap exits 3", r.finished && r.exitCode == 3,
          "exit=" + std::to_string(r.exitCode) + " " + r.serveError);
    check("2 refused work dir: the refusal is logged",
          contains(text, "refusing to use the working directory"), text);
    check("2 refused work dir: and so is a verdict", contains(text, "result: NotRun -- exit 3"),
          text);
    check("2 refused work dir: nothing claims a copy was started",
          !contains(text, "started the working copy"), text);
  }

  // 3. The bootstrap hands over: the copy's start is recorded with whether it broke away, the
  //    bootstrap says the verdict is the copy's, and the copy's own verdict follows.
  //
  //    Ten times, because the two processes append to the same log at the same moment and a lost
  //    line is intermittent: the first version of this case caught "bootstrap done" missing in one
  //    run of two, from a deny-write open in the other process. One pass proves nothing about that.
  {
    constexpr int kHandovers = 10;
    int complete = 0;
    std::string firstIncomplete;
    bool brokeAway = false;
    bool fellBack = false;
    for (int i = 0; i < kHandovers; ++i) {
      const std::wstring n = std::to_wstring(i);
      const std::wstring log = root + L"\\handover" + n + L".log";
      const std::wstring pipe =
          L"\\\\.\\pipe\\remote60-updexit-" + std::to_wstring(pid) + L"-boot" + n;
      const Run r = run_updater(exe, common_args(root + L"\\work3-" + n, log) +
                                         L" --credential-pipe \"" + pipe + L"\"",
                                pipe, notAFrame);
      const bool copyVerdict = wait_for_log(log, "result: NotRun -- exit 6", 20000);
      const std::string text = read_file(log);
      brokeAway = brokeAway || contains(text, "(broke away from the job)");
      fellBack = fellBack || contains(text, "(breakaway refused;");
      const bool ok = r.finished && r.exitCode == 0 &&
                      contains(text, "started the working copy pid=") &&
                      (contains(text, "(broke away from the job)") ||
                       contains(text, "(breakaway refused;")) &&
                      contains(text, "bootstrap done") &&
                      !contains(text, "result: NotRun -- exit 0") && copyVerdict;
      if (ok) {
        ++complete;
      } else if (firstIncomplete.empty()) {
        firstIncomplete = "run " + std::to_string(i) + " exit=" + std::to_string(r.exitCode) +
                          " " + r.serveError + " log:\n" + text;
      }
    }
    std::printf("NOTE  3 handover: this runner %s\n",
                brokeAway ? "allowed breakaway" : (fellBack ? "refused breakaway" : "(unknown)"));
    // Each run must have: bootstrap exit 0, "started the working copy pid=" with the breakaway
    // answer, "bootstrap done" and no bootstrap verdict of its own, then the copy's verdict.
    check("3 handover: every one of " + std::to_string(kHandovers) +
              " handovers records the start, the breakaway, the bootstrap's last word and the "
              "copy's verdict (" + std::to_string(complete) + "/" + std::to_string(kHandovers) + ")",
          complete == kHandovers, firstIncomplete);
  }

  // The working copy may still be closing its image; give it a moment before removing the tree.
  for (int i = 0; i < 20; ++i) {
    remove_tree(root);
    if (GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES) break;
    Sleep(250);
  }
  check("the temp root is removed", GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES,
        narrow(root));

  if (gFailures == 0) {
    std::printf("RESULT: ALL PASS\n");
    return 0;
  }
  std::printf("RESULT: FAILED (%d)\n", gFailures);
  return 1;
}
