// keepalive-r2 e (Codex codex_avd_guard_src_r3.md J1): the start of e2e_job_run's command -- put in its job before it
// runs, or ended and that end CONFIRMED -- with every failure branch checked. The Win32 calls come through JobStartApi
// so the failure branches can be driven by a stand-in (e2e_job_run_start_test); e2e_job_run uses the real ones.
#ifndef REMOTE60_NATIVE_POC_E2E_JOB_RUN_START_HPP
#define REMOTE60_NATIVE_POC_E2E_JOB_RUN_START_HPP

#include <windows.h>

#include <cstdio>
#include <string>

namespace remote60::native_poc::e2e {

struct JobStartApi {
  BOOL (*assign)(HANDLE job, HANDLE process) = [](HANDLE j, HANDLE p) { return AssignProcessToJobObject(j, p); };
  BOOL (*inJob)(HANDLE process, HANDLE job, PBOOL result) = [](HANDLE p, HANDLE j, PBOOL r) { return IsProcessInJob(p, j, r); };
  BOOL (*terminate)(HANDLE process, UINT code) = [](HANDLE p, UINT c) { return TerminateProcess(p, c); };
  DWORD (*wait)(HANDLE h, DWORD ms) = [](HANDLE h, DWORD ms) { return WaitForSingleObject(h, ms); };
  DWORD (*resume)(HANDLE thread) = [](HANDLE t) { return ResumeThread(t); };
  BOOL (*terminateJob)(HANDLE job, UINT code) = [](HANDLE j, UINT c) { return TerminateJobObject(j, c); };
  DWORD (*active)(HANDLE job) = [](HANDLE j) -> DWORD {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acc{};
    if (!QueryInformationJobObject(j, JobObjectBasicAccountingInformation, &acc, sizeof(acc), nullptr)) return 0xFFFFFFFFu;
    return acc.ActiveProcesses;
  };
};

struct JobStartResult {
  bool running = false;    // in the job, resumed: the caller waits for it
  int exitCode = 2;        // when not running: 2 = ended before it ran (confirmed), 98 = its end NOT confirmed
  std::string line;        // the diagnosis, said as it is
};

// `pi`: the command created SUSPENDED (it has not run). Assigned to `job`, its membership checked, then resumed. Any
// failure: the command (a suspended process, or -- after a failed resume -- the job) is ended through this owner's
// handles, and the end is said confirmed only when a wait SAW it.
inline JobStartResult e2e_job_start(const JobStartApi& api, HANDLE job, const PROCESS_INFORMATION& pi) {
  JobStartResult r;
  BOOL inJob = FALSE;
  const BOOL assigned = api.assign(job, pi.hProcess);
  const BOOL asked = assigned && api.inJob(pi.hProcess, job, &inJob);
  if (!assigned || !asked || !inJob) {
    const DWORD why = GetLastError();
    const BOOL term = api.terminate(pi.hProcess, 2);
    const DWORD w = api.wait(pi.hProcess, 5000);
    const bool ended = w == WAIT_OBJECT_0;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "job_run: the command could not be put in its job (assign=%d in_job=%d error=%lu) -- it never ran; "
                  "terminate=%d wait=%lu ended_confirmed=%d",
                  assigned ? 1 : 0, (asked && inJob) ? 1 : 0, why, term ? 1 : 0, w, ended ? 1 : 0);
    r.line = buf;
    r.exitCode = ended ? 2 : 98;
    return r;
  }
  if (api.resume(pi.hThread) == static_cast<DWORD>(-1)) {
    const DWORD why = GetLastError();
    const BOOL term = api.terminateJob(job, 2);  // in its job: the job is ended (it and anything of it)
    const DWORD w = api.wait(pi.hProcess, 5000);
    bool empty = false;
    for (int i = 0; i < 100 && !empty; ++i) {
      empty = api.active(job) == 0;
      if (!empty) Sleep(50);
    }
    const bool ended = w == WAIT_OBJECT_0 && empty;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "job_run: the command could not be resumed (error=%lu) -- its job ended: terminate=%d wait=%lu "
                  "job_empty=%d ended_confirmed=%d",
                  why, term ? 1 : 0, w, empty ? 1 : 0, ended ? 1 : 0);
    r.line = buf;
    r.exitCode = ended ? 2 : 98;
    return r;
  }
  r.running = true;
  return r;
}

}  // namespace remote60::native_poc::e2e

#endif
