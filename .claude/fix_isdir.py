import io

BS = chr(92)

# restore the production code
p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()
d = d.replace('    if (false && !ensure_parent_dirs(config_.installDir, name, &createdDirs_)) {',
              '    if (!ensure_parent_dirs(config_.installDir, name, &createdDirs_)) {', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/update_release_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''bool exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}'''
new = '''bool exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

/**
 * A directory that is actually there.
 *
 * Written out rather than inlined because the obvious inline version is wrong in a way that
 * passes: INVALID_FILE_ATTRIBUTES is 0xFFFFFFFF, so `GetFileAttributesW(p) & FILE_ATTRIBUTE_
 * DIRECTORY` is true for a path that does not exist at all. Two assertions here were written that
 * way and only showed it when the folder creation they were checking was deliberately disabled.
 */
bool is_directory(const std::wstring& path) {
  const DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}'''
assert old in d, 'exists anchor'
d = d.replace(old, new, 1)

old = ('      check("R5: the folder that did not exist was created",\n'
       '            GetFileAttributesW((install + L"@@plugins@@nested").c_str()) & '
       'FILE_ATTRIBUTE_DIRECTORY);').replace('@@', BS * 2)
new = ('      check("R5: the folder that did not exist was created",\n'
       '            is_directory(install + L"@@plugins@@nested"));').replace('@@', BS * 2)
assert old in d, 'R5 created-folder anchor'
d = d.replace(old, new, 1)

old = ('      check("R5: the folder the installation already had is still there",\n'
       '            GetFileAttributesW((install + L"@@ui").c_str()) & '
       'FILE_ATTRIBUTE_DIRECTORY);').replace('@@', BS * 2)
new = ('      check("R5: the folder the installation already had is still there",\n'
       '            is_directory(install + L"@@ui"));').replace('@@', BS * 2)
assert old in d, 'R5 existing-folder anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('is_directory in place')
