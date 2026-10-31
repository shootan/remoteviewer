import io
BS=chr(92)
p='apps/native_poc/src/update_effects_test.cpp'
d=io.open(p,encoding='utf-8').read()
bad=[('L"'+BS+'AlphaPayload.bin"','L"'+BS*2+'AlphaPayload.bin"'),
     ('L"'+BS+'AlphaPayload.bin.gnlink-old"','L"'+BS*2+'AlphaPayload.bin.gnlink-old"')]
n=0
for a,b in bad:
    n+=d.count(a); d=d.replace(a,b)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('fixed',n,'mangled paths')
