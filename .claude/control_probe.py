import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      const BOOL deletedAfter = DeleteFileW(victim.c_str());'''
new='''      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      CloseHandle(pi.hProcess);
      const BOOL deletedAfter = DeleteFileW(victim.c_str());'''
assert old in d
d=d.replace(old,new,1)
d=d.replace('''      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
  }''','''      CloseHandle(pi.hThread);
    }
  }''',1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('probe: close the process handle before deleting')
