import io

BS = chr(92)
p = 'apps/native_poc/src/updater_options_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '      L"--work-dir",          L"C:@@ProgramData@@GNLink@@work",'.replace('@@', BS * 2)
new = ('      // The real location: a sibling of the install directory under %ProgramFiles%, where\n'
       '      // the default ACL is already administrators-only. %TEMP% and %ProgramData% are\n'
       '      // user-writable and were excluded by design (history #427).\n'
       '      L"--work-dir",          L"C:@@Program Files@@GNLink.update",').replace('@@', BS * 2)
assert old in d, 'work dir fixture'
d = d.replace(old, new, 1)

old = '              parsed.options.workDir == L"C:@@ProgramData@@GNLink@@work" &&'.replace('@@', BS * 2)
new = '              parsed.options.workDir == L"C:@@Program Files@@GNLink.update" &&'.replace('@@', BS * 2)
assert old in d, 'work dir assertion'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('fixture uses the real location')
