# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:70])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


# ---------------------------------------------------------------- the swap actually replaces it
edit('apps/native_poc/src/update_effects_test.cpp', [
 ('''    check("setup-member: both have backups before registration",
          exists(install + L"\\\\GNLinkSetup.exe.gnlink-old"));
    e.DiscardDownload();
  }
''',
  '''    check("setup-member: both have backups before registration",
          exists(install + L"\\\\GNLinkSetup.exe.gnlink-old"));
    e.DiscardDownload();
  }

  // ---------------------------------------------------------------- the updater replaces itself

  {
    // The list said GNLinkUpdater.exe was a member. Nothing had ever put a new one in staging and
    // watched the installed one change, so "the updater can now be updated" rested on a name
    // being in a vector -- which is the same shape as the mistake that left it out for every
    // release. This replaces it for real.
    //
    // The reason it is safe is that the running updater is a copy OUTSIDE installDir, so the
    // destination of this swap is not the image executing it. updaterImagePath is set to that
    // working-copy shape here, exactly as updater_effects builds it in production.
    const std::string kOldUpdater = "OLD-UPDATER-BINARY";
    write_text(install + L"\\\\AlphaPayload.bin", kOldAlpha);
    write_text(install + L"\\\\GNLinkUpdater.exe", kOldUpdater);

    const std::wstring workingCopy = staging + L"\\\\..\\\\gnlink-updater-workingcopy";
    CreateDirectoryW(workingCopy.c_str(), nullptr);

    UpdateEffectsConfig c = base_config(install, staging);
    c.payloadNames = {L"AlphaPayload.bin", L"GNLinkUpdater.exe"};
    c.updaterImagePath = workingCopy + L"\\\\GNLinkUpdater.exe";
    std::string why;
    check("updater-member: the config is accepted", c.validate(&why), why);

    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields({"AlphaPayload.bin", "GNLinkUpdater.exe"});
    check("updater-member: download", e.Download(f), e.last_error());
    check("updater-member: swap succeeds", e.Swap(), e.last_error());
    check("updater-member: the installed GNLinkUpdater.exe really changed",
          read_text(install + L"\\\\GNLinkUpdater.exe") == kArtifactBytes,
          read_text(install + L"\\\\GNLinkUpdater.exe"));
    check("updater-member: ...and the old one was kept as a backup",
          exists(install + L"\\\\GNLinkUpdater.exe.gnlink-old"));
    e.DiscardDownload();

    // The guard that makes the above safe, from the other side: an updater running from INSIDE
    // the directory being replaced must be refused. Without this the first check would pass for
    // the wrong reason -- because nothing was checking, rather than because the layout is right.
    UpdateEffectsConfig bad = base_config(install, staging);
    bad.payloadNames = {L"AlphaPayload.bin", L"GNLinkUpdater.exe"};
    bad.updaterImagePath = install + L"\\\\GNLinkUpdater.exe";
    std::string badWhy;
    check("updater-member: an updater running from the install directory is refused",
          !bad.validate(&badWhy), badWhy);
    check("updater-member: ...and the reason says so",
          badWhy.find("running updater") != std::string::npos ||
              badWhy.find("inside the directory") != std::string::npos,
          badWhy);

    RemoveDirectoryW(workingCopy.c_str());
  }
'''),
])

# ---------------------------------------------------------------- the assembly drives production's list
edit('apps/native_poc/src/updater_assembly_test.cpp', [
 ('''  // The names the assembly will try to replace come from the product's own image list, so the
  // install directory is seeded with all of them.
  for (const std::wstring& name : product_image_names()) {
    write_file(install + L"\\\\" + name, "old-" + narrow(name));
  }
  CreateDirectoryW((install + L"\\\\ui").c_str(), nullptr);
  write_file(install + L"\\\\ui\\\\shell.html", "old-shell");
  write_file(install + L"\\\\ui\\\\macro.html", "old-macro");''',
  '''  // Seeded from product_payload_names(), which is the list production actually replaces. It used
  // to be seeded from product_image_names() plus the two data files -- a list that differed from
  // production's by exactly the file this suite exists to cover, so the assembly was being driven
  // with a payload the product does not use.
  CreateDirectoryW((install + L"\\\\ui").c_str(), nullptr);
  for (const std::wstring& name : product_payload_names()) {
    write_file(install + L"\\\\" + name, "old-" + narrow(name));
  }'''),
 ('''    for (const std::wstring& name : product_image_names()) add(name);
    add(L"ui\\\\shell.html");
    add(L"ui\\\\macro.html");''',
  '''    for (const std::wstring& name : product_payload_names()) add(name);'''),
 ('''    deps.payloadNames = product_image_names();
    deps.payloadNames.push_back(L"ui\\\\shell.html");
    deps.payloadNames.push_back(L"ui\\\\macro.html");''',
  '''    deps.payloadNames = product_payload_names();'''),
])
