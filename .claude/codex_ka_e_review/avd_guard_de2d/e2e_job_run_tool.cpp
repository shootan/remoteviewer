// keepalive-r2 e (Codex codex_avd_guard_src_r2.md G2): runs ONE command -- the AVD bundle's fixture -- inside a
// kill-on-close job of its own, so that nothing the command starts (directly or not) outlives the bundle, whether the
// command ends normally, crashes, is forced to end, or this tool itself is killed. Test tooling; never packaged.
//
//   e2e_job_run.exe --stop-file <path> --grace-sec <n> --cmd "<command line>"
//
//   * the job is made and its JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE read back; the command is created SUSPENDED, assigned,
//     its membership checked (IsProcessInJob), and only then resumed -- it can start nothing outside the job;
//   * it waits for the command. Once <stop-file> exists, the command has <grace-sec> to end by itself; after that the
//     job (the command and everything of it) is terminated: forced;
//   * when the command has ended, whatever of the job is still alive (its descendants: e.g. a fixture that crashed
//     without its own cleanup) is terminated: residual;
//   * the end of everything is confirmed by the job's active-process count reaching 0 (else: NOT confirmed);
//   * one line on stdout: "job_run: child_exit=<c> residual=<n> forced=<0|1> ended_confirmed=<0|1>"; the exit code is
//     the command's when nothing had to be forced or terminated and the end was confirmed, else 95 (residual killed),
//     97 (forced), 98 (end not confirmed), 2 (could not start inside the job -- nothing ran).
// Nothing outside its own job is ever touched (no pid, no name).

#include <windows.h>

#include <cstdio>
#include <string>

namespace {
DWORD active_processes(HANDLE job) {
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acc{};
  if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &acc, sizeof(acc), nullptr)) return 0xFFFFFFFFu;
  return acc.ActiveProcesses;
}
bool wait_empty(HANDLE job, DWORD ms) {
  const ULONGLONG until = GetTickCount64() + ms;
  for (;;) {
    if (active_processes(job) == 0) return true;
    if (GetTickCount64() > until) return false;
    Sleep(50);
  }
}
bool exists(const std::wstring& p) { return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::wstring stopFile, cmd;
  DWORD graceSec = 600;
  for (int i = 1; i + 1 < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--stop-file") stopFile = argv[++i];
    else if (a == L"--grace-sec") graceSec = static_cast<DWORD>(_wtoi(argv[++i]));
    else if (a == L"--cmd") cmd = argv[++i];
  }
  if (cmd.empty()) {
    std::printf("job_run: no --cmd\n");
    return 2;
  }
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
  lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION back{};
  if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim)) ||
      !QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &back, sizeof(back), nullptr) ||
      (back.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) == 0) {
    std::printf("job_run: no verified kill-on-close job -- nothing started\n");
    return 2;
  }
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi{};
  std::wstring mutableCmd = cmd;
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
    std::printf("job_run: the command could not be created (%lu) -- nothing started\n", GetLastError());
    return 2;
  }
  BOOL inJob = FALSE;
  if (!AssignProcessToJobObject(job, pi.hProcess) || !IsProcessInJob(pi.hProcess, job, &inJob) || !inJob) {
    TerminateProcess(pi.hProcess, 2);  // created suspended: it never ran
    WaitForSingleObject(pi.hProcess, 5000);
    std::printf("job_run: the command could not be put in the job -- ended before it ran\n");
    return 2;
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);

  bool forced = false;
  ULONGLONG stopSeen = 0;
  for (;;) {
    if (WaitForSingleObject(pi.hProcess, 200) == WAIT_OBJECT_0) break;
    if (!stopSeen && exists(stopFile)) stopSeen = GetTickCount64();
    if (stopSeen && GetTickCount64() - stopSeen > static_cast<ULONGLONG>(graceSec) * 1000ull) {
      forced = true;
      TerminateJobObject(job, 97);  // the command and everything of it
      WaitForSingleObject(pi.hProcess, 15000);
      break;
    }
  }
  DWORD childExit = 0xFFFFFFFFu;
  GetExitCodeProcess(pi.hProcess, &childExit);
  CloseHandle(pi.hProcess);
  DWORD residual = active_processes(job);
  if (residual == 0xFFFFFFFFu) residual = 0;
  if (residual > 0) TerminateJobObject(job, 95);  // descendants the command left behind
  const bool confirmed = wait_empty(job, 15000);
  std::printf("job_run: child_exit=%lu residual=%lu forced=%d ended_confirmed=%d\n", childExit, residual, forced ? 1 : 0,
              confirmed ? 1 : 0);
  std::fflush(stdout);
  CloseHandle(job);
  if (!confirmed) return 98;
  if (forced) return 97;
  if (residual > 0) return 95;
  return static_cast<int>(childExit);
}
