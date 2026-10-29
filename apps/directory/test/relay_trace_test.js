// apk-decoder-liveness r3 (Codex design r3 D2): the relay's TEST trace (REMOTE60_RELAY_TRACE=1, the local AVD
// fixture's setting). A throwaway server on spare ports, run twice through relay_test.js (a fake host and a fake phone
// talking real packets): with the trace OFF it writes no trace line; with it ON it says so, every flow's direction /
// kind counts and last times appear (at the 5 s sweep or at the close), credentials never do -- and the relay works
// the same both ways (relay_test passes).
//
//   REMOTE60_TEST_TMP=D:/remote/remote/.claude/test-tmp/<name> node test/relay_trace_test.js
const { spawn, spawnSync } = require('child_process');
const path = require('path');
const testTmp = require('./test_tmp');

const serverPath = path.join(__dirname, '..', 'server.js');
const testRoot = testTmp.root();
const dataPath = testTmp.file(`remote60-relay-trace-test-${process.pid}.json`);
const env = { ...process.env, REMOTE60_TEST_TMP: testRoot, TEMP: testRoot, TMP: testRoot, TMPDIR: testRoot,
              REMOTE60_DIR_DATA: dataPath, REMOTE60_DIR_PORT: '18090', REMOTE60_DIR_UDP_PORT: '18091',
              REMOTE60_DIR_SIGNUP_KEY: 'test-signup-key', T_PORT: '18090', T_UDP: '18091',
              REMOTE60_RELAY_ENABLED: '1', REMOTE60_RELAY_IP: '127.0.0.1', REMOTE60_RELAY_PORT: '18453',
              REMOTE60_RELAY_GRACE_MS: '400', REMOTE60_RELAY_ALLOW_IPS: '127.0.0.1',
              REMOTE60_RELAY_ALLOW_ACCOUNTS: 'tester', T_RELAY: '18453' };

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

async function runOnce(trace) {
  testTmp.remove(dataPath);
  spawnSync(process.execPath, [serverPath, '--add-account', 'tester', 'test-pass-1234'], { env, stdio: 'ignore' });
  spawnSync(process.execPath, [serverPath, '--add-account', 'outsider', 'outsider-pass-1234'], { env, stdio: 'ignore' });
  const serverEnv = trace ? { ...env, REMOTE60_RELAY_TRACE: '1' } : env;
  const server = spawn(process.execPath, [serverPath], { env: serverEnv, stdio: ['ignore', 'pipe', 'inherit'] });
  let out = '';
  server.stdout.on('data', (c) => (out += c));
  await sleep(1500);
  const code = await new Promise((resolve) => {
    const child = spawn(process.execPath, [path.join(__dirname, 'relay_test.js')], { env, stdio: 'inherit' });
    child.on('exit', (c) => resolve(c ?? 1));
  });
  await sleep(6000);  // one sweep (5 s)
  await new Promise((resolve) => {
    server.on('exit', () => resolve());
    server.kill();
  });
  testTmp.remove(dataPath);
  return { code, out };
}

(async () => {
  console.log('--- the relay with the trace OFF ---');
  const off = await runOnce(false);
  check('the relay works (relay_test)', off.code === 0, `exit ${off.code}`);
  check('no trace line is written', !off.out.includes('[relay-trace]'));

  console.log('\n--- the relay with the trace ON (REMOTE60_RELAY_TRACE=1) ---');
  const on = await runOnce(true);
  const lines = on.out.split(/\r?\n/).filter((l) => l.startsWith('[relay-trace]'));
  const flows = lines.filter((l) => / connect=\S+ (snap|close) /.test(l));
  check('the relay works the same (relay_test)', on.code === 0, `exit ${on.code}`);
  check('the trace says it is on', lines.some((l) => l.includes('ENABLED')));
  check('flows are traced both ways (a c2h and an h2c last time)',
        flows.some((l) => /c2hRx=\d+/.test(l) && /h2cRx=\d+/.test(l)), `lines=${flows.length}`);
  check('the clocks are named (mono, mono0, wall, wall0)',
        flows.length > 0 && flows.every((l) => / mono=\d+ mono0=\d+ /.test(l) && / wall=\S+/.test(l) && / wall0=\S+/.test(l)));
  const closes = flows.filter((l) => / close reason=/.test(l));
  check('a closed flow carries its 1 s buckets by direction and kind',
        closes.length === 0 || closes.some((l) => /buckets=\[\d+:\S*(c2h|h2c)\.(rx|sent)\./.test(l)),
        `closes=${closes.length}`);
  check('no credential in the output', !on.out.includes('test-pass-1234') && !on.out.includes('outsider-pass-1234'));
  // apk-decoder-inbox r2: the bytes that arrived, per direction -- KNOWN lengths: relay_test.js relays only 49-byte
  // datagrams (Hello / HelloAck / punch, HELLO_BYTES) and 1200-byte opaque media, so each flow's last snapshot must say
  // bytes = 49 x a + 1200 x b with a + b = packets (a, b >= 0 whole numbers) -- and media (b >= 1) was counted
  const byFlow = new Map();
  for (const l of flows) {
    const m = / connect=(\S+) .* rxBytes c2h=(\d+)\/(\d+) h2c=(\d+)\/(\d+) /.exec(l);
    if (m) byFlow.set(m[1], m.slice(2).map(Number));
  }
  const totals = [...byFlow.values()].reduce((a, v) => a.map((x, i) => x + v[i]), [0, 0, 0, 0]);
  check('each flow\'s arrived bytes are counted per direction (rxBytes c2h=B/n h2c=B/n)', byFlow.size > 0 && totals[1] + totals[3] > 0,
        `flows=${byFlow.size} c2h=${totals[0]}/${totals[1]} h2c=${totals[2]}/${totals[3]}`);
  const media = (bytes, n) => {
    const extra = bytes - 49 * n;  // what the 1200-byte packets add over 49 each
    return extra >= 0 && extra % 1151 === 0 && extra / 1151 <= n ? extra / 1151 : -1;
  };
  const mc = media(totals[0], totals[1]);
  const mh = media(totals[2], totals[3]);
  check('the count is exact for the known lengths (49 / 1200): whole Hello + media packets, media seen both ways',
        mc >= 1 && mh >= 1, `c2h=${totals[0]}/${totals[1]} (media ${mc}) h2c=${totals[2]}/${totals[3]} (media ${mh})`);
  if (flows.length) console.log('  e.g. ' + flows[flows.length - 1].slice(0, 500));
  console.log(`\nrelay_trace_test: ${failures === 0 ? 'ALL PASS' : failures + ' FAILED'}`);
  process.exit(failures === 0 ? 0 : 1);
})();
