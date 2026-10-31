base = 'C:/work/gnlink-wt/ka-r3/apps/directory/test/'
spec = {
    'recovery_fault_test.js': 'server-recovery-',
    'relay_wire_test.js': 'relay-wire-',
    'account_admin_test.js': 'account-admin-',
    'device_credential_test.js': 'device-credential-',
}
for f, prefix in spec.items():
    p = base + f
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    old = ("// r5 (Codex N4): under the suite's test root when the runner names one, else this worktree's .claude/test-tmp.\n"
           "const scratchRoot = process.env.REMOTE60_TEST_TMP ? path.resolve(process.env.REMOTE60_TEST_TMP) : path.join(root, '.claude', 'test-tmp');\n"
           "fs.mkdirSync(scratchRoot, { recursive: true });\n"
           "const scratch = fs.mkdtempSync(path.join(scratchRoot, '%s'));\n"
           "assert(!path.relative(scratchRoot, scratch).startsWith('..'));\n") % prefix
    new = ("// r6 (Codex N4 b): the suite's checked test root only (test_tmp.js) -- no raw variable, no worktree fallback;\n"
           "// run alone without REMOTE60_TEST_TMP, this stops before writing anything.\n"
           "const testTmp = require('./test_tmp');\n"
           "const scratchRoot = testTmp.root();\n"
           "const scratch = testTmp.scratchDir('%s');\n") % prefix
    assert s.count(old) == 1, (f, s.count(old))
    s = s.replace(old, new)
    for o, n in [("fs.rmSync(scratch, { recursive: true, force: true })", "testTmp.remove(scratch, { recursive: true })")]:
        s = s.replace(o, n)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))
    print(f, 'ok', s.count('testTmp.remove(scratch'))
