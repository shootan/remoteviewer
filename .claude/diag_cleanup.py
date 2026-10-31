import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
i=d.find('''      // By NAME only, straight out of the snapshot -- no OpenProcess.''')
j=d.find('''      HANDLE probe = CreateFileW(path.c_str()''')
assert i!=-1 and j!=-1 and j>i,'diag block'
d=d[:i]+d[j:]
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('duplicate probe removed; the open-for-write reason stays')
