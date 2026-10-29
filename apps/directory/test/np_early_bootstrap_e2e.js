// np-impl r1: the whole viewer path -- the product's directory_session_open (observe, /api/connect, the
// candidate race) -- against real directory servers, across new/old server x new/old viewer, with a fake
// host that either answers punches (a direct path exists) or stays silent (only the relay is left).
//
// The viewer side is remote60_np_early_bootstrap_probe (a test tool around the product function); the
// servers are this tree's server.js and an older one that does not know relayEarly (NP_OLD_SERVER, a copy
// of the base commit's apps/directory). The grace is 1500 ms here so "the server's grace" (old viewer, old
// server) and "the viewer's commit point" (1000 ms) can be told apart in the timings.
//
//   REMOTE60_TEST_TMP=<anchor>/<name> NP_PROBE_EXE=<probe.exe> NP_OLD_SERVER=<dir with server.js> \
//     node np_early_bootstrap_e2e.js
const { spawn, spawnSync } = require('child_process');
const dgram = require('dgram');
const http = require('http');
const path = require('path');
const testTmp = require('./test_tmp');

const PROBE = process.env.NP_PROBE_EXE;
const OLD_SERVER = process.env.NP_OLD_SERVER;
const HTTP = 18280, UDP = 18281, RELAY = 18543, GRACE = 1500;
const MAGIC = 0x31435052, BYTES = 49, KIND_PUNCH = 303;

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function api(method, p, body, token) {
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : null;
    const req = http.request({ host: '127.0.0.1', port: HTTP, path: p, method,
      headers: Object.assign(payload ? { 'content-type': 'application/json', 'content-length': Buffer.byteLength(payload) } : {},
                             token ? { authorization: 'Bearer ' + token } : {}) }, (res) => {
      let data = '';
      res.on('data', (c) => (data += c));
      res.on('end', () => { try { resolve({ status: res.statusCode, body: JSON.parse(data || '{}') }); } catch { resolve({ status: res.statusCode, body: {} }); } });
    });
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function punchPacket() {
  const b = Buffer.alloc(BYTES);
  b.writeUInt32LE(MAGIC, 0); b.writeUInt16LE(KIND_PUNCH, 4); b.writeUInt16LE(BYTES, 6); b.writeUInt32LE(2, 8);
  return b;
}

// The fake host: observes and heartbeats from its media socket, as the real one does; in 'answer' mode it
// answers every punch that does not come from the directory (the viewer's), in 'silent' mode none.
async function startHost(mode, hostToken) {
  const sock = dgram.createSocket('udp4');
  await new Promise((r) => sock.bind(0, '127.0.0.1', r));
  let observed = null;
  sock.on('message', (msg, rinfo) => {
    if (msg[0] === 0x7b) { try { observed = JSON.parse(msg.toString()); } catch { /* not ours */ } return; }
    if (mode === 'answer' && rinfo.port !== UDP && msg.length === BYTES && msg.readUInt16LE(4) === KIND_PUNCH) {
      sock.send(punchPacket(), rinfo.port, rinfo.address);
    }
  });
  const beat = async () => {
    sock.send(Buffer.from('OBSERVE np-host-observe'), UDP, '127.0.0.1');
    await sleep(150);
    await api('POST', '/api/host/heartbeat', { hostToken, hostName: 'np-host', observeToken: 'np-host-observe',
                                               localUdpPort: sock.address().port, localIps: [] });
  };
  await beat();
  const timer = setInterval(() => { beat().catch(() => {}); }, 5000);
  return { sock, stop: () => { clearInterval(timer); sock.close(); }, observed: () => observed };
}

function runProbe(hostId, relayEarly) {
  return new Promise((resolve) => {
    const child = spawn(PROBE, [`http://127.0.0.1:${HTTP}`, 'tester', 'test-pass-1234', hostId, relayEarly ? '1' : '0'],
                        { stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '';
    child.stdout.on('data', (c) => (out += c));
    child.stderr.on('data', (c) => (out += c));
    child.on('exit', (code) => {
      const race = (out.match(/\[race\][^\n]*/) || [''])[0];
      const attempt = (out.match(/\[attempt\] connect=\S+ candidates[^\n]*/) || [''])[0];
      const probe = (out.match(/PROBE [^\n]*/) || [''])[0];
      const field = (line, k) => { const m = line.match(new RegExp(`${k}=(\\S+)`)); return m ? m[1] : ''; };
      resolve({ code, out, race, attempt, probe,
                mode: field(race, 'mode'), decision: field(race, 'decision'), chosenMs: Number(field(race, 'chosenMs')),
                relayEarly: field(attempt, 'relayEarly'), kind: field(probe, 'kind'), relay: field(probe, 'relay') });
    });
  });
}

async function withServer(serverJs, env, body) {
  const server = spawn(process.execPath, [serverJs], { env, stdio: 'ignore' });
  await sleep(1500);
  try { await body(); } finally {
    await new Promise((r) => { server.on('exit', r); server.kill(); });
  }
}

(async () => {
  if (!PROBE || !OLD_SERVER) {
    console.log('SKIP  np_early_bootstrap_e2e (set NP_PROBE_EXE and NP_OLD_SERVER)');
    process.exit(77);
  }
  const root = testTmp.root();
  const env = { ...process.env, TEMP: root, TMP: root, TMPDIR: root,
                REMOTE60_DIR_PORT: String(HTTP), REMOTE60_DIR_UDP_PORT: String(UDP), REMOTE60_DIR_SIGNUP_KEY: 'k',
                REMOTE60_RELAY_ENABLED: '1', REMOTE60_RELAY_IP: '127.0.0.1', REMOTE60_RELAY_PORT: String(RELAY),
                REMOTE60_RELAY_GRACE_MS: String(GRACE), REMOTE60_RELAY_ALLOW_IPS: '127.0.0.1',
                REMOTE60_RELAY_ALLOW_ACCOUNTS: 'tester' };
  const servers = [['new', path.join(__dirname, '..', 'server.js')], ['old', path.join(OLD_SERVER, 'server.js')]];
  for (const [label, serverJs] of servers) {
    const data = testTmp.file(`np-e2e-${label}-${process.pid}.json`);
    testTmp.remove(data);
    const senv = { ...env, REMOTE60_DIR_DATA: data };
    spawnSync(process.execPath, [serverJs, '--add-account', 'tester', 'test-pass-1234'], { env: senv, stdio: 'ignore' });
    await withServer(serverJs, senv, async () => {
      console.log(`\n--- ${label} server ---`);
      const reg = await api('POST', '/api/host/register', { id: 'tester', pw: 'test-pass-1234', hostName: 'np-host', machineId: `np-${label}` });
      check(`${label}: host registers`, reg.status === 200, `status=${reg.status}`);
      for (const mode of ['silent', 'answer']) {
        const host = await startHost(mode, reg.body.hostToken);
        await sleep(300);
        for (const viewerNew of [true, false]) {
          const r = await runProbe(reg.body.hostId, viewerNew);
          const tag = `${label} server, ${viewerNew ? 'new' : 'old'} viewer, host ${mode}`;
          console.log(`      ${r.attempt}\n      ${r.race}\n      ${r.probe}`);
          check(`${tag}: the session opens`, r.code === 0 && /open=1/.test(r.probe), `code=${r.code}`);
          if (mode === 'answer') {
            check(`${tag}: direct is chosen`, r.decision === 'direct' && r.kind === 'public' && r.relay === '0',
                  `decision=${r.decision} kind=${r.kind} relay=${r.relay} chosenMs=${r.chosenMs}`);
            continue;
          }
          const early = label === 'new' && viewerNew;
          check(`${tag}: ${early ? 'early race (accepted)' : 'legacy race'}`,
                early ? (r.relayEarly === 'accepted' && r.mode === 'early')
                      : (r.mode === 'legacy' && (viewerNew ? r.relayEarly === 'unknown' : r.relayEarly === '')),
                `relayEarly=${r.relayEarly || '(not asked)'} mode=${r.mode}`);
          check(`${tag}: the relay, labelled relay`, r.kind === 'relay' && r.relay === '1',
                `kind=${r.kind} relay=${r.relay}`);
          check(`${tag}: chosen ${early ? 'at the commit point (~1000)' : 'after the server grace (~1500)'}`,
                early ? (r.chosenMs >= 1000 && r.chosenMs < 1200) : (r.chosenMs >= GRACE - 50 && r.chosenMs < GRACE + 300),
                `chosenMs=${r.chosenMs}`);
        }
        host.stop();
      }
    });
    testTmp.remove(data);
  }
  console.log(`\n${failures === 0 ? 'ALL PASS' : failures + ' FAILED'}`);
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
