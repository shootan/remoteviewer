import io

# --- header: record the directories the swap had to create
p = 'apps/native_poc/src/update_effects.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  /** Names moved aside during the current swap, so a partial failure can be undone exactly. */
  std::vector<std::wstring> movedAside_;'''
new = '''  /** Names moved aside during the current swap, so a partial failure can be undone exactly. */
  std::vector<std::wstring> movedAside_;
  /**
   * Directories this swap created under installDir, deepest last.
   *
   * A payload name may name a destination in a subdirectory (`ui\\\\shell.html`), and a release
   * that adds a file under a folder that does not exist yet has to be able to place it. Rollback
   * removes these again, so an abandoned update does not leave folders behind that the previous
   * build never had.
   */
  std::vector<std::wstring> createdDirs_;'''
assert old in d, 'hpp member anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# --- implementation
p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''bool file_exists(const std::wstring& path) {'''
new = '''/**
 * Creates every missing directory on the way to `path`'s parent, appending each one it actually
 * created to `created` so a rollback can undo exactly that much.
 *
 * The name behind `path` has already been through check_payload_names, which is what makes this
 * safe to do at all: the relative part cannot climb out of the install directory, name a drive,
 * or carry an alternate data stream. This creates directories under a checked relative path; it
 * does not decide what that path may be.
 */
bool ensure_parent_dirs(const std::wstring& root,
                        const std::wstring& relative,
                        std::vector<std::wstring>* created) {
  std::wstring current = root;
  size_t start = 0;
  for (size_t i = 0; i < relative.size(); ++i) {
    if (relative[i] != L'\\\\' && relative[i] != L'/') continue;
    const std::wstring component = relative.substr(start, i - start);
    start = i + 1;
    if (component.empty()) continue;
    current += L'\\\\';
    current += component;
    if (CreateDirectoryW(current.c_str(), nullptr)) {
      created->push_back(current);
      continue;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
  }
  return true;
}

bool file_exists(const std::wstring& path) {'''
assert old in d, 'cpp helper anchor'
d = d.replace(old, new, 1)

old = '''  movedAside_.clear();
  swapped_ = false;

  // Nothing moves unless a whole release is staged.'''
new = '''  movedAside_.clear();
  createdDirs_.clear();
  swapped_ = false;

  // Nothing moves unless a whole release is staged.'''
assert old in d, 'swap head anchor'
d = d.replace(old, new, 1)

old = '''    const std::wstring live = install_path(name);
    const std::wstring staged = staged_path_for(stagedReleaseId_, name);
    if (!CopyFileW(staged.c_str(), live.c_str(), FALSE)) {'''
new = '''    const std::wstring live = install_path(name);
    const std::wstring staged = staged_path_for(stagedReleaseId_, name);
    // A release may add a file in a folder this install does not have yet.
    if (!ensure_parent_dirs(config_.installDir, name, &createdDirs_)) {
      lastError_ = "could not create the destination folder for " + to_utf8(name);
      (void)Rollback();
      return false;
    }
    if (!CopyFileW(staged.c_str(), live.c_str(), FALSE)) {'''
assert old in d, 'phase two anchor'
d = d.replace(old, new, 1)

old = '''  movedAside_.clear();
  swapped_ = false;

  // The registration goes back to what was captured, not to the version being abandoned.'''
new = '''  movedAside_.clear();
  swapped_ = false;

  // Folders this attempt created are removed deepest first, and only while empty -- a directory
  // that has something else in it was not this update's to take away.
  for (auto it = createdDirs_.rbegin(); it != createdDirs_.rend(); ++it) {
    RemoveDirectoryW(it->c_str());
  }
  createdDirs_.clear();

  // The registration goes back to what was captured, not to the version being abandoned.'''
assert old in d, 'rollback anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('subdirectory destinations supported')
