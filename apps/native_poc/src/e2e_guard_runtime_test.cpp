// The run-time half of the injection guard and the child helpers, through their REAL call paths (2026-10-28, Codex
// ba2eaf2 SOURCE G1 / F, r6; 85f9383 N1 / N2, r7). No host, no network peer, no input or power API: the "host" is a
// fixed string that the test changes, and the "child" is this executable sleeping (--sleeper) inside a kill-on-close
// job. Each case runs in its own child process (the guard ends a run with std::_Exit); the parent reads its exit code
// and output. Separate from the initial gate's pure tests (e2e_injection_guard_test).
//   G1:  after the gate, power ON / another input mode / input taken with injection on -> INVALID, child ended.
//   N1:  the final judgement alone (a monitor that never polled) catches it; output that arrives AFTER polling stopped
//        is caught by Finalize -- and by the pre-result check while the launch is not finalized; a launch never
//        finalized (abandoned) is INVALID; an output not known complete is INVALID; a quiet run passes.
//   N2:  a case's contradiction ends the WHOLE test before the next case launches, its logs kept; a case whose
//        child's end was not confirmed is said so.
//   F-a: no port secured -> INVALID before any child.
//   F-c: an end is confirmed only when a wait saw it.

#include <winsock2.h>
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "e2e_child_util.hpp"
#include "e2e_injection_guard.hpp"

namespace e = remote60::native_poc::e2e;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
std::wstring self_path() {
  wchar_t b[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, b, MAX_PATH);
  return b;
}
std::string offLines() {
  return "[native-video-host] " + e::e2e_power_off_line() + "\n[native-video-host] " + e::e2e_injection_off_line() + "\n";
}
const char* kPowerOn =
    "[native-video-host] host power management=on (local startup policy, default): execution state kept, display wake at a stream start\n";

// ---- the child modes
int sleeper() {
  Sleep(60000);
  return 0;
}

int monitor_case(const std::string& kind) {
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
  lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim));
  std::wstring cmd = L"\"" + self_path() + L"\" --sleeper";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    std::printf("NO_SLEEPER\n");
    return 9;
  }
  e::e2e_adopt_or_invalid(job, pi, "runtime-case");
  std::printf("SLEEPER_PID=%lu\n", pi.dwProcessId);
  std::fflush(stdout);
  std::mutex mu;
  std::string out = offLines();
  auto get = [&] {
    std::lock_guard<std::mutex> l(mu);
    return out;
  };
  auto append = [&](const std::string& line) {
    std::lock_guard<std::mutex> l(mu);
    out += line;
  };
  std::string why;
  if (!e::e2e_wait_injection_off(get, 1000, &why)) {
    std::printf("GATE_REFUSED %s\n", why.c_str());
    return 8;
  }
  std::printf("GATE_PASSED\n");  // the initial gate, reported apart from the run-time judgement
  std::fflush(stdout);

  if (kind == "final_only") {
    // a monitor that never polls: only the pre-result check can see the contradiction
    e::E2eInjectionMonitor mon(get, job, "runtime-final_only", /*poll=*/false);
    append(kPowerOn);
    Sleep(300);
    std::printf("PHASE_BEFORE_FINAL\n");  // nothing has judged it yet
    std::fflush(stdout);
    e::e2e_injection_final_check_all();
    std::printf("MISSED\n");
    return 0;
  }
  if (kind == "tail_finalize" || kind == "tail_live" || kind == "abandoned" || kind == "incomplete" || kind == "quiet") {
    auto mon = std::make_unique<e::E2eInjectionMonitor>(get, job, "runtime-" + kind);
    Sleep(300);
    mon->StopPolling();  // polling ended; the host's last output has not come yet
    std::printf("PHASE_POLLING_STOPPED\n");
    std::fflush(stdout);
    if (kind == "tail_finalize") {
      append(kPowerOn);           // the tail, after polling
      mon->Finalize(true);        // the complete output judged
    } else if (kind == "tail_live") {
      append(kPowerOn);           // the tail; never finalized
      e::e2e_injection_final_check_all();
    } else if (kind == "abandoned") {
      mon.reset();                // destroyed without its final judgement
      e::e2e_injection_final_check_all();
    } else if (kind == "incomplete") {
      mon->Finalize(false);       // the end of its output not confirmed
    } else {  // quiet
      append("[native-video-host][input-timing] seq=1 eventKind=6 injectionEnabled=0 ackSent=1\n");  // receipt only
      mon->Finalize(true);
      e::e2e_injection_final_check_all();
      std::printf("NO_CONTRADICTION\n");
      TerminateJobObject(job, 0);
      return 0;
    }
    std::printf("MISSED\n");
    return 0;
  }
  // polling catches it (G1)
  e::E2eInjectionMonitor mon(get, job, "runtime-" + kind);
  Sleep(400);
  if (kind == "power_on") append(kPowerOn);
  if (kind == "input_other") append("[native-video-host] input injection disabled (unsupported mode) mode=bogus\n");
  if (kind == "took_input") append("[native-video-host][input-timing] seq=1 eventKind=6 injectionEnabled=1 ackSent=1\n");
  Sleep(2000);
  std::printf("MISSED\n");
  return 0;
}

int case_invalid(const std::string& kind) {
  // N2: two cases in order; the first one's contradiction must end the test before the second launches
  std::set<std::wstring> keep;
  if (kind == "unconfirmed") keep.insert(L"case1");
  std::printf("CASE1_RAN\n");
  std::fflush(stdout);
  const std::string out = offLines() + kPowerOn;
  std::string why;
  if (e::e2e_injection_contradicted(out, &why)) e::e2e_case_invalid(L"D:\\run-folder\\", keep, L"case1", why);
  std::printf("CASE2_LAUNCHED\n");
  return 0;
}

int port_case() {
  (void)e::e2e_port_or_invalid("runtime-port", [] { return static_cast<uint16_t>(0); });
  std::printf("MISSED\n");
  return 0;
}

int unconfirmed_case() {
  HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // its "end" can never be seen
  const bool ok = e::e2e_end_child_confirmed(ev, 1, 200);
  std::printf(ok ? "WRONGLY_CONFIRMED\n" : "NOT_CONFIRMED_SAID\n");
  CloseHandle(ev);
  return ok ? 1 : 0;
}

// ---- the parent
struct Run {
  DWORD code = 0xFFFFFFFFu;
  std::string out;
};
Run run_self(const std::wstring& args) {
  Run r;
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  CreatePipe(&rd, &wr, &sa, 1 << 16);
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr;
  si.hStdError = wr;
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + self_path() + L"\" " + args;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(wr);
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) r.out.append(buf, got);
    WaitForSingleObject(pi.hProcess, 30000);
    GetExitCodeProcess(pi.hProcess, &r.code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  } else {
    CloseHandle(wr);
  }
  CloseHandle(rd);
  return r;
}
bool has(const Run& r, const char* s) { return r.out.find(s) != std::string::npos; }
bool sleeper_gone(const std::string& out) {
  const size_t at = out.find("SLEEPER_PID=");
  if (at == std::string::npos) return false;
  const DWORD pid = static_cast<DWORD>(std::strtoul(out.c_str() + at + 12, nullptr, 10));
  HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (!h) return true;
  const bool gone = WaitForSingleObject(h, 5000) == WAIT_OBJECT_0;
  CloseHandle(h);
  return gone;
}
bool invalid_run(const Run& r) {
  return r.code == 2 && has(r, "FAIL  RUN runtime-") && has(r, "RESULT: INVALID") && !has(r, "MISSED") &&
         has(r, "ended (confirmed)") && sleeper_gone(r.out);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "--sleeper") return sleeper();
  if (argc >= 3 && std::string(argv[1]) == "--monitor-case") return monitor_case(argv[2]);
  if (argc >= 3 && std::string(argv[1]) == "--case-invalid") return case_invalid(argv[2]);
  if (argc >= 2 && std::string(argv[1]) == "--port-case") return port_case();
  if (argc >= 2 && std::string(argv[1]) == "--unconfirmed-case") return unconfirmed_case();

  std::printf("--- G1: after the gate, polling catches a later contradiction ---\n");
  for (const char* kind : {"power_on", "input_other", "took_input"}) {
    const Run r = run_self(L"--monitor-case " + std::wstring(kind, kind + strlen(kind)));
    char what[160];
    std::snprintf(what, sizeof(what), "%s: gate passed, then INVALID by polling, its child ended (confirmed)", kind);
    check(what, has(r, "GATE_PASSED") && invalid_run(r), "exit " + std::to_string(r.code));
  }

  std::printf("\n--- N1: the final judgement, apart from polling ---\n");
  {
    const Run r = run_self(L"--monitor-case final_only");
    check("final only (a monitor that never polled): the contradiction is caught by the pre-result check alone",
          has(r, "PHASE_BEFORE_FINAL") && invalid_run(r), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case tail_finalize");
    check("output arriving AFTER polling stopped (the host's tail): caught by Finalize on the complete output",
          has(r, "PHASE_POLLING_STOPPED") && invalid_run(r), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case tail_live");
    check("the same tail, never finalized: the pre-result check judges that launch anyway (no \"0 live -> PASS\")",
          has(r, "PHASE_POLLING_STOPPED") && invalid_run(r), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case abandoned");
    check("a launch destroyed without its final judgement: INVALID at the pre-result check",
          invalid_run(r) && has(r, "never judged on its complete output"), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case incomplete");
    check("an output not known complete (end / EOF not confirmed): INVALID",
          invalid_run(r) && has(r, "not known complete"), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case quiet");
    check("a run that never contradicts (receipt only, injectionEnabled=0), finalized: passes, not stopped",
          r.code == 0 && has(r, "NO_CONTRADICTION") && !has(r, "RESULT: INVALID") &&
              has(r, "1 judged on their complete output"),
          "exit " + std::to_string(r.code));
  }

  std::printf("\n--- N2: a case's contradiction ends the whole test ---\n");
  {
    const Run r = run_self(L"--case-invalid confirmed");
    check("case 1 contradicts: INVALID exit 2 before case 2 launches; its logs kept, its child's end confirmed",
          r.code == 2 && has(r, "CASE1_RAN") && !has(r, "CASE2_LAUNCHED") && has(r, "no further launch") &&
              has(r, "logs are kept") && has(r, "its child's end confirmed"),
          "exit " + std::to_string(r.code));
    const Run u = run_self(L"--case-invalid unconfirmed");
    check("...a case whose child's end was not confirmed is said so",
          u.code == 2 && has(u, "its child's end NOT confirmed") && !has(u, "CASE2_LAUNCHED"), "exit " + std::to_string(u.code));
  }

  std::printf("\n--- F-a: no port, no launch ---\n");
  {
    const Run r = run_self(L"--port-case");
    check("a port that cannot be secured (0) ends the run INVALID before any child", r.code == 2 &&
          has(r, "no loopback port could be secured") && !has(r, "MISSED"), "exit " + std::to_string(r.code));
    check("socket() failing (no WSAStartup yet): the helper returns 0 (the caller then ends INVALID)",
          e::e2e_free_loopback_udp_port() == 0);
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    check("a real loopback port is secured (non-zero)", e::e2e_free_loopback_udp_port() != 0);
    WSACleanup();
  }

  std::printf("\n--- F-c: an end is confirmed only when it is seen ---\n");
  {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + self_path() + L"\" --sleeper";
    const bool made = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
    check("a sleeping child of this test: its end confirmed (terminated, the wait saw it)",
          made && e::e2e_end_child_confirmed(pi.hProcess, 1));
    if (made) {
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
    const Run r = run_self(L"--unconfirmed-case");
    check("a handle whose end cannot be seen: NOT confirmed, said so -- never \"ended\"",
          r.code == 0 && has(r, "NOT_CONFIRMED_SAID") && has(r, "NOT confirmed"), "exit " + std::to_string(r.code));
  }

  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT PASS" : "RESULT FAIL");
  return gFailures == 0 ? 0 : 1;
}
