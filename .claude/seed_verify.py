import io
BS=chr(92)
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
d=d.replace('''    check("3 both fail: the old bytes are back  LOG=" + r.log + " ERR=" + r.effectsError''','''    check("3 both fail: the old bytes are back"''',1)

old='''    write_file(install + L"'''+BS*2+'''" + kHostName, oldBody);
    write_file(install + L"'''+BS*2+'''" + kClientName, oldBody);
    DeleteFileW(log.c_str());
  };'''
new='''    // Written, and then READ BACK.
    //
    // write_file cannot overwrite a file something is still running, and it said nothing when it
    // failed -- so a scenario could begin on the previous one's bytes and then fail an assertion
    // about bytes, which is what "3 both fail: the old bytes are back" was really reporting. The
    // starting state is either established or said out loud; it is never assumed.
    for (const std::wstring& name : {kHostName, kClientName}) {
      const std::wstring path = install + L"'''+BS*2+'''" + name;
      for (int attempt = 0; attempt < 100; ++attempt) {
        write_file(path, oldBody);
        if (read_file(path) == oldBody) break;
        Sleep(50);
      }
      check("this scenario starts from the old bytes", read_file(path) == oldBody,
            narrow(name) + " is " + std::to_string(read_file(path).size()) + " bytes");
    }
    DeleteFileW(log.c_str());
  };'''
assert old in d,'seed write'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('seed verifies the starting state')
