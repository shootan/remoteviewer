import io

p = 'apps/native_poc/src/update_release_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = ('    c.payloadNames = {L"GNLinkHost.exe", L"ui' + chr(92) * 2 + 'shell.html", '
       'L"GNLinkSetup.exe",\n                      L"ui' + chr(92) * 2 + 'new_panel.html"};')
new = ('    // The added file comes FIRST, and the file whose copy is blocked comes LAST. That order\n'
       '    // is the whole case. Blocking the added file itself proves nothing: its copy never\n'
       '    // runs, so there is nothing on disk to clean up and "the added file is not there"\n'
       '    // holds whether or not Rollback does anything. That is exactly how the first version\n'
       '    // of this case was written, and disabling the removal in Rollback did not fail it.\n'
       '    c.payloadNames = {L"ui' + chr(92) * 2 + 'new_panel.html", L"GNLinkHost.exe",\n'
       '                      L"ui' + chr(92) * 2 + 'shell.html", L"GNLinkSetup.exe"};')
assert old in d, 'payload order anchor'
d = d.replace(old, new, 1)

old = ('            stagedLast = staging + L"' + chr(92) * 2 + '" + name + L"' + chr(92) * 2 +
       'ui_new_panel.html";')
new = ('            stagedBlocked = staging + L"' + chr(92) * 2 + '" + name + L"' + chr(92) * 2 +
       'GNLinkSetup.exe";')
assert old in d, 'staged path anchor'
d = d.replace(old, new, 1)
d = d.replace('    std::wstring stagedLast;\n', '    std::wstring stagedBlocked;\n', 1)
d = d.replace('check("R4c: the staged file to block was found", exists(stagedLast), '
              'narrow(stagedLast));',
              'check("R4c: the staged file to block was found", exists(stagedBlocked),\n'
              '          narrow(stagedBlocked));', 1)
d = d.replace('HANDLE held = CreateFileW(stagedLast.c_str()', 'HANDLE held = '
              'CreateFileW(stagedBlocked.c_str()', 1)

old = '''    // Phase two copies from the staged files. Holding the LAST one open with no sharing makes its
    // copy fail after the earlier three have already been placed -- CopyFileW cannot open a source
    // nobody is allowed to read.'''
new = '''    // Phase two copies from the staged files. Holding the last one open with no sharing makes its
    // copy fail after the earlier three -- including the added file -- have already been placed;
    // CopyFileW cannot open a source nobody is allowed to read.'''
assert old in d, 'phase two comment anchor'
d = d.replace(old, new, 1)

old = '''    check("R4c: and it names the file it could not place",
          e.last_error().find("could not place") != std::string::npos &&
              e.last_error().find("new_panel.html") != std::string::npos,
          e.last_error());'''
new = '''    check("R4c: and it names the file it could not place",
          e.last_error().find("could not place") != std::string::npos &&
              e.last_error().find("GNLinkSetup.exe") != std::string::npos,
          e.last_error());'''
assert old in d, 'error name anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('reordered')
