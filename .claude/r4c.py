import io

p = 'apps/native_poc/src/update_release_test.cpp'
d = io.open(p, encoding='utf-8').read()

anchor = '''  // ---------------------------------------------------------------- cleanup
'''
new = '''  {
    // The harder half of a mid-swap failure: phase one has finished, some new files are ALREADY
    // in place, and phase two then fails. Rollback has two different jobs here -- put back the
    // files that were moved aside, and remove the ones that were merely added, which have no
    // backup to restore and would otherwise stay behind as a permanent change from an update
    // that did not happen.
    seed_install();
    const auto before = snapshot(install);

    // A release that also ADDS a file the installation does not have.
    Release added = a;
    added.id = "r-0.2.105-added";
    added.bodies.push_back({"ui\\\\new_panel.html", "<html>panel added by 0.2.105</html>"});
    build_document(&added);

    Server server;
    server.current = &added;
    UpdateEffectsConfig c = base_config(&server);
    c.payloadNames = {L"GNLinkHost.exe", L"ui\\\\shell.html", L"GNLinkSetup.exe",
                      L"ui\\\\new_panel.html"};

    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(added.document, std::string(128, '0'));

    ManifestFields fields;
    fields.schema = 2;
    fields.releaseId = added.id;
    fields.platform = "windows";
    fields.arch = "x64";
    fields.version = added.version;
    for (const Body& body : added.bodies) {
      ManifestArtifact art;
      art.name = body.name;
      art.size = body.bytes.size();
      art.sha256 = sha256_of(body.bytes);
      art.url = "https://updates.example/" + added.version + "/" + body.name;
      fields.artifacts.push_back(art);
    }
    check("R4c: the release stages", e.Download(fields), e.last_error());
    check("R4c: the release verifies", e.VerifyDownload(fields), e.last_error());

    // Phase two copies from the staged files. Holding the LAST one open with no sharing makes its
    // copy fail after the earlier three have already been placed -- CopyFileW cannot open a source
    // nobody is allowed to read.
    std::wstring stagedLast;
    {
      WIN32_FIND_DATAW find{};
      HANDLE h = FindFirstFileW((staging + L"\\\\*").c_str(), &find);
      if (h != INVALID_HANDLE_VALUE) {
        do {
          const std::wstring name = find.cFileName;
          if (name == L"." || name == L"..") continue;
          if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            stagedLast = staging + L"\\\\" + name + L"\\\\ui_new_panel.html";
          }
        } while (FindNextFileW(h, &find));
        FindClose(h);
      }
    }
    check("R4c: the staged file to block was found", exists(stagedLast), narrow(stagedLast));
    HANDLE held = CreateFileW(stagedLast.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0,
                              nullptr);
    check("R4c: it is held with no sharing", held != INVALID_HANDLE_VALUE);

    check("R4c: the swap fails in phase two", !e.Swap(), e.last_error());
    check("R4c: and it names the file it could not place",
          e.last_error().find("could not place") != std::string::npos &&
              e.last_error().find("new_panel.html") != std::string::npos,
          e.last_error());

    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    e.DiscardDownload();

    const std::string diff = describe_difference(before, snapshot(install));
    check("R4c: the installation is byte-identical again", diff.empty(), diff);
    // Named separately because this is the part a rollback that only restores backups gets wrong.
    check("R4c: the added file was removed rather than left behind",
          !exists(install + L"\\\\ui\\\\new_panel.html"));
    check("R4c: the replaced files are the previous build's",
          read_file(install + L"\\\\GNLinkHost.exe") == "HOST-BINARY-CONTENT-v104" &&
              read_file(install + L"\\\\ui\\\\shell.html") == "<html>v104</html>" &&
              read_file(install + L"\\\\GNLinkSetup.exe") ==
                  "SETUP-BINARY-CONTENT-v104-and-then-some-padding");
    check("R4c: no backups are left over",
          !exists(install + L"\\\\GNLinkHost.exe.gnlink-old") &&
              !exists(install + L"\\\\ui\\\\shell.html.gnlink-old") &&
              !exists(install + L"\\\\GNLinkSetup.exe.gnlink-old"));
  }

''' + anchor
assert anchor in d, 'cleanup anchor'
d = d.replace(anchor, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('R4c added')
