// old-ref r1 (Codex codex_old_apk_lane_peer_lost_decision_r1.md): the fixture ends Node by its handle, so a flow's close
// line (the only one that carried its 1 s buckets) may never be written. The traced copy's periodic snap now carries the
// buckets of the seconds completed since the previous snap. Here: the traced server, the real relay_test.js through it,
// one sweep, then the server is KILLED (no close of its own): its output must already hold a snap line with buckets by
// direction and header kind (c2h / h2c .rx / .sent), and no credential. Run from <copy>/apps/directory/test/.
//
//   REMOTE60_TEST_TMP=D:/remote/remote/.claude/test-tmp/<name> node relay_trace_snap_test.js
const { spawn, spawnSync } = require('child_process');
const path = require('path');
const testTmp = require('./test_tmp');

const serverPath = path.join(__dirname, '..', 'server.js');
const testRoot = testTmp.root();
const dataPath = testTmp.file(`remote60-relay-snap-test-${process.pid}.json`);
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

(async () => {
  testTmp.remove(dataPath);
  spawnSync(process.execPath, [serverPath, '--add-account', 'tester', 'test-pass-1234'], { env, stdio: 'ignore' });
  spawnSync(process.execPath, [serverPath, '--add-account', 'outsider', 'outsider-pass-1234'], { env, stdio: 'ignore' });
  const server = spawn(process.execPath, [serverPath], { env: { ...env, REMOTE60_RELAY_TRACE: '1' },
                                                         stdio: ['ignore', 'pipe', 'inherit'] });
  let out = '';
  server.stdout.on('data', (c) => (out += c));
  await sleep(1500);
  const code = await new Promise((resolve) => {
    const child = spawn(process.execPath, [path.join(__dirname, 'relay_test.js')], { env, stdio: 'ignore' });
    child.on('exit', (c) => resolve(c ?? 1));
  });
  await sleep(6000);  // at least one 5 s sweep after the flows ran
  const before = out;  // everything written BEFORE the kill
  await new Promise((resolve) => { server.on('exit', () => resolve()); server.kill('SIGKILL'); });
  testTmp.remove(dataPath);
  const snaps = before.split(/\r?\n/).filter((l) => /^\[relay-trace\] connect=\S+ snap /.test(l));
  const withBuckets = snaps.filter((l) => /buckets=\[\d+:\S*(c2h|h2c)\.(rx|sent)\.[a-zA-Z]+=\d+/.test(l));
  check('the relay works (relay_test)', code === 0, `exit ${code}`);
  check('a periodic snap written before the kill carries 1 s buckets by direction and header kind',
        withBuckets.length > 0, `snaps=${snaps.length} with buckets=${withBuckets.length}`);
  check('every snap names how far its buckets go (bucketsThrough)', snaps.length > 0 && snaps.every((l) => / bucketsThrough=-?\d+/.test(l)));
  check('no credential in the output', !before.includes('test-pass-1234') && !before.includes('outsider-pass-1234'));
  if (withBuckets.length) console.log('  e.g. ' + withBuckets[0].slice(0, 400));
  console.log(`\nrelay_trace_snap_test: ${failures === 0 ? 'ALL PASS' : failures + ' FAILED'}`);
  process.exit(failures === 0 ? 0 : 1);
})();
