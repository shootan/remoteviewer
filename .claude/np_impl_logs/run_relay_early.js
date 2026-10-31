// scratch (not committed): start a relay server (argv[2] = server.js) with the run.js relay env and run relay_early_test.js
const { spawn, spawnSync } = require('child_process');
const path = require('path');
const serverPath = process.argv[2];
const testPath = process.argv[3];
const root = 'D:/remote/remote/.claude/test-tmp/np-impl';
const data = path.join(root, `relay-early-scratch-${process.pid}.json`);
const env = { ...process.env, REMOTE60_TEST_TMP: root, TEMP: root, TMP: root, REMOTE60_DIR_DATA: data,
  REMOTE60_DIR_PORT: '18080', REMOTE60_DIR_UDP_PORT: '18081', REMOTE60_DIR_SIGNUP_KEY: 'test-signup-key',
  T_PORT: '18080', T_UDP: '18081', REMOTE60_RELAY_ENABLED: '1', REMOTE60_RELAY_IP: '127.0.0.1',
  REMOTE60_RELAY_PORT: '18443', REMOTE60_RELAY_GRACE_MS: '400', REMOTE60_RELAY_ALLOW_IPS: '127.0.0.1',
  REMOTE60_RELAY_ALLOW_ACCOUNTS: 'tester', T_RELAY: '18443' };
spawnSync(process.execPath, [serverPath, '--add-account', 'tester', 'test-pass-1234'], { env, stdio: 'ignore' });
spawnSync(process.execPath, [serverPath, '--add-account', 'outsider', 'outsider-pass-1234'], { env, stdio: 'ignore' });
const server = spawn(process.execPath, [serverPath], { env, stdio: 'ignore' });
setTimeout(() => {
  const t = spawn(process.execPath, [testPath], { env, stdio: 'inherit' });
  t.on('exit', (code) => { server.kill(); console.log(`exit=${code}`); setTimeout(() => process.exit(code ?? 1), 300); });
}, 1500);
