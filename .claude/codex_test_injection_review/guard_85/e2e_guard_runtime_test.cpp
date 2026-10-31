// The run-time half of the injection guard and the child helpers, through their REAL call paths (2026-10-28, Codex
// ba2eaf2 SOURCE G1 / F, r6). No host, no network peer, no input or power API: the "host" is a fixed string that the
// test changes, and the "child" is this executable sleeping (--sleeper) inside a kill-on-close job.
//   G1: an output that passed the initial gate (both OFF lines) and LATER says power ON / another input mode / took
//       input with injection on -> the monitor stops the run: exit 2, "RESULT: INVALID", its job's child ended
//       (confirmed) -- and a run that never contradicts is not stopped. The same through the final check alone.
//   F-a: no port secured -> INVALID before any child exists.
//   F-c: a child's end confirmed only when a wait saw it; a handle whose end cannot be seen -> "NOT confirmed".
// Each case runs in its own child process (the guard ends a run with std::_Exit); the parent reads its exit code and
// output. This is separate from the initial gate's pure tests (e2e_injection_guard_test).

#include <winsock2.h>
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <mutex>
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
  e::e2e_adopt_or_invalid(job, pi, "runtime-case");  // the shared adoption: resumed only inside the verified job
  std::printf("SLEEPER_PID=%lu\n", pi.dwProcessId);
  std::fflush(stdout);
  std::mutex mu;
  std::string out = offLines();
  auto get = [&] {
    std::lock_guard<std::mutex> l(mu);
    return out;
  };
  std::string why;
  if (!e::e2e_wait_injection_off(get, 1000, &why)) {
    std::printf("GATE_REFUSED %s\n", why.c_str());
    return 8;
  }
  std::printf("GATE_PASSED\n");  // the initial gate, reported apart from the run-time judgement
  std::fflush(stdout);
  if (kind == "final") {
    // the final check alone (no polling yet): the contradiction appears, then the pre-result check runs
    {
      std::lock_guard<std::mutex> l(mu);
      out += "[native-video-host] host power management=on (local startup policy, default): execution state kept, display wake at a stream start\n";
    }
    std::vector<std::unique_ptr<e::E2eInjectionMonitor>> mons;
    mons.push_back(std::make_unique<e::E2eInjectionMonitor>(get, job, "runtime-final"));
    e::e2e_injection_final_check_all();
    std::printf("MISSED\n");
    return 0;
  }
  e::E2eInjectionMonitor mon(get, job, "runtime-" + kind);
  Sleep(400);
  {
    std::lock_guard<std::mutex> l(mu);
    if (kind == "power_on") {
      out += "[native-video-host] host power management=on (local startup policy, default): execution state kept, display wake at a stream start\n";
    } else if (kind == "input_other") {
      out += "[native-video-host] input injection disabled (unsupported mode) mode=bogus\n";
    } else if (kind == "took_input") {
      out += "[native-video-host][input-timing] seq=1 eventKind=6 injectionEnabled=1 ackSent=1\n";
    } else if (kind == "quiet") {
      out += "[native-video-host][input-timing] seq=1 eventKind=6 injectionEnabled=0 ackSent=1\n";  // receipt only
    }
  }
  Sleep(2000);
  mon.Stop();
  e::e2e_injection_final_check_all();
  std::printf(kind == "quiet" ? "NO_CONTRADICTION\n" : "MISSED\n");
  TerminateJobObject(job, 0);
  return 0;
}

int port_case() {
  (void)e::e2e_port_or_invalid("runtime-port", [] { return static_cast<uint16_t>(0); });
  std::printf("MISSED\n");
  return 0;
}

int unconfirmed_case() {
  // a handle whose "end" can never be seen (an event, not a process): terminate fails, the wait times out
  HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
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
bool sleeper_gone(const std::string& out) {
  const size_t at = out.find("SLEEPER_PID=");
  if (at == std::string::npos) return false;
  const DWORD pid = static_cast<DWORD>(std::strtoul(out.c_str() + at + 12, nullptr, 10));
  HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (!h) return true;  // no such process any more
  const bool gone = WaitForSingleObject(h, 5000) == WAIT_OBJECT_0;
  CloseHandle(h);
  return gone;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "--sleeper") return sleeper();
  if (argc >= 3 && std::string(argv[1]) == "--monitor-case") return monitor_case(argv[2]);
  if (argc >= 2 && std::string(argv[1]) == "--port-case") return port_case();
  if (argc >= 2 && std::string(argv[1]) == "--unconfirmed-case") return unconfirmed_case();

  std::printf("--- G1: after the initial gate, a later contradiction stops the run ---\n");
  for (const char* kind : {"power_on", "input_other", "took_input"}) {
    const Run r = run_self(L"--monitor-case " + std::wstring(kind, kind + strlen(kind)));
    const bool gate = r.out.find("GATE_PASSED") != std::string::npos;
    const bool invalid = r.code == 2 && r.out.find("FAIL  RUN runtime-") != std::string::npos &&
                         r.out.find("RESULT: INVALID") != std::string::npos && r.out.find("MISSED") == std::string::npos;
    const bool ended = r.out.find("ended (confirmed)") != std::string::npos && sleeper_gone(r.out);
    char what[160];
    std::snprintf(what, sizeof(what), "%s: the gate passed first, then the run was stopped INVALID and its child ended (confirmed)", kind);
    check(what, gate && invalid && ended, "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case quiet");
    check("a run that never contradicts (receipt only, injectionEnabled=0) is not stopped",
          r.code == 0 && r.out.find("NO_CONTRADICTION") != std::string::npos && r.out.find("RESULT: INVALID") == std::string::npos,
          "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case final");
    check("the contradiction caught by the pre-result check (e2e_injection_final_check_all) too: INVALID, child ended",
          r.code == 2 && r.out.find("RESULT: INVALID") != std::string::npos && r.out.find("MISSED") == std::string::npos &&
              sleeper_gone(r.out),
          "exit " + std::to_string(r.code));
  }

  std::printf("\n--- F-a: no port, no launch ---\n");
  {
    const Run r = run_self(L"--port-case");
    check("a port that cannot be secured (0) ends the run INVALID before any child", r.code == 2 &&
          r.out.find("no loopback port could be secured") != std::string::npos && r.out.find("MISSED") == std::string::npos,
          "exit " + std::to_string(r.code));
    // a real failure first: before WSAStartup, socket() fails -- the helper says 0, never a made-up port
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
          r.code == 0 && r.out.find("NOT_CONFIRMED_SAID") != std::string::npos && r.out.find("NOT confirmed") != std::string::npos,
          "exit " + std::to_string(r.code));
  }

  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT PASS" : "RESULT FAIL");
  return gFailures == 0 ? 0 : 1;
}
