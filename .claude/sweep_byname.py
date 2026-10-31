import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
i=d.find('std::vector<std::pair<std::wstring, DWORD>> running_under(const std::wstring& dir) {')
j=d.find('\n}\n',i)
assert i!=-1 and j!=-1,'running_under'
new='''std::vector<std::pair<std::wstring, DWORD>> running_under(const std::wstring& dir) {
  std::vector<std::pair<std::wstring, DWORD>> found;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return found;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      // Identified from the SNAPSHOT, with no OpenProcess.
      //
      // This used to open each process to read its full image path, and skip any it could not
      // open -- which turned out to be exactly the processes it existed to stop. They were listed
      // in the snapshot by name the whole time; the sweep simply could not see them, reported
      // nothing running, and left them holding their images. The next scenario then could not
      // write its starting bytes, and failed an assertion about bytes with no process visible
      // anywhere to explain it.
      //
      // A prefix match on the name, because a swap renames what it replaces to
      // `<name>.gnlink-old` and a process running that file keeps running under the new name.
      // These two names appear nowhere else on a machine, which is what makes a name enough.
      const std::wstring leaf = entry.szExeFile;
      const auto starts_with = [&leaf](const wchar_t* name) {
        const size_t n = wcslen(name);
        return leaf.size() >= n && _wcsnicmp(leaf.c_str(), name, n) == 0;
      };
      (void)dir;
      if (starts_with(kHostName) || starts_with(kClientName)) {
        found.push_back({leaf, entry.th32ProcessID});
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}'''
d=d[:i]+new+d[j+2:]
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('the sweep sees by name')
