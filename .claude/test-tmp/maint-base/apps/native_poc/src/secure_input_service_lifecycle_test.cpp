// prelogin r21 E1 (Codex codex_prelogin_r20_integ.md): the secure-input pipe server ending while the service is
// meant to run must take the PRODUCT service down its own failure path -- running cleared, the agent stopped,
// every worker joined, then SERVICE_STOPPED exactly once with the original non-zero error -- and an ordinary
// external STOP must end with error 0.
//
// What runs: GNLinkInputServiceLifecycleTest.exe -- the product's secure_input_service_main.cpp and its
// sources, built with REMOTE60_SIS_TEST_SEAM (secure_input_service_test_seam.hpp says exactly what that
// replaces: the SCM boundary is printed, the pipes get per-run names and a DACL this user can open, the
// secure-input pipe's create fails on the calls this test names, and OS effects -- prelogin record, worker,
// user Host, ProgramData log -- are off). service_main, service_control, RunMultiClientPipeServer, the
// workers and report_service_status are the product's. This test only starts it, connects clients to its
// secure-input pipe, signals the STOP event, and reads the status lines it printed.
//
// Not shown here: the real SCM (registration, rights, QueryServiceStatusEx), SYSTEM, the input agent.
//
//   A first     the first instance's create fails                       -> STOPPED once, error = injected
//   B three     creates 2,3,4 (next listener, per client) fail in a row -> STOPPED once, error = injected
//   C control   creates 2 fail, 3 ok (served), 4,5 fail                 -> still running; then STOP -> error 0
//   D stop      no fault, external STOP                                  -> STOPPED once, error 0

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
bool wait_until(const std::function<bool()>& done, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    Sleep(20);
  }
  return done();
}
std::wstring dir_of_self() {
  wchar_t p[MAX_PATH];
  GetModuleFileNameW(nullptr, p, MAX_PATH);
  std::wstring s(p);
  return s.substr(0, s.find_last_of(L'\\') + 1);
}
std::string read_all(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
std::string narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }

constexpr DWORD kInjected = 1450;  // ERROR_NO_SYSTEM_RESOURCES

struct Run {
  std::wstring suffix, logPath, stopEvent;
  PROCESS_INFORMATION pi{};
  HANDLE stopEv = nullptr;
  bool Start(const std::wstring& tag, const std::wstring& failPlan) {
    suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + tag;
    const std::wstring dir = dir_of_self();
    logPath = dir + L"sis_lifecycle_" + tag + L".log";
    stopEvent = L"Local\\GNLinkSisTest-" + suffix + L"-stop";
    stopEv = CreateEventW(nullptr, TRUE, FALSE, stopEvent.c_str());
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_SUFFIX", suffix.c_str());
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_CREATE_FAIL", failPlan.c_str());
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_CREATE_ERROR", std::to_wstring(kInjected).c_str());
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_STOP_EVENT", stopEvent.c_str());
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_DIAG", (dir + L"sis_lifecycle_" + tag + L".diag.log").c_str());
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log;
    si.hStdError = log;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring cmd = L"\"" + dir + L"GNLinkInputServiceLifecycleTest.exe\" --lifecycle-test";
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi);
    CloseHandle(log);
    return ok != FALSE;
  }
  std::string Log() const { return read_all(logPath); }
  bool Has(const std::string& needle) const { return Log().find(needle) != std::string::npos; }
  std::wstring InputPipe() const { return L"\\\\.\\pipe\\GNLinkSisTest-" + suffix + L"-input"; }
  bool Exited(int ms) { return WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0; }
  void Kill() {
    if (pi.hProcess && WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 99);
  }
  ~Run() {
    Kill();
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
    if (stopEv) CloseHandle(stopEv);
  }
};

// "status state=S error=E" lines, in order.
std::vector<std::pair<unsigned long, unsigned long>> statuses(const std::string& log) {
  std::vector<std::pair<unsigned long, unsigned long>> out;
  std::istringstream in(log);
  std::string line;
  while (std::getline(in, line)) {
    unsigned long s = 0, e = 0;
    const size_t at = line.find("status state=");
    if (at != std::string::npos && std::sscanf(line.c_str() + at, "status state=%lu error=%lu", &s, &e) == 2) out.push_back({s, e});
  }
  return out;
}
size_t pos_of(const std::string& log, const std::string& needle) {
  const size_t p = log.find(needle);
  return p == std::string::npos ? static_cast<size_t>(-1) : p;
}

// The common judgement of an ended run: one STOPPED, last, with `error`; the cleanup lines before it.
void judge_stopped(const std::string& name, Run& run, unsigned long error, bool failurePath) {
  const bool exited = run.Exited(15000);
  if (!exited) run.Kill();
  const std::string log = run.Log();
  const auto st = statuses(log);
  int stopped = 0;
  for (const auto& s : st) stopped += s.first == SERVICE_STOPPED ? 1 : 0;
  const bool lastIsStopped = !st.empty() && st.back().first == SERVICE_STOPPED;
  const unsigned long lastError = st.empty() ? 0xFFFFFFFFul : st.back().second;
  const size_t stoppedAt = log.rfind("status state=1 ");
  const bool cleanupFirst = stoppedAt != std::string::npos && pos_of(log, "stop_agent") < stoppedAt &&
                            pos_of(log, "joined: prelogin supervisor") < stoppedAt &&
                            pos_of(log, "joined: admin pipe server") < stoppedAt &&
                            pos_of(log, "joined: unlock worker") < stoppedAt;
  const bool cleared = !failurePath || pos_of(log, "running cleared") < stoppedAt;
  const bool returned = log.find("service_main returned") != std::string::npos;
  std::string seq;
  for (const auto& s : st) seq += std::to_string(s.first) + "/" + std::to_string(s.second) + " ";
  check(name + ": the process ended by itself (service_main returned)", exited && returned);
  check(name + ": SERVICE_STOPPED exactly once, as the last status, error=" + std::to_string(error),
        stopped == 1 && lastIsStopped && lastError == error, "statuses " + seq);
  check(name + (failurePath ? ": running cleared, agent stopped, every worker joined -- before STOPPED"
                            : ": agent stopped, every worker joined -- before STOPPED"),
        cleanupFirst && cleared);
}

// One client to the run's secure-input pipe; returns once the service has made its next create (the hand-over
// decision for this client), the handle still open in *out (or closed when !keep).
bool client_round(Run& run, int expectCreate, bool keep, HANDLE* out) {
  HANDLE h = INVALID_HANDLE_VALUE;
  const std::wstring name = run.InputPipe();
  wait_until([&] {
    h = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeW(name.c_str(), 50);
    return h != INVALID_HANDLE_VALUE;
  }, 3000);
  if (h == INVALID_HANDLE_VALUE) return false;
  const std::string needle = "secure pipe create #" + std::to_string(expectCreate) + " ";
  const bool decided = wait_until([&] { return run.Has(needle); }, 3000);
  Sleep(100);
  if (keep) {
    *out = h;
  } else {
    CloseHandle(h);
  }
  return decided;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the product service's lifecycle when its secure-input pipe server ends (prelogin r21 E1)\n");

  {
    std::printf("\n--- A: the first instance cannot be created ---\n");
    Run run;
    check("A: the test build starts", run.Start(L"A", L"1"));
    judge_stopped("A", run, kInjected, true);
    check("A: no external STOP was involved", !run.Has("external STOP"));
  }
  {
    std::printf("\n--- B: the next listener fails three times in a row ---\n");
    Run run;
    check("B: the test build starts", run.Start(L"B", L"2,3,4"));
    const bool up = wait_until([&] { return run.Has("secure pipe create #1 -> real"); }, 5000);
    HANDLE unused = nullptr;
    const bool c1 = up && client_round(run, 2, false, &unused);
    const bool c2 = c1 && client_round(run, 3, false, &unused);
    const bool still = c2 && !run.Exited(300) && statuses(run.Log()).back().first == SERVICE_RUNNING;
    const bool c3 = c2 && client_round(run, 4, false, &unused);
    check("B: after two failures the service still runs; the third client brings the third", up && c1 && c2 && still && c3);
    judge_stopped("B", run, kInjected, true);
  }
  {
    std::printf("\n--- C: 1 failure -> success -> 2 failures does not end it; then an external STOP ---\n");
    Run run;
    check("C: the test build starts", run.Start(L"C", L"2,4,5"));
    const bool up = wait_until([&] { return run.Has("secure pipe create #1 -> real"); }, 5000);
    HANDLE unused = nullptr, served = INVALID_HANDLE_VALUE;
    const bool r1 = up && client_round(run, 2, false, &unused);    // #2 fails: refused
    const bool r2 = r1 && client_round(run, 3, true, &served);     // #3 real: this client is served
    const bool r3 = r2 && client_round(run, 4, false, &unused);    // #4 fails
    const bool r4 = r3 && client_round(run, 5, false, &unused);    // #5 fails -- two in a row after a success
    Sleep(1000);
    const auto st = statuses(run.Log());
    const bool running = !run.Exited(0) && !st.empty() && st.back().first == SERVICE_RUNNING &&
                         !run.Has("serving ended while running");
    check("C: the counter was reset by the success -- still RUNNING after fail, ok, fail, fail", r1 && r2 && r3 && r4 && running);
    SetEvent(run.stopEv);
    judge_stopped("C", run, 0, false);
    check("C: the STOP went through the product handler (STOP_PENDING before STOPPED)", [&] {
      const auto s = statuses(run.Log());
      return s.size() >= 2 && s[s.size() - 2].first == SERVICE_STOP_PENDING;
    }());
    if (served != INVALID_HANDLE_VALUE) CloseHandle(served);
  }
  {
    std::printf("\n--- D: an ordinary external STOP ---\n");
    Run run;
    check("D: the test build starts", run.Start(L"D", L""));
    const bool up = wait_until([&] { return run.Has("secure pipe create #1 -> real"); }, 5000);
    Sleep(300);
    SetEvent(run.stopEv);
    check("D: it came up", up);
    judge_stopped("D", run, 0, false);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
