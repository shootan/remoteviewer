import io

BS = chr(92)
p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ------------------------------------------------------------------ a knob for the unownable route
old = '''    bool startClient = true;    // ditto the client'''
new = '''    bool startClient = true;    // ditto the client
    /**
     * Whether the client can be started by a route that hands back a handle.
     *
     * False models an elevated updater whose token launch fails and which falls through to the
     * shell -- started, and no way to say which process it is. The consequence is the point.
     */
    bool clientOwnable = true;'''
assert old in d, 'knob'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ inject the owning launcher
old = '''      live.isStillRunning = [knobs](const ProcessTarget&) {
        return knobs.quiesceOk ? RelaunchConfig::Liveness::Exited
                               : RelaunchConfig::Liveness::Running;
      };'''
new = '''      live.isStillRunning = [knobs](const ProcessTarget&) {
        return knobs.quiesceOk ? RelaunchConfig::Liveness::Exited
                               : RelaunchConfig::Liveness::Running;
      };
      // The user-context launch, modelling the elevated updater.
      //
      // Production duplicates the shell's token and calls CreateProcessWithTokenW, which needs
      // SeImpersonatePrivilege -- an elevated process has it, and the updater is elevated. A test
      // runner usually is not, so the real call fails here and the product falls through to the
      // shell; that fall-through is a real path and gets its own case below, but it is not what
      // an installed updater does, and letting it stand in for one would make every scenario
      // measure the fallback.
      //
      // What matters is the shape and not the API: a launch that HANDS BACK A HANDLE. This does
      // that, so ownership downstream is real ownership.
      live.launchInUserContext = [knobs](const std::wstring& exePath, const std::wstring& workDir,
                                        void** handleOut, uint32_t* pidOut) {
        if (!knobs.clientOwnable) return false;  // "the token launch did not work here"
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\\"" + exePath + L"\\"";
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, workDir.empty() ? nullptr : workDir.c_str(), &si, &pi)) {
          return false;
        }
        CloseHandle(pi.hThread);
        if (handleOut) *handleOut = pi.hProcess; else CloseHandle(pi.hProcess);
        if (pidOut) *pidOut = pi.dwProcessId;
        return true;
      };'''
assert old in d, 'isStillRunning injection'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ the control for the unowned case
old = '''  // ================================================================ control: whose process is it'''
new = '''  // ============================================ 7. started, and nobody can say which process it is
  //
  // The client comes back through the shell because the owning route did not work. It is running
  // and this attempt cannot prove which process it is, so it will not stop it -- and a rollback
  // that would have to move the file it is holding does not begin.
  //
  // Refusing costs something real: the machine keeps the new build when it wanted the old one.
  // The alternative costs more. Terminating on the strength of a name or a snapshot difference
  // means terminating whatever else answers to it, and a half-finished restore leaves an
  // installation that is part old and part new with the rollback blamed for it.

  {
    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.clientOwnable = false;  // the token launch fails; the shell takes over
    k.newHealthOk = false;    // and the new build is unhealthy, so a rollback is wanted
    const Result r = run_scenario(k);
    check("7 unowned client: the rollback does not proceed",
          r.outcome.result == UpdateResult::RollbackFailed, result_name(r.outcome.result));
    check("7 unowned client: and it says why -- no proof of ownership",
          r.log.find("no proof of ownership") != std::string::npos, r.log);
    check("7 unowned client: the reason names the refusal, not a file error",
          r.effectsError.find("not rolling back") != std::string::npos, r.effectsError);
    // Nothing was half-restored. The files are still the new ones, consistently, which is what
    // makes another attempt possible.
    check("7 unowned client: the files were left as the swap left them", r.hostBytes == newBody,
          std::to_string(r.hostBytes.size()));
    check("7 unowned client: and the backups are still there to go back with", r.hostBackupLeft);
  }
  {
    // The counter-control. Same scenario, same unhealthy build, and the client started by a route
    // that returns a handle -- so it can be stopped and the rollback runs. Without this the case
    // above would also pass if rollbacks simply never worked.
    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.clientOwnable = true;
    k.newHealthOk = false;
    const Result r = run_scenario(k);
    check("7 counter-control: an owned client is stopped and the rollback runs",
          r.outcome.result != UpdateResult::RollbackFailed, result_name(r.outcome.result));
    check("7 counter-control: the old bytes really are back", r.hostBytes == oldBody,
          std::to_string(r.hostBytes.size()));
  }

  // ================================================================ control: whose process is it'''
assert old in d, 'control anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('scenarios launch the client by an owning route, and the unowned case has its own scenario')
