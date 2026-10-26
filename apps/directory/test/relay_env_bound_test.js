// keepalive-r2 r4 (Codex N3): the relay's standby bounds read from the environment must be finite positive
// integers -- anything else falls back to the default and SAYS so. Before r4, Math.max(1, Number('abc')) was NaN and
// every comparison against it was false: the bound vanished. This starts server.js with bad values on spare ports
// and reads what it prints.
const { spawn } = require('child_process');
const path = require('path');
const os = require('os');
const fs = require('fs');

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

const dataPath = path.join(os.tmpdir(), `remote60-envbound-${process.pid}.json`);
const env = { ...process.env, REMOTE60_DIR_DATA: dataPath, REMOTE60_LOG_DISABLED: '1',
              REMOTE60_DIR_PORT: '18090', REMOTE60_DIR_UDP_PORT: '18091',
              REMOTE60_RELAY_ENABLED: '1', REMOTE60_RELAY_IP: '127.0.0.1', REMOTE60_RELAY_PORT: '18453',
              REMOTE60_RELAY_STANDBY_MAX_PER_ACCOUNT: 'abc',
              REMOTE60_RELAY_STANDBY_MAX_PER_WIRE_IP: '2.5',
              REMOTE60_RELAY_STANDBY_MAX_SOCKETS: '-3' };
const server = spawn(process.execPath, [path.join(__dirname, '..', 'server.js')], { env });
let out = '';
server.stdout.on('data', (c) => (out += c));
server.stderr.on('data', (c) => (out += c));
setTimeout(() => {
  check('a non-number bound falls back and says so (MAX_PER_ACCOUNT=abc -> 24)',
        out.includes('REMOTE60_RELAY_STANDBY_MAX_PER_ACCOUNT="abc" is not a positive integer; using 24'));
  check('a fraction falls back (MAX_PER_WIRE_IP=2.5 -> 24)',
        out.includes('REMOTE60_RELAY_STANDBY_MAX_PER_WIRE_IP="2.5" is not a positive integer; using 24'));
  check('a negative falls back (MAX_SOCKETS=-3 -> 256)',
        out.includes('REMOTE60_RELAY_STANDBY_MAX_SOCKETS="-3" is not a positive integer; using 256'));
  check('...and the server still starts', out.includes('[relay] listening on udp'));
  server.kill();
  server.on('exit', () => {
    fs.rmSync(dataPath, { force: true });
    console.log(`\nRESULT: ${failures ? 'FAILED' : 'ALL PASS'}  (${failures} failed)`);
    process.exit(failures ? 1 : 0);
  });
}, 2500);
