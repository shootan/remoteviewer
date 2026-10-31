import io
BS=chr(92)
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()

old='''      for (int attempt = 0; attempt < 100; ++attempt) {
        if (DeleteFileW(backup.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) break;
        Sleep(50);
      }'''
new='''      for (int attempt = 0; attempt < 20; ++attempt) {
        if (DeleteFileW(backup.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) break;
        // A file that will not go is a file something is running. Sweeping again is the answer,
        // because the thing running it is a shell-routed launch that appeared after the previous
        // sweep had already stopped looking -- waiting alone never clears it.
        stop_everything_under(install);
      }'''
assert old in d,'backup loop'
d=d.replace(old,new,1)

old='''      for (int attempt = 0; attempt < 100; ++attempt) {
        write_file(path, oldBody);
        if (read_file(path) == oldBody) break;
        Sleep(50);
      }'''
new='''      for (int attempt = 0; attempt < 20; ++attempt) {
        write_file(path, oldBody);
        if (read_file(path) == oldBody) break;
        // Same reason as above: the file cannot be overwritten because a process launched by the
        // previous scenario is running it, and it turned up after that scenario's final sweep.
        stop_everything_under(install);
      }'''
assert old in d,'write loop'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('seed sweeps when a file will not budge')
