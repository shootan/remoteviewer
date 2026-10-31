"""rel-0164: the directory tests wait for their server to ANSWER (test/server_ready.js) instead of a fixed sleep or a
5-8 s poll. Exact replacements, each asserted once; only waiting changes (no check, no server code)."""
import io

T = 'C:/work/gnlink-wt/rel-0164/apps/directory/test/'
REQ = "const { waitForServer, waitForOutput } = require('./server_ready');\n"


def edit(name, pairs, add_require_after):
    p = T + name
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    s = raw.decode('utf-8').replace('\r\n', '\n')
    for a, b in pairs:
        n = s.count(a)
        assert n == 1, (name, n, a[:70])
        s = s.replace(a, b)
    if add_require_after:
        assert s.count(add_require_after) == 1, (name, 'require anchor')
        s = s.replace(add_require_after, add_require_after + REQ)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
    print(name, 'ok', len(pairs))


# run.js: the nine fixed 1500 ms sleeps after a server start
p = T + 'run.js'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
blocks = [
    ("  let server = startServer();\n  await sleep(1500);\n",
     "  let server = startServer();\n  await waitForServer(server, env.REMOTE60_DIR_PORT, 'run.js main');\n"),
    ("  server = startServer();\n  await sleep(1500);\n",
     "  server = startServer();\n  await waitForServer(server, env.REMOTE60_DIR_PORT, 'run.js restart');\n"),
]
for a, b in blocks:
    assert s.count(a) == 1, ('run.js', a[:40], s.count(a))
    s = s.replace(a, b)
for envname in ['diagEnv', 'relayEnv', 'standbyEnv', 'observeEnv', 'logEnv', 'updateEnv', 'lanEnv']:
    a = "  server = spawn(process.execPath, [serverPath], { env: %s, stdio: 'ignore' });\n  await sleep(1500);\n" % envname
    b = ("  server = spawn(process.execPath, [serverPath], { env: %s, stdio: 'ignore' });\n"
         "  await waitForServer(server, %s.REMOTE60_DIR_PORT, 'run.js %s');\n") % (envname, envname, envname)
    assert s.count(a) == 1, ('run.js', envname, s.count(a))
    s = s.replace(a, b)
anchor = "const sleep = (ms) => new Promise((r) => setTimeout(r, ms));\n"
assert s.count(anchor) == 1
s = s.replace(anchor, anchor + REQ)
assert 'await sleep(1500)' not in s
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('run.js ok')

# the three 100 x 50 ms (5 s) polls -> the helper's 15 s bound
edit('recovery_fault_test.js', [
    ("  let ready = false;\n  for (let i = 0; i < 100 && !ready; ++i) { try { ready = (await api('/healthz')).status === 200; } catch {} if (!ready) await sleep(50); }\n  assert(ready, 'isolated server starts');\n",
     "  await waitForServer(server, port, 'recovery_fault_test');  // isolated server starts (bounded 15 s, else exit 3)\n"),
], "const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));\n")
edit('device_credential_test.js', [
    ("  let ready = false;\n  for (let i = 0; i < 100 && !ready; ++i) {\n    try { ready = (await api('/healthz')).status === 200; } catch { /* not yet */ }\n    if (!ready) await sleep(50);\n  }\n  assert(ready, 'the server starts');\n}\n",
     "  await waitForServer(server, port, 'device_credential_test');  // the server starts (bounded 15 s, else exit 3)\n}\n"),
], "const sleep = (ms) => new Promise((r) => setTimeout(r, ms));\n")
edit('account_admin_test.js', [
    ("  let ready = false;\n  for (let i = 0; i < 100 && !ready; ++i) {\n    try { ready = (await api('/healthz')).status === 200; } catch { /* not yet */ }\n    if (!ready) await sleep(50);\n  }\n  assert(ready, 'the server starts');\n",
     "  await waitForServer(server, port, 'account_admin_test');  // the server starts (bounded 15 s, else exit 3)\n"),
], "const sleep = (ms) => new Promise((r) => setTimeout(r, ms));\n")

# the two 8 s local polls -> the helper
for name, port, label in [('observe_contract_test.js', 'HTTP', 'observe_contract_test'),
                          ('proxy_contamination_test.js', 'SERVER_HTTP', 'proxy_contamination_test')]:
    p = T + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    i = s.index('function waitForServer() {')
    j = s.index('\n}\n', i) + 3
    s = s[:i] + ("function waitForServer(child) {\n  return require('./server_ready').waitForServer(child, %s, '%s');\n}\n"
                 % (port, label)) + s[j:]
    assert s.count('await waitForServer();') == 1, (name, 'call')
    s = s.replace('await waitForServer();', 'await waitForServer(server);')
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
    print(name, 'ok')

# the fixed sleeps after a spawn
edit('relay_wire_test.js', [
    ("  server.stderr.on('data', (c) => (serverLog += c));\n  await sleep(1500);\n",
     "  server.stderr.on('data', (c) => (serverLog += c));\n  await waitForServer(server, HTTP, 'relay_wire_test ' + MODE);\n"),
], "const sleep = (ms) => new Promise((r) => setTimeout(r, ms));\n")
edit('connect_diag_test.js', [
    ("  try {\n    await sleep(900);\n\n    let r = await api('POST', '/api/login'",
     "  try {\n    await waitForServer(server, HTTP, 'connect_diag_test');\n\n    let r = await api('POST', '/api/login'"),
], "const sleep = (ms) => new Promise((r) => setTimeout(r, ms));\n")
edit('wake_resend_test.js', [
    ("  try {\n    await sleep(900);\n\n    let r = await api('POST', '/api/login'",
     "  try {\n    await waitForServer(server, HTTP, 'wake_resend_test');\n\n    let r = await api('POST', '/api/login'"),
], "const sleep = (ms) => new Promise((r) => setTimeout(r, ms));\n")
edit('np_early_bootstrap_e2e.js', [
    ("  const server = spawn(process.execPath, [serverJs], { env, stdio: 'ignore' });\n  await sleep(1500);\n",
     "  const server = spawn(process.execPath, [serverJs], { env, stdio: 'ignore' });\n  await waitForServer(server, HTTP, 'np_early_bootstrap_e2e ' + serverJs);\n"),
], "const sleep = (ms) => new Promise((r) => setTimeout(r, ms));\n")

# relay_env_bound: it judges the startup OUTPUT -- wait until the server says it listens (bounded), then judge
edit('relay_env_bound_test.js', [
    ("setTimeout(() => {\n  check('a non-number bound falls back",
     "waitForOutput(() => out, '[relay] listening on udp', 'relay_env_bound_test').then(() => {\n  check('a non-number bound falls back"),
    ("    process.exit(failures ? 1 : 0);\n  });\n}, 2500);",
     "    process.exit(failures ? 1 : 0);\n  });\n});"),
], "const testTmp = require('./test_tmp');  // r5 (Codex N4): only inside REMOTE60_TEST_TMP\n")
p = T + 'relay_env_bound_test.js'
s = open(p, encoding='utf-8').read()
print('relay_env_bound tail:', repr(s[-260:]))
