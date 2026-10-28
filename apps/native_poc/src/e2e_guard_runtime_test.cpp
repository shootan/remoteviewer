// The run-time half of the injection guard and the child helpers, through their REAL call paths (2026-10-28, Codex
// ba2eaf2 SOURCE G1 / F, r6; 85f9383 N1 / N2, r7). No host, no network peer, no input or power API: the "host" is a
// fixed string that the test changes, and the "child" is this executable sleeping (--sleeper) inside a kill-on-close
// job. Each case runs in its own child process (the guard ends a run with std::_Exit); the parent reads its exit code
// and output. Separate from the initial gate's pure tests (e2e_injection_guard_test).
//   G1:  after the gate, power ON / another input mode / input taken with injection on -> INVALID, child ended.
//   N1:  the final judgement alone (a monitor that never polled) catches it; output that arrives AFTER polling stopped
//        is caught by Finalize -- and by the pre-result check while the launch is not finalized; a launch never
//        finalized (abandoned) is INVALID; an output not known complete is INVALID; a quiet run passes.
//   r8 N1: the final judgement needs the producer's end AND a confirmed EOF AND the OFF lines in that final text: a
//        missing log file, a read error part-way (a message pipe's ERROR_MORE_DATA), a cut reader, a complete but
//        empty output, an unconfirmed producer end -- each INVALID; a log file / pipe read to EOF with the OFF lines
//        passes (the positive controls). The job is borrowed: dropped at Finalize / destruction.
//   r8 N2: the startup-fault verdict (fatal's --prelogin worker): the input line may be absent, power OFF may not;
//        any contradiction, an incomplete read or an unconfirmed end refuses.
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

// r8: a file of this test (its own temp folder) with `text`; empty path on failure
std::wstring temp_file(const char* tag, const std::string& text) {
  wchar_t dir[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, dir);
  const std::wstring path = std::wstring(dir) + L"e2e_guard_runtime_" + std::wstring(tag, tag + strlen(tag)) + L"_" +
                            std::to_wstring(GetCurrentProcessId()) + L".log";
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  DWORD put = 0;
  const bool ok = text.empty() || (WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) && put == text.size());
  CloseHandle(h);
  return ok ? path : std::wstring();
}

// r8: a message-mode pipe holding one message larger than a read -- ReadFile hands part of it with ERROR_MORE_DATA
// (a read error after some bytes that is not an end of output)
e::E2eFinalOutput partial_pipe_read(const std::string& message, e::E2eReadEnd* endOut, DWORD* errOut) {
  const std::wstring name = L"\\\\.\\pipe\\e2e_guard_runtime_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                            std::to_wstring(GetTickCount64());
  HANDLE srv = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, 1 << 16, 1 << 16, 0, nullptr);
  HANDLE cli = CreateFileW(name.c_str(), GENERIC_READ | FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(cli, &mode, nullptr, nullptr);
  DWORD put = 0;
  WriteFile(srv, message.data(), static_cast<DWORD>(message.size()), &put, nullptr);
  std::string text;
  DWORD err = 0;
  const e::E2eReadEnd end = e::e2e_read_to_end(cli, nullptr, [&](const char* p, size_t n) { text.append(p, n); }, &err);
  CloseHandle(cli);
  CloseHandle(srv);
  if (endOut) *endOut = end;
  if (errOut) *errOut = err;
  return e::e2e_final_pipe(end, err, text);
}

// r8: an anonymous pipe written then its writer closed -- read to its real end (broken pipe = EOF)
e::E2eFinalOutput eof_pipe_read(const std::string& text, e::E2eReadEnd* endOut) {
  HANDLE rd = nullptr, wr = nullptr;
  CreatePipe(&rd, &wr, nullptr, 1 << 16);
  DWORD put = 0;
  WriteFile(wr, text.data(), static_cast<DWORD>(text.size()), &put, nullptr);
  CloseHandle(wr);
  std::string got;
  DWORD err = 0;
  const e::E2eReadEnd end = e::e2e_read_to_end(rd, nullptr, [&](const char* p, size_t n) { got.append(p, n); }, &err);
  CloseHandle(rd);
  if (endOut) *endOut = end;
  return e::e2e_final_pipe(end, err, got);
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
  // r8 N1: the final snapshot itself not confirmed / not holding the OFF lines -- each INVALID at Finalize
  if (kind == "missing_file" || kind == "empty_file" || kind == "pipe_partial" || kind == "pipe_cut" ||
      kind == "producer_unconfirmed" || kind == "quiet_file" || kind == "quiet_pipe") {
    auto mon = std::make_unique<e::E2eInjectionMonitor>(get, job, "runtime-" + kind);
    Sleep(300);
    mon->StopPolling();
    std::printf("PHASE_POLLING_STOPPED\n");  // polling saw nothing wrong: only the final judgement can refuse
    std::fflush(stdout);
    if (kind == "missing_file") {
      mon->Finalize(true, e::e2e_final_file(L"C:\\e2e-guard-runtime-no-such-dir\\host.log"));  // Codex's reproduction
    } else if (kind == "empty_file") {
      const std::wstring f = temp_file("empty", "");  // opens and reads to EOF -- 0 bytes, no OFF line
      const e::E2eFinalOutput fin = e::e2e_final_file(f);
      DeleteFileW(f.c_str());
      std::printf("EMPTY_COMPLETE=%d BYTES=%zu\n", fin.complete ? 1 : 0, fin.text.size());
      mon->Finalize(true, fin);
    } else if (kind == "pipe_partial") {
      e::E2eReadEnd end = e::E2eReadEnd::Running;
      DWORD err = 0;
      const e::E2eFinalOutput fin = partial_pipe_read(offLines() + std::string(20000, 'x'), &end, &err);
      std::printf("PARTIAL end=%d err=%lu bytes=%zu\n", static_cast<int>(end), err, fin.text.size());
      mon->Finalize(true, fin);  // the OFF lines are IN the part read -- still not complete
    } else if (kind == "pipe_cut") {
      mon->Finalize(true, e::e2e_final_pipe(e::E2eReadEnd::Cut, 0, get()));  // a reader stopped by its owner (joined)
    } else if (kind == "producer_unconfirmed") {
      mon->Finalize(false, e::e2e_final_pipe(e::E2eReadEnd::Eof, 0, get()));  // the text complete, its producer not ended
    } else {
      e::E2eFinalOutput fin;
      if (kind == "quiet_file") {
        const std::wstring f = temp_file("quiet", offLines());
        fin = e::e2e_final_file(f);
        DeleteFileW(f.c_str());
      } else {
        e::E2eReadEnd end = e::E2eReadEnd::Running;
        fin = eof_pipe_read(offLines(), &end);
        std::printf("EOF_PIPE end=%d\n", static_cast<int>(end));
      }
      mon->Finalize(true, fin);
      e::e2e_injection_final_check_all();
      std::printf("NO_CONTRADICTION\n");
      TerminateJobObject(job, 0);
      return 0;
    }
    std::printf("MISSED\n");
    return 0;
  }
  if (kind == "tail_finalize" || kind == "tail_live" || kind == "abandoned" || kind == "quiet") {
    auto mon = std::make_unique<e::E2eInjectionMonitor>(get, job, "runtime-" + kind);
    Sleep(300);
    mon->StopPolling();  // polling ended; the host's last output has not come yet
    std::printf("PHASE_POLLING_STOPPED\n");
    std::fflush(stdout);
    if (kind == "tail_finalize") {
      append(kPowerOn);           // the tail, after polling
      mon->Finalize(true, e::e2e_final_pipe(e::E2eReadEnd::Eof, 0, get()));  // the complete output judged
    } else if (kind == "tail_live") {
      append(kPowerOn);           // the tail; never finalized
      e::e2e_injection_final_check_all();
    } else if (kind == "abandoned") {
      mon.reset();                // destroyed without its final judgement
      e::e2e_injection_final_check_all();
    } else {  // quiet
      append("[native-video-host][input-timing] seq=1 eventKind=6 injectionEnabled=0 ackSent=1\n");  // receipt only
      mon->Finalize(true, e::e2e_final_pipe(e::E2eReadEnd::Eof, 0, get()));
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
// r8 N1: a launch whose monitor is gone no longer holds its job (borrowed): INVALID, the end said NOT confirmed by the
// guard -- the child still goes with the job's last handle (kill-on-close at the test's exit)
bool invalid_run_job_released(const Run& r) {
  return r.code == 2 && has(r, "FAIL  RUN runtime-") && has(r, "RESULT: INVALID") && !has(r, "MISSED") &&
         has(r, "NOT confirmed ended") && sleeper_gone(r.out);
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
    check("a launch destroyed without its final judgement: INVALID at the pre-result check (its borrowed job no longer "
          "touched: said NOT confirmed; the child goes with the job's close)",
          invalid_run_job_released(r) && has(r, "never judged on its complete output"), "exit " + std::to_string(r.code));
  }

  std::printf("\n--- r8 N1: the final snapshot -- producer end, EOF, OFF lines, all confirmed ---\n");
  {
    const Run r = run_self(L"--monitor-case missing_file");
    check("a log file that does not exist (Codex's reproduction: initial OFF 1, final 0 bytes): INVALID, not \"complete\"",
          invalid_run(r) && has(r, "could not be opened") && !has(r, "judged on their complete"), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case empty_file");
    check("a log read to EOF but empty (0 bytes, no OFF line): INVALID -- complete is not enough",
          invalid_run(r) && has(r, "EMPTY_COMPLETE=1 BYTES=0") && has(r, "does not hold the initial OFF lines"),
          "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case pipe_partial");
    check("a read error part-way (message pipe, ERROR_MORE_DATA after 8192 bytes; the OFF lines in the part read): INVALID",
          invalid_run(r) && has(r, "PARTIAL end=2 err=234 bytes=8192") && has(r, "read error (error 234) after 8192 bytes"),
          "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case pipe_cut");
    check("a reader cut by its owner (joined, no EOF): INVALID -- a join is not an end of output",
          invalid_run(r) && has(r, "read cut by its owner"), "exit " + std::to_string(r.code));
  }
  {
    const Run r = run_self(L"--monitor-case producer_unconfirmed");
    check("the text complete but its producer's end not confirmed: INVALID",
          invalid_run(r) && has(r, "producer's end is not confirmed"), "exit " + std::to_string(r.code));
  }
  {
    const Run f = run_self(L"--monitor-case quiet_file");
    check("positive control: a log file read to EOF holding the OFF lines -- passes, not stopped",
          f.code == 0 && has(f, "NO_CONTRADICTION") && !has(f, "RESULT: INVALID") && has(f, "1 judged on their complete output"),
          "exit " + std::to_string(f.code));
    const Run p = run_self(L"--monitor-case quiet_pipe");
    check("positive control: an anonymous pipe read to its broken-pipe end (EOF) holding the OFF lines -- passes",
          p.code == 0 && has(p, "EOF_PIPE end=1") && has(p, "NO_CONTRADICTION") && !has(p, "RESULT: INVALID"),
          "exit " + std::to_string(p.code));
  }
  {
    // the read helper's own ends, in this process (no guard exit involved)
    const std::wstring wf = temp_file("writeonly", "abc");
    HANDLE w = CreateFileW(wf.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD err = 0;
    std::string got;
    const e::E2eReadEnd end = e::e2e_read_to_end(w, nullptr, [&](const char* p, size_t n) { got.append(p, n); }, &err);
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    DeleteFileW(wf.c_str());
    check("a handle that cannot be read (write-only): Error with its code, never EOF",
          end == e::E2eReadEnd::Error && err != 0, "end=" + std::to_string(static_cast<int>(end)) + " err=" + std::to_string(err));
    const e::E2eFinalOutput miss = e::e2e_final_file(L"C:\\e2e-guard-runtime-no-such-dir\\host.log");
    check("e2e_final_file on a missing file: not complete, 0 bytes, why said", !miss.complete && miss.text.empty() &&
          miss.why.find("could not be opened") != std::string::npos, miss.why);
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // any open handle: the owner stops before a read
    const e::E2eReadEnd cut = e::e2e_read_to_end(ev, [] { return false; }, [](const char*, size_t) {}, &err);
    CloseHandle(ev);
    check("a read whose owner says stop: Cut", cut == e::E2eReadEnd::Cut);
  }

  std::printf("\n--- r8 N2: the startup-fault verdict (fatal's --prelogin worker) ---\n");
  {
    const std::string power = "[native-video-host] " + e::e2e_power_off_line() + "\n";
    const std::string fault = "FATAL: std::exception \"x\" at step=startup_configure_from_env -- exit 49\n";
    auto fin = [](const std::string& t) { return e::e2e_final_pipe(e::E2eReadEnd::Eof, 0, t); };
    bool inputSaid = true;
    std::string v = e::e2e_startup_fault_verdict(true, fin(power + fault), &inputSaid);
    check("power OFF said, the input line absent (a fault before the input path): accepted, input absent said",
          v.empty() && !inputSaid, v);
    v = e::e2e_startup_fault_verdict(true, fin(offLines() + fault), &inputSaid);
    check("power OFF and input OFF both said: accepted, input said", v.empty() && inputSaid, v);
    struct Neg {
      const char* what;
      bool ended;
      e::E2eFinalOutput f;
      const char* why;
    };
    const Neg negs[] = {
        {"no power line (0 bytes, a missing / empty log)", true, fin(""), "does not say power management OFF"},
        {"only the input line, no power line", true, fin("[native-video-host] " + std::string(e::e2e_injection_off_line()) + "\n" + fault),
         "does not say power management OFF"},
        {"power ON", true, fin(std::string(kPowerOn) + fault), "not OFF"},
        {"power OFF then ON", true, fin(power + kPowerOn), "not OFF"},
        {"another input mode", true, fin(power + "[native-video-host] input injection disabled (unsupported mode) mode=bogus\n"),
         "contradict"},
        {"input taken with injection on", true, fin(power + "x injectionEnabled=1 y\n"), "injectionEnabled=1"},
        {"a read error part-way", true, e::e2e_final_pipe(e::E2eReadEnd::Error, 234, power), "not known complete"},
        {"a missing file", true, e::e2e_final_file(L"C:\\e2e-guard-runtime-no-such-dir\\w.log"), "could not be opened"},
        {"the worker's end not confirmed", false, fin(power + fault), "producer's end is not confirmed"},
    };
    for (const Neg& n : negs) {
      const std::string w = e::e2e_startup_fault_verdict(n.ended, n.f, nullptr);
      check((std::string("startup fault refused: ") + n.what).c_str(), !w.empty() && w.find(n.why) != std::string::npos, w);
    }
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
