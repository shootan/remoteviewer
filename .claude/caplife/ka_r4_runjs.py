p = 'C:/work/gnlink-wt/ka-r3/apps/directory/test/run.js'
B = chr(92)
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
old = ("  const standbyEnv = { ...relayEnv, REMOTE60_RELAY_HOST_FACING_IP: '127.0.0.1',\n"
       "                       REMOTE60_RELAY_HOST_FACING_BIND: '127.0.0.1' };")
new = ("  const standbyEnv = { ...relayEnv, REMOTE60_RELAY_HOST_FACING_IP: '127.0.0.1',\n"
       "                       REMOTE60_RELAY_HOST_FACING_BIND: '127.0.0.1',\n"
       "                       // r4: every test client is 127.0.0.1 and the tester account -- raised so the per-host\n"
       "                       // bound (8) is what the cases meet\n"
       "                       REMOTE60_RELAY_STANDBY_MAX_PER_ACCOUNT: '200', REMOTE60_RELAY_STANDBY_MAX_PER_WIRE_IP: '200' };")
assert s.count(old) == 1
s = s.replace(old, new)
old2 = ("    const child = spawn(process.execPath, [path.join(__dirname, 'relay_standby_test.js')],\n"
        "                        { env: standbyEnv, stdio: 'inherit' });\n"
        "    child.on('exit', (code) => resolve(code ?? 1));\n"
        "  });\n")
new2 = ("    const child = spawn(process.execPath, [path.join(__dirname, 'relay_standby_test.js')],\n"
        "                        { env: standbyEnv, stdio: 'inherit' });\n"
        "    child.on('exit', (code) => resolve(code ?? 1));\n"
        "  }) || await new Promise((resolve) => {\n"
        "    // r4 (Codex N1, N3): the first pin needs the host's proof; the eight hold either way round\n"
        "    const child = spawn(process.execPath, [path.join(__dirname, 'relay_standby_r4_test.js')],\n"
        "                        { env: standbyEnv, stdio: 'inherit' });\n"
        "    child.on('exit', (code) => resolve(code ?? 1));\n"
        "  }) || await new Promise((resolve) => {\n"
        "    // r4 (Codex N3): bad bounds in the environment fall back (its own server, own ports)\n"
        "    const child = spawn(process.execPath, [path.join(__dirname, 'relay_env_bound_test.js')],\n"
        "                        { env, stdio: 'inherit' });\n"
        "    child.on('exit', (code) => resolve(code ?? 1));\n"
        "  });\n")
assert s.count(old2) == 1
s = s.replace(old2, new2)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

q = 'D:/remote/remote/.claude/caplife/run_standby.js'
t = open(q, encoding='utf-8').read()
t = t.replace("dir = 'C:\\\\work\\\\gnlink-wt\\\\ka-r3\\\\apps\\\\directory'", "dir = 'C:\\\\work\\\\gnlink-wt\\\\ka-r3\\\\apps\\\\directory'")
o3 = "REMOTE60_RELAY_HOST_FACING_IP: '127.0.0.1', REMOTE60_RELAY_HOST_FACING_BIND: '127.0.0.1' };"
assert t.count(o3) == 1
t = t.replace(o3, "REMOTE60_RELAY_HOST_FACING_IP: '127.0.0.1', REMOTE60_RELAY_HOST_FACING_BIND: '127.0.0.1',\n              REMOTE60_RELAY_STANDBY_MAX_PER_ACCOUNT: '200', REMOTE60_RELAY_STANDBY_MAX_PER_WIRE_IP: '200' };")
o4 = "const child = spawn(process.execPath, [path.join(dir, 'test', 'relay_standby_test.js')], { env, stdio: 'inherit' });"
assert t.count(o4) == 1
t = t.replace(o4, "const child = spawn(process.execPath, [path.join(dir, 'test', process.argv[3] || 'relay_standby_test.js')], { env, stdio: 'inherit' });")
open(q, 'w', encoding='utf-8').write(t)
print('ok')
