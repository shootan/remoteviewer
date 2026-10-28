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

  const int childFailures = ka_pc_e2e::child_failures() - setupFailures;
  check("no failure counted for the real children (the provoked set-up refusals aside)", childFailures == 0,
        std::to_string(childFailures));
  std::printf("\nka_pc_e2e_child_test: %s\n", gFailures ? (std::to_string(gFailures) + " FAILED").c_str() : "ALL PASS");
  return gFailures ? 1 : 0;
}
