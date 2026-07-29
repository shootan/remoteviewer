// Boots a throwaway directory server on spare ports and runs the end-to-end checks against it,
// then restarts that server to confirm host registrations outlive the process.
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

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function startServer() {
  return spawn(process.execPath, [serverPath], { env, stdio: 'ignore' });
}

function runTest(args, opts = {}) {
  const [script, ...scriptArgs] = args;
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [path.join(__dirname, script), ...scriptArgs], {
      env,
      // The register phase hands its token back on stdout, so that one is captured.
      stdio: opts.capture ? ['ignore', 'pipe', 'inherit'] : 'inherit',
    });
    let out = '';
    if (opts.capture) child.stdout.on('data', (c) => (out += c));
    child.on('exit', (code) => resolve({ code: code ?? 1, out: out.trim() }));
  });
}

function stopServer(server) {
  return new Promise((resolve) => {
    server.on('exit', () => resolve());
    server.kill();
  });
}

function cleanup() {
  fs.rmSync(dataPath, { force: true });
}

(async () => {
  let server = startServer();
  await sleep(1500);

  const main = await runTest(['directory_test.js']);
  if (main.code !== 0) {
    await stopServer(server);
    cleanup();
    process.exit(main.code);
  }

  console.log('\n--- restart survival ---');
  const registered = await runTest(['restart_test.js', 'register'], { capture: true });
  if (registered.code !== 0 || !registered.out) {
    await stopServer(server);
    cleanup();
    console.log('RESULT: FAILED (could not register before restart)');
    process.exit(1);
  }

  await stopServer(server);
  server = startServer();
  await sleep(1500);

  const verified = await runTest(['restart_test.js', 'verify', registered.out]);

  await stopServer(server);
  cleanup();
  console.log(verified.code === 0 ? '\nRESULT: ALL PASS' : '\nRESULT: FAILED');
  process.exit(verified.code);
})();
