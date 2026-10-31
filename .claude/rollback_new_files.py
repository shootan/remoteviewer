import io

p = 'apps/native_poc/src/update_effects.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  /** Names moved aside during the current swap, so a partial failure can be undone exactly. */
  std::vector<std::wstring> movedAside_;'''
new = '''  /** Names moved aside during the current swap, so a partial failure can be undone exactly. */
  std::vector<std::wstring> movedAside_;
  /**
   * Names this swap actually placed.
   *
   * Not the same list as movedAside_, and the difference is the point: a release that ADDS a file
   * places something that has no backup, so "put back what was moved aside" would leave it there.
   * A rollback has to remove those too, or an abandoned update still changes the installation.
   */
  std::vector<std::wstring> placed_;'''
assert old in d
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  movedAside_.clear();
  createdDirs_.clear();
  swapped_ = false;'''
new = '''  movedAside_.clear();
  createdDirs_.clear();
  placed_.clear();
  swapped_ = false;'''
assert old in d
d = d.replace(old, new, 1)

old = '''      lastError_ = "could not place " + to_utf8(name);
      (void)Rollback();
      return false;
    }
  }'''
new = '''      lastError_ = "could not place " + to_utf8(name);
      (void)Rollback();
      return false;
    }
    placed_.push_back(name);
  }'''
assert old in d
d = d.replace(old, new, 1)

old = '''  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    const std::wstring backup = backup_path(name);
    const bool hasBackup =
        std::find(movedAside_.begin(), movedAside_.end(), name) != movedAside_.end();
    if (!hasBackup) continue;'''
new = '''  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    const std::wstring backup = backup_path(name);
    const bool hasBackup =
        std::find(movedAside_.begin(), movedAside_.end(), name) != movedAside_.end();
    if (!hasBackup) {
      // Nothing was moved aside for this name, so if something is there now, this attempt put it
      // there and it did not exist before. Removing it is what restores the installation.
      const bool wasPlaced = std::find(placed_.begin(), placed_.end(), name) != placed_.end();
      if (wasPlaced && file_exists(live) && !DeleteFileW(live.c_str())) ok = false;
      continue;
    }'''
assert old in d
d = d.replace(old, new, 1)

old = '''  movedAside_.clear();
  swapped_ = false;

  // Folders this attempt created'''
new = '''  movedAside_.clear();
  placed_.clear();
  swapped_ = false;

  // Folders this attempt created'''
assert old in d
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('rollback removes newly added files')
