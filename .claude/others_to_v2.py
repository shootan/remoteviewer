import io
import re

H = '0' * 64

# A schema-2 document builder used by the other suites' fixtures.
V2_DOC = ('"schema=2\\nreleaseId=r-test\\nplatform=windows\\narch=x64\\nversion=0.2.105\\n"\n'
          '    "artifact=GNLinkHost.exe|3475968|' + H + '|https://u.example/h.exe\\n"')

# ---------------------------------------------------------------- state machine test
p = 'apps/native_poc/src/update_state_machine_test.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''const char* kDocument =
    "schema=1\\n"
    "platform=windows\\n"
    "version=0.2.105\\n"
    "artifact=GNLinkSetup-0.2.105.exe\\n"
    "size=3475968\\n"
    "sha256=039b3a47d36850292c2049400719ddeb0b17fd39f50ce73dec445e6d5d4dff2f\\n";'''
new = ('''const char* kDocument =
    "schema=2\\n"
    "releaseId=r-0.2.105\\n"
    "platform=windows\\n"
    "arch=x64\\n"
    "version=0.2.105\\n"
    "artifact=GNLinkSetup.exe|3475968|''' + H + '''|https://u.example/s.exe\\n";''')
assert old in d, 'state machine document'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('state machine test moved to schema 2')

# ---------------------------------------------------------------- update check test
p = 'apps/native_poc/src/update_check_test.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''const char* kDocument =
    "schema=1\\n"
    "platform=windows\\n"
    "version=0.2.105\\n"
    "artifact=GNLinkSetup-0.2.105.exe\\n"
    "size=3475968\\n"
    "sha256=039b3a47d36850292c2049400719ddeb0b17fd39f50ce73dec445e6d5d4dff2f\\n";'''
new = ('''const char* kDocument =
    "schema=2\\n"
    "releaseId=r-0.2.105\\n"
    "platform=windows\\n"
    "arch=x64\\n"
    "version=0.2.105\\n"
    "artifact=GNLinkSetup.exe|3475968|''' + H + '''|https://u.example/s.exe\\n";''')
assert old in d, 'check test document'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('update check test moved to schema 2')

# ---------------------------------------------------------------- effects test fixtures
p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()
# every inline set_manifest document
pattern = re.compile(
    r'"schema=1\\nplatform=windows\\nversion=([0-9.]+)\\nartifact=test\.bin\\nsize=" \+\s*'
    r'std::to_string\(artifact_fields\(\)\.size\) \+ "\\nsha256=" \+ gArtifactSha \+ "\\n"')
def repl(m):
    ver = m.group(1)
    return ('"schema=2\\nreleaseId=r-' + ver + '\\nplatform=windows\\narch=x64\\nversion=' + ver +
            '\\nartifact=test.bin|" +\n                       std::to_string(artifact_fields().size)'
            ' + "|" + gArtifactSha + "|https://u.example/a\\n"')
d, n = pattern.subn(repl, d)
print('effects test documents rewritten:', n)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
