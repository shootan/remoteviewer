import io
import re

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

# 1) size/hash mismatch cases must mutate the ARTIFACT, not the retired single fields
d = d.replace('''    ManifestFields wrongSize = f;
    wrongSize.size = f.size + 1;''',
              '''    ManifestFields wrongSize = f;
    wrongSize.artifacts[0].size = f.artifacts[0].size + 1;''', 1)
d = d.replace('''    ManifestFields wrongHash = f;
    wrongHash.sha256 = std::string(64, 'a');''',
              '''    ManifestFields wrongHash = f;
    wrongHash.artifacts[0].sha256 = std::string(64, 'a');''', 1)

# 2) the end-to-end manifest documents have to list the files the swap replaces, because staging
#    is now keyed to the release AND checked against payloadNames before anything moves.
pattern = re.compile(
    r'"schema=2\\nreleaseId=r-([0-9.]+)\\nplatform=windows\\narch=x64\\nversion=\1\\n'
    r'artifact=test\.bin\|" \+\s*\n\s*std::to_string\(artifact_fields\(\)\.size\) \+ "\|" \+ '
    r'gArtifactSha \+ "\|https://u\.example/a\\n"')
def repl(m):
    ver = m.group(1)
    return ('manifest_for(' + '"' + ver + '"' + ', {L"AlphaPayload.bin", L"BetaPayload.bin"})')
d, n1 = pattern.subn(repl, d)
print('end-to-end documents rewritten:', n1)

# a helper that builds a matching document from the payload names
old = '''ManifestFields artifact_fields('''
new = '''/**
 * A signed-shaped document listing exactly the files a swap will replace.
 *
 * The staging path checks that every payload name is present in the staged release before it
 * moves anything, so a document naming other files is not merely inconvenient -- it is refused,
 * which is the behaviour worth having.
 */
std::string manifest_for(const std::string& version, const std::vector<std::wstring>& names) {
  std::string doc = "schema=2\\nreleaseId=r-" + version +
                    "\\nplatform=windows\\narch=x64\\nversion=" + version + "\\n";
  for (const std::wstring& n : names) {
    const std::string narrow(n.begin(), n.end());
    doc += "artifact=" + narrow + "|" +
           std::to_string(std::char_traits<char>::length(kArtifactBytes)) + "|" + gArtifactSha +
           "|https://u.example/" + narrow + "\\n";
  }
  return doc;
}

ManifestFields artifact_fields('''
assert old in d, 'helper anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('done')
