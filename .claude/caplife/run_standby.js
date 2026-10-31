// Quick runner for relay_standby_test.js only (the suite runs it from run.js).
const { spawn, spawnSync } = require('child_process');
const path = require('path');
const fs = require('fs');
// r5 (Codex N4): files only under the test root, through the suite's own guard.
const testTmp = require('C:/work/gnlink-wt/ka-r3/apps/directory/test/test_tmp');
process.env.REMOTE60_TEST_TMP = process.env.REMOTE60_TEST_TMP || 'D:/remote/remote/.claude/test-tmp/ka-r5-n4';
const dir = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\directory';
const serverPath = path.join(dir, 'server.js');
const dataPath = testTmp.file(`remote60-standby-test-${process.pid}.json`);
const env = { ...process.env, TEMP: testTmp.root(), TMP: testTmp.root(), TMPDIR: testTmp.root(), REMOTE60_DIR_DATA: dataPath,
              REMOTE60_DIR_PORT: '18080', REMOTE60_DIR_UDP_PORT: '18081',
              REMOTE60_DIR_SIGNUP_KEY: 'test-signup-key', T_PORT: '18080', T_UDP: '18081',
              REMOTE60_RELAY_ENABLED: '1', REMOTE60_RELAY_IP: '127.0.0.1', REMOTE60_RELAY_PORT: '18443',
              REMOTE60_RELAY_GRACE_MS: '400', REMOTE60_RELAY_ALLOW_IPS: '127.0.0.1',
              REMOTE60_RELAY_ALLOW_ACCOUNTS: 'tester', T_RELAY: '18443',
              REMOTE60_RELAY_HOST_FACING_IP: '127.0.0.1', REMOTE60_RELAY_HOST_FACING_BIND: '127.0.0.1',
              REMOTE60_RELAY_STANDBY_MAX_PER_ACCOUNT: '200', REMOTE60_RELAY_STANDBY_MAX_PER_WIRE_IP: '200' };
testTmp.remove(dataPath);
spawnSync(process.execPath, [serverPath, '--add-account', 'tester', 'test-pass-1234'], { env, stdio: 'ignore' });
spawnSync(process.execPath, [serverPath, '--add-account', 'outsider', 'outsider-pass-1234'], { env, stdio: 'ignore' });
const logPath = process.argv[2] || testTmp.file('standby-server.log');
const log = fs.openSync(logPath, 'w');
const server = spawn(process.execPath, [serverPath], { env, stdio: ['ignore', log, log] });
setTimeout(() => {
  const child = spawn(process.execPath, [path.join(dir, 'test', process.argv[3] || 'relay_standby_test.js')], { env, stdio: 'inherit' });
  child.on('exit', (code) => {
    server.kill();
    server.on('exit', () => {
      testTmp.remove(dataPath);
      console.log(`runner: test exit ${code}; server log ${logPath}`);
      process.exit(code ?? 1);
    });
  });
}, 1500);
