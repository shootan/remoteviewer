import io
p='apps/native_poc/src/update_effects_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''    check("a swap with nowhere left to put the stuck backup fails", !e.Swap(), e.last_error());
    check("...and nothing was left replaced",'''
new='''    check("a swap with nowhere left to put the stuck backup fails", !e.Swap(), e.last_error());
    // The cause travels with the symptom. "Could not move aside" on its own sends an operator to
    // look at the payload; the file that is actually stuck is the backup behind it.
    check("...and names the cause, not just the file that would not move",
          e.last_error().find("nowhere left to put it") != std::string::npos, e.last_error());
    check("...and nothing was left replaced",'''
assert old in d,'cap assert'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('assertion added')
