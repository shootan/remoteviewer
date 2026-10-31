P = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/directory/test/run.js'
CR = chr(13)
s = open(P, encoding='utf-8', newline='').read()
crlf = (CR + '\n') in s
if crlf:
    s = s.replace(CR + '\n', '\n')
a = '''  await stopServer(server);
  if (relay !== 0) {
    cleanup();
    console.log('\\nRESULT: FAILED');
    process.exit(relay);
  }
'''
assert s.count(a) == 1, s.count(a)
b = a + '''
  // apk-decoder-liveness r3 (Codex D2): the same relay run with the TEST trace on (REMOTE60_RELAY_TRACE=1, the local AVD
  // fixture's setting): it says it is on, and each flow's direction / kind counts and last times appear at the sweep --
  // and the relay still works with it (relay_test passes). With it off (the run above) nothing is counted.
  console.log('\\n--- relay with the test trace (REMOTE60_RELAY_TRACE=1) ---');
  server = spawn(process.execPath, [serverPath], { env: { ...relayEnv, REMOTE60_RELAY_TRACE: '1' },
                                                    stdio: ['ignore', 'pipe', 'inherit'] });
  let traceOut = '';
  server.stdout.on('data', (c) => (traceOut += c));
  await sleep(1500);
  const relayTraced = await new Promise((resolve) => {
    const child = spawn(process.execPath, [path.join(__dirname, 'relay_test.js')],
                        { env: relayEnv, stdio: 'inherit' });
    child.on('exit', (code) => resolve(code ?? 1));
  });
  await sleep(6000);  // one sweep (5 s) writes the snapshots
  await stopServer(server);
  const traceLines = traceOut.split(/\\r?\\n/).filter((l) => l.startsWith('[relay-trace]'));
  const enabled = traceLines.some((l) => l.includes('ENABLED'));
  const flows = traceLines.filter((l) => / connect=\\S+ (snap|close) /.test(l));
  const counted = flows.some((l) => /c2hRx=\\d+/.test(l) && /h2cRx=\\d+/.test(l));
  const noPayload = !traceOut.includes('outsider-pass') && !traceOut.includes('test-pass-1234');
  console.log(`${relayTraced === 0 ? 'PASS' : 'FAIL'}  the relay works with the trace on (relay_test exit ${relayTraced})`);
  console.log(`${enabled ? 'PASS' : 'FAIL'}  the trace says it is on`);
  console.log(`${counted ? 'PASS' : 'FAIL'}  flows are traced both ways (c2h and h2c last times)  lines=${flows.length}`);
  console.log(`${noPayload ? 'PASS' : 'FAIL'}  no credential in the trace output`);
  if (flows.length) console.log('  e.g. ' + flows[flows.length - 1].slice(0, 400));
  if (relayTraced !== 0 || !enabled || !counted || !noPayload) {
    cleanup();
    console.log('\\nRESULT: FAILED');
    process.exit(1);
  }
'''
s = s.replace(a, b)
s = s.replace('\n', CR + '\n') if crlf else s
open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok run.js')
