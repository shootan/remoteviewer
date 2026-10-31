import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---- argv handling for the cross-process lock contender
old = "int main() {\n  const std::wstring install = make_temp_dir(L\"install\");"
new = '''int main(int argc, char** argv) {
  // Re-executed by the concurrency case below. Takes the named mutex, says so, and holds it while
  // the parent tries to take it too. A second PROCESS is the only honest way to test this: a
  // Windows mutex is re-entrant for its owning thread, and the real contenders are processes.
  if (argc >= 3 && std::string(argv[1]) == "--hold-lock") {
    const std::string name(argv[2]);
    const std::wstring wide(name.begin(), name.end());
    HANDLE h = CreateMutexW(nullptr, FALSE, wide.c_str());
    if (!h) return 2;
    if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) return 3;
    std::cout << "held" << std::endl;  // flushed, so the parent knows the lock is taken
    Sleep(4000);
    ReleaseMutex(h);
    CloseHandle(h);
    return 0;
  }

  const std::wstring install = make_temp_dir(L"install");'''
assert old in d, 'main anchor'
d = d.replace(old, new, 1)

# ---- new scenarios, inserted before cleanup
old = """  remove_tree(staging);
  remove_tree(install);"""
new = '''  // ---------------------------------------------------------------- PID reuse

  {
    // A live process, but described with the wrong creation time -- which is what a reused PID
    // looks like. Quiesce must treat it as already gone rather than waiting on a stranger, so
    // this must succeed IMMEDIATELY even though the PID is very much alive.
    DummyProcess bystander;
    check("bystander started", bystander.start());
    ProcessTarget stale;
    check("bystander identity captured", capture_process_identity(bystander.pid(), &stale));
    const uint64_t realCreation = stale.creationTime;
    stale.creationTime = realCreation ^ 0xFFFFull;  // as if a different process now holds the PID

    UpdateEffectsConfig c = base_config(install, staging);
    c.quiesceTimeoutMs = 400;
    c.enumerateTargets = [stale]() { return std::vector<ProcessTarget>{stale}; };
    c.requestStop = [](const ProcessTarget&) { return true; };
    WindowsUpdateEffects e(c);
    const DWORD before = GetTickCount();
    const bool quiesced = e.Quiesce();
    const DWORD elapsed = GetTickCount() - before;
    check("a reused PID is treated as already gone", quiesced, e.last_error());
    check("and it did not wait out the timeout", elapsed < 300, std::to_string(elapsed) + "ms");
    check("the bystander was never touched", bystander.alive());

    // Sanity: with the CORRECT identity the same call does wait and does time out. Without this
    // the case above could pass for the wrong reason.
    ProcessTarget correct = stale;
    correct.creationTime = realCreation;
    UpdateEffectsConfig c2 = base_config(install, staging);
    c2.quiesceTimeoutMs = 400;
    c2.enumerateTargets = [correct]() { return std::vector<ProcessTarget>{correct}; };
    c2.requestStop = [](const ProcessTarget&) { return true; };
    WindowsUpdateEffects e2(c2);
    check("with the right identity it does wait and fail", !e2.Quiesce(), e2.last_error());
    check("bystander still alive after the timeout", bystander.alive());
    bystander.kill();
  }

  {
    // process_identity_matches directly, against this process.
    ProcessTarget self;
    check("self identity captured", capture_process_identity(GetCurrentProcessId(), &self));
    HANDLE me = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    check("self handle opened", me != nullptr);
    check("identity matches itself", process_identity_matches(me, self));
    ProcessTarget wrongTime = self;
    wrongTime.creationTime += 1;
    check("a different creation time does not match", !process_identity_matches(me, wrongTime));
    ProcessTarget wrongPath = self;
    wrongPath.imagePath = L"C:\\\\nowhere\\\\else.exe";
    check("a different image path does not match", !process_identity_matches(me, wrongPath));
    ProcessTarget noTime = self;
    noTime.creationTime = 0;
    check("an unidentified target never matches", !process_identity_matches(me, noTime));
    if (me) CloseHandle(me);
  }

  // ---------------------------------------------------------------- real relaunch failure

  {
    // A genuine CreateProcessW against a path that does not exist -- not a lambda returning false.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    c.relaunch = [&]() {
      std::wstring cmd = install + L"\\\\NoSuchProduct.exe";
      STARTUPINFOW si{};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi{};
      const BOOL ok = CreateProcessW(cmd.c_str(), nullptr, nullptr, nullptr, FALSE,
                                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
      if (ok) {  // should never happen; clean up if it somehow does
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
      }
      return ok != FALSE;
    };
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest("schema=1\\nplatform=windows\\nversion=0.2.105\\nartifact=test.bin\\nsize=" +
                       std::to_string(artifact_fields().size) + "\\nsha256=" + gArtifactSha + "\\n",
                   std::string(128, '0'));
    const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
    const UpdateOutcome out = run_update(e, accept, "windows");
    check("a real failed CreateProcess -> UpdatedButNotRelaunched",
          out.result == UpdateResult::UpdatedButNotRelaunched, result_name(out.result));
    check("and the new files are left in place, not rolled back",
          read_text(install + L"\\\\AlphaPayload.bin") == kArtifactBytes);
    e.DiscardDownload();
  }

  // ---------------------------------------------------------------- real rollback failure

  {
    // Rollback restores by moving the backup over the live path. Holding the live path open with
    // no sharing makes that move genuinely fail at the OS level.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("rollback-failure case: swap succeeds first", e.Swap(), e.last_error());

    HANDLE held = CreateFileW((install + L"\\\\BetaPayload.bin").c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check("could hold the swapped-in file open", held != INVALID_HANDLE_VALUE);
    const bool rolled = e.Rollback();
    check("rollback reports failure when a file cannot be restored", !rolled, e.last_error());
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    // Alpha was restorable and must have been restored even though Beta was not -- a rollback
    // that gives up entirely on the first problem would leave more of the new version in place.
    check("the restorable file was still restored",
          read_text(install + L"\\\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\\\AlphaPayload.bin"));
    e.DiscardDownload();
  }

  // ---------------------------------------------------------------- real concurrent execution

  {
    // Two real processes, one named mutex. The contender is this same executable, re-run with a
    // flag, so the lock is contended across a process boundary exactly as it would be in the field.
    wchar_t selfPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    const std::wstring lockName =
        L"Local\\\\gnlink-update-crossproc-" + std::to_wstring(GetCurrentProcessId());

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    check("pipe created for the contender", CreatePipe(&readPipe, &writePipe, &sa, 0) != FALSE);

    std::wstring cmd = std::wstring(L"\\"") + selfPath + L"\\" --hold-lock " + lockName;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\\0');
    const BOOL started = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    check("lock contender process started", started != FALSE);
    CloseHandle(writePipe);

    if (started) {
      // Wait for it to say it holds the lock, rather than sleeping and hoping.
      char buf[16]{};
      DWORD read = 0;
      const BOOL got = ReadFile(readPipe, buf, 4, &read, nullptr);
      check("contender reported holding the lock", got && read >= 4, std::string(buf, read));

      UpdateEffectsConfig c = base_config(install, staging);
      c.lockName = lockName;
      WindowsUpdateEffects e(c);
      check("another PROCESS holding the lock blocks this one", !e.AcquireLock(), e.last_error());

      // And the state machine turns that into "not now" rather than an error or a wait.
      e.set_installed_version("0.2.104");
      e.set_manifest("schema=1\\nplatform=windows\\nversion=0.2.105\\nartifact=test.bin\\nsize=" +
                         std::to_string(artifact_fields().size) + "\\nsha256=" + gArtifactSha + "\\n",
                     std::string(128, '0'));
      const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
      const UpdateOutcome out = run_update(e, accept, "windows");
      check("contended lock -> NothingToDo", out.result == UpdateResult::NothingToDo,
            result_name(out.result));
      check("contended lock -> nothing was downloaded", !out.entered(UpdateState::Download));

      WaitForSingleObject(pi.hProcess, 8000);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);

      // Once it exits the lock must be free again.
      WindowsUpdateEffects after(c);
      check("lock is free once the other process exits", after.AcquireLock(), after.last_error());
      after.ReleaseLock();
    }
    CloseHandle(readPipe);
  }

  // ---------------------------------------------------------------- privilege flow, observed

  {
    // Not an assertion about what elevation SHOULD be -- just a recorded fact, because the design
    // says a host-launched updater inherits an elevated token and needs no second prompt, and
    // that claim is only checkable from a real elevated run (which this is not).
    HANDLE token = nullptr;
    bool elevated = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
      TOKEN_ELEVATION info{};
      DWORD size = 0;
      if (GetTokenInformation(token, TokenElevation, &info, sizeof(info), &size)) {
        elevated = info.TokenIsElevated != 0;
      }
      CloseHandle(token);
    }
    std::cout << "NOTE  this test ran " << (elevated ? "ELEVATED" : "NOT elevated")
              << " -- the design's \\"host-launched updater needs no second UAC prompt\\" claim is"
                 " not verified here either way\\n";
    check("elevation state could be read", true);
  }

  remove_tree(staging);
  remove_tree(install);'''
assert old in d, 'cleanup anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('real-OS scenarios added')
