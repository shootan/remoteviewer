// np-impl r1 (Codex NP design, contract 2): the relayEarly request flag.
//
// A connect that asks for relayEarly and is told 'accepted' gets its relay punch answered at once --
// its viewer owns the wait from then on. Everything else keeps the server grace: a connect that did
// not ask (an older viewer), another client behind the same wire IP, a standby lease, an account that
// is offered no relay. Each of those is a case below, and each would turn an older viewer's race into
// "relay wins at once" if the flag leaked to it.
//
// Run against a server started with REMOTE60_RELAY_ENABLED=1 and a grace of at least 400 ms.
const http = require('http');
const dgram = require('dgram');

const HTTP = Number(process.env.T_PORT || 18080);
const UDP = Number(process.env.T_UDP || 18081);
const RELAY_PORT = Number(process.env.T_RELAY || 18443);
const GRACE_MS = Number(process.env.REMOTE60_RELAY_GRACE_MS || 400);

const MAGIC = 0x31435052;
const HELLO_BYTES = 49;
const KIND_PUNCH = 303;

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function api(method, path, body, token) {
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : null;
    const req = http.request(
      { host: '127.0.0.1', port: HTTP, path, method,
        headers: Object.assign(
          payload ? { 'content-type': 'application/json', 'content-length': Buffer.byteLength(payload) } : {},
          token ? { authorization: 'Bearer ' + token } : {}) },
      (res) => {
        let data = '';
        res.on('data', (c) => (data += c));
        res.on('end', () => {
          try { resolve({ status: res.statusCode, body: JSON.parse(data || '{}') }); }
          catch { resolve({ status: res.statusCode, body: {} }); }
        });
      });
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function buildPunch() {
  const buf = Buffer.alloc(HELLO_BYTES);
  buf.writeUInt32LE(MAGIC, 0);
  buf.writeUInt16LE(KIND_PUNCH, 4);
  buf.writeUInt16LE(HELLO_BYTES, 6);
  buf.writeUInt32LE(2, 8);
  return buf;
}

function recorder() {
  const sock = dgram.createSocket('udp4');
  const got = [];
  sock.on('message', (msg, rinfo) => got.push({ msg, rinfo, at: Date.now() }));
  return { sock, got };
}
const bind = (sock) => new Promise((r) => sock.bind(0, '127.0.0.1', r));

function observe(sock, token) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('observe timeout')), 3000);
    const onMessage = (msg) => {
      let seen;
      try { seen = JSON.parse(msg.toString()); } catch { return; }
      clearTimeout(timer);
      sock.off('message', onMessage);
      resolve(seen);
    };
    sock.on('message', onMessage);
    sock.send(Buffer.from('OBSERVE ' + token), UDP, '127.0.0.1');
  });
}

// Punches the relay from `client` and reports when the first relay answer arrived (ms after the punch),
// or -1 when none came within `waitMs`.
async function punchAndTime(client, waitMs) {
  client.got.length = 0;
  const sentAt = Date.now();
  client.sock.send(buildPunch(), RELAY_PORT, '127.0.0.1');
  const until = sentAt + waitMs;
  while (Date.now() < until) {
    const hit = client.got.find((p) => p.rinfo.port === RELAY_PORT && p.msg.length === HELLO_BYTES &&
                                       p.msg.readUInt32LE(0) === MAGIC && p.msg.readUInt16LE(4) === KIND_PUNCH);
    if (hit) return hit.at - sentAt;
    await sleep(10);
  }
  return -1;
}

async function freshClient(tag) {
  const c = recorder();
  await bind(c.sock);
  await observe(c.sock, tag);
  c.got.length = 0;
  return c;
}

(async () => {
  if (GRACE_MS < 400) {
    check('grace is long enough to tell "at once" from "after the grace"', false, `grace=${GRACE_MS}`);
    process.exit(1);
  }
  let r = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
  const session = r.body.sessionToken;
  check('login', r.status === 200 && !!session, `status=${r.status}`);

  r = await api('POST', '/api/host/register',
                { id: 'tester', pw: 'test-pass-1234', hostName: 'early-host', machineId: 'machine-early' });
  const hostToken = r.body.hostToken;
  const hostId = r.body.hostId;
  const host = recorder();
  await bind(host.sock);
  await observe(host.sock, 'early-host-observe');
  await api('POST', '/api/host/heartbeat', {
    hostToken, hostName: 'early-host', observeToken: 'early-host-observe',
    localUdpPort: host.sock.address().port, localIps: ['192.168.20.51'],
  });

  // ---------------------------------------------------------------- an older viewer: no flag
  const old = await freshClient('early-old');
  r = await api('POST', '/api/connect', { hostId, observeToken: 'early-old' }, session);
  check('a connect without the flag succeeds', r.status === 200, `status=${r.status}`);
  check('a connect without the flag is told nothing about it', !('relayEarly' in r.body),
        JSON.stringify(r.body.relayEarly));
  let ms = await punchAndTime(old, GRACE_MS + 600);
  check('without the flag the relay still waits out the grace', ms >= GRACE_MS - 50,
        `firstAnswerMs=${ms} grace=${GRACE_MS}`);

  // ---------------------------------------------------------------- relayEarly accepted
  const early = await freshClient('early-new');
  r = await api('POST', '/api/connect', { hostId, observeToken: 'early-new', relayEarly: true }, session);
  check('a connect with the flag is told accepted', r.status === 200 && r.body.relayEarly === 'accepted',
        `status=${r.status} relayEarly=${r.body.relayEarly}`);
  check('the relay candidate is still offered, still last',
        (r.body.candidates || []).length > 1 && r.body.candidates[r.body.candidates.length - 1].kind === 'relay',
        JSON.stringify((r.body.candidates || []).map((c) => c.kind)));
  ms = await punchAndTime(early, GRACE_MS + 600);
  check('an accepted connect is answered at once', ms >= 0 && ms < Math.min(200, GRACE_MS / 2),
        `firstAnswerMs=${ms}`);
  await sleep(400);
  check('it is still answered at most three times', early.got.filter((p) => p.rinfo.port === RELAY_PORT).length <= 3,
        `answers=${early.got.length}`);

  // ---------------------------------------------------------------- same wire IP, other socket
  // The eligibility entry is keyed by wire IP; the newest connect (the accepted one above) owns it. A
  // second client on the same wire IP that punches under it is an older viewer as far as the relay can
  // tell, and must keep the grace.
  const neighbour = await freshClient('early-neighbour');
  ms = await punchAndTime(neighbour, GRACE_MS + 600);
  check('another socket on the same wire IP keeps the grace', ms === -1 || ms >= GRACE_MS - 50,
        `firstAnswerMs=${ms}`);

  // ---------------------------------------------------------------- a later connect without the flag
  // Newest wins: an accepted connect followed by an unflagged one from the same wire IP leaves the
  // unflagged one's timing -- the flag does not stick to the address.
  const again = await freshClient('early-again');
  r = await api('POST', '/api/connect', { hostId, observeToken: 'early-again' }, session);
  ms = await punchAndTime(again, GRACE_MS + 600);
  check('an unflagged connect after an accepted one keeps the grace', ms >= GRACE_MS - 50,
        `firstAnswerMs=${ms}`);

  // ---------------------------------------------------------------- standby: its own class
  const standby = await freshClient('early-standby');
  r = await api('POST', '/api/connect',
                { hostId, observeToken: 'early-standby', relayEarly: true, standby: true }, session);
  check('a standby connect is never accepted for relayEarly',
        r.status === 200 && r.body.relayEarly !== 'accepted' && typeof r.body.relayEarly === 'string',
        `status=${r.status} relayEarly=${r.body.relayEarly} standbyRelay=${r.body.standbyRelay}`);

  // ---------------------------------------------------------------- no relay offered
  const outsiderLogin = await api('POST', '/api/login', { id: 'outsider', pw: 'outsider-pass-1234' });
  const outsider = outsiderLogin.body.sessionToken;
  r = await api('POST', '/api/host/register',
                { id: 'outsider', pw: 'outsider-pass-1234', hostName: 'early-other', machineId: 'machine-early-o' });
  const otherHostId = r.body.hostId;
  await api('POST', '/api/host/heartbeat', {
    hostToken: r.body.hostToken, hostName: 'early-other', observeToken: 'early-host-observe',
  });
  const outsiderClient = await freshClient('early-outsider');
  r = await api('POST', '/api/connect',
                { hostId: otherHostId, observeToken: 'early-outsider', relayEarly: true }, outsider);
  check('an account offered no relay is told no-relay',
        r.status === 200 && r.body.relayEarly === 'no-relay' &&
          !(r.body.candidates || []).some((c) => c.kind === 'relay'),
        `status=${r.status} relayEarly=${r.body.relayEarly}`);
  ms = await punchAndTime(outsiderClient, GRACE_MS + 600);
  // The newest eligibility entry for 127.0.0.1 is the tester's (the outsider's connect makes none), so a
  // punch from here is the neighbour case again: it must not be answered early.
  check('and its punch is not answered early', ms === -1 || ms >= GRACE_MS - 50, `firstAnswerMs=${ms}`);

  // ---------------------------------------------------------------- an early answer binds nothing
  // Codex NP contract: the early answer must not take the host from a session already running. Only a
  // Hello binds; a viewer that holds the relay answer and then goes direct never sends one. So: a live
  // relayed session, then an accepted early connect whose punch is answered at once and which says no
  // Hello -- the live session still carries media both ways.
  const live = await freshClient('early-live');
  r = await api('POST', '/api/connect', { hostId, observeToken: 'early-live' }, session);
  const liveToken = r.body.punchToken;
  const hello = Buffer.alloc(HELLO_BYTES);
  hello.writeUInt32LE(MAGIC, 0);
  hello.writeUInt16LE(300, 4);  // Hello
  hello.writeUInt16LE(HELLO_BYTES, 6);
  hello.writeUInt32LE(2, 8);
  hello.writeUInt32LE(0x2, 12);
  hello.write(liveToken, 16, 'latin1');
  host.got.length = 0;
  live.sock.send(hello, RELAY_PORT, '127.0.0.1');
  await sleep(300);
  const fwd = host.got.find((p) => p.msg.length === HELLO_BYTES && p.msg.readUInt16LE(4) === 300);
  check('a live relayed session is bound (setup)', !!fwd, `hostReceived=${host.got.length}`);
  if (fwd) {
    const ack = Buffer.from(hello);
    ack.writeUInt16LE(301, 4);  // HelloAck
    ack.writeUInt32LE(0x2 | 0x4, 12);
    ack.fill(0, 16);
    host.sock.send(ack, fwd.rinfo.port, fwd.rinfo.address);
    await sleep(200);
  }
  const prober = await freshClient('early-prober');
  r = await api('POST', '/api/connect', { hostId, observeToken: 'early-prober', relayEarly: true }, session);
  ms = await punchAndTime(prober, 600);
  check('the prober is answered at once (accepted)', r.body.relayEarly === 'accepted' && ms >= 0 && ms < 200,
        `relayEarly=${r.body.relayEarly} firstAnswerMs=${ms}`);
  await sleep(300);
  host.got.length = 0;
  live.got.length = 0;
  const up = Buffer.alloc(800, 0xab);
  up.writeUInt32LE(MAGIC, 0);
  live.sock.send(up, RELAY_PORT, '127.0.0.1');
  const down = Buffer.alloc(800, 0xcd);
  down.writeUInt32LE(MAGIC, 0);
  host.sock.send(down, UDP, '127.0.0.1');
  await sleep(300);
  check('after the early answer the live session still reaches the host', host.got.some((p) => p.msg.equals(up)),
        `hostReceived=${host.got.length}`);
  check('and the host still reaches it', live.got.some((p) => p.msg.equals(down)), `liveReceived=${live.got.length}`);

  for (const s of [host, old, early, neighbour, again, standby, outsiderClient, live, prober]) s.sock.close();
  console.log(`\n${failures === 0 ? 'ALL PASS' : failures + ' FAILED'}`);
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
