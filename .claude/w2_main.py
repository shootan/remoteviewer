import io

BS = chr(92)
p = 'apps/native_poc/installer/installer_main.cpp'
d = io.open(p, encoding='utf-8').read()

# --- payload entry
old = '''    {IDR_PAYLOAD_CLIENT_VIEWER, L"GNLinkViewer.exe"},'''
new = '''    {IDR_PAYLOAD_CLIENT_VIEWER, L"GNLinkViewer.exe"},
    // Installed here like anything else. It runs from a copy of itself in a sibling directory, so
    // the file sitting here is never the one executing and a later update can replace it. Putting
    // it outside the install directory instead would make it unreplaceable -- payload names are
    // relative and refuse traversal -- and uninstall would orphan it.
    {IDR_PAYLOAD_UPDATER, L"GNLinkUpdater.exe"},'''
assert old in d, 'payload anchor'
d = d.replace(old, new, 1)

# --- the work directory, named next to the install folder name
old = '''constexpr wchar_t kSetupFileName[] = L"GNLinkSetup.exe";'''
new = '''constexpr wchar_t kSetupFileName[] = L"GNLinkSetup.exe";
/**
 * Where the updater runs from: a SIBLING of the install folder, not a child.
 *
 * A child would be inside the directory being replaced, which the updater refuses outright. And
 * it has to be somewhere only administrators can write, because what runs from here runs
 * elevated -- a user-writable location leaves a window between the copy and the launch in which
 * the binary can be swapped. Under %ProgramFiles% that is already true by the default ACL, so
 * there is nothing to set up and therefore nothing to forget. %TEMP% and %ProgramData% were
 * excluded for exactly that reason (design survey, history #427).
 */
constexpr wchar_t kUpdaterWorkFolderName[] = L"GNLink.update";'''
assert old in d, 'setup name anchor'
d = d.replace(old, new, 1)

# --- helper beside install_dir()
old = '''int do_install() {'''
new = '''/** %ProgramFiles%@@GNLink.update -- see kUpdaterWorkFolderName. Empty when it cannot be resolved. */
std::wstring updater_work_dir() {
  const std::wstring base = program_files_dir();
  if (base.empty()) return {};
  return base + L"@@" + kUpdaterWorkFolderName;
}

int do_install() {'''.replace('@@', BS * 2)
assert old in d, 'do_install anchor'
d = d.replace(old, new, 1)

# --- create it during install
old = '''  // Keep the installer itself alongside the product so Add/Remove Programs has something to run.'''
new = '''  // Created now rather than by the updater on first use, so it exists with the ACL it inherits
  // from %ProgramFiles% before anything is ever written into it. Not fatal if it fails -- the
  // updater creates it too, and an update that cannot run is better than an install that stops.
  {
    const std::wstring workDir = updater_work_dir();
    if (!workDir.empty() && !CreateDirectoryW(workDir.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
      // Recorded, not reported: the user came here to install the product, and this folder only
      // matters the first time an update runs.
      OutputDebugStringW(L"GNLink: could not create the updater working directory\\n");
    }
  }

  // Keep the installer itself alongside the product so Add/Remove Programs has something to run.'''
assert old in d, 'install work dir anchor'
d = d.replace(old, new, 1)

# --- remove it during uninstall
old = '''  (void)RemoveDirectoryW(directory.c_str());
  (void)RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, KEY_WOW64_64KEY, 0);'''
new = '''  (void)RemoveDirectoryW(directory.c_str());

  // The updater's working copy lives outside the install directory, so removing that directory
  // does not take it with it. An update running right now still holds its copy open, which is why
  // the same delete-after-reboot fallback is used here as for the installer's own image.
  {
    const std::wstring workDir = updater_work_dir();
    if (!workDir.empty()) {
      WIN32_FIND_DATAW found{};
      HANDLE search = FindFirstFileW((workDir + L"@@*").c_str(), &found);
      if (search != INVALID_HANDLE_VALUE) {
        do {
          const std::wstring name = found.cFileName;
          if (name == L"." || name == L"..") continue;
          const std::wstring path = workDir + L"@@" + name;
          if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            (void)MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
          }
        } while (FindNextFileW(search, &found));
        FindClose(search);
      }
      if (!RemoveDirectoryW(workDir.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        (void)MoveFileExW(workDir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
      }
    }
  }

  (void)RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, KEY_WOW64_64KEY, 0);'''.replace('@@', BS * 2)
assert old in d, 'uninstall anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('installer wired for the updater')
