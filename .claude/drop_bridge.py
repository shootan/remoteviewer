import io

# --- the bridge itself
p = 'apps/native_poc/src/update_manifest.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''
  // Transitional bridge, and marked as one. The effects layer still stages and verifies a single
  // file, so when a release happens to contain exactly one artifact its identity is mirrored into
  // the legacy fields that layer reads. This goes away when staging consumes artifacts[] -- and
  // deliberately does NOT fire for a multi-artifact release, so nothing can silently update using
  // only the first file of several.
  if (fields.artifacts.size() == 1) {
    fields.artifact = fields.artifacts[0].name;
    fields.size = fields.artifacts[0].size;
    fields.sha256 = fields.artifacts[0].sha256;
  }
'''
assert old in d, 'bridge anchor'
d = d.replace(old, '', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# --- the fields it fed
p = 'apps/native_poc/src/update_manifest.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  std::string version;
  // Single-artifact fields, kept for the effects layer that has not been migrated to artifacts[]
  // yet. Schema 2 does NOT parse them from the document -- they are set by callers that still
  // work one file at a time, and they will go when the staging path consumes the list.
  std::string artifact;
  uint64_t size = 0;
  std::string sha256;          // lowercase hex, 64 characters
  uint64_t androidVersionCode = 0;  // 0 when absent; only meaningful for platform=android'''
new = '''  std::string version;
  uint64_t androidVersionCode = 0;  // 0 when absent; only meaningful for platform=android'''
assert old in d, 'fields anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# --- the one test that still sets it
p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()
d = d.replace('  f.artifact = "test.bin";\n', '', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('transitional bridge removed')
