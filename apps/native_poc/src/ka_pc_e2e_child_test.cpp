// ka-pc e2e (Codex codex_ka_pc_test_src_r1.md N2): the shared ka-pc e2e Child (ka_pc_e2e_child.hpp) on its own --
// no host, no viewer, no window, no network, no WebView. Its children are this executable itself (--print-and-sleep).
//
//   1. the generic end verdict: an end not confirmed / a read error / a cut read / a log not kept is never a pass
//   2. a set-up step that fails (pipe, NUL, attribute list, HANDLE_LIST whitelist): no process created, counted
//   3. a real child: ended (confirmed), read to EOF, its output in its own log as it was read, a trailer beside it
//   4. a host's Finalize that ends the run INVALID (std::_Exit): the output read so far and the trailer are on disk
//      before it -- run in a scenario process (this executable, --scenario-finalize-invalid), itself a Child
//
// Writes only under REMOTE60_TEST_TMP (a folder directly under D:/remote/remote/.claude/test-tmp), in a NEW folder of
// this run; deletes nothing.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <string>

#include "ka_pc_e2e_child.hpp"
#include "ka_pc_e2e_webview.hpp"

namespace {

namespace e2e = remote60::native_poc::e2e;
using ka_pc_e2e::Child;

int gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!ok) ++gFailures;
}

std::wstring self_path() {
  wchar_t b[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, b, MAX_PATH);
  return b;
}
std::wstring self_dir() {
  const std::wstring s = self_path();
  return s.substr(0, s.find_last_of(L"\\/") + 1);
}
std::string file_text(const std::wstring& path) {
  const e2e::E2eFinalOutput f = e2e::e2e_final_file(path);
  return f.complete ? f.text : std::string();
}

bool init_root(std::wstring* root, std::string* why) {
  wchar_t given[1024] = {};
  if (!GetEnvironmentVariableW(L"REMOTE60_TEST_TMP", given, 1024)) { *why = "REMOTE60_TEST_TMP is not set"; return false; }
  wchar_t full[1024] = {};
  wchar_t* name = nullptr;
  if (!GetFullPathNameW(given, 1024, full, &name) || !name || !*name) { *why = "unusable REMOTE60_TEST_TMP"; return false; }
  std::wstring parent(full, name - full);
  while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/')) parent.pop_back();
  if (_wcsicmp(parent.c_str(), L"D:\\remote\\remote\\.claude\\test-tmp") != 0) {
    *why = "REMOTE60_TEST_TMP must be a folder directly under D:/remote/remote/.claude/test-tmp";
    return false;
  }
  const DWORD a = GetFileAttributesW(full);
  if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT)) { *why = "the test root is a link"; return false; }
  CreateDirectoryW(full, nullptr);
  const std::wstring run = std::wstring(full) + L"\\run-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64());
  if (!CreateDirectoryW(run.c_str(), nullptr) || !ka_pc_e2e::resolves_to_itself(run)) {
    *why = "this run's own folder could not be made new";
    return false;
  }
  *root = run + L"\\";
  return true;
}

constexpr char kEvidence[] = "ka-child-evidence: this line was read before the end";

// The scenario (a process of its own): a child that printed its line, judged as a HOST by the shared guard's monitor
// (no OFF lines: its Finalize must end this process INVALID, exit 2) -- its log and trailer must be on disk already.
int scenario_finalize_invalid(const std::wstring& dir) {
  Child c;
  c.logPath = dir + L"\\child.log";
  if (!c.Start(L"\"" + self_path() + L"\" --print-and-sleep", self_dir(), nullptr)) return 10;
  if (!c.WaitFor([](const std::string& t) { return t.find(kEvidence) != std::string::npos; }, 10000)) return 11;
  c.monitor = std::make_unique<e2e::E2eInjectionMonitor>([&c] { return c.Text(); }, c.job, "scenario child", false);
  c.End();   // Finalize: no OFF lines -> FAIL RUN ... RESULT: INVALID, std::_Exit(2)
  return 12;  // never reached when Finalize refuses
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc >= 2 && std::strcmp(argv[1], "--spawn-and-sleep") == 0) {
    // a descendant in the same job (no breakaway), sharing this stdout; both say the line
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + self_path() + L"\" --print-and-sleep";
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    std::printf("%s\n", kEvidence);
    std::fflush(stdout);
    Sleep(60000);
    return 0;
  }
  if (argc >= 2 && std::strcmp(argv[1], "--print-and-sleep") == 0) {
    std::printf("%s\n", kEvidence);
    std::fflush(stdout);
    Sleep(60000);
    return 0;
  }
  if (argc >= 3 && std::strcmp(argv[1], "--scenario-finalize-invalid") == 0) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, nullptr, 0);
    std::wstring dir(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, dir.data(), n);
    return scenario_finalize_invalid(dir);
  }

  std::wstring root;
  std::string why;
  if (!init_root(&root, &why)) {
    std::printf("FAIL  no test root: %s\nRESULT: INVALID\n", why.c_str());
    return 2;
  }
  std::printf("test root: %s\n", ka_pc_e2e::narrow_path(root).c_str());

  // ---------------------------------------------------------------- 1. the generic end verdict (pure)
  using E = e2e::E2eReadEnd;
  check("verdict: end confirmed + EOF + log kept -> accepted", ka_pc_e2e::child_end_why(true, E::Eof, 0, false, 0).empty());
  check("verdict: an end NOT confirmed is a failure (even with EOF)", !ka_pc_e2e::child_end_why(false, E::Eof, 0, false, 0).empty());
  check("verdict: a read error is a failure", !ka_pc_e2e::child_end_why(true, E::Error, 6, false, 0).empty());
  check("verdict: a read cut by its owner is a failure", !ka_pc_e2e::child_end_why(true, E::Cut, 0, false, 0).empty());
  check("verdict: a read still running is a failure", !ka_pc_e2e::child_end_why(true, E::Running, 0, false, 0).empty());
  check("verdict: a log not kept whole is a failure", !ka_pc_e2e::child_end_why(true, E::Eof, 0, true, 112).empty());

  // ---------------------------------------------------------------- 2. a failed set-up step: no process
  for (const char* step : {"pipe", "noinherit", "nul", "attrsize", "attrinit", "handlelist"}) {
    const int failuresBefore = ka_pc_e2e::child_failures(), startedBefore = ka_pc_e2e::children_started();
    ka_pc_e2e::start_step_refused_for_contrast() = step;
    bool started = true;
    HANDLE process = nullptr;
    {
      Child c;
      c.logPath = root + L"refused-" + std::wstring(step, step + std::strlen(step)) + L".log";
      started = c.Start(L"\"" + self_path() + L"\" --print-and-sleep", self_dir(), nullptr);
      process = c.pi.hProcess;
    }
    ka_pc_e2e::start_step_refused_for_contrast() = nullptr;
    check(std::string("set-up step '") + step + "' fails: Start refuses, no process created, the failure counted",
          !started && !process && ka_pc_e2e::children_started() == startedBefore &&
              ka_pc_e2e::child_failures() == failuresBefore + 1);
  }
  const int setupFailures = ka_pc_e2e::child_failures();

  // ---------------------------------------------------------------- 3. a real child, ended and judged
  {
    Child c;
    c.logPath = root + L"real-child.log";
    const bool started = c.Start(L"\"" + self_path() + L"\" --print-and-sleep", self_dir(), nullptr);
    const bool said = started && c.WaitFor([](const std::string& t) { return t.find(kEvidence) != std::string::npos; }, 10000);
    const bool inLogBeforeEnd = said && file_text(c.logPath).find(kEvidence) != std::string::npos;
    c.End();
    const std::string trailer = file_text(c.logPath + L".end");
    check("a real child: started in its job, its line read", said);
    check("...its output is in its own log while it still runs (written as read, not at End)", inLogBeforeEnd);
    check("...End: confirmed end, EOF, log kept -- no failure counted", ka_pc_e2e::child_failures() == setupFailures,
          std::to_string(ka_pc_e2e::child_failures() - setupFailures));
    check("...its trailer says so", trailer.find("producer end: confirmed") != std::string::npos &&
                                        trailer.find("read: end of output") != std::string::npos &&
                                        trailer.find("log: kept") != std::string::npos,
          trailer);
    c.End();  // again: nothing (ended once)
    check("...a second End changes nothing", ka_pc_e2e::child_failures() == setupFailures);
  }
  {
    Child never;  // a child never started (NOT RUN): its End is no failure
    never.End();
    check("a child never started: End is no failure", ka_pc_e2e::child_failures() == setupFailures);
  }

  // ---------------------------------------------------------------- 4. Finalize ends the run INVALID: evidence first
  {
    const std::wstring dir = root + L"scenario\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    Child s;
    s.logPath = root + L"scenario.log";
    // the folder without its trailing backslash: a backslash before the closing quote would escape it
    const bool started = s.Start(L"\"" + self_path() + L"\" --scenario-finalize-invalid \"" +
                                     ka_pc_e2e::no_trailing_slash(dir) + L"\"",
                                 self_dir(), nullptr);
    const bool exited = started && s.Exited(30000);
    DWORD code = 0;
    if (exited) GetExitCodeProcess(s.pi.hProcess, &code);
    const std::string said = s.Text();
    s.End();
    const std::string childLog = file_text(dir + L"child.log");
    const std::string childTrailer = file_text(dir + L"child.log.end");
    check("the scenario's host Finalize refused (no OFF lines): exit 2, RESULT: INVALID",
          exited && code == 2 && said.find("RESULT: INVALID") != std::string::npos, "exit " + std::to_string(code));
    check("...the child's output read before that is in its log", childLog.find(kEvidence) != std::string::npos);
    check("...and its trailer was written before Finalize (end confirmed, EOF)",
          childTrailer.find("producer end: confirmed") != std::string::npos &&
              childTrailer.find("read: end of output") != std::string::npos,
          childTrailer);
  }

  // ---------------------------------------------------------------- 5. the WebView preflight (no environment created)
  {
    std::wstring tmp;
    const bool temp = ka_pc_e2e::private_temp(root, &tmp, &why);
    check("this run's TEMP: made new, set, and GetTempPathW returns it", temp, why);
    const std::wstring shellUdf = tmp + ka_pc_e2e::kShellUdfName, macroUdf = tmp + ka_pc_e2e::kMacroUdfName;
    const std::string shellPre = ka_pc_e2e::webview_fix_and_preflight(root, ka_pc_e2e::kShellUdfName, shellUdf);
    const std::string macroPre = ka_pc_e2e::webview_fix_and_preflight(root, ka_pc_e2e::kMacroUdfName, macroUdf);
    check("the shell's and the macro's preflight hold on this PC now (no other WEBVIEW2_* variable, no WebView2 policy)",
          temp && shellPre.empty() && macroPre.empty(), shellPre + macroPre);
    const bool otherRoot =
        !ka_pc_e2e::webview_preflight(root, ka_pc_e2e::kMacroUdfName, root + L"other-root\\GNLinkMacro").empty();
    const bool crossed = !ka_pc_e2e::webview_preflight(root, ka_pc_e2e::kShellUdfName, macroUdf).empty();
    SetEnvironmentVariableW(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", L"C:\\ka-pc-preflight-contrast");
    const bool extra = ka_pc_e2e::webview_preflight(root, ka_pc_e2e::kMacroUdfName, macroUdf).find("other WebView2") !=
                       std::string::npos;
    SetEnvironmentVariableW(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", nullptr);
    SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", shellUdf.c_str());  // the shell's while the macro's is asked
    const bool moved = !ka_pc_e2e::webview_preflight(root, ka_pc_e2e::kMacroUdfName, macroUdf).empty();
    const bool back = ka_pc_e2e::webview_fix_and_preflight(root, ka_pc_e2e::kMacroUdfName, macroUdf).empty();
    check("...it refuses another owned path, the shell's name for the macro's folder, an extra WEBVIEW2_* variable and "
          "the shell's UDF in the variable while the macro's is asked; passes again once restored",
          otherRoot && crossed && extra && moved && back);
    check("no macro window of a process of this build exists now", !ka_pc_e2e::own_macro_window_exists(self_dir()));
  }

  // ---------------------------------------------------------------- 6. r3: a child ended by closing its job (crash step)
  int expectedFailures = 0;  // the contrasts below that MUST count a failure
  {
    ka_pc_e2e::CrashClose all;
    all.done = all.killOnClose = all.member = all.singleRoot = all.rootAlive = all.closed = all.rootEnded = true;
    bool each = ka_pc_e2e::crash_close_why(all).empty();
    for (bool ka_pc_e2e::CrashClose::*f : {&ka_pc_e2e::CrashClose::done, &ka_pc_e2e::CrashClose::killOnClose,
                                           &ka_pc_e2e::CrashClose::member, &ka_pc_e2e::CrashClose::singleRoot,
                                           &ka_pc_e2e::CrashClose::rootAlive, &ka_pc_e2e::CrashClose::closed,
                                           &ka_pc_e2e::CrashClose::rootEnded}) {
      ka_pc_e2e::CrashClose one = all;
      one.*f = false;
      each = each && !ka_pc_e2e::crash_close_why(one).empty();
    }
    check("crash-close record: whole -> accepted; any one item missing (no record, kill-on-close, membership, single root, "
          "alive before, close, root ended) -> refused",
          each);
  }
  auto start_self = [&](Child& c, const wchar_t* name, const wchar_t* mode) {
    c.logPath = root + name;
    return c.Start(L"\"" + self_path() + L"\" " + mode, self_dir(), nullptr) &&
           c.WaitFor([](const std::string& t) { return t.find(kEvidence) != std::string::npos; }, 10000);
  };
  {
    // before r3 (the managed test's old crash step): the job closed by hand, set null -- the child DID end (as in the
    // 10-29 managed run), and that is still refused: a job that is just gone is no evidence
    const int before = ka_pc_e2e::child_failures();
    Child c;
    const bool up = start_self(c, L"crash-by-hand.log", L"--print-and-sleep");
    CloseHandle(c.job);
    c.job = nullptr;
    const bool exited = c.Exited(5000);
    c.End();
    const std::string tr = file_text(c.logPath + L".end");
    check("the old crash step (job closed by hand, set null): the child ended, and its end is still NOT confirmed",
          up && exited && ka_pc_e2e::child_failures() == before + 1 &&
              tr.find("evidence: no job and no crash-close record") != std::string::npos,
          tr);
    ++expectedFailures;
  }
  {
    // r3: the same end through the explicit step -- accepted, its trailer says what it rests on
    const int before = ka_pc_e2e::child_failures();
    Child c;
    const bool up = start_self(c, L"crash-step.log", L"--print-and-sleep");
    std::string why;
    const bool crashed = up && c.CrashCloseJob(5000, &why);
    c.End();
    const std::string tr = file_text(c.logPath + L".end");
    check("the r3 crash step: kill-on-close, membership, the root alone, the close and the root's end recorded -> its end "
          "confirmed, EOF, log kept (no failure)",
          crashed && ka_pc_e2e::child_failures() == before &&
              tr.find("evidence: crash-close by this test, single root") != std::string::npos &&
              tr.find("read: end of output") != std::string::npos,
          why + " | " + tr);
  }
  {
    // a child with a descendant in its job: not a single root -- refused (never widened to several processes)
    const int before = ka_pc_e2e::child_failures();
    Child c;
    const bool up = start_self(c, L"crash-two.log", L"--spawn-and-sleep");
    const bool two = up && c.WaitFor([](const std::string& t) {
      size_t n = 0;
      for (size_t at = t.find(kEvidence); at != std::string::npos; at = t.find(kEvidence, at + 1)) ++n;
      return n >= 2;
    }, 10000);
    std::string why;
    const bool crashed = c.CrashCloseJob(5000, &why);
    c.End();
    check("a job with a descendant (not the root alone): the crash step refuses, its end is NOT confirmed",
          two && !crashed && why.find("other than the root") != std::string::npos &&
              ka_pc_e2e::child_failures() == before + 1,
          why);
    ++expectedFailures;
  }
  {
    // the close itself refused (the contrast seam, asked before the close): the job stays open, End ends the child
    // through it -- and that cleanup is NOT taken as the crash step's evidence
    const int before = ka_pc_e2e::child_failures();
    Child c;
    const bool up = start_self(c, L"crash-close-refused.log", L"--print-and-sleep");
    ka_pc_e2e::start_step_refused_for_contrast() = "crashclose";
    std::string why;
    const bool crashed = c.CrashCloseJob(2000, &why);
    ka_pc_e2e::start_step_refused_for_contrast() = nullptr;
    const bool jobKept = c.job != nullptr;
    c.End();
    check("the job's close fails: the crash step refuses, the job is kept for cleanup, its end is NOT confirmed",
          up && !crashed && jobKept && why.find("could not be closed") != std::string::npos &&
              ka_pc_e2e::child_failures() == before + 1,
          why);
    ++expectedFailures;
  }
  {
    // a host (a monitor is attached): never crash-closed
    const int before = ka_pc_e2e::child_failures();
    Child c;
    const bool up = start_self(c, L"crash-host.log", L"--print-and-sleep");
    c.monitor = std::make_unique<e2e::E2eInjectionMonitor>([&c] { return c.Text(); }, c.job, "contrast host", false);
    std::string why;
    const bool crashed = c.CrashCloseJob(2000, &why);
    c.monitor.reset();  // not finalized on purpose (this is not a host run); its end is judged as any child's
    c.End();
    check("a host (monitor attached): the crash step refuses and does nothing (its job still ends it normally)",
          up && !crashed && !c.crash.done && ka_pc_e2e::child_failures() == before, why);
  }

  const int childFailures = ka_pc_e2e::child_failures() - setupFailures - expectedFailures;
  check("no failure counted for the real children (the provoked set-up refusals aside)", childFailures == 0,
        std::to_string(childFailures));
  std::printf("\nka_pc_e2e_child_test: %s\n", gFailures ? (std::to_string(gFailures) + " FAILED").c_str() : "ALL PASS");
  return gFailures ? 1 : 0;
}
