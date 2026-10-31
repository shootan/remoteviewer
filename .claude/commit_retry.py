import io
p='apps/native_poc/src/update_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  std::vector<std::wstring> stuck;
  std::vector<DWORD> reasons;
  for (const std::wstring& name : movedAside_) {
    const std::wstring backup = backup_path(name);
    if (DeleteFileW(backup.c_str())) continue;
    const DWORD why = GetLastError();
    if (why == ERROR_FILE_NOT_FOUND) continue;  // already gone is the desired state
    stuck.push_back(name);
    reasons.push_back(why);
  }'''
new='''  std::vector<std::wstring> stuck;
  std::vector<DWORD> reasons;
  for (const std::wstring& name : movedAside_) {
    const std::wstring backup = backup_path(name);
    DWORD why = 0;
    // Briefly retried, because the usual reason a backup will not delete is transient and
    // measured in milliseconds: a process that WAS running it has exited, and Windows has not yet
    // released the image section. Deleting a file that is a running image is refused outright
    // (renaming one is not, which is how the backup came to exist at all), and for a moment after
    // the last thread ends it is still refused. Giving up on the first attempt turned that moment
    // into a permanent file beside the installation.
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (DeleteFileW(backup.c_str())) { why = 0; break; }
      why = GetLastError();
      if (why == ERROR_FILE_NOT_FOUND) break;  // already gone is the desired state
      Sleep(50);
    }
    if (why == 0 || why == ERROR_FILE_NOT_FOUND) continue;
    // Still there after a second of asking. Not transient, so it is recorded rather than waited
    // on: something holds it, and blocking an update on a file that is only litter would be the
    // worse trade.
    stuck.push_back(name);
    reasons.push_back(why);
  }'''
assert old in d,'commit loop'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('bounded retry before recording')
