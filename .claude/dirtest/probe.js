// Spawns the real directory server on isolated ports and asks /healthz what it advertises.
// Reading the range check is not the same as watching it refuse; these are the values that
// decide whether a client is sent somewhere that will never answer.
const { spawn } = require('child_process');
const http = require('http');
const path = require('path');

const SERVER = path.resolve(__dirname, '../../apps/directory/server.js');
const cases = [
  ['(unset)', undefined], ['29181', '29181'], ['0', '0'], ['65535', '65535'],
  ['65536', '65536'], ['-1', '-1'], ['abc', 'abc'], ['29181.5', '29181.5'],
  ['  29181  ', '  29181  '], ['', ''],
];

function get(port) {
  return new Promise((res) => {
    const req = http.get({ host: '127.0.0.1', port, path: '/healthz', timeout: 3000 }, (r) => {
      let b = ''; r.on('data', (d) => b += d); r.on('end', () => res(b));
    });
    req.on('error', (e) => res('ERR ' + e.message));
    req.on('timeout', () => { req.destroy(); res('TIMEOUT'); });
  });
}

(async () => {
  let port = 18100;
  for (const [label, value] of cases) {
    const httpPort = port++, udpPort = port++;
    const env = { ...process.env, REMOTE60_DIR_PORT: String(httpPort),
      REMOTE60_DIR_UDP_PORT: String(udpPort),
      REMOTE60_DIR_DATA: path.join(__dirname, 'data.json'),
      REMOTE60_LOG_DISABLED: '1' };
    if (value !== undefined) env.REMOTE60_DIR_OBSERVE_PORT = value;
    const child = spawn(process.execPath, [SERVER], { env, stdio: 'ignore' });
    await new Promise((r) => setTimeout(r, 700));
    const body = await get(httpPort);
    child.kill();
    console.log('%-12s listen=%d -> %s', JSON.stringify(label), udpPort, body);
  }
})();
