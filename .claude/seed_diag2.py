import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''      std::string who;
      for (const auto& pair : running_under(install)) {
        who += narrow(pair.first) + "#" + std::to_string(pair.second) + " ";
      }'''
new='''      std::string who;
      for (const auto& pair : running_under(install)) {
        who += narrow(pair.first) + "#" + std::to_string(pair.second) + " ";
      }
      // By NAME only, straight out of the snapshot -- no OpenProcess. running_under needs a
      // handle to read the image path, and anything it cannot open is invisible to it, which is
      // also invisible to the sweep that is supposed to stop it.
      {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
          PROCESSENTRY32W e{};
          e.dwSize = sizeof(e);
          if (Process32FirstW(snap, &e)) {
            do {
              const std::wstring leaf = e.szExeFile;
              if (leaf.rfind(L"Scn", 0) == 0) {
                who += "[byname " + narrow(leaf) + "#" + std::to_string(e.th32ProcessID) + "]";
              }
            } while (Process32NextW(snap, &e));
          }
          CloseHandle(snap);
        }
      }'''
assert old in d
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('name-only probe added')
