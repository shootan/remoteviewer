import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
i=d.find('      // And WHY. A file cannot be deleted while a process is running it')
j=d.find('    }\n    return r;',i)
assert i!=-1 and j!=-1,'reason block'
new='''      // And the reason, as far as it is known. A surviving backup is recorded WITH the error the
      // delete returned, because "it did not delete" cannot be acted on and "it returned 5" can.
      //
      // What that 5 means here is not settled. ACCESS_DENIED on a plain archive file (attrs 32)
      // is what Windows returns for a running image -- but no process is running this one at the
      // moment the snapshot is taken, and a second of retries does not clear it. The obvious
      // explanation, that something exited and the image section had not been released yet, does
      // not survive the retry. So the assertion is about what is recorded, not about a cause
      // nobody has established; the open question is in the ledger rather than hidden in a check
      // that would have to be written vaguely enough to pass.
      for (const std::wstring& name : r.orphaned) {
        check("a surviving backup carries the reason the delete failed",
              r.effectsError.find(narrow(name)) != std::string::npos &&
                  r.effectsError.find("error ") != std::string::npos,
              r.effectsError);
      }
'''
d=d[:i]+new+d[j:]
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('assert what is known, not what is guessed')
