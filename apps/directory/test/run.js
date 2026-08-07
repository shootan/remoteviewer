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
              REMOTE60_DIR_SIGNUP_KEY: 'test-signup-key',
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
  if (verified.code !== 0) {
    cleanup();
    console.log('\nRESULT: FAILED');
    process.exit(verified.code);
  }

  // The diagnostics get their own server because they are opt-in: running them in the main pass
  // would prove the flags work but not that leaving them unset changes nothing, and "unset
  // changes nothing" is the property everything above depends on.
  console.log('\n--- nat diagnostics (opt-in) ---');
  const diagEnv = { ...env, REMOTE60_NAT_DIAG_ENABLED: '1',
                    REMOTE60_NAT_DIAG_IP: '127.0.0.1', REMOTE60_NAT_DIAG_PORT: '18443',
                    T_DIAG: '18443' };
  server = spawn(process.execPath, [serverPath], { env: diagEnv, stdio: 'ignore' });
  await sleep(1500);
  const diag = await new Promise((resolve) => {
    const child = spawn(process.execPath, [path.join(__dirname, 'nat_diag_test.js')],
                        { env: diagEnv, stdio: 'inherit' });
    child.on('exit', (code) => resolve(code ?? 1));
  });

  await stopServer(server);
  if (diag !== 0) {
    cleanup();
    console.log('\nRESULT: FAILED');
    process.exit(diag);
  }

  // The relay likewise gets its own server, and for the same reason twice over: the pass above
  // proves it stays out of the way when unset, and this one proves it works when set. The grace
  // is shortened because the test is about ordering, not about the production number.
  console.log('\n--- relay (opt-in) ---');
  const relayEnv = { ...env, REMOTE60_RELAY_ENABLED: '1',
                     REMOTE60_RELAY_IP: '127.0.0.1', REMOTE60_RELAY_PORT: '18443',
                     REMOTE60_RELAY_GRACE_MS: '400',
                     REMOTE60_RELAY_ALLOW_IPS: '127.0.0.1',
                     REMOTE60_RELAY_ALLOW_ACCOUNTS: 'tester',
                     T_RELAY: '18443' };
  server = spawn(process.execPath, [serverPath], { env: relayEnv, stdio: 'ignore' });
  await sleep(1500);
  const relay = await new Promise((resolve) => {
    const child = spawn(process.execPath, [path.join(__dirname, 'relay_test.js')],
                        { env: relayEnv, stdio: 'inherit' });
    child.on('exit', (code) => resolve(code ?? 1));
  });

  await stopServer(server);
  cleanup();
  console.log(relay === 0 ? '\nRESULT: ALL PASS' : '\nRESULT: FAILED');
  process.exit(relay);
})();
