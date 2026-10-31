import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  const auto seed = [&]() {
    write_file(install + L"\\\\" + kHostName, oldBody);
    write_file(install + L"\\\\" + kClientName, oldBody);
    // After the sweep above, nothing is running from these, so they really can go. A backup left
    // behind blocks the next move-aside and the swap fails for a reason that has nothing to do
    // with the scenario.
    DeleteFileW((install + L"\\\\" + kHostName + L".gnlink-old").c_str());
    DeleteFileW((install + L"\\\\" + kClientName + L".gnlink-old").c_str());
    DeleteFileW(log.c_str());
  };'''
new='''  const auto seed = [&]() {
    // The backups go FIRST, and the wait is the point.
    //
    // A leftover `<name>.gnlink-old` blocks the next move-aside -- the swap replaces it, and a
    // file that will not delete will not be replaced either -- so the scenario after it fails for
    // a reason that has nothing to do with what it tests. That is not hypothetical: scenarios 3
    // through 6 failed exactly this way, reporting "the old bytes are back" and "nothing on disk
    // changed", while the actual cause was one file left over from scenario 2.
    //
    // The delete is retried because the lock is released on its own, just not immediately, and
    // ignoring the result is what turned a few seconds of waiting into four misleading failures.
    for (const std::wstring& name : {kHostName, kClientName}) {
      const std::wstring backup = install + L"\\\\" + name + L".gnlink-old";
      for (int attempt = 0; attempt < 100; ++attempt) {
        if (DeleteFileW(backup.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) break;
        Sleep(50);
      }
      // Said out loud rather than left to become the next scenario's failure.
      check("the previous scenario left nothing that blocks this one",
            GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES, narrow(backup));
    }
    write_file(install + L"\\\\" + kHostName, oldBody);
    write_file(install + L"\\\\" + kClientName, oldBody);
    DeleteFileW(log.c_str());
  };'''
assert old in d,'seed'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('seed waits for the previous scenario to let go')
