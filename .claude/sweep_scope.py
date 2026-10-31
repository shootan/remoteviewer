import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''      (void)dir;
      if (starts_with(kHostName) || starts_with(kClientName)) {
        found.push_back({leaf, entry.th32ProcessID});
      }'''
new='''      if (!starts_with(kHostName) && !starts_with(kClientName)) continue;
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
assert old in d,'name match'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('sweep scoped by directory where it can be, inclusive where it cannot')
