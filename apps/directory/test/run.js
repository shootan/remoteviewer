// Boots a throwaway directory server on spare ports and runs the end-to-end checks against it.
const { spawn, spawnSync } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');

const serverPath = path.join(__dirname, '..', 'server.js');
const dataPath = path.join(os.tmpdir(), `remote60-directory-test-${process.pid}.json`);
const env = { ...process.env, REMOTE60_DIR_DATA: dataPath,
              REMOTE60_DIR_PORT: '18080', REMOTE60_DIR_UDP_PORT: '18081',
              T_PORT: '18080', T_UDP: '18081' };

fs.rmSync(dataPath, { force: true });
spawnSync(process.execPath, [serverPath, '--add-account', 'tester', 'test-pass-1234'],
          { env, stdio: 'ignore' });

const server = spawn(process.execPath, [serverPath], { env, stdio: 'ignore' });
setTimeout(() => {
  const t = spawn(process.execPath, [path.join(__dirname, 'directory_test.js')],
                  { env, stdio: 'inherit' });
  t.on('exit', (code) => {
    server.kill();
    fs.rmSync(dataPath, { force: true });
    process.exit(code ?? 1);
  });
}, 1500);
