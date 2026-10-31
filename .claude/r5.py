import io

BS = chr(92)

p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()
d = d.replace('      if (false && wasPlaced && file_exists(live)',
              '      if (wasPlaced && file_exists(live)', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/update_release_test.cpp'
d = io.open(p, encoding='utf-8').read()

anchor = '  // ---------------------------------------------------------------- cleanup\n'
block = '''  // ================================================================ 5. a release adds a folder

  {
    // Every case so far replaces files in folders the installation already has, because that is
    // what an update usually does. A release that adds a file somewhere new is the case that says
    // whether the destination path is created or merely assumed -- and the vector manifest already
    // names a destination in a subdirectory, so this is not a hypothetical shape.
    seed_install();
    const auto before = snapshot(install);

    Release grown = a;
    grown.id = "r-0.2.105-grown";
    grown.bodies.push_back({"plugins@@nested@@thing.dat", "a file in a folder that did not exist"});
    grown.document.clear();
    build_document(&grown);

    const std::vector<std::wstring> grownPayload = {L"GNLinkHost.exe", L"ui@@shell.html",
                                                    L"GNLinkSetup.exe",
                                                    L"plugins@@nested@@thing.dat"};

    {
      Server server;
      server.current = &grown;
      UpdateEffectsConfig c = base_config(&server);
      c.payloadNames = grownPayload;
      WindowsUpdateEffects e(c);
      e.set_installed_version("0.2.104");
      e.set_manifest(grown.document, std::string(128, '0'));

      const UpdateOutcome out = run_update(e, accept, "windows");
      check("R5: the release installs", out.result == UpdateResult::Updated,
            std::string(result_name(out.result)) + " / " + e.last_error());
      check("R5: the folder that did not exist was created",
            GetFileAttributesW((install + L"@@plugins@@nested").c_str()) & FILE_ATTRIBUTE_DIRECTORY);
      check("R5: and the file is in it",
            read_file(install + L"@@plugins@@nested@@thing.dat") ==
                "a file in a folder that did not exist");
    }

    // The other half: the same release, abandoned. The folder it had to create is not something
    // the previous build had, so leaving it behind would be a change from an update that did not
    // happen -- quieter than a stray file, and still a change.
    seed_install();
    {
      Server server;
      server.current = &grown;
      UpdateEffectsConfig c = base_config(&server);
      c.payloadNames = grownPayload;
      c.healthCheck = []() { return false; };
      WindowsUpdateEffects e(c);
      e.set_installed_version("0.2.104");
      e.set_manifest(grown.document, std::string(128, '0'));

      const UpdateOutcome out = run_update(e, accept, "windows");
      check("R5: the abandoned attempt rolls back", out.result == UpdateResult::RolledBack,
            std::string(result_name(out.result)) + " / " + e.last_error());
      const std::string diff = describe_difference(before, snapshot(install));
      check("R5: the installation is byte-identical again", diff.empty(), diff);
      check("R5: the created folder is gone too",
            !exists(install + L"@@plugins@@nested") && !exists(install + L"@@plugins"));
      check("R5: the folder the installation already had is still there",
            GetFileAttributesW((install + L"@@ui").c_str()) & FILE_ATTRIBUTE_DIRECTORY);
    }
  }

'''
block = block.replace('@@', BS * 2)
assert anchor in d, 'cleanup anchor'
d = d.replace(anchor, block + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('R5 added')
