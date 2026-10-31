import io

p = 'apps/native_poc/src/update_manifest.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  result.status = ManifestStatus::Ok;
  result.manifest = ManifestLoader::make(std::move(fields));
  return result;
}'''
new = '''  // Transitional bridge, and marked as one. The effects layer still stages and verifies a single
  // file, so when a release happens to contain exactly one artifact its identity is mirrored into
  // the legacy fields that layer reads. This goes away when staging consumes artifacts[] -- and
  // deliberately does NOT fire for a multi-artifact release, so nothing can silently update using
  // only the first file of several.
  if (fields.artifacts.size() == 1) {
    fields.artifact = fields.artifacts[0].name;
    fields.size = fields.artifacts[0].size;
    fields.sha256 = fields.artifacts[0].sha256;
  }

  result.status = ManifestStatus::Ok;
  result.manifest = ManifestLoader::make(std::move(fields));
  return result;
}'''
assert old in d, 'tail anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('single-artifact bridge added')
