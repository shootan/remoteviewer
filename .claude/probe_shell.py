import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''    stop_everything_under(install);
    seed();'''
new='''    stop_everything_under(install);
    if (getenv("GNLINK_SCN_NO_SHELL")) set_shell_launch_disabled_for_test(true);
    seed();'''
assert old in d
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('probe added')
