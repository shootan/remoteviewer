import io
p='apps/native_poc/src/update_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='''    const std::wstring backup = backup_path(name);
    DeleteFileW(backup.c_str());
    if (!MoveFileExW(live.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      lastError_ = "could not move aside " + to_utf8(name);
      (void)Rollback();
      return false;
    }'''
new='''    const std::wstring backup = backup_path(name);
    // A backup from an earlier update may still be sitting on this name. Normally it deletes and
    // the question does not arise; when it will not, the whole update used to stop here -- a file
    // that is only litter blocking the next release from being installed at all.
    //
    // So it is moved out of the name instead. RENAMING a file nothing can delete is allowed --
    // that asymmetry is the reason the swap works this way at all, since it renames images that
    // are running -- and it costs one more piece of litter to keep the update possible. The
    // survivor is recorded, not forgotten, for the same reason the commit records its own.
    if (!DeleteFileW(backup.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
      for (int n = 1; n <= 50; ++n) {
        const std::wstring aside = backup + L"." + std::to_wstring(n);
        if (GetFileAttributesW(aside.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        if (MoveFileExW(backup.c_str(), aside.c_str(), 0)) {
          orphanedBackups_.push_back(name);
          lastError_ = "an older backup of " + to_utf8(name) +
                       " could not be deleted and was moved to " + to_utf8(aside);
        }
        break;
      }
    }
    if (!MoveFileExW(live.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      lastError_ = "could not move aside " + to_utf8(name) + " (error " +
                   std::to_string(GetLastError()) + ")";
      (void)Rollback();
      return false;
    }'''
assert old in d,'move aside'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('a stale backup no longer blocks the next update')
