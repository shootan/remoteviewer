import io
BS=chr(92)
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''      check("this scenario starts from the old bytes", read_file(path) == oldBody,
            narrow(name) + " is " + std::to_string(read_file(path).size()) + " bytes");'''
new='''      std::string who;
      for (const auto& pair : running_under(install)) {
        who += narrow(pair.first) + "#" + std::to_string(pair.second) + " ";
      }
      HANDLE probe = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
      const DWORD openErr = (probe == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
      if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
      check("this scenario starts from the old bytes", read_file(path) == oldBody,
            narrow(name) + " is " + std::to_string(read_file(path).size()) +
                " bytes; open-for-write said " + std::to_string(openErr) + "; running: " + who);'''
assert old in d,'seed check'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('diagnostic added')
