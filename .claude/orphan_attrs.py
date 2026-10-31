import io
p='apps/native_poc/src/update_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='''      names += to_utf8(stuck[i]) + " (error " + std::to_string(reasons[i]) + ")";'''
new='''      const DWORD attrs = GetFileAttributesW(backup_path(stuck[i]).c_str());
      names += to_utf8(stuck[i]) + " (error " + std::to_string(reasons[i]) + ", attrs " +
               std::to_string(attrs) + ")";'''
assert old in d
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('attrs too')
