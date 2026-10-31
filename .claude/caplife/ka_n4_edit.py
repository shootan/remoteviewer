base = 'C:/work/gnlink-wt/ka-r3/apps/directory/test/'

def edit(rel, pairs):
    p = base + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new, count in pairs:
        assert s.count(old) == count, (rel, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('run.js', [
    ("const os = require('os');\n", "const testTmp = require('./test_tmp');  // r5 (Codex N4): every file of the suite under REMOTE60_TEST_TMP\n", 1),
    ("const dataPath = path.join(os.tmpdir(), `remote60-directory-test-${process.pid}.json`);\nconst env = { ...process.env, REMOTE60_DIR_DATA: dataPath,",
     "const testRoot = testTmp.root();\nconst dataPath = testTmp.file(`remote60-directory-test-${process.pid}.json`);\n"
     "// The children's os.tmpdir() is the test root too (TEMP/TMP on Windows, TMPDIR elsewhere), and the scratch-folder\n"
     "// tests read REMOTE60_TEST_TMP: nothing the suite starts writes or deletes outside it.\n"
     "const env = { ...process.env, REMOTE60_TEST_TMP: testRoot, TEMP: testRoot, TMP: testRoot, TMPDIR: testRoot,\n"
     "              REMOTE60_DIR_DATA: dataPath,", 1),
    ("fs.rmSync(dataPath, { force: true });", "testTmp.remove(dataPath);", 2),
    ("path.join(os.tmpdir(), `remote60-directory-logs-${process.pid}`)", "testTmp.file(`remote60-directory-logs-${process.pid}`)", 1),
    ("fs.rmSync(logDir, { recursive: true, force: true });", "testTmp.remove(logDir, { recursive: true });", 2),
    ("path.join(os.tmpdir(), `remote60-update-${process.pid}`)", "testTmp.file(`remote60-update-${process.pid}`)", 1),
    ("fs.rmSync(updateDir, { recursive: true, force: true });", "testTmp.remove(updateDir, { recursive: true });", 2),
])

edit('relay_env_bound_test.js', [
    ("const os = require('os');\n", "const testTmp = require('./test_tmp');  // r5 (Codex N4): only inside REMOTE60_TEST_TMP\n", 1),
    ("path.join(os.tmpdir(), `remote60-envbound-${process.pid}.json`)", "testTmp.file(`remote60-envbound-${process.pid}.json`)", 1),
    ("fs.rmSync(dataPath, { force: true });", "testTmp.remove(dataPath);", 1),
])

for rel in ('recovery_fault_test.js', 'relay_wire_test.js', 'account_admin_test.js', 'device_credential_test.js'):
    edit(rel, [
        ("const scratchRoot = path.join(root, '.claude', 'test-tmp');",
         "// r5 (Codex N4): under the suite's test root when the runner names one, else this worktree's .claude/test-tmp.\n"
         "const scratchRoot = process.env.REMOTE60_TEST_TMP ? path.resolve(process.env.REMOTE60_TEST_TMP) : path.join(root, '.claude', 'test-tmp');", 1),
    ])
print('ok')

for rel in ('recovery_fault_test.js', 'account_admin_test.js', 'device_credential_test.js', 'relay_wire_test.js'):
    edit(rel, [("assert(!path.relative(root, scratch).startsWith('..'));", "assert(!path.relative(scratchRoot, scratch).startsWith('..'));", 1)])
edit('relay_wire_test.js', [("  if (path.relative(root, scratch).startsWith('..')) return;   // never outside the worktree",
                             "  if (path.relative(scratchRoot, scratch).startsWith('..')) return;   // never outside the scratch root", 1)])
print('ok2')
