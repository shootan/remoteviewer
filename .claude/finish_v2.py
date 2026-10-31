import io

# ---- ManifestFields: mark the single-artifact fields as what they now are
p = 'apps/native_poc/src/update_manifest.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  std::string artifact;
  uint64_t size = 0;
  std::string sha256;          // lowercase hex, 64 characters'''
new = '''  // Single-artifact fields, kept for the effects layer that has not been migrated to artifacts[]
  // yet. Schema 2 does NOT parse them from the document -- they are set by callers that still
  // work one file at a time, and they will go when the staging path consumes the list.
  std::string artifact;
  uint64_t size = 0;
  std::string sha256;          // lowercase hex, 64 characters'''
assert old in d
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---- effects test fixture: schema 2 shape
p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''ManifestFields artifact_fields() {
  ManifestFields f;
  f.schema = 1;'''
new = '''ManifestFields artifact_fields() {
  ManifestFields f;
  f.schema = 2;
  f.releaseId = "r-0.2.105";
  f.arch = "x64";'''
assert old in d
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('fixtures updated')
