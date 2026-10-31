import io
p='apps/native_poc/src/update_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='''      if (!moved) {
        // The swap below will fail on the taken name, and it would report only that the file
        // would not move. That is the symptom; this is the cause, and without it an operator
        // reads "could not move aside" and looks at the wrong file.
        lastError_ = "an older backup of " + to_utf8(name) +
                     " cannot be deleted and there is nowhere left to put it (" +
                     std::to_string(kMaxStaleBackups) + " already set aside)";
      }
    }'''
new='''      if (!moved) {
        // Carried to the failure below rather than written to lastError_ here, because the
        // move-aside about to fail would overwrite it -- leaving the operator the symptom ("the
        // file would not move") and none of the cause. That is exactly what happened the first
        // time this was written.
        blocked = " -- an older backup of the same name cannot be deleted and there is nowhere "
                  "left to put it (" + std::to_string(kMaxStaleBackups) + " already set aside)";
      }
    }'''
assert old in d,'cap block'
d=d.replace(old,new,1)

old='''    const std::wstring backup = backup_path(name);
    // A backup from an earlier update'''
new='''    const std::wstring backup = backup_path(name);
    // Why the name could not be freed, when it could not be. Empty in the ordinary case.
    std::string blocked;
    // A backup from an earlier update'''
assert old in d,'backup decl'
d=d.replace(old,new,1)

old='''      lastError_ = "could not move aside " + to_utf8(name) + " (error " +
                   std::to_string(GetLastError()) + ")";'''
new='''      lastError_ = "could not move aside " + to_utf8(name) + " (error " +
                   std::to_string(GetLastError()) + ")" + blocked;'''
assert old in d,'move aside failure'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('the cause travels with the symptom')
