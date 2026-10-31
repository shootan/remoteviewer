import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''        check("a backup that survived is one something is still running from", held,
              narrow(image) + " -- running: " + running);'''
new='''        check("a backup that survived is one something is still running from", held,
              narrow(image) + " -- running: " + running + " -- effects said: " + r.effectsError);'''
assert old in d
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('ok')
