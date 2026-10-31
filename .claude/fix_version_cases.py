import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  {
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
  }'''
new = '''  {
    // The version string is looked for in the Setup, which is the artifact that carries one --
    // the rest are data. Here it does not, even though its hash is right.
    UpdateEffectsConfig c = base_config(install, staging);
    c.expectedVersion = "0.2.105";
    c.payloadNames = {L"GNLinkSetup.exe"};
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields({"GNLinkSetup.exe"});
    check("download for the version case", e.Download(f), e.last_error());
    check("a Setup that does not carry the expected version is refused", !e.VerifyDownload(f),
          e.last_error());
    check("and the reason names the version",
          e.last_error().find("0.2.105") != std::string::npos, e.last_error());
    e.DiscardDownload();
  }
  {
    // And an artifact that DOES carry it passes, so the case above is not passing for the trivial
    // reason that the check always fails.
    UpdateEffectsConfig c = base_config(install, staging);
    c.expectedVersion = "0.2.105";
    c.payloadNames = {L"GNLinkSetup.exe"};
    std::string utf16;
    for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\\0'); }
    c.fetchArtifact = [utf16](const ManifestArtifact&, const std::wstring& dest) {
      write_text(dest, utf16);
      return true;
    };
    WindowsUpdateEffects e(c);
    ManifestFields f = artifact_fields({"GNLinkSetup.exe"});
    {
      const std::wstring probe = staging + L"\\\\probe_v.bin";
      write_text(probe, utf16);
      uint64_t size = 0;
      file_size_bytes(probe, &size);
      f.artifacts[0].size = size;
      f.artifacts[0].sha256 = sha256_file_hex(probe);
      DeleteFileW(probe.c_str());
    }
    check("download for the positive version case", e.Download(f), e.last_error());
    check("a Setup that carries the expected version passes", e.VerifyDownload(f), e.last_error());
    e.DiscardDownload();
  }'''
assert old in d, 'negative version case'
d = d.replace(old, new, 1)

# the disagreeing-manifest case
old2 = '''    WindowsUpdateEffects e(c);
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
    e.Download(f);'''
new2 = '''    c.payloadNames = {L"GNLinkSetup.exe"};
    WindowsUpdateEffects e(c);
    ManifestFields f = artifact_fields({"GNLinkSetup.exe"});
    f.version = "0.9.9";  // disagrees with expectedVersion
    {
      // Size and hash have to match the bytes the fetcher writes, or the earlier checks fire first.
      const std::wstring probe = staging + L"\\\\probe2.bin";
      std::string utf16;
      for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\\0'); }
      write_text(probe, utf16);
      uint64_t size = 0;
      file_size_bytes(probe, &size);
      f.artifacts[0].size = size;
      f.artifacts[0].sha256 = sha256_file_hex(probe);
      DeleteFileW(probe.c_str());
    }
    check("download for the disagreement case", e.Download(f), e.last_error());'''
assert old2 in d, 'disagreement case'
d = d.replace(old2, new2, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('version cases repaired')
