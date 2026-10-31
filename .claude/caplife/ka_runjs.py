p = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\directory\\test\\run.js'
B = chr(92)
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
fail = "    console.log('" + B + "nRESULT: FAILED');\n"
old = ("  await stopServer(server);\n  if (relay !== 0) {\n    cleanup();\n" + fail + "    process.exit(relay);\n  }\n")
new = old + (
    "\n"
    "  // keepalive-r2 relay decision (a): standby leases, each with its own host-facing socket (loopback-bound),\n"
    "  // beside the legacy lease. Its own server: the reserved sockets are counted per process.\n"
    "  console.log('" + B + "n--- relay standby leases (keepalive-r2 (a)) ---');\n"
    "  const standbyEnv = { ...relayEnv, REMOTE60_RELAY_HOST_FACING_IP: '127.0.0.1',\n"
    "                       REMOTE60_RELAY_HOST_FACING_BIND: '127.0.0.1' };\n"
    "  server = spawn(process.execPath, [serverPath], { env: standbyEnv, stdio: 'ignore' });\n"
    "  await sleep(1500);\n"
    "  const standby = await new Promise((resolve) => {\n"
    "    const child = spawn(process.execPath, [path.join(__dirname, 'relay_standby_test.js')],\n"
    "                        { env: standbyEnv, stdio: 'inherit' });\n"
    "    child.on('exit', (code) => resolve(code ?? 1));\n"
    "  });\n"
    "  await stopServer(server);\n"
    "  if (standby !== 0) {\n"
    "    cleanup();\n" + fail +
    "    process.exit(standby);\n"
    "  }\n")
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
