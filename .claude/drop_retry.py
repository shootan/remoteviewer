import io
p='apps/native_poc/src/update_effects.cpp'
d=io.open(p,encoding='utf-8').read()
i=d.find('''    DWORD why = 0;
    // Briefly retried''')
j=d.find('''    stuck.push_back(name);
    reasons.push_back(why);''',i)
assert i!=-1 and j!=-1,'retry block'
new='''    // Not retried. It was, for a second, and it changed nothing -- the failure below is not the
    // transient "the image section has not been released yet" that a retry would clear. What the
    // second did do was slow every commit down and shift the timing of the scenarios around it,
    // which is a cost paid for no result.
    if (DeleteFileW(backup.c_str())) continue;
    const DWORD why = GetLastError();
    if (why == ERROR_FILE_NOT_FOUND) continue;  // already gone is the desired state
'''
d=d[:i]+new+d[j:]
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('retry removed')
