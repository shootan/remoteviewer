// keepalive-r2 r4 (Codex codex_keepalive_r2_spike_r3.md N1, N3): the relay's standby leases --
//   N1  a lease's host-facing socket pins ONLY the host's punch carrying that lease's current hostProof (handed to
//       the host alone, in its heartbeat's pendingPunch). Another socket on the host's IP, first or not, with no
//       proof, a wrong one or another lease's, pins nothing, makes nothing ready and reaches no client; a moved
//       host gets a fresh proof and the old one, replayed, pins nothing.
//   N3  the host's eight (legacy included) hold whichever class comes first: a legacy Hello beside eight standby
//       leases is refused and drops nothing; replacing the legacy lease keeps eight.
// Each case on a host of its own (registered here), so the counts of one do not leak into another.
// Run against the same server as relay_standby_test.js (run.js: relay on, host-facing 127.0.0.1, per-account /
// per-IP bounds raised for the test).
const http = require('http');
const dgram = require('dgram');

const HTTP = Number(process.env.T_PORT || 18080);
const UDP = Number(process.env.T_UDP || 18081);
const RELAY_PORT = Number(process.env.T_RELAY || 18443);

const MAGIC = 0x31435052;
const HELLO_BYTES = 49;
const STANDBY_BYTES = 104;
const KIND_HELLO = 300;
const KIND_HELLO_ACK = 301;
const KIND_PUNCH = 303;
const KIND_SB_HELLO = 311;
const KIND_SB_ACK = 312;
const KIND_SB_PING = 313;
const KIND_SB_PONG = 314;
const FEATURE_FEC = 0x2;
const FEATURE_DIRECTORY_AUTH = 0x4;

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
function buildHello(kind, token, features) {
  const buf = Buffer.alloc(HELLO_BYTES);
  buf.writeUInt32LE(MAGIC, 0);
  buf.writeUInt16LE(kind, 4);
  buf.writeUInt16LE(HELLO_BYTES, 6);
  buf.writeUInt32LE(2, 8);
  buf.writeUInt32LE(features || 0, 12);
  if (token) buf.write(token, 16, 'latin1');
  return buf;
}
function buildStandby(kind, { token = '', device = 0, entryId = 0n, gen = 0, status = 0 } = {}) {
  const buf = Buffer.alloc(STANDBY_BYTES);
  buf.writeUInt32LE(MAGIC, 0);
  buf.writeUInt16LE(kind, 4);
  buf.writeUInt16LE(STANDBY_BYTES, 6);
  buf.writeUInt32LE(2, 8);
  buf.writeUInt32LE(status, 12);
  buf.fill(device, 16, 32);
  buf.writeBigUInt64LE(BigInt(entryId), 32);
  buf.writeUInt32LE(gen, 56);
  if (token) buf.write(token, 68, 'latin1');
  return buf;
}
const kindOf = (msg) => (msg.length >= 6 && msg.readUInt32LE(0) === MAGIC ? msg.readUInt16LE(4) : -1);
function recorder() {
  const sock = dgram.createSocket('udp4');
  const got = [];
  sock.on('message', (msg, rinfo) => got.push({ msg, rinfo }));
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
async function waitFor(pred, ms = 1500) {
  const until = Date.now() + ms;
  while (Date.now() < until) {
    const v = pred();
    if (v) return v;
    await sleep(20);
  }
  return pred();
}
const notPunch = (list) => list.filter((p) => kindOf(p.msg) !== KIND_PUNCH);

let session = '';
const socks = [];
let seq = 0;

// A fake host: media socket (observe + heartbeat), and a "NAT" socket for what it sends toward lease sockets. It
// answers StandbyHello (Ack), Ping (Pong), legacy Hello (HelloAck). punchRelay=false: it does NOT punch the relay
// entries by itself (the test does, to order the race).
async function mkHost(name) {
  const r = await api('POST', '/api/host/register', { id: 'tester', pw: 'test-pass-1234', hostName: name, machineId: 'm-' + name });
  const h = { name, hostId: r.body.hostId, hostToken: r.body.hostToken, media: recorder(), nat: recorder(), obs: name + '-obs-' + (++seq) };
  await bind(h.media.sock);
  await bind(h.nat.sock);
  socks.push(h.media.sock, h.nat.sock);
  let entry = 0x7000n;
  for (const rec of [h.media, h.nat]) {
    rec.sock.on('message', (msg, rinfo) => {
      const k = kindOf(msg);
      const reply = (b) => rec.sock.send(b, rinfo.port, rinfo.address);
      if (k === KIND_SB_HELLO) reply(buildStandby(KIND_SB_ACK, { entryId: ++entry, status: 0 }));
      else if (k === KIND_SB_PING) reply(buildStandby(KIND_SB_PONG, { entryId: msg.readBigUInt64LE(32), status: 0 }));
      else if (k === KIND_HELLO) reply(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH));
    });
  }
  await observe(h.media.sock, h.obs);
  h.heartbeat = async (punchRelay = true, sock = h.media.sock, obsToken = h.obs) => {
    const hb = await api('POST', '/api/host/heartbeat', { hostToken: h.hostToken, hostName: name, observeToken: obsToken,
                                                           localUdpPort: sock.address().port });
    const pending = hb.body.pendingPunch || [];
    if (punchRelay) {
      for (const p of pending) {
        if (p.hostProof) h.nat.sock.send(buildHello(KIND_PUNCH, p.hostProof, 0), p.port, p.ip);
      }
    }
    return pending;
  };
  await h.heartbeat();
  return h;
}
async function mkClient(tag) {
  const c = recorder();
  await bind(c.sock);
  socks.push(c.sock);
  c.obs = tag + '-obs-' + (++seq);
  await observe(c.sock, c.obs);
  c.got.length = 0;
  return c;
}
const connect = (h, c, standby) => api('POST', '/api/connect',
  Object.assign({ hostId: h.hostId, observeToken: c.obs }, standby ? { standby: true } : {}), session);

(async () => {
  const r = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
  session = r.body.sessionToken;
  check('login', r.status === 200 && !!session);

  // ------------------------------------------------------------ N1: the first pin needs the host's proof
  {
    const h = await mkHost('r4-n1');
    const c = await mkClient('n1c');
    const rc = await connect(h, c, true);
    check('N1: a standby connect is reserved', rc.body.standbyRelay === 'reserved', JSON.stringify(rc.body.standbyRelay));
    const pending = await h.heartbeat(false);  // the host has its proof; it has not punched yet
    const mine = pending.find((p) => p.hostProof);
    check('N1: the heartbeat hands the HOST a hostProof for the lease (32 hex), not the client token',
          !!mine && /^[0-9a-f]{32}$/.test(mine.hostProof) && mine.hostProof !== rc.body.punchToken &&
          !pending.some((p) => p.punchToken === rc.body.punchToken && p.hostProof));
    // a stranger on the host's IP, FIRST: no proof / a wrong proof / a fake StandbyAck
    const stranger = recorder();
    await bind(stranger.sock);
    socks.push(stranger.sock);
    stranger.sock.send(buildHello(KIND_PUNCH, '', 0), mine.port, '127.0.0.1');
    stranger.sock.send(buildHello(KIND_PUNCH, 'f'.repeat(32), 0), mine.port, '127.0.0.1');
    stranger.sock.send(buildStandby(KIND_SB_ACK, { entryId: 0xBADn, status: 0 }), mine.port, '127.0.0.1');
    await sleep(200);
    c.sock.send(buildStandby(KIND_SB_HELLO, { token: rc.body.punchToken, device: 0x41, gen: 1 }), RELAY_PORT, '127.0.0.1');
    await sleep(400);
    check('N1: a stranger first on the host IP (no proof, a wrong proof, a fake StandbyAck) pins nothing: the client gets ' +
          'nothing back, and its StandbyHello does not reach the stranger',
          notPunch(c.got).length === 0 && !stranger.got.some((p) => kindOf(p.msg) === KIND_SB_HELLO),
          `client=${notPunch(c.got).length} strangerHello=${stranger.got.filter((p) => kindOf(p.msg) === KIND_SB_HELLO).length}`);
    // the host's punch with the proof pins it; the client's retransmit now reaches the HOST
    h.nat.sock.send(buildHello(KIND_PUNCH, mine.hostProof, 0), mine.port, mine.ip);
    await sleep(200);
    stranger.got.length = 0;
    c.sock.send(buildStandby(KIND_SB_HELLO, { token: rc.body.punchToken, device: 0x41, gen: 1 }), RELAY_PORT, '127.0.0.1');
    const ack = await waitFor(() => c.got.find((p) => kindOf(p.msg) === KIND_SB_ACK));
    check('N1: the host\'s punch with the proof pins the lease: the StandbyHello reaches the host, its Ack the client',
          !!ack && h.nat.got.some((p) => kindOf(p.msg) === KIND_SB_HELLO) && stranger.got.length === 0);
    // after the right proof, a stranger still gets nowhere
    c.got.length = 0;
    stranger.sock.send(buildStandby(KIND_SB_PONG, { entryId: 1n, status: 0 }), mine.port, '127.0.0.1');
    await sleep(200);
    check('N1: after the pin, the stranger\'s datagram reaches nobody', c.got.length === 0);

    // cross-lease proof: lease 2's socket gets lease 1's proof first
    const c2 = await mkClient('n1c2');
    const rc2 = await connect(h, c2, true);
    const pending2 = await h.heartbeat(false);
    const lease2 = pending2.find((p) => p.hostProof);
    stranger.sock.send(buildHello(KIND_PUNCH, mine.hostProof, 0), lease2.port, '127.0.0.1');
    await sleep(200);
    c2.sock.send(buildStandby(KIND_SB_HELLO, { token: rc2.body.punchToken, device: 0x42, gen: 1 }), RELAY_PORT, '127.0.0.1');
    await sleep(400);
    check('N1: ANOTHER lease\'s proof pins nothing (lease 2 stays unpinned; its client gets nothing)',
          notPunch(c2.got).length === 0 && !stranger.got.some((p) => kindOf(p.msg) === KIND_SB_HELLO));
    h.nat.sock.send(buildHello(KIND_PUNCH, lease2.hostProof, 0), lease2.port, lease2.ip);
    await sleep(200);
    c2.sock.send(buildStandby(KIND_SB_HELLO, { token: rc2.body.punchToken, device: 0x42, gen: 1 }), RELAY_PORT, '127.0.0.1');
    check('N1: ...its own proof pins it', !!(await waitFor(() => c2.got.find((p) => kindOf(p.msg) === KIND_SB_ACK))));

    // the host moves (a new media socket, a new observation): the pin goes, a fresh proof comes
    const moved = recorder();
    await bind(moved.sock);
    socks.push(moved.sock);
    const obs2 = 'r4-n1-moved-' + (++seq);
    await observe(moved.sock, obs2);
    const pendingMove = await h.heartbeat(false, moved.sock, obs2);
    const fresh = pendingMove.filter((p) => p.hostProof);
    check('N1: a moved host gets a FRESH proof for each lease with that heartbeat',
          fresh.length === 2 && !fresh.some((p) => p.hostProof === mine.hostProof || p.hostProof === lease2.hostProof),
          JSON.stringify(fresh.map((p) => p.port)));
    const fresh1 = fresh.find((p) => p.port === mine.port);
    // the OLD proof, replayed (from the host's old socket), pins nothing
    h.nat.sock.send(buildHello(KIND_PUNCH, mine.hostProof, 0), mine.port, mine.ip);
    await sleep(200);
    h.nat.got.length = 0;
    c.got.length = 0;
    c.sock.send(buildStandby(KIND_SB_PING, { entryId: ack.msg.readBigUInt64LE(32) }), RELAY_PORT, '127.0.0.1');
    await sleep(400);
    check('N1: after the move the OLD proof, replayed, pins nothing (the client\'s ping is not answered via the old tuple)',
          c.got.filter((p) => kindOf(p.msg) === KIND_SB_PONG).length === 0);
    // the host's new socket punches the fresh proof: pinned again, traffic flows
    const movedNat = recorder();
    await bind(movedNat.sock);
    socks.push(movedNat.sock);
    movedNat.sock.on('message', (msg, rinfo) => {
      if (kindOf(msg) === KIND_SB_PING) movedNat.sock.send(buildStandby(KIND_SB_PONG, { entryId: msg.readBigUInt64LE(32), status: 0 }), rinfo.port, rinfo.address);
    });
    movedNat.sock.send(buildHello(KIND_PUNCH, fresh1.hostProof, 0), fresh1.port, fresh1.ip);
    await sleep(200);
    c.got.length = 0;
    c.sock.send(buildStandby(KIND_SB_PING, { entryId: ack.msg.readBigUInt64LE(32) }), RELAY_PORT, '127.0.0.1');
    check('N1: the fresh proof from the host\'s new socket pins it again: the ping is answered',
          !!(await waitFor(() => c.got.find((p) => kindOf(p.msg) === KIND_SB_PONG))));
  }

  // ------------------------------------------------------------ N3: eight standby, then a legacy Hello
  {
    const h = await mkHost('r4-n3a');
    const clients = [];
    const outcomes = [];
    for (let i = 0; i < 8; ++i) {
      const c = await mkClient('n3a' + i);
      c.token = (await connect(h, c, true)).body;
      clients.push(c);
      outcomes.push(c.token.standbyRelay);
    }
    await h.heartbeat(true);  // the host punches every lease with its proof
    await sleep(300);
    // bind all eight (StandbyHello -> Ack)
    const entries = [];
    for (let i = 0; i < 8; ++i) {
      const c = clients[i];
      c.sock.send(buildStandby(KIND_SB_HELLO, { token: c.token.punchToken, device: 0x60 + i, gen: 1 }), RELAY_PORT, '127.0.0.1');
      const a = await waitFor(() => c.got.find((p) => kindOf(p.msg) === KIND_SB_ACK));
      entries.push(a ? a.msg.readBigUInt64LE(32) : 0n);
    }
    check('N3: eight standby leases fill a host, all bound', outcomes.every((o) => o === 'reserved') && entries.every((e) => e !== 0n),
          JSON.stringify(outcomes));
    const L = await mkClient('n3aL');
    const rl = await connect(h, L, false);
    await h.heartbeat(true);
    h.media.got.length = 0;
    L.sock.send(buildHello(KIND_HELLO, rl.body.punchToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
    await sleep(500);
    check('N3: a NEW legacy Hello beside eight standby leases is refused: it never reaches the host, no HelloAck',
          !h.media.got.some((p) => kindOf(p.msg) === KIND_HELLO) && !L.got.some((p) => kindOf(p.msg) === KIND_HELLO_ACK));
    // nothing was dropped: every one of the eight still answers its ping
    let pongs = 0;
    for (let i = 0; i < 8; ++i) {
      const c = clients[i];
      c.got.length = 0;
      c.sock.send(buildStandby(KIND_SB_PING, { entryId: entries[i] }), RELAY_PORT, '127.0.0.1');
      if (await waitFor(() => c.got.find((p) => kindOf(p.msg) === KIND_SB_PONG), 1000)) ++pongs;
    }
    check('N3: ...and the refusal dropped nothing: all eight standby leases still carry their pings', pongs === 8, String(pongs));
  }

  // ------------------------------------------------------------ N3: legacy + seven standby; the legacy replaced
  {
    const h = await mkHost('r4-n3b');
    const L1 = await mkClient('n3bL1');
    const rl1 = await connect(h, L1, false);
    await h.heartbeat(true);
    L1.sock.send(buildHello(KIND_HELLO, rl1.body.punchToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
    check('N3: the legacy lease is up', !!(await waitFor(() => L1.got.find((p) => kindOf(p.msg) === KIND_HELLO_ACK))));
    const outcomes = [];
    for (let i = 0; i < 7; ++i) outcomes.push((await connect(h, await mkClient('n3b' + i), true)).body.standbyRelay);
    const eighth = (await connect(h, await mkClient('n3b8'), true)).body.standbyRelay;
    check('N3: legacy 1 + standby 7 = eight; an eighth standby is host-full', outcomes.every((o) => o === 'reserved') && eighth === 'host-full',
          JSON.stringify(outcomes) + ' ' + eighth);
    const L2 = await mkClient('n3bL2');
    const rl2 = await connect(h, L2, false);
    await h.heartbeat(true);
    L2.sock.send(buildHello(KIND_HELLO, rl2.body.punchToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
    check('N3: REPLACING the legacy lease is allowed (still eight)', !!(await waitFor(() => L2.got.find((p) => kindOf(p.msg) === KIND_HELLO_ACK))));
    const again = (await connect(h, await mkClient('n3b9'), true)).body.standbyRelay;
    check('N3: ...and the host stays at eight (a standby connect is still host-full)', again === 'host-full', again);
  }

  for (const s of socks) { try { s.close(); } catch { /* closed */ } }
  console.log(`\nRESULT: ${failures ? 'FAILED' : 'ALL PASS'}  (${failures} failed)`);
  process.exit(failures ? 1 : 0);
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
