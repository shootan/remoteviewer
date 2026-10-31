import io

# ---------------------------------------------------------------- updater: run_from_copy
p = 'apps/native_poc/src/updater_main.cpp'
d = io.open(p, encoding='utf-8').read()
d = d.replace('#include "update_http.hpp"', '#include "update_http.hpp"\n#include "update_job_guard.hpp"', 1)

old = '''bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs) {
  if (!ensure_directory(options.workDir)) {'''
new = '''bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs) {
  // Checked BEFORE creating it and before copying into it. A directory that already exists was
  // made by somebody, and accepting it because CreateDirectory reported ERROR_ALREADY_EXISTS is
  // accepting whatever permissions they gave it -- at which point this elevated process copies an
  // executable into a place they control and runs it.
  {
    std::string why;
    const LaunchGuardVerdict verdict = check_work_directory(options.workDir, &why);
    if (verdict != LaunchGuardVerdict::Ok) {
      log_line(std::string("refusing to use the working directory: ") +
               launch_guard_verdict_name(verdict) + " -- " + why);
      return false;
    }
  }
  if (!ensure_directory(options.workDir)) {'''
assert old in d, 'run_from_copy anchor'
d = d.replace(old, new, 1)

old = '''  BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP, nullptr,
                                options.workDir.c_str(), &si, &pi);
  if (!started) {
    started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                             CREATE_NEW_PROCESS_GROUP, nullptr, options.workDir.c_str(), &si, &pi);
    if (started) log_line("the working copy could not break away from its job object");
  }
  if (!started) {'''
new = '''  BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP, nullptr,
                                options.workDir.c_str(), &si, &pi);
  bool brokeAway = started != FALSE;
  if (!started) {
    // Breakaway was refused. Whether that matters depends on the job we are in -- and it is asked
    // rather than assumed, because refusing on every job would fail on machines where nothing was
    // ever at risk, and that is how a safety check ends up switched off.
    const JobKind kind = job_kind_of_current_process();
    const LaunchGuardVerdict verdict = judge_launch(kind, false);
    if (verdict != LaunchGuardVerdict::Ok) {
      // Not a warning. Proceeding here permits exactly what ledger item (b) describes: the host
      // being stopped -- which is the next thing that happens -- taking the updater with it, part
      // way through replacing files, with nothing left running to put them back.
      log_line(std::string("refusing to start the working copy: ") +
               launch_guard_verdict_name(verdict) +
               " (job kind " + std::to_string(static_cast<int>(kind)) + ")");
      return false;
    }
    started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                             CREATE_NEW_PROCESS_GROUP, nullptr, options.workDir.c_str(), &si, &pi);
    if (started) log_line("breakaway was not needed: this process is not in a job that kills its children");
  }
  (void)brokeAway;
  if (!started) {'''
assert old in d, 'updater breakaway anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- host: start_update_handoff
p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()
d = d.replace('#include "update_handoff.hpp"',
              '#include "update_handoff.hpp"\n#include "update_job_guard.hpp"', 1)

old = '''  BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                CREATE_BREAKAWAY_FROM_JOB, nullptr, installDir.c_str(), &si, &pi);
  if (!started) {
    started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                             installDir.c_str(), &si, &pi);
    if (started) append_host_app_log("[host-app] update: the updater could not break away from a job object");
  }
  if (!started) {'''
new = '''  BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                CREATE_BREAKAWAY_FROM_JOB, nullptr, installDir.c_str(), &si, &pi);
  if (!started) {
    // Refused breakaway only matters if this process is in a job that would take its children
    // down. Asked rather than assumed, and answered no by default: an updater that dies when this
    // process stops -- which is the very next step -- leaves files half replaced and nothing
    // running to restore them (ledger (b)).
    namespace upd = remote60::native_poc::update;
    const upd::JobKind kind = upd::job_kind_of_current_process();
    const upd::LaunchGuardVerdict verdict = upd::judge_launch(kind, false);
    if (verdict != upd::LaunchGuardVerdict::Ok) {
      append_host_app_log(std::string("[host-app] update: refusing to start the updater -- ") +
                          upd::launch_guard_verdict_name(verdict));
      CloseHandle(readyEvent);
      MessageBoxW(window,
                  L"지금은 업데이트를 시작할 수 없습니다.\\n\\n"
                  L"설치된 버전은 그대로이며 계속 사용할 수 있습니다.",
                  kProductName, MB_OK | MB_ICONINFORMATION);
      return;
    }
    started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                             installDir.c_str(), &si, &pi);
  }
  if (!started) {'''
assert old in d, 'host breakaway anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('fail-closed guards wired')
