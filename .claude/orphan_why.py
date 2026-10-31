import io
p='apps/native_poc/src/update_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  std::vector<std::wstring> stuck;
  for (const std::wstring& name : movedAside_) {
    const std::wstring backup = backup_path(name);
    if (DeleteFileW(backup.c_str())) continue;
    if (GetLastError() == ERROR_FILE_NOT_FOUND) continue;  // already gone is the desired state
    stuck.push_back(name);
  }'''
new='''  std::vector<std::wstring> stuck;
  std::vector<DWORD> reasons;
  for (const std::wstring& name : movedAside_) {
    const std::wstring backup = backup_path(name);
    if (DeleteFileW(backup.c_str())) continue;
    const DWORD why = GetLastError();
    if (why == ERROR_FILE_NOT_FOUND) continue;  // already gone is the desired state
    stuck.push_back(name);
    reasons.push_back(why);
  }'''
assert old in d,'commit loop'
d=d.replace(old,new,1)
old='''    std::string names;
    for (const std::wstring& name : stuck) {
      if (!names.empty()) names += ", ";
      names += to_utf8(name);
    }'''
new='''    std::string names;
    for (size_t i = 0; i < stuck.size(); ++i) {
      if (!names.empty()) names += ", ";
      // The error code, because "it did not delete" and "why it did not delete" are different
      // facts and only the second one can be acted on. 32 is a sharing violation -- something has
      // it open; 5 is access denied -- typically a process is RUNNING it.
      names += to_utf8(stuck[i]) + " (error " + std::to_string(reasons[i]) + ")";
    }'''
assert old in d,'commit message'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('commit says why')
