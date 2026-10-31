import io

BSS = 'L"' + chr(92) + chr(92) + '"'
p = 'apps/native_poc/src/update_relaunch_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ------------------------------------------------------------------ inside the repository
old = '''std::wstring make_temp_dir_named(const wchar_t* tag) {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  wchar_t unique[MAX_PATH]{};
  swprintf(unique, MAX_PATH, L"%sgnlink-exec-%lu-%s", base, GetCurrentProcessId(), tag);
  CreateDirectoryW(unique, nullptr);
  return unique;
}'''
new = '''/**
 * A directory for this run's fixtures, inside the repository.
 *
 * It was under %TEMP%. Nothing here deletes anything it did not create, so that was not the same
 * violation the scenario suite had -- but the reason to move is the one that matters more: this
 * test fires launches through the user's shell, and the files it leaves are the files a late
 * request will look for. They belong where this project can see them.
 */
std::wstring make_run_dir_named(const wchar_t* tag) {
  std::wstring base = GNLINK_EXEC_ROOT;
  for (wchar_t& c : base) {
    if (c == L'/') c = L''' + chr(92) + chr(92) + '''';
  }
  std::wstring built;
  for (size_t i = 0; i < base.size(); ++i) {
    built.push_back(base[i]);
    if (base[i] == L''' + chr(92) + chr(92) + '''' || i + 1 == base.size()) {
      CreateDirectoryW(built.c_str(), nullptr);
    }
  }
  wchar_t leaf[128]{};
  swprintf(leaf, 128, L"exec-%lu-%s", GetCurrentProcessId(), tag);
  const std::wstring unique = base + ''' + BSS + ''' + leaf;
  CreateDirectoryW(unique.c_str(), nullptr);
  return unique;
}'''
assert old in d, 'make_temp_dir_named'
d = d.replace(old, new, 1)
d = d.replace('make_temp_dir_named(', 'make_run_dir_named(')

# ------------------------------------------------------------------ teardown waits for the launch
old = '''    // The dummies exit on their own, but a .cmd still running holds its file open and the
    // directory will not go. Give them a moment rather than leaving litter in %TEMP%.
    Sleep(500);
    DeleteFileW(witness.c_str());
    remove_tree_flat(root);'''
new = '''    // A launch routed through the shell is performed by explorer, later, and nothing here can ask
    // whether it has happened yet. Removing these files while a request is still in flight is
    // what puts "Windows cannot find ..." on the user's desktop -- from a test, on a machine
    // somebody is using. That happened, from this suite's sibling.
    //
    // So the client's witness line is waited for, and if it never arrives the directory STAYS.
    // A late request then finds a real dummy, which writes its line and exits. Litter inside the
    // repository is a small price; a dialog on somebody's screen is not something to trade
    // against it.
    bool landed = false;
    for (int waited = 0; waited < 30000 && !landed; waited += 250) {
      if (read_witness(witness).find("DummyClient.cmd") != std::string::npos) landed = true;
      if (!landed) Sleep(250);
    }
    if (landed) {
      DeleteFileW(witness.c_str());
      remove_tree_flat(root);
    } else {
      std::cout << "NOTE  a shell-routed launch never recorded itself; keeping " << narrow(root)
                << " so a late request finds a real file rather than a dialog\\n";
    }'''
assert old in d, 'teardown'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ------------------------------------------------------------------ the build supplies the root
p = 'apps/native_poc/CMakeLists.txt'
c = io.open(p, encoding='utf-8').read()
old = '''target_include_directories(remote60_update_relaunch_test PRIVATE src)'''
new = '''# Inside the repository: this suite fires launches through the user's shell, and the files it
# leaves behind are the ones a late request will look for.
target_compile_definitions(remote60_update_relaunch_test
  PRIVATE GNLINK_EXEC_ROOT=L"${CMAKE_SOURCE_DIR}/.claude/relaunch-runs")
target_include_directories(remote60_update_relaunch_test PRIVATE src)'''
assert old in c, 'relaunch test include'
c = c.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(c)
print('relaunch test: inside the repo, and it waits before removing anything')
