// keepalive-r2 e (Codex codex_avd_guard_src_r3.md J1): e2e_job_run's start, its failure branches driven by a stand-in
// for the Win32 calls (JobStartApi) -- no process, no job is created; the handles are never used by the stand-in.
//   assign / membership fails: the suspended command is terminated; its end is "confirmed" ONLY when the wait saw it
//   (terminate false + wait timeout: ended_confirmed=0, exit 98 -- the case 8a84198 / de2d60a called "ended")
//   resume fails: the job is terminated; confirmed only when the wait saw the command end AND the job is empty
//   all succeed: running

#include <windows.h>

#include <cstdio>
#include <string>

#include "e2e_job_run_start.hpp"

namespace e = remote60::native_poc::e2e;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
struct Fake {
  BOOL assign = TRUE, askOk = TRUE, inJob = TRUE, terminate = TRUE, terminateJob = TRUE;
  DWORD wait = WAIT_OBJECT_0, resume = 1, active = 0;
  int terminates = 0, terminateJobs = 0, waits = 0;
} g;
e::JobStartApi fake_api() {
  e::JobStartApi a;
  a.assign = [](HANDLE, HANDLE) { return g.assign; };
  a.inJob = [](HANDLE, HANDLE, PBOOL r) { *r = g.inJob; return g.askOk; };
  a.terminate = [](HANDLE, UINT) { ++g.terminates; return g.terminate; };
  a.wait = [](HANDLE, DWORD) { ++g.waits; return g.wait; };
  a.resume = [](HANDLE) { return g.resume; };
  a.terminateJob = [](HANDLE, UINT) { ++g.terminateJobs; return g.terminateJob; };
  a.active = [](HANDLE) { return g.active; };
  return a;
}
e::JobStartResult run(const Fake& f) {
  g = f;
  PROCESS_INFORMATION pi{};
  return e::e2e_job_start(fake_api(), nullptr, pi);
}
}  // namespace

int main() {
  std::printf("e2e_job_run_start_test (J1)\n\n");
  {
    Fake f;
    f.assign = FALSE;
    f.terminate = FALSE;
    f.wait = WAIT_TIMEOUT;
    const auto r = run(f);
    check("assign fails, terminate fails, wait times out: NOT called ended -- ended_confirmed=0, exit 98 (Codex's case)",
          !r.running && r.exitCode == 98 && r.line.find("terminate=0") != std::string::npos &&
              r.line.find("ended_confirmed=0") != std::string::npos && g.terminates == 1 && g.waits == 1,
          r.line);
  }
  {
    Fake f;
    f.assign = FALSE;
    const auto r = run(f);
    check("assign fails, terminate and wait succeed: ended before it ran, confirmed (exit 2)",
          !r.running && r.exitCode == 2 && r.line.find("ended_confirmed=1") != std::string::npos, r.line);
  }
  {
    Fake f;
    f.inJob = FALSE;
    f.wait = WAIT_TIMEOUT;
    const auto r = run(f);
    check("assigned but not a member: terminated, end not seen -- not confirmed (exit 98)",
          !r.running && r.exitCode == 98 && r.line.find("in_job=0") != std::string::npos, r.line);
  }
  {
    Fake f;
    f.askOk = FALSE;
    const auto r = run(f);
    check("the membership cannot be asked: treated as not in the job (terminated, confirmed: exit 2)",
          !r.running && r.exitCode == 2 && r.line.find("in_job=0") != std::string::npos, r.line);
  }
  {
    Fake f;
    f.resume = static_cast<DWORD>(-1);
    const auto r = run(f);
    check("resume fails: the job is ended; confirmed (command ended, job empty): exit 2, never waited on as running",
          !r.running && r.exitCode == 2 && g.terminateJobs == 1 && r.line.find("could not be resumed") != std::string::npos &&
              r.line.find("ended_confirmed=1") != std::string::npos,
          r.line);
  }
  {
    Fake f;
    f.resume = static_cast<DWORD>(-1);
    f.wait = WAIT_TIMEOUT;
    const auto r = run(f);
    check("resume fails and the command's end is not seen: not confirmed (exit 98)",
          !r.running && r.exitCode == 98 && r.line.find("ended_confirmed=0") != std::string::npos, r.line);
  }
  {
    Fake f;
    f.resume = static_cast<DWORD>(-1);
    f.active = 1;
    const auto r = run(f);
    check("resume fails and the job does not empty: not confirmed (exit 98)",
          !r.running && r.exitCode == 98 && r.line.find("job_empty=0") != std::string::npos, r.line);
  }
  {
    Fake f;
    const auto r = run(f);
    check("everything succeeds: running (the caller waits for it), nothing terminated", r.running && g.terminates == 0 &&
          g.terminateJobs == 0);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
