// 2026-10-28 (verifier, after worker2's finding in the ka-pc e2e; Codex codex_test_injection_finding.md rules 1-6 and
// "OFF 표현 상충 판단 -- mode=none 채택"): GNLinkStream injects input by default (host_args.hpp
// enableInputInjection = true). A test host must never drive THIS PC's input:
//   1. it is started with injection explicitly OFF: the exact argument `--input-injection-mode none` (the parser's
//      supported value for Disabled, host_input_inject.cpp parse_input_injection_mode -- not an unknown value's
//      fallback). Not a --config: a profile clears every runtime override it can carry, not only the injection;
//   2. nothing connects to it before THIS launch's own output said that state, whole:
//      "input injection disabled (unsupported mode) mode=none" ending its line (the startup wording is the
//      host's; the mode value must be exactly none) -- not a prefix match, not an earlier run's line;
//   3. anything else (enabled, another mode / another off reason, a config error, silence) is a setup failure: the
//      test stops at once -- RESULT: INVALID, exit 2 -- before any client exists (its host dies with its job object);
//   4. a contradiction later (injectionEnabled=1 in its input log) makes the run INVALID as well.
// This turns off INPUT injection only. Other OS effects (SAS delegation, credential typing, clipboard, restart, the
// display wake at a stream start) are not covered by it -- see os_input_effects.hpp for the record-only test build.
// The host still receives, logs and acknowledges input; that is receipt, not an OS injection.
#ifndef REMOTE60_NATIVE_POC_E2E_INJECTION_GUARD_HPP
#define REMOTE60_NATIVE_POC_E2E_INJECTION_GUARD_HPP

#include <windows.h>

#include <chrono>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <functional>
#include <string>
#include <thread>

#include "host_power_policy.hpp"

namespace remote60::native_poc::e2e {

// The explicit OFF state for `--input-injection-mode none`, as host_startup_config.cpp prints it (a whole line).
inline const char* e2e_injection_off_line() { return "input injection disabled (unsupported mode) mode=none"; }

// The explicit power OFF state (Codex Finding-3, host_power_policy.hpp), the host's own whole line.
inline std::string e2e_power_off_line() {
  HostPowerPolicy off;
  off.enabled = false;
  off.fromCommandLine = true;
  return host_power_policy_line(off);
}

// The arguments for the host's command line: input injection OFF and host power management OFF (both local,
// startup-only; nothing is written). A host that does not know --host-power-management (an older build) never says
// the power line -- the gate refuses it.
inline std::wstring e2e_injection_off_arg(const std::wstring& /*stagingDir*/) {
  return L" --input-injection-mode none --host-power-management off";
}

// What a host wrote to a log file so far (shared read: the host keeps writing it).
inline std::string e2e_read_text_file(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  std::string out;
  char buf[8192];
  DWORD got = 0;
  while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) out.append(buf, got);
  CloseHandle(h);
  return out;
}

// r8 N1: how a read of a host's output ended. Eof only for a CONFIRMED end of output -- ReadFile TRUE with 0 bytes,
// or FALSE with ERROR_BROKEN_PIPE (an anonymous pipe whose last writer is gone; a joined reader is not that proof).
// Any other failure is Error (its code kept), after however many bytes came; a read stopped by its owner is Cut.
enum class E2eReadEnd : int { Running = 0, Eof = 1, Error = 2, Cut = 3 };

inline E2eReadEnd e2e_read_to_end(HANDLE h, const std::function<bool()>& keepGoing,
                                  const std::function<void(const char*, size_t)>& append, DWORD* err) {
  if (err) *err = 0;
  if (!h || h == INVALID_HANDLE_VALUE) {
    if (err) *err = ERROR_INVALID_HANDLE;
    return E2eReadEnd::Error;
  }
  char buf[8192];
  for (;;) {
    if (keepGoing && !keepGoing()) return E2eReadEnd::Cut;
    DWORD got = 0;
    if (!ReadFile(h, buf, sizeof(buf), &got, nullptr)) {
      const DWORD e = GetLastError();
      if (got > 0) append(buf, got);  // ERROR_MORE_DATA and the like hand over part of a message: kept, still an error
      if (e == ERROR_BROKEN_PIPE) return E2eReadEnd::Eof;
      if (err) *err = e;
      return E2eReadEnd::Error;
    }
    if (got == 0) return E2eReadEnd::Eof;
    append(buf, got);
  }
}

inline const char* e2e_read_end_name(E2eReadEnd e) {
  switch (e) {
    case E2eReadEnd::Running: return "still reading";
    case E2eReadEnd::Eof: return "end of output";
    case E2eReadEnd::Error: return "read error";
    case E2eReadEnd::Cut: return "read cut by its owner";
  }
  return "?";
}

// r8 N1: a launch's FINAL output: complete only when its whole text was read to a confirmed end of output.
struct E2eFinalOutput {
  bool complete = false;
  std::string text;
  std::string why;  // when not complete: what was not confirmed
};

// The final snapshot of a log file (the producer already ended): open, then read to EOF -- an open failure (a missing
// file) or a read error part-way is NOT complete (never an empty or partial text taken as the whole output).
inline E2eFinalOutput e2e_final_file(const std::wstring& path) {
  E2eFinalOutput f;
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    f.why = "its log could not be opened (error " + std::to_string(GetLastError()) + ")";
    return f;
  }
  DWORD err = 0;
  const E2eReadEnd end = e2e_read_to_end(h, nullptr, [&](const char* p, size_t n) { f.text.append(p, n); }, &err);
  CloseHandle(h);
  f.complete = end == E2eReadEnd::Eof;
  if (!f.complete) {
    f.why = std::string("its log: ") + e2e_read_end_name(end) + " (error " + std::to_string(err) + ") after " +
            std::to_string(f.text.size()) + " bytes";
  }
  return f;
}

// The final snapshot of a pipe a reader thread drained: complete only when that reader reached Eof.
inline E2eFinalOutput e2e_final_pipe(E2eReadEnd end, DWORD err, std::string text) {
  E2eFinalOutput f;
  f.text = std::move(text);
  f.complete = end == E2eReadEnd::Eof;
  if (!f.complete) {
    f.why = std::string("its pipe: ") + e2e_read_end_name(end) + " (error " + std::to_string(err) + ") after " +
            std::to_string(f.text.size()) + " bytes";
  }
  return f;
}

// One whole line, said: 1 = exactly `want` up to its line end, 0 = not yet (absent or still being written),
// -1 = a line with that prefix that is something else.
// r5 P2-2: EVERY line with that prefix, not only the first: one that differs anywhere -- before or after a matching
// one -- refuses the launch (a later "power ON" after "power OFF" is a contradiction, not noise).
inline int e2e_whole_line(const std::string& out, const std::string& prefix, const std::string& want) {
  int seen = 0;
  for (size_t at = out.find(prefix); at != std::string::npos; at = out.find(prefix, at + prefix.size())) {
    const size_t end = at + want.size();
    if (out.compare(at, want.size(), want) == 0) {
      if (end >= out.size()) return seen > 0 ? seen : 0;  // the last line still being written
      if (out[end] != '\r' && out[end] != '\n') return -1;
      seen = 1;
      continue;
    }
    // a shorter tail still being written (the last occurrence) is "not yet"; anything that differs is refused
    if (out.size() - at < want.size() && want.compare(0, out.size() - at, out, at, out.size() - at) == 0) return seen > 0 ? seen : 0;
    return -1;
  }
  return seen;
}

// The power half (Finding-3): 1 = this launch said power management OFF, whole; -1 = on / anything else.
inline int e2e_power_state(const std::string& out, std::string* why) {
  if (out.find("invalid --host-power-management value") != std::string::npos) {
    if (why) *why = "the host refused its power argument";
    return -1;
  }
  const int st = e2e_whole_line(out, "host power management=", e2e_power_off_line());
  if (st < 0 && why) *why = "the host's power management is not OFF: " + out.substr(out.find("host power management="), 90);
  return st;
}

// The judgement on what a host said so far: 1 = input injection OFF and power management OFF, both whole lines;
// 0 = not yet; -1 = never acceptable (enabled, another off reason, power on, a refused argument / config).
inline int e2e_injection_state(const std::string& out, std::string* why) {
  const int power = e2e_power_state(out, why);
  if (power < 0) return -1;
  if (out.find("input injection enabled") != std::string::npos) {
    if (why) *why = "the host said input injection ENABLED";
    return -1;
  }
  if (out.find("failed to load --config") != std::string::npos) {
    if (why) *why = "the host could not load its injection-off config";
    return -1;
  }
  // r5 P2-2: every injection-state line of this launch must be the explicit OFF line (a contradiction anywhere refuses)
  {
    const int every = e2e_whole_line(out, "input injection ", e2e_injection_off_line());
    if (every < 0) {
      if (why) *why = "the host's input injection states contradict the explicit OFF";
      return -1;
    }
  }
  const size_t at = out.find("input injection disabled");
  if (at != std::string::npos) {
    const char* want = e2e_injection_off_line();
    const size_t end = at + std::strlen(want);
    // the whole line: the mode value is exactly "none" (not "none2", not a prefix of something else)
    if (out.compare(at, std::strlen(want), want) == 0 && end < out.size() && (out[end] == '\r' || out[end] == '\n')) {
      return power == 1 ? 1 : 0;  // input OFF said; the power line too, or not yet
    }
    if (out.compare(at, std::strlen(want), want) == 0 && end >= out.size()) return 0;  // the line is still being written
    if (why) *why = "the host is off for another reason, not the explicit setting: " + out.substr(at, 80);
    return -1;
  }
  return 0;
}

// Waits for this launch's own words. true only for the explicit OFF state.
inline bool e2e_wait_injection_off(const std::function<std::string()>& hostOutput, uint32_t budgetMs, std::string* why) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  for (;;) {
    const int st = e2e_injection_state(hostOutput(), why);
    if (st != 0) return st > 0;
    if (std::chrono::steady_clock::now() >= deadline) {
      if (why) *why = "the host never said its input injection is disabled";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

// A record-only test host (GNLinkStreamInputRecordTest, os_input_effects.hpp): its own state line, said by THIS
// launch before anything connects -- its dispatch runs (injection "enabled" is its logical state), its OS effects are
// recorded and never performed. Not the same thing as injection off, and not to be confused with it.
inline const char* e2e_os_record_only_line() { return "os-input-record: os input effects=record-only"; }
inline bool e2e_wait_os_record_only(const std::function<std::string()>& hostOutput, uint32_t budgetMs, std::string* why) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  for (;;) {
    const std::string out = hostOutput();
    if (out.find(e2e_os_record_only_line()) != std::string::npos) return true;
    if (out.find("[native-video-host]") != std::string::npos && out.find("input injection") != std::string::npos) {
      // it said its injection state without the record-only line: a product (effect-performing) host
      if (why) *why = "the host said its injection state but not record-only -- an OS-effect host";
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      if (why) *why = "the host never said it is record-only";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

// Fail closed: the test ends here, before any client exists. Its hosts die with their job objects
// (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) at process exit; nothing of the user's is touched.
[[noreturn]] inline void e2e_injection_setup_failed(const char* where, const std::string& why) {
  std::printf("FAIL  SETUP %s: input injection is not provably off (%s) -- stopped before any client connected\n"
              "RESULT: INVALID\n", where, why.c_str());
  std::fflush(stdout);
  std::_Exit(2);
}

// r5 (Codex N2): a child started CREATE_SUSPENDED runs only once it is provably inside a kill-on-close job: the job
// exists, its limit really holds JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE (read back, not assumed from the setter), the
// child is assigned to it -- then, and only then, it is resumed. Any failure: the still-suspended child is ended
// through this test's own handle (no PID / name kill), waited for, its handles closed, and the test ends INVALID.
// So a later fail-closed exit (std::_Exit, no destructors) still takes every running child with the job.
inline void e2e_adopt_or_invalid(HANDLE job, PROCESS_INFORMATION& pi, const char* where) {
  std::string why;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
  if (!job) {
    why = "no job object";
  } else if (!QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info), nullptr) ||
             !(info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)) {
    why = "the job does not hold kill-on-close";
  } else if (!AssignProcessToJobObject(job, pi.hProcess)) {
    why = "the child could not be put in the job (" + std::to_string(GetLastError()) + ")";
  } else if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
    why = "the child could not be resumed (" + std::to_string(GetLastError()) + ")";
  }
  if (why.empty()) return;
  // r6 F-c: what the recovery confirmed, said as such -- never "ended" unless the wait saw it end
  const char* ended = "no child handle";
  if (pi.hProcess) {
    const BOOL terminated = TerminateProcess(pi.hProcess, 1);
    const DWORD waited = WaitForSingleObject(pi.hProcess, 10000);
    ended = waited == WAIT_OBJECT_0 ? "the child ended before it ran (confirmed)"
            : terminated            ? "terminate asked, its end NOT confirmed within 10 s"
                                    : "terminate failed, its end NOT confirmed";
    CloseHandle(pi.hProcess);
    pi.hProcess = nullptr;
  }
  if (pi.hThread) {
    CloseHandle(pi.hThread);
    pi.hThread = nullptr;
  }
  std::printf("FAIL  SETUP %s: %s -- %s\nRESULT: INVALID\n", where, why.c_str(), ended);
  std::fflush(stdout);
  std::_Exit(2);
}

// Rule 4 (r6 G1): a contradiction anywhere in the launch's WHOLE output so far -- the full state judged again (any
// power / input state line that is not the explicit OFF, before or after one that is: a later power ON included),
// or a host that took input with injection on (injectionEnabled=1). "Not yet said" is not a contradiction here --
// the initial gate already required both lines.
inline bool e2e_injection_contradicted(const std::string& hostOutput, std::string* why = nullptr) {
  std::string w;
  if (e2e_injection_state(hostOutput, &w) < 0) {
    if (why) *why = w;
    return true;
  }
  if (hostOutput.find("injectionEnabled=1") != std::string::npos) {
    if (why) *why = "the host took input with injection on (injectionEnabled=1)";
    return true;
  }
  return false;
}

// r8 N1: the final judgement of a launch, all of: its producer's end confirmed AND its output read to a confirmed end
// AND that final snapshot still holds the initial OFF lines (input + power, whole) AND nothing contradicts them.
// Empty = accepted; otherwise why it is not. (Nothing confirmed is never taken as "nothing said".)
inline std::string e2e_final_verdict(bool producerEnded, const E2eFinalOutput& fin) {
  if (!producerEnded) return "the producer's end is not confirmed (its output may still grow)";
  if (!fin.complete) return "the final output is not known complete: " + fin.why;
  std::string why;
  const int st = e2e_injection_state(fin.text, &why);
  if (st < 0) return why;
  if (st == 0) {
    return "the final output (" + std::to_string(fin.text.size()) +
           " bytes) does not hold the initial OFF lines (input injection + power management, whole)";
  }
  if (e2e_injection_contradicted(fin.text, &why)) return why;
  return {};
}

// r8 N2: the same for a launch that ends in a STARTUP FAULT before its input path is configured (the fatal test's
// --prelogin worker throws at startup_configure_from_env): its input-state line may legitimately be absent. Then:
// its end confirmed, its output complete, power management OFF said whole, and no contradiction anywhere (an input
// line that is present must be the explicit OFF; injectionEnabled=1 never). *inputSaid: whether the input line was there.
inline std::string e2e_startup_fault_verdict(bool producerEnded, const E2eFinalOutput& fin, bool* inputSaid) {
  if (inputSaid) *inputSaid = false;
  if (!producerEnded) return "the producer's end is not confirmed (its output may still grow)";
  if (!fin.complete) return "the final output is not known complete: " + fin.why;
  std::string why;
  if (e2e_injection_contradicted(fin.text, &why)) return why;
  const int power = e2e_power_state(fin.text, &why);
  if (power != 1) {
    return "the final output (" + std::to_string(fin.text.size()) + " bytes) does not say power management OFF, whole";
  }
  if (inputSaid) *inputSaid = e2e_whole_line(fin.text, "input injection ", e2e_injection_off_line()) == 1;
  return {};
}

// Ends a job's processes and says whether that was confirmed (its active process count reached 0 within 10 s).
inline bool e2e_end_job_confirmed(HANDLE job) {
  if (!job) return false;
  if (!TerminateJobObject(job, 2)) return false;
  for (int i = 0; i < 200; ++i) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acc{};
    if (QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &acc, sizeof(acc), nullptr) &&
        acc.ActiveProcesses == 0) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

// The run is INVALID: its own children (the job) are ended -- confirmed or said not to be -- and the test stops.
[[noreturn]] inline void e2e_injection_run_invalid(const char* where, const std::string& why, HANDLE job) {
  const bool ended = e2e_end_job_confirmed(job);
  std::printf("FAIL  RUN %s: input / power state contradicted during the run (%s) -- its children %s\n"
              "RESULT: INVALID\n", where, why.c_str(), ended ? "ended (confirmed)" : "NOT confirmed ended");
  std::fflush(stdout);
  std::_Exit(2);
}

// r6 G1 / r7 N1: after the initial gate, the same judgement keeps running, in three steps of its own:
//   1. polling -- the launch's whole output every 200 ms while the host lives (constructor; poll=false: none);
//   2. StopPolling() -- before the host is ended (nothing judged yet: its last output may still come);
//   3. Finalize(producerEnded, final) -- AFTER the producer ended and its output was read to its end: the final
//      snapshot (given, not re-read) judged by e2e_final_verdict -- the end, the EOF and the OFF lines each confirmed,
//      else INVALID (r8 N1).
// The job is BORROWED: its owner keeps it open until Finalize returned (or the monitor is gone); the monitor drops it
// at Finalize / destruction, so nothing later acts on a closed (possibly reused) handle value.
// A contradiction at any step -> its job ended (confirmed or said not), FAIL RUN ..., RESULT: INVALID, exit 2.
// Every launch stays registered for the test's life: e2e_injection_final_check_all() (before the result) judges each
// one -- a finalized launch by its final verdict, a live one by its output now, an abandoned one (destroyed without
// Finalize) is INVALID. "No live monitor" is never taken as "all judged". Reported apart from the initial gate.
class E2eInjectionMonitor {
 public:
  struct State {
    std::mutex mu;
    std::function<std::string()> output;  // released at Finalize / destruction (the producer's owner may go)
    HANDLE job = nullptr;                  // borrowed; dropped at Finalize / destruction (r8 N1)
    size_t finalBytes = 0;
    std::string where;
    bool finalized = false;
    bool abandoned = false;
  };
  struct Reg {
    std::mutex mu;
    std::vector<std::shared_ptr<State>> all;
  };
  static Reg& Registry() {
    static Reg r;
    return r;
  }

  E2eInjectionMonitor(std::function<std::string()> output, HANDLE job, std::string where, bool poll = true)
      : st_(std::make_shared<State>()) {
    st_->output = std::move(output);
    st_->job = job;
    st_->where = std::move(where);
    {
      std::lock_guard<std::mutex> lock(Registry().mu);
      Registry().all.push_back(st_);
    }
    if (poll) {
      thread_ = std::thread([this] {
        while (!stop_.load()) {
          Judge(*st_);
          for (int i = 0; i < 4 && !stop_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      });
    }
  }
  ~E2eInjectionMonitor() {
    StopPolling();
    std::lock_guard<std::mutex> lock(st_->mu);
    if (!st_->finalized) st_->abandoned = true;  // never judged on its complete output
    st_->output = nullptr;
    st_->job = nullptr;  // r8 N1: the owner may close it after this
  }
  E2eInjectionMonitor(const E2eInjectionMonitor&) = delete;
  E2eInjectionMonitor& operator=(const E2eInjectionMonitor&) = delete;

  void StopPolling() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
  }
  // r8 N1: the producer's end (confirmed or not) and its final output (complete or not) -- judged together; the job
  // must still be open here (its owner closes it after)
  void Finalize(bool producerEnded, const E2eFinalOutput& final) {
    StopPolling();
    {
      std::lock_guard<std::mutex> lock(st_->mu);
      if (st_->finalized) return;  // once
    }
    const std::string why = e2e_final_verdict(producerEnded, final);
    if (!why.empty()) e2e_injection_run_invalid(st_->where.c_str(), "final judgement: " + why, st_->job);
    std::lock_guard<std::mutex> lock(st_->mu);
    st_->finalized = true;
    st_->finalBytes = final.text.size();
    st_->output = nullptr;
    st_->job = nullptr;
  }

  // one judgement of a launch's output now (a released output: nothing to judge)
  static void Judge(State& st) {
    std::string out, where;
    HANDLE job = nullptr;
    {
      std::lock_guard<std::mutex> lock(st.mu);
      if (!st.output) return;
      out = st.output();
      where = st.where;
      job = st.job;
    }
    std::string why;
    if (e2e_injection_contradicted(out, &why)) e2e_injection_run_invalid(where.c_str(), why, job);
  }

 private:
  std::shared_ptr<State> st_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

// Before a test prints its result: every launch of the run judged (r6 G1 / r7 N1).
inline int e2e_injection_final_check_all() {
  std::vector<std::shared_ptr<E2eInjectionMonitor::State>> v;
  {
    std::lock_guard<std::mutex> lock(E2eInjectionMonitor::Registry().mu);
    v = E2eInjectionMonitor::Registry().all;
  }
  int finalized = 0, live = 0;
  size_t finalBytes = 0;
  for (auto& st : v) {
    bool fin = false, aband = false;
    std::string where;
    HANDLE job = nullptr;
    {
      std::lock_guard<std::mutex> lock(st->mu);
      fin = st->finalized;
      aband = st->abandoned;
      where = st->where;
      job = st->job;  // null once finalized / abandoned (r8 N1: never a closed handle)
      if (fin) finalBytes += st->finalBytes;
    }
    if (aband) e2e_injection_run_invalid(where.c_str(), "a launch was never judged on its complete output", job);
    if (fin) {
      ++finalized;
    } else {
      E2eInjectionMonitor::Judge(*st);  // still running: judged on its output now
      ++live;
    }
  }
  std::printf("PASS  input / power state: no contradiction during the run (%zu launch(es): %d judged on their complete "
              "output -- end, EOF and OFF lines confirmed, %zu bytes --, %d still running judged now)\n", v.size(), finalized,
              finalBytes, live);
  return static_cast<int>(v.size());
}

// The tests whose very subject is an injection the OS performs (2026-10-28): a host that injects into a window of
// the test's own -- but in window mode a KEY goes through SendInput whenever that window holds the focus
// (host_input_inject.cpp, the kind 5/6 branch). That is this PC's real input path. Such a test is REFUSED by default:
// exit 3, "RESULT: REFUSED" -- never counted as a pass -- before it opens a window, a clipboard, a hook or a host.
// REMOTE60_ALLOW_REAL_INPUT_TEST=1 is only the technical switch for an approved, isolated destination (a VM); it is
// NOT a permission and not evidence of isolation. No agent sets it on a PC someone is using (Codex: real-input 4).
// The automatic regression of these tests' product logic belongs in a record-only test build (os_input_effects.hpp),
// not here.
inline bool e2e_real_input_refused(const char* test, const char* why) {
  wchar_t v[8] = {};
  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_REAL_INPUT_TEST", v, 8) > 0 && wcscmp(v, L"1") == 0) return false;
  std::printf("REFUSED  %s: %s -- this PC's real input path. Not run without REMOTE60_ALLOW_REAL_INPUT_TEST=1 "
              "(e2e_injection_guard.hpp)\nRESULT: REFUSED\n", test, why);
  return true;
}

}  // namespace remote60::native_poc::e2e

#endif
