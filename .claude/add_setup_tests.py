import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

# base_config gains the new fields so existing cases keep validating
old = """  c.quiesceTimeoutMs = 5000;
  return c;
}"""
new = """  c.quiesceTimeoutMs = 5000;
  // The updater's own image, so validate() can check it is not among the payload rather than
  // trusting that it never would be.
  {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    c.updaterImagePath = self;
  }
  return c;
}

/** The same config, but treating GNLinkSetup.exe as a member of the package. */
UpdateEffectsConfig setup_member_config(const std::wstring& install, const std::wstring& staging) {
  UpdateEffectsConfig c = base_config(install, staging);
  // Condition 3 of the package contract: the maintenance binary is a full member, staged, backed
  // up, replaced and rolled back exactly like a product file -- not copied afterwards.
  c.payloadNames = {L"AlphaPayload.bin", L"GNLinkSetup.exe"};
  return c;
}"""
assert old in d, 'base_config anchor'
d = d.replace(old, new, 1)

# new scenarios, before cleanup
old = """  remove_tree(staging);
  remove_tree(install);"""
new = '''  // ---------------------------------------------------------------- Setup as a package member

  const std::string kOldSetup = "OLD-SETUP-BINARY";
  const auto seed_with_setup = [&]() {
    write_text(install + L"\\\\AlphaPayload.bin", kOldAlpha);
    write_text(install + L"\\\\GNLinkSetup.exe", kOldSetup);
  };

  {
    // The point of condition 3: the maintenance binary goes through the same swap. After an
    // update the uninstall entry must not still be pointing at a binary from an older build.
    seed_with_setup();
    UpdateEffectsConfig c = setup_member_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    check("setup-member: download", e.Download(f), e.last_error());
    check("setup-member: swap succeeds", e.Swap(), e.last_error());
    check("setup-member: the product file was replaced",
          read_text(install + L"\\\\AlphaPayload.bin") == kArtifactBytes);
    check("setup-member: GNLinkSetup.exe was replaced TOO",
          read_text(install + L"\\\\GNLinkSetup.exe") == kArtifactBytes,
          read_text(install + L"\\\\GNLinkSetup.exe"));
    check("setup-member: both have backups before registration",
          exists(install + L"\\\\GNLinkSetup.exe.gnlink-old"));
    e.DiscardDownload();
  }

  {
    // Condition 5, first regression: the artifact does not match its hash. Nothing may be
    // replaced -- including the Setup.
    seed_with_setup();
    UpdateEffectsConfig c = setup_member_config(install, staging);
    WindowsUpdateEffects e(c);
    ManifestFields f = artifact_fields();
    e.Download(f);
    f.sha256 = std::string(64, 'b');
    check("hash mismatch is refused", !e.VerifyDownload(f), e.last_error());
    check("and the old Setup is untouched",
          read_text(install + L"\\\\GNLinkSetup.exe") == kOldSetup);
    check("and the old product file is untouched",
          read_text(install + L"\\\\AlphaPayload.bin") == kOldAlpha);
    e.DiscardDownload();
  }

  {
    // Condition 5, second regression: the Setup on disk is locked, the way it would be if it were
    // running. The swap must leave everything as it was -- no half-updated install.
    seed_with_setup();
    UpdateEffectsConfig c = setup_member_config(install, staging);
    WindowsUpdateEffects e(c);
    e.Download(artifact_fields());

    HANDLE held = CreateFileW((install + L"\\\\GNLinkSetup.exe").c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check("could lock the Setup binary", held != INVALID_HANDLE_VALUE);
    check("swap fails when the Setup is locked", !e.Swap(), e.last_error());
    check("the product file was NOT left replaced",
          read_text(install + L"\\\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\\\AlphaPayload.bin"));
    check("no backups left behind", !exists(install + L"\\\\AlphaPayload.bin.gnlink-old"));
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    check("the locked Setup is still the old one",
          read_text(install + L"\\\\GNLinkSetup.exe") == kOldSetup);
    e.DiscardDownload();
  }

  {
    // Condition 5, third regression: the swap succeeds and something afterwards fails. The
    // rollback has to restore the OLD Setup as well, not just the product files -- otherwise the
    // install is left with a new maintenance binary and old everything else.
    seed_with_setup();
    UpdateEffectsConfig c = setup_member_config(install, staging);
    c.registerInstall = []() { return false; };  // fails after the swap
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest("schema=1\\nplatform=windows\\nversion=0.2.105\\nartifact=test.bin\\nsize=" +
                       std::to_string(artifact_fields().size) + "\\nsha256=" + gArtifactSha + "\\n",
                   std::string(128, '0'));
    const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
    const UpdateOutcome out = run_update(e, accept, "windows");
    check("a post-swap failure rolls back", out.result == UpdateResult::RolledBack,
          std::string(result_name(out.result)) + " " + out.detail);
    check("rollback restored the product file",
          read_text(install + L"\\\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\\\AlphaPayload.bin"));
    check("rollback restored the OLD Setup too",
          read_text(install + L"\\\\GNLinkSetup.exe") == kOldSetup,
          read_text(install + L"\\\\GNLinkSetup.exe"));
    check("no backups left after rollback",
          !exists(install + L"\\\\GNLinkSetup.exe.gnlink-old"));
  }

  // ---------------------------------------------------------------- the updater cannot replace itself

  {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring selfPath = self;
    const size_t slash = selfPath.find_last_of(L"\\\\/");
    const std::wstring selfName = selfPath.substr(slash + 1);

    UpdateEffectsConfig c = base_config(install, staging);
    c.payloadNames = {L"AlphaPayload.bin", selfName};
    std::string why;
    check("naming the running updater as payload is refused", !c.validate(&why), why);
    check("and the reason says so", why.find("updater") != std::string::npos, why);
  }
  {
    // The updater sitting inside the directory it would replace is the same hazard by a different
    // route, and design 3.2 puts it outside for exactly this reason.
    UpdateEffectsConfig c = base_config(install, staging);
    c.updaterImagePath = install + L"\\\\SomeUpdater.exe";
    std::string why;
    check("an updater inside the install dir is refused", !c.validate(&why), why);
  }

  // ---------------------------------------------------------------- version consistency

  {
    // Condition 4. The artifact here is plain text containing the version as UTF-16, standing in
    // for a PE image carrying a wide string literal.
    const std::wstring probe = staging + L"\\\\versioned.bin";
    std::string utf16;
    for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\\0'); }
    write_text(probe, "prefix" + utf16 + "suffix");
    check("the version is found when present",
          file_contains_utf16_version(probe, "0.2.105"));
    check("a different version is not found",
          !file_contains_utf16_version(probe, "0.3.0"));
    check("an empty version is never found", !file_contains_utf16_version(probe, ""));
    check("a missing file is not a match", !file_contains_utf16_version(staging + L"\\\\nope.bin", "0.2.105"));
    DeleteFileW(probe.c_str());
  }

  {
    // Wired in: an artifact that does not carry the expected version fails verification even
    // though its hash is right.
    UpdateEffectsConfig c = base_config(install, staging);
    c.expectedVersion = "0.2.105";
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("an artifact without the expected version is refused", !e.VerifyDownload(f),
          e.last_error());
    check("and the reason names the version",
          e.last_error().find("0.2.105") != std::string::npos, e.last_error());
    e.DiscardDownload();
  }
  {
    // And the manifest's own version must agree with what we were told to expect.
    UpdateEffectsConfig c = base_config(install, staging);
    c.expectedVersion = "0.2.105";
    c.fetchArtifact = [](const ManifestFields&, const std::wstring& dest) {
      std::string utf16;
      for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\\0'); }
      write_text(dest, utf16);
      return true;
    };
    WindowsUpdateEffects e(c);
    ManifestFields f;
    f.schema = 1;
    f.platform = "windows";
    f.version = "0.9.9";  // disagrees with expectedVersion
    f.artifact = "test.bin";
    {
      // Size and hash have to match the bytes the fetcher writes, or the earlier checks fire first.
      const std::wstring probe = staging + L"\\\\probe2.bin";
      std::string utf16;
      for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\\0'); }
      write_text(probe, utf16);
      uint64_t size = 0;
      file_size_bytes(probe, &size);
      f.size = size;
      f.sha256 = sha256_file_hex(probe);
      DeleteFileW(probe.c_str());
    }
    e.Download(f);
    check("a manifest version that disagrees with the expected one is refused",
          !e.VerifyDownload(f), e.last_error());
    check("and the reason names both", e.last_error().find("0.9.9") != std::string::npos,
          e.last_error());
    e.DiscardDownload();
  }

  remove_tree(staging);
  remove_tree(install);'''
assert old in d, 'cleanup anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('setup-member and version tests added')
