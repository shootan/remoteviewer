import io

BS = chr(92)
p = 'apps/native_poc/src/update_relaunch.hpp'
d = io.open(p, encoding='utf-8').read()

# --------------------------------------------------------------- outcome: ownership is its own fact
old = '''  /** The process this attempt started, when it started one. 0 otherwise. */
  uint32_t startedPid = 0;'''
new = '''  /** The process this attempt started, when it started one AND can prove which. 0 otherwise. */
  uint32_t startedPid = 0;
  /**
   * True when something was started but this attempt cannot say which process it is.
   *
   * Started and owned are different facts, and they were being conflated. A launch routed through
   * the shell is performed by the shell, so nothing comes back: no handle, no PID. Comparing
   * process snapshots taken either side of the call looks like it recovers the PID, and it does
   * not -- a user starting the same program at that moment produces exactly the same difference,
   * and the handle opened on that basis pins a stranger's identity rather than establishing ours.
   *
   * So it is recorded as unknown. Nothing unknown is terminated, and a rollback does not begin
   * while one exists, because a rollback moves files this process may still be holding.
   */
  bool ownershipUnknown = false;'''
assert old in d, 'startedPid'
d = d.replace(old, new, 1)

# --------------------------------------------------------------- config: the user-context launcher
old = '''  enum class Liveness { Running, Exited, Unknown };
  std::function<Liveness(const ProcessTarget&)> isStillRunning;
};'''
new = '''  enum class Liveness { Running, Exited, Unknown };
  std::function<Liveness(const ProcessTarget&)> isStillRunning;

  /**
   * Starts an image in the interactive user's context and hands back a handle to the process.
   *
   * The handle is the whole point. The client must not be started with the updater's
   * administrator token -- it would run with rights it has never had -- so it has to be launched
   * as the logged-on user; and the route that does that through the shell performs the launch in
   * another process and returns nothing. Without a handle there is no way to say which process
   * was the one asked for, and everything downstream that stops or waits on it is guesswork.
   *
   * The real implementation duplicates the shell's own token and calls CreateProcessWithTokenW,
   * which returns a PROCESS_INFORMATION like any other launch. It needs SeImpersonatePrivilege,
   * which the updater has because it runs elevated, and which is exactly why the updater is the
   * right place to do this.
   *
   * Empty means the real one -- not "skip it". An optional seam here would be the same defect as
   * the liveness check that production never filled in: switched off in the only build that
   * matters, while every test passed.
   *
   * `processHandleOut` receives a handle the caller owns. `pidOut` receives its PID.
   */
  std::function<bool(const std::wstring& exePath, const std::wstring& workDir,
                     void** processHandleOut, uint32_t* pidOut)>
      launchInUserContext;
};'''
assert old in d, 'liveness field tail'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# --------------------------------------------------------------- the real token launch
p = 'apps/native_poc/src/update_relaunch.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments) {'''
new = '''/**
 * Starts `exePath` as the logged-on user and returns a handle to it.
 *
 * The shell's own process is the reference for "the interactive user": whoever owns the desktop
 * owns that window, so duplicating its token gets the right user, the right session and the right
 * integrity level without this having to work any of that out. CreateProcessWithTokenW then
 * behaves like an ordinary launch -- it returns a PROCESS_INFORMATION -- which is the difference
 * that matters. ShellExecute through the desktop does the same job and hands back nothing.
 *
 * Needs SeImpersonatePrivilege. An elevated process has it; that is the reason this belongs to
 * the updater rather than to something the product does for itself.
 */
bool launch_as_shell_user(const std::wstring& exePath, const std::wstring& workDir,
                          void** processHandleOut, uint32_t* pidOut) {
  if (gShellLaunchDisabled) return false;  // the same switch the tests use for the other route
  // Asked before anything else: a path that is not there cannot be started, and finding that out
  // here keeps it out of the shell fallback, which has no way to fail quietly.
  if (GetFileAttributesW(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

  HWND shell = GetShellWindow();
  if (!shell) return false;  // no interactive desktop -- nothing to run as
  DWORD shellPid = 0;
  GetWindowThreadProcessId(shell, &shellPid);
  if (!shellPid) return false;

  HANDLE shellProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, shellPid);
  if (!shellProcess) return false;
  HANDLE shellToken = nullptr;
  const BOOL gotToken = OpenProcessToken(shellProcess, TOKEN_DUPLICATE | TOKEN_QUERY, &shellToken);
  CloseHandle(shellProcess);
  if (!gotToken) return false;

  HANDLE primary = nullptr;
  const BOOL duplicated = DuplicateTokenEx(shellToken, MAXIMUM_ALLOWED, nullptr,
                                           SecurityImpersonation, TokenPrimary, &primary);
  CloseHandle(shellToken);
  if (!duplicated) return false;

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring commandLine = L"''' + BS + BS + '''"" + exePath + L"''' + BS + BS + '''"";
  const BOOL started =
      CreateProcessWithTokenW(primary, 0, nullptr, commandLine.data(), 0, nullptr,
                              workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);
  CloseHandle(primary);
  if (!started) return false;

  CloseHandle(pi.hThread);
  if (processHandleOut) *processHandleOut = pi.hProcess;
  else CloseHandle(pi.hProcess);
  if (pidOut) *pidOut = pi.dwProcessId;
  return true;
}

bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments) {'''
assert old in d, 'launch_via_shell'
d = d.replace(old, new, 1)

# --------------------------------------------------------------- the loop: own it or say you do not
start = d.find('      } else if (entry.kind == RelaunchKind::UserProcess) {')
assert start != -1, 'user process branch'
end = d.find('      } else {', start)
assert end != -1 and end > start, 'branch end'
new_branch = '''      } else if (entry.kind == RelaunchKind::UserProcess) {
        // Owned, or declared unowned. There is no third option any more.
        //
        // This used to launch through the shell and then work out which process appeared by
        // comparing snapshots taken either side of the call. One new process meant "that is
        // ours". It does not: a user starting the same program in that window produces the same
        // single difference, and the handle opened on the strength of it fixes a STRANGER's
        // identity -- which a rollback would then terminate. The snapshot comparison is gone.
        const auto launch =
            shared->config.launchInUserContext
                ? shared->config.launchInUserContext
                : std::function<bool(const std::wstring&, const std::wstring&, void**, uint32_t*)>(
                      launch_as_shell_user);
        void* handle = nullptr;
        uint32_t pid = 0;
        if (launch(exePath, shared->config.installDir, &handle, &pid)) {
          outcome.started = true;
          outcome.startedPid = pid;
          if (handle) shared->ownedHandles.push_back({entry.imageName, static_cast<HANDLE>(handle)});
          outcome.detail = "started in the user context";
        } else if (launch_via_shell(exePath, L"")) {
          // Running, and unidentified. Reported as such rather than guessed at: the consequence
          // is that this attempt will not stop it and a rollback will not start, which is the
          // correct trade against terminating something that might belong to the user.
          outcome.started = true;
          outcome.ownershipUnknown = true;
          outcome.detail = "started by the shell, which returns no handle -- this attempt cannot "
                           "prove which process is its own, so it will not stop it";
        } else {
          // Not started, and deliberately not retried as a child of this process. Starting it
          // here would give it an administrator token it never had. A safe failure, not a
          // success, so it is recorded as a failure and the user is told.
          outcome.detail = "could not be started in the user context, and was NOT started "
                           "elevated instead";
        }
'''
d = d[:start] + new_branch + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('user-context launch returns a handle, or ownership is declared unknown')
