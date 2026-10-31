import io

BSC = "L'" + chr(92) + chr(92) + "'"      # C++ literal for a backslash char
BSS = 'L"' + chr(92) + chr(92) + '"'      # C++ literal for a backslash string

p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ------------------------------------------------------------------ 2) where the run lives
old = '''std::wstring temp_root() {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  wchar_t path[MAX_PATH]{};
  swprintf(path, MAX_PATH, L"%sgnlink-scn-%lu", base, GetCurrentProcessId());
  CreateDirectoryW(path, nullptr);
  return path;
}'''
new = '''/** The configured root, with separators normalised. Never guessed at runtime. */
std::wstring scn_root_base() {
  std::wstring base = GNLINK_SCN_ROOT;
  for (wchar_t& c : base) {
    if (c == L'/') c = ''' + BSC + ''';
  }
  return base;
}

/**
 * Where this run puts its files: INSIDE the repository, never %TEMP%.
 *
 * It used to build its tree under GetTempPath and then delete everything matching
 * `gnlink-scn-*` there on startup. That is deleting outside the repository root, which the
 * top-level rule forbids outright -- and permission to run a test is not permission to remove
 * files elsewhere on the machine. That the pattern happened to match only this suite's own output
 * is not the point; the point is that nothing here gets to decide that about a directory it does
 * not own.
 *
 * The root comes from the build (GNLINK_SCN_ROOT, from CMAKE_SOURCE_DIR), so it cannot drift.
 */
std::wstring run_root() {
  const std::wstring base = scn_root_base();
  // Component by component, because the parents may not exist and this must not assume they do.
  std::wstring built;
  for (size_t i = 0; i < base.size(); ++i) {
    built.push_back(base[i]);
    if (base[i] == ''' + BSC + ''' || i + 1 == base.size()) CreateDirectoryW(built.c_str(), nullptr);
  }
  wchar_t leaf[64]{};
  swprintf(leaf, 64, L"scn-%lu", GetCurrentProcessId());
  const std::wstring path = base + ''' + BSS + ''' + leaf;
  CreateDirectoryW(path.c_str(), nullptr);
  return path;
}

/** True when `path` really is inside this suite's own root. Nothing is removed without it. */
bool inside_run_root(const std::wstring& path) {
  const std::wstring base = scn_root_base();
  if (base.empty() || path.size() <= base.size()) return false;
  return _wcsnicmp(path.c_str(), base.c_str(), base.size()) == 0;
}'''
assert old in d, 'temp_root'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ 1) who may be stopped
old = '''      if (!starts_with(kHostName) && !starts_with(kClientName)) continue;
      // Scoped by directory when the directory can be established, and INCLUDED when it cannot.
      //
      // The path needs a handle, and a process this cannot open is precisely the one that was
      // being missed -- so an unreadable path is not grounds for leaving something alone. It is
      // the other way round: known-elsewhere is the only reason to skip. Scenario 6 relies on
      // that, running a stub under the same name outside the install directory so the sweep
      // leaves it to be the process that will not quiesce.
      bool mine = true;
      if (!dir.empty()) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (h) {
          wchar_t image[MAX_PATH]{};
          DWORD size = MAX_PATH;
          if (QueryFullProcessImageNameW(h, 0, image, &size)) {
            const std::wstring path = image;
            mine = path.size() >= dir.size() &&
                   _wcsnicmp(path.c_str(), dir.c_str(), dir.size()) == 0;
          }
          CloseHandle(h);
        }
      }
      if (mine) found.push_back({leaf, entry.th32ProcessID});'''
new = '''      if (!starts_with(kHostName) && !starts_with(kClientName)) continue;
      // The path must be READ, and it must be under `dir`. A name is not ownership.
      //
      // This defaulted to "mine" when the path could not be read, so a process this could not
      // identify was terminated on the strength of its file name -- and "nothing else on this
      // machine is called ScnHost.exe" is a guess about the whole machine, not evidence about one
      // process. A second run of this same suite would have been killed by the first. A PID can
      // also be reused between the snapshot and the terminate, so the name is not even evidence
      // about the PID.
      //
      // Unreadable is UNKNOWN now, and unknown is never terminated -- it is reported, which is
      // the same rule the product follows for a liveness check that cannot be completed.
      if (dir.empty()) continue;  // an empty scope is not a narrow scope, it is no scope
      HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
      if (!h) continue;
      wchar_t image[MAX_PATH]{};
      DWORD size = MAX_PATH;
      const bool readPath = QueryFullProcessImageNameW(h, 0, image, &size) != FALSE;
      CloseHandle(h);
      if (!readPath) continue;
      const std::wstring path = image;
      if (path.size() < dir.size() || _wcsnicmp(path.c_str(), dir.c_str(), dir.size()) != 0) {
        continue;  // same name, somewhere else. Not ours, and not touched.
      }
      found.push_back({path, entry.th32ProcessID});'''
assert old in d, 'running_under scope'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ report what cannot be placed
old = '''/** Stops everything running out of `dir`. Only this test's temp tree is ever passed here. */'''
new = '''/**
 * Processes carrying this suite's names that could NOT be placed -- no handle, or no path.
 *
 * Reported, never stopped. These are exactly the ones the sweep used to kill on the strength of a
 * name; whatever they are, this run has no evidence that they belong to it.
 */
std::vector<DWORD> named_but_unidentified() {
  std::vector<DWORD> unknown;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return unknown;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      const std::wstring leaf = entry.szExeFile;
      const auto starts_with = [&leaf](const wchar_t* name) {
        const size_t n = wcslen(name);
        return leaf.size() >= n && _wcsnicmp(leaf.c_str(), name, n) == 0;
      };
      if (!starts_with(kHostName) && !starts_with(kClientName)) continue;
      HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
      if (!h) {
        unknown.push_back(entry.th32ProcessID);
        continue;
      }
      wchar_t image[MAX_PATH]{};
      DWORD size = MAX_PATH;
      if (!QueryFullProcessImageNameW(h, 0, image, &size)) unknown.push_back(entry.th32ProcessID);
      CloseHandle(h);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return unknown;
}

/** Stops what is running out of `dir` -- and only what was confirmed to be running out of it. */'''
assert old in d, 'stop_everything doc'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ startup: no global sweep, no deletion
old = '''void remove_stale_runs() {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  const std::wstring pattern = std::wstring(base) + L"gnlink-scn-*";
  std::vector<std::wstring> stale;
  WIN32_FIND_DATAW find{};
  HANDLE h = FindFirstFileW(pattern.c_str(), &find);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = find.cFileName;
      if (name == L"." || name == L"..") continue;
      stale.push_back(std::wstring(base) + name);
    } while (FindNextFileW(h, &find));
    FindClose(h);
  }
  for (const std::wstring& dir : stale) remove_tree(dir);
}'''
new = '''void report_leftovers() {
  // READ ONLY. Nothing in here deletes anything.
  //
  // This used to enumerate %TEMP% for `gnlink-scn-*` and remove_tree every match -- deleting
  // outside the repository root, which is forbidden, on the authority of a filename pattern.
  // Earlier runs did leave directories there. They are LISTED so that a person can decide, and
  // that decision is not this program's to make.
  const auto list = [](const std::wstring& pattern, const std::wstring& base, const char* where) {
    WIN32_FIND_DATAW find{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &find);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
      const std::wstring name = find.cFileName;
      if (name == L"." || name == L"..") continue;
      std::cout << "NOTE  left by an earlier run, NOT removed (" << where
                << "): " << narrow(base + name) << "\\n";
    } while (FindNextFileW(h, &find));
    FindClose(h);
  };

  const std::wstring base = scn_root_base() + ''' + BSS + ''';
  list(base + L"scn-*", base, "this suite's own root");

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  // Where this suite used to write. Named so the older litter is visible, and left alone.
  list(std::wstring(temp) + L"gnlink-scn-*", temp, "%TEMP%, from before this moved into the repo");
}'''
assert old in d, 'remove_stale_runs'
d = d.replace(old, new, 1)

old = '''  // Previous runs first: their leftovers are inert copies of a shell, but litter that grows is
  // still litter, and this file has already been the source of some.
  stop_everything_under(L"");
  remove_stale_runs();

  const std::wstring root = temp_root();'''
new = '''  // What earlier runs left is LISTED, not swept and not deleted.
  //
  // The sweep used to run with an empty scope -- `stop_everything_under(L"")` -- which is not a
  // narrow scope but the absence of one: every process on the machine whose name began with one
  // of these two, whoever had started it. Nothing here has any business doing that.
  report_leftovers();

  const std::wstring root = run_root();'''
assert old in d, 'main startup'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ final cleanup stays inside the root
old = '''  stop_everything_under(root);
  Sleep(2000);
  stop_everything_under(root);
  remove_tree(root);'''
new = '''  stop_everything_under(root);
  Sleep(2000);
  stop_everything_under(root);
  // Only this run's own directory, and only after checking that is what it is. A path that does
  // not sit under the configured root is left alone, however it came to be passed here.
  if (inside_run_root(root)) {
    remove_tree(root);
  } else {
    std::cout << "NOTE  refusing to remove " << narrow(root)
              << " -- it is not under this suite's root\\n";
  }
  for (DWORD pid : named_but_unidentified()) {
    std::cout << "NOTE  named like this suite's fixtures but not identifiable, so left running: "
              << "pid " << pid << "\\n";
  }'''
assert old in d, 'final cleanup'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('run root moved into the repo; the sweep confirms before it stops anything')
