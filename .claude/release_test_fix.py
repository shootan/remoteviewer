import io

p = 'apps/native_poc/src/update_release_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---- R1: drop the unused instance, and say why expectedVersion is left off
old = '''    UpdateEffectsConfig c = base_config(&server);
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    // A real process to stop, so that "0 terminations" in the failure cases below means something.
    DummyProcess dummy;
    check("R1: dummy process started", dummy.start());
    ProcessTarget target;
    check("R1: dummy identity captured", capture_process_identity(dummy.pid(), &target));
    UpdateEffectsConfig withTarget = c;
    bool stopped = false;
    withTarget.enumerateTargets = [&]() {
      return stopped ? std::vector<ProcessTarget>{} : std::vector<ProcessTarget>{target};
    };
    withTarget.requestStop = [&](const ProcessTarget&) {
      ++stopCount;
      stopped = true;
      dummy.kill();
      return true;
    };
    WindowsUpdateEffects withProcess(withTarget);
    withProcess.set_installed_version("0.2.104");
    withProcess.set_manifest(a.document, std::string(128, '0'));

    stopCount = 0;
    const UpdateOutcome out = run_update(withProcess, accept, "windows");
    check("R1: the release installs", out.result == UpdateResult::Updated,
          std::string(result_name(out.result)) + " " + out.detail + " / " +
              withProcess.last_error());'''
new = '''    // A real process to stop, so that "0 terminations" in the failure cases below means something.
    DummyProcess dummy;
    check("R1: dummy process started", dummy.start());
    ProcessTarget target;
    check("R1: dummy identity captured", capture_process_identity(dummy.pid(), &target));

    UpdateEffectsConfig c = base_config(&server);
    // expectedVersion is deliberately left empty. That check looks for the version as a UTF-16
    // literal inside the artifact, which is a property of a real PE built by this project; these
    // vector bodies are short ASCII stand-ins and carry no such literal. The check has its own
    // cases in update_effects_test.cpp against bodies built for it. What this suite is testing is
    // placement, and inventing a fake UTF-16 body here would trade a real file for a synthetic one
    // and lose the thing that makes the suite worth having.
    bool stopped = false;
    c.enumerateTargets = [&]() {
      return stopped ? std::vector<ProcessTarget>{} : std::vector<ProcessTarget>{target};
    };
    c.requestStop = [&](const ProcessTarget&) {
      ++stopCount;
      stopped = true;
      dummy.kill();
      return true;
    };
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    stopCount = 0;
    const UpdateOutcome out = run_update(e, accept, "windows");
    check("R1: the release installs", out.result == UpdateResult::Updated,
          std::string(result_name(out.result)) + " " + out.detail + " / " + e.last_error());'''
assert old in d, 'R1 anchor'
d = d.replace(old, new, 1)

# ---- R4: replace the stub with the two real cases
old = '''    Server server;
    server.current = &a;
    UpdateEffectsConfig c = base_config(&server);
    RegistrationEffects reg = make_registration_effects(make_target(L"0.2.105"), recordingOps);
    c.captureRegistration = reg.capture;
    c.registerInstall = reg.apply;
    c.restoreRegistration = reg.restore;

    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    check("R4: the release downloads", e.Download(a.document.empty() ? ManifestFields{}
                                                                     : ManifestFields{}),
          "placeholder");  // replaced below

    remove_tree(staging);
    CreateDirectoryW(staging.c_str(), nullptr);
    (void)before;
  }'''
new = '''    // The third payload name cannot be moved aside: something else has it open with no sharing,
    // which is what a running executable looks like to a swap.
    HANDLE held = CreateFileW((install + L"\\\\GNLinkSetup.exe").c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    check("R4: a payload file is held open", held != INVALID_HANDLE_VALUE);

    Server server;
    server.current = &a;
    UpdateEffectsConfig c = base_config(&server);
    RegistrationEffects reg = make_registration_effects(make_target(L"0.2.105"), recordingOps);
    c.captureRegistration = reg.capture;
    c.registerInstall = reg.apply;
    c.restoreRegistration = reg.restore;

    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    const UpdateOutcome out = run_update(e, accept, "windows");
    check("R4: the attempt fails at the swap", out.result == UpdateResult::Failed,
          std::string(result_name(out.result)) + " / " + e.last_error());
    check("R4: and it fails on the file that is held", e.last_error().find("GNLinkSetup.exe") !=
                                                           std::string::npos,
          e.last_error());

    // Released before the snapshot: a file this process holds with no sharing cannot be read back,
    // and comparing against an unreadable file would pass for the wrong reason.
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);

    const std::string diff = describe_difference(before, snapshot(install));
    check("R4: every existing file is back, byte for byte", diff.empty(), diff);
    check("R4: the two files that DID move aside were restored",
          read_file(install + L"\\\\GNLinkHost.exe") == "HOST-BINARY-CONTENT-v104" &&
              read_file(install + L"\\\\ui\\\\shell.html") == "<html>v104</html>");
    check("R4: no release A bytes were left behind",
          read_file(install + L"\\\\GNLinkHost.exe") != hostBytes &&
              read_file(install + L"\\\\ui\\\\shell.html") != shellBytes);
    check("R4: the registration still names the previous version",
          read_display_version() == L"0.2.104", narrow(read_display_version()));
  }

  {
    // The other half of the same requirement: a failure AFTER the swap and AFTER registration.
    // Here the registry really was moved to 0.2.105, so "restored" is a claim with something
    // behind it -- the files go back to the previous build AND DisplayVersion goes back with them.
    seed_install();
    {
      RegistrationEffects seedReg = make_registration_effects(make_target(L"0.2.104"),
                                                              recordingOps);
      check("R4b: previous registration seeded", seedReg.apply());
    }
    const auto before = snapshot(install);

    Server server;
    server.current = &a;
    UpdateEffectsConfig c = base_config(&server);
    RegistrationEffects reg = make_registration_effects(make_target(L"0.2.105"), recordingOps);
    c.captureRegistration = reg.capture;
    c.registerInstall = reg.apply;
    c.restoreRegistration = reg.restore;
    c.healthCheck = []() { return false; };  // the new build does not come up

    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    const UpdateOutcome out = run_update(e, accept, "windows");
    check("R4b: the attempt rolls back", out.result == UpdateResult::RolledBack,
          std::string(result_name(out.result)) + " / " + e.last_error());
    const std::string diff = describe_difference(before, snapshot(install));
    check("R4b: every file is the previous build again", diff.empty(), diff);
    check("R4b: DisplayVersion is the previous version, not the abandoned one",
          read_display_version() == L"0.2.104", narrow(read_display_version()));
    check("R4b: no backup files are left over",
          !exists(install + L"\\\\GNLinkHost.exe.gnlink-old") &&
              !exists(install + L"\\\\GNLinkSetup.exe.gnlink-old") &&
              !exists(install + L"\\\\ui\\\\shell.html.gnlink-old"));
  }'''
assert old in d, 'R4 anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('release test completed')
