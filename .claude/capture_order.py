import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''    Result r;
    r.outcome = outcome;'''
new='''    // EVERYTHING the assertions read is captured HERE, before this run's cleanup touches
    // anything. It has to be: the sweep below terminates processes and the tree is removed at the
    // end, so a check that looked afterwards could see a file freed or a process gone because
    // THIS TEST tidied up, and report it as the product having restored or stopped something.
    // "It is not running now" is only evidence if nothing in between was trying to make that true.
    Result r;
    r.outcome = outcome;'''
assert old in d,'capture point'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('capture point marked')
