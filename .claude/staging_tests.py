import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---- fetchArtifact lambdas move to the per-artifact signature
d = d.replace('''  c.fetchArtifact = [](const ManifestFields&, const std::wstring& dest) {
    write_text(dest, kArtifactBytes);
    return true;
  };''',
              '''  c.fetchArtifact = [](const ManifestArtifact&, const std::wstring& dest) {
    write_text(dest, kArtifactBytes);
    return true;
  };''', 1)

d = d.replace('''    c.fetchArtifact = [](const ManifestFields&, const std::wstring& dest) {
      write_text(dest, "short");
      return false;  // reports failure after writing
    };''',
              '''    c.fetchArtifact = [](const ManifestArtifact&, const std::wstring& dest) {
      write_text(dest, "short");
      return false;  // reports failure after writing
    };''', 1)

d = d.replace('''    c.fetchArtifact = [](const ManifestFields&, const std::wstring& dest) {
      std::string utf16;
      for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\\0'); }
      write_text(dest, utf16);
      return true;
    };''',
              '''    c.fetchArtifact = [](const ManifestArtifact&, const std::wstring& dest) {
      std::string utf16;
      for (char ch : std::string("0.2.105")) { utf16.push_back(ch); utf16.push_back('\\0'); }
      write_text(dest, utf16);
      return true;
    };''', 1)

# ---- artifact_fields() gains the list the staging path now needs
old = '''ManifestFields artifact_fields() {
  ManifestFields f;
  f.schema = 2;
  f.releaseId = "r-0.2.105";
  f.arch = "x64";'''
new = '''ManifestFields artifact_fields(const std::vector<std::string>& names = {"AlphaPayload.bin",
                                                                          "BetaPayload.bin"}) {
  ManifestFields f;
  f.schema = 2;
  f.releaseId = "r-0.2.105";
  f.arch = "x64";
  for (const std::string& n : names) {
    ManifestArtifact a;
    a.name = n;
    a.size = std::char_traits<char>::length(kArtifactBytes);
    a.sha256 = gArtifactSha;
    a.url = "https://u.example/" + n;
    f.artifacts.push_back(a);
  }'''
assert old in d, 'artifact_fields anchor'
d = d.replace(old, new, 1)

# setup-member cases need the matching artifact list
d = d.replace('const ManifestFields f = artifact_fields();\n    check("setup-member: download"',
              'const ManifestFields f = artifact_fields({"AlphaPayload.bin", "GNLinkSetup.exe"});\n'
              '    check("setup-member: download"', 1)
d = d.replace('''    ManifestFields f = artifact_fields();
    e.Download(f);
    f.sha256 = std::string(64, 'b');''',
              '''    ManifestFields f = artifact_fields({"AlphaPayload.bin", "GNLinkSetup.exe"});
    e.Download(f);
    f.artifacts[1].sha256 = std::string(64, 'b');''', 1)
d = d.replace('''    e.Download(artifact_fields());

    HANDLE held = CreateFileW((install + L"\\\\GNLinkSetup.exe").c_str()''',
              '''    e.Download(artifact_fields({"AlphaPayload.bin", "GNLinkSetup.exe"}));

    HANDLE held = CreateFileW((install + L"\\\\GNLinkSetup.exe").c_str()''', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('test signatures updated')
