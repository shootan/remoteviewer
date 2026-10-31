import io

BSS = 'L"' + chr(92) + chr(92) + '"'
p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  // ================================================================ control
  //
  // Scenario 4 without the release-before-rollback step.'''
new = '''  // ================================================================ control: whose process is it
  //
  // The sweep terminates processes. What stops it terminating somebody else's is the only
  // question that matters about it, and until now nothing asked: it matched on the file NAME and
  // killed anything that matched, including -- when it could not read a path -- processes it had
  // no evidence about at all. A second run of this suite would have been shot by the first.

  {
    // Same name, different directory: the stub scenario 6 depends on, stated as its own claim.
    const std::wstring elsewhere = root + ''' + BSS + '''"elsewhere";
    CreateDirectoryW(elsewhere.c_str(), nullptr);
    const std::wstring stranger = elsewhere + ''' + BSS + '''" + kHostName;
    CopyFileW(comspec, stranger.c_str(), FALSE);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\\"" + stranger + L"\\"";
    const BOOL up = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                   nullptr, elsewhere.c_str(), &si, &pi);
    check("ownership control: a same-named process exists outside the install directory",
          up != FALSE);
    if (up) {
      stop_everything_under(install);
      const bool alive = WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT;
      check("ownership control: sweeping the install directory does not touch it", alive);
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    DeleteFileW(stranger.c_str());
    RemoveDirectoryW(elsewhere.c_str());
  }

  {
    // A CONCURRENT RUN of this same suite: same names, same layout, a sibling root. This is the
    // one the old sweep would certainly have killed -- it ran with an empty scope at startup,
    // which is not a narrow scope but the absence of one.
    const std::wstring neighbour = scn_root_base() + ''' + BSS + '''"scn-neighbour-probe";
    const std::wstring neighbourInstall = neighbour + ''' + BSS + '''"install";
    CreateDirectoryW(neighbour.c_str(), nullptr);
    CreateDirectoryW(neighbourInstall.c_str(), nullptr);
    const std::wstring theirs = neighbourInstall + ''' + BSS + '''" + kClientName;
    CopyFileW(comspec, theirs.c_str(), FALSE);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\\"" + theirs + L"\\"";
    const BOOL up = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                   nullptr, neighbourInstall.c_str(), &si, &pi);
    check("ownership control: another run's process exists", up != FALSE);
    if (up) {
      stop_everything_under(install);
      stop_everything_under(root);
      const bool alive = WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT;
      check("ownership control: sweeping this run does not touch another run's process", alive);
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    // Removed by the run that created it, which is this one -- and it is under the configured
    // root, so the guard that protects everything else is not being stepped around here.
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (DeleteFileW(theirs.c_str()) || GetFileAttributesW(theirs.c_str()) ==
                                             INVALID_FILE_ATTRIBUTES) {
        break;
      }
      Sleep(50);
    }
    RemoveDirectoryW(neighbourInstall.c_str());
    RemoveDirectoryW(neighbour.c_str());
    check("ownership control: the probe cleans up after itself",
          GetFileAttributesW(neighbour.c_str()) == INVALID_FILE_ATTRIBUTES, narrow(neighbour));
  }

  {
    // And the refusal, stated directly: a path outside the configured root is never removed.
    check("ownership control: the run root is inside the repository", inside_run_root(root),
          narrow(root));
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    check("ownership control: %TEMP% is not inside it, so it can never be removed",
          !inside_run_root(std::wstring(temp) + L"gnlink-scn-1"), narrow(temp));
  }

  // ================================================================ control
  //
  // Scenario 4 without the release-before-rollback step.'''
assert old in d, 'control anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ownership counter-controls added')
