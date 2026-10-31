import io

p = 'apps/native_poc/src/update_manifest_test.cpp'
d = io.open(p, encoding='utf-8').read()

HASH = '0' * 64
GOOD = 'artifact=GNLinkHost.exe|24|' + HASH + '|https://u.example/h.exe'

# A schema-2 head every case builds on, so a case only shows what it is testing.
helper = '''/** A valid schema-2 head. Cases append or override, so each shows only what it is testing. */
std::string doc_head(const char* platform = "windows", const char* arch = "x64",
                     const char* version = "1.0.0") {
  return std::string("schema=2\\nreleaseId=r-test\\nplatform=") + platform + "\\narch=" + arch +
         "\\nversion=" + version + "\\n";
}
const char* kGoodArtifact =
    "artifact=GNLinkHost.exe|24|''' + HASH + '''|https://u.example/h.exe\\n";

const char* status_name(ManifestStatus s) {'''
old = 'const char* status_name(ManifestStatus s) {'
assert old in d
d = d.replace(old, helper, 1)

# ---- field-rule table
start = d.find('  struct Case {\n    const char* name;\n    const char* doc;\n    ManifestStatus expect;\n  };')
assert start >= 0, 'case struct'
end = d.find('  for (const Case& c : cases) {', start)
assert end > start, 'case loop'
new_table = '''  struct Case {
    const char* name;
    std::string doc;
    ManifestStatus expect;
  };
  const Case cases[] = {
      {"a well formed manifest", doc_head() + kGoodArtifact, ManifestStatus::Ok},
      {"missing schema", std::string("releaseId=r\\nplatform=windows\\narch=x64\\nversion=1\\n") +
                             kGoodArtifact,
       ManifestStatus::Malformed},
      {"unknown schema", std::string("schema=99\\nreleaseId=r\\nplatform=windows\\narch=x64\\n"
                                     "version=1\\n") + kGoodArtifact,
       ManifestStatus::UnsupportedSchema},
      {"schema 1 is no longer accepted",
       std::string("schema=1\\nreleaseId=r\\nplatform=windows\\narch=x64\\nversion=1\\n") +
           kGoodArtifact,
       ManifestStatus::UnsupportedSchema},
      {"missing releaseId",
       std::string("schema=2\\nplatform=windows\\narch=x64\\nversion=1\\n") + kGoodArtifact,
       ManifestStatus::Malformed},
      {"missing arch",
       std::string("schema=2\\nreleaseId=r\\nplatform=windows\\nversion=1\\n") + kGoodArtifact,
       ManifestStatus::Malformed},
      // Not for this machine is not an error, the same way a platform mismatch is not.
      {"a different arch", doc_head("windows", "arm64") + kGoodArtifact,
       ManifestStatus::WrongPlatform},
      {"a different platform", doc_head("android") + kGoodArtifact, ManifestStatus::WrongPlatform},
      {"missing version", std::string("schema=2\\nreleaseId=r\\nplatform=windows\\narch=x64\\n") +
                              kGoodArtifact,
       ManifestStatus::Malformed},
      {"no artifacts at all", doc_head(), ManifestStatus::Malformed},
      {"zero size", doc_head() + "artifact=a.exe|0|" + std::string(64, '0') +
                        "|https://u.example/a\\n",
       ManifestStatus::Malformed},
      {"an artifact larger than the per-file limit",
       doc_head() + "artifact=a.exe|999999999999|" + std::string(64, '0') +
           "|https://u.example/a\\n",
       ManifestStatus::Malformed},
      {"uppercase sha256 rejected",
       doc_head() + "artifact=a.exe|1|ABCDEF" + std::string(58, '0') + "|https://u.example/a\\n",
       ManifestStatus::Malformed},
      {"short sha256 rejected", doc_head() + "artifact=a.exe|1|abcdef|https://u.example/a\\n",
       ManifestStatus::Malformed},
      // The transport boundary is enforced at parse time, not only at fetch time -- so a caller
      // that fetched some other way could not bypass it.
      {"an http:// artifact URL is refused",
       doc_head() + "artifact=a.exe|1|" + std::string(64, '0') + "|http://u.example/a\\n",
       ManifestStatus::Malformed},
      {"a URL with credentials is refused",
       doc_head() + "artifact=a.exe|1|" + std::string(64, '0') +
           "|https://evil@u.example/a\\n",
       ManifestStatus::Malformed},
      {"a malformed artifact line is refused",
       doc_head() + "artifact=a.exe|1|onlythree\\n", ManifestStatus::Malformed},
      {"a non-numeric size is refused",
       doc_head() + "artifact=a.exe|big|" + std::string(64, '0') + "|https://u.example/a\\n",
       ManifestStatus::Malformed},
      {"unknown key is ignored, not rejected",
       doc_head() + "futureField=whatever\\n" + kGoodArtifact, ManifestStatus::Ok},
      {"comments and blank lines are ignored",
       std::string("# a comment\\n\\n") + doc_head() + kGoodArtifact, ManifestStatus::Ok},
  };
'''
d = d[:start] + new_table + d[end:]

# the loop uses c.doc as std::string now
d = d.replace('const ManifestResult r = load_manifest(c.doc, sigHex, "windows", acceptingVerifier);',
              'const ManifestResult r = load_manifest(c.doc, sigHex, "windows", acceptingVerifier);', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('field-rule table moved to schema 2')
