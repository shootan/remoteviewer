import io

BS = chr(92)
p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

# --- scenario 6: make Quiesce genuinely fail, with a real process that really does not exit
old = '''  // ================================================================ 6. quiesce does not complete

  {
    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.quiesceOk = false;  // and so nothing actually left
    const Result r = run_scenario(k);'''
new = '''  // ================================================================ 6. quiesce does not complete

  {
    // A real process that really does not exit, with its real identity -- so Quiesce waits on it
    // and gives up, rather than the enumerated pid simply not existing (which looks like "already
    // gone" and lets the update proceed).
    seed();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"@Q" + install + L"@@" + kHostName + L"@Q";
    const BOOL up = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                   nullptr, install.c_str(), &si, &pi);
    check("6 quiesce incomplete: a stubborn process is running", up != FALSE);

    Knobs k;
    ProcessTarget stubborn;
    if (up && capture_process_identity(pi.dwProcessId, &stubborn)) {
      k.stopped = {stubborn};
    } else {
      k.stopped = {hostTarget};
    }
    k.quiesceOk = false;  // and so nothing actually left
    const Result r = run_scenario(k);
    if (up) {
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }'''
new = new.replace('@Q', BS + '"').replace('@@', BS * 2)
assert old in d, 'scenario 6'
d = d.replace(old, new, 1)

# --- the control: it is DELETING a running image that fails, not renaming it
old = '''    if (started) {
      const std::wstring aside = victim + L".moving";
      const BOOL moved = MoveFileExW(victim.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING);
      check("control: moving a running image aside fails, which is why the stop comes first",
            moved == FALSE, std::to_string(GetLastError()));
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      const BOOL movedAfter = MoveFileExW(victim.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING);
      check("control: and succeeds once it has stopped", movedAfter != FALSE,
            std::to_string(GetLastError()));
      DeleteFileW(aside.c_str());
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }'''
new = '''    if (started) {
      // Windows permits RENAMING a running image, so the move-aside in phase one is not what
      // fails. What fails is the DELETE, which is what a rollback does to a file it placed before
      // moving the backup back -- and that is the step the stop-first ordering exists for. The
      // first version of this control asserted the rename failed; it does not, and the check
      // passed for the wrong reason until the error code was read.
      const BOOL deleted = DeleteFileW(victim.c_str());
      check("control: deleting a running image fails, which is why the stop comes first",
            deleted == FALSE && GetLastError() == ERROR_ACCESS_DENIED,
            std::to_string(GetLastError()));
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      const BOOL deletedAfter = DeleteFileW(victim.c_str());
      check("control: and succeeds once it has stopped", deletedAfter != FALSE,
            std::to_string(GetLastError()));
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }'''
assert old in d, 'control'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('scenario 6 and the control corrected')
