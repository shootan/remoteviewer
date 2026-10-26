// keepalive-r2 relay decision (a): several STANDBY leases on one host, each with its own host-facing socket,
// beside the legacy newest-wins lease -- with a fake host and fake clients talking real packets.
//
// The fake host answers Standby* the way the real one does at the wire (StandbyHello -> StandbyAck with an entry,
// Ping -> Pong, Activate -> HelloAck with DirectoryAuth) and records the source of everything it receives: that
// source is all the real host's adopted-peer gate can tell clients apart by. It also behaves like a host behind a
// symmetric NAT: its media socket observes and heartbeats, but what it sends toward a relay lease's own port leaves
// from ANOTHER socket (a NAT maps each destination anew), so a lease must keep the tuple the host really used and
// not take the heartbeat's.
//
// Run against a server started with REMOTE60_RELAY_ENABLED=1, REMOTE60_RELAY_HOST_FACING_IP=127.0.0.1 and
// REMOTE60_RELAY_HOST_FACING_BIND=127.0.0.1 (run.js does). Loopback only: every address is 127.0.0.1, so a lease
// telling a foreign HOST ADDRESS from the host's is not exercised here -- only a foreign port.
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
const KIND_SB_ACTIVATE = 315;
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
// UdpStandbyPacket (poc_protocol.hpp, #pragma pack(1)): magic 0, kind 4, size 6, version 8, status 12,
// deviceId 16, entryId 32, entrySecret 40, clientGen 56, leaseMs 60, features 64, authToken 68, reserved 101.
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

(async () => {
  let r = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
  const session = r.body.sessionToken;
  check('login', r.status === 200 && !!session, `status=${r.status}`);
  r = await api('POST', '/api/host/register',
                { id: 'tester', pw: 'test-pass-1234', hostName: 'standby-host', machineId: 'machine-standby' });
  const hostToken = r.body.hostToken;
  const hostId = r.body.hostId;
  check('host registers', r.status === 200 && !!hostToken, `status=${r.status}`);

  // ------------------------------------------------------------ the fake host
  const host = recorder();      // media socket: observe, heartbeat, legacy relay traffic
  const hostNat = recorder();   // what the host sends toward a lease's own port leaves from here (symmetric NAT)
  await bind(host.sock);
  await bind(hostNat.sock);
  await observe(host.sock, 'standby-host-observe');
  let nextEntry = 0x5000n;
  const answer = (rec, sockForReply) => {
    rec.sock.on('message', (msg, rinfo) => {
      const k = kindOf(msg);
      const reply = (buf) => sockForReply.send(buf, rinfo.port, rinfo.address);
      if (k === KIND_SB_HELLO) reply(buildStandby(KIND_SB_ACK, { entryId: ++nextEntry, status: 0 }));
      else if (k === KIND_SB_PING) reply(buildStandby(KIND_SB_PONG, { entryId: msg.readBigUInt64LE(32), status: 0 }));
      else if (k === KIND_SB_ACTIVATE) reply(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH));
      else if (k === KIND_HELLO) reply(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH));
    });
  };
  answer(host, host.sock);
  answer(hostNat, hostNat.sock);
  // A heartbeat, and the punches the real host sends to every pendingPunch entry -- relay-facing ones from the
  // "NAT" socket (another destination, another mapping), client ones from the media socket.
  const heartbeat = async () => {
    const hb = await api('POST', '/api/host/heartbeat', {
      hostToken, hostName: 'standby-host', observeToken: 'standby-host-observe',
      localUdpPort: host.sock.address().port,
    });
    const pending = hb.body.pendingPunch || [];
    for (const p of pending) {
      const fromNat = p.ip === '127.0.0.1' && p.port !== host.sock.address().port;
      (fromNat ? hostNat.sock : host.sock).send(buildHello(KIND_PUNCH, '', 0), p.port, p.ip);
    }
    return pending;
  };
  await heartbeat();

  // ------------------------------------------------------------ clients
  const mkClient = async (name) => {
    const c = recorder();
    await bind(c.sock);
    await observe(c.sock, `standby-${name}-observe`);
    c.got.length = 0;
    c.name = name;
    return c;
  };
  const connect = async (c, standby) => {
    const res = await api('POST', '/api/connect',
                          Object.assign({ hostId, observeToken: `standby-${c.name}-observe` }, standby ? { standby: true } : {}),
                          session);
    return res;
  };
  // What the host received from client `c`, by the source tuple it came from.
  const hostRx = (pred) => [...host.got, ...hostNat.got].filter((p) => pred(p.msg));
  const B = await mkClient('b');
  const C = await mkClient('c');
  const rb = await connect(B, true);
  const rc = await connect(C, true);
  check('two standby connects are reserved, each offered the relay',
        rb.body.standbyRelay === 'reserved' && rc.body.standbyRelay === 'reserved' &&
        (rb.body.candidates || []).some((x) => x.kind === 'relay') && (rc.body.candidates || []).some((x) => x.kind === 'relay'),
        `b=${rb.body.standbyRelay} c=${rc.body.standbyRelay}`);
  const pending = await heartbeat();
  const facing = pending.filter((p) => p.ip === '127.0.0.1' && p.punchToken !== rb.body.punchToken && p.punchToken !== rc.body.punchToken);
  check('the heartbeat lists the clients AND one relay-facing entry per standby lease, under tokens no client holds',
        pending.some((p) => p.punchToken === rb.body.punchToken) && pending.some((p) => p.punchToken === rc.body.punchToken) &&
        facing.length === 2 && facing[0].port !== facing[1].port && facing.every((p) => p.port !== UDP && p.port !== RELAY_PORT),
        JSON.stringify(pending.map((p) => `${p.ip}:${p.port}`)));
  await sleep(150);

  // ------------------------------------------------------------ admission, each through its own host-facing socket
  host.got.length = 0;
  hostNat.got.length = 0;
  B.sock.send(buildStandby(KIND_SB_HELLO, { token: rb.body.punchToken, device: 0xB1, gen: 1 }), RELAY_PORT, '127.0.0.1');
  C.sock.send(buildStandby(KIND_SB_HELLO, { token: rc.body.punchToken, device: 0xC1, gen: 1 }), RELAY_PORT, '127.0.0.1');
  const bAck = await waitFor(() => B.got.find((p) => kindOf(p.msg) === KIND_SB_ACK));
  const cAck = await waitFor(() => C.got.find((p) => kindOf(p.msg) === KIND_SB_ACK));
  const hellos = hostRx((m) => kindOf(m) === KIND_SB_HELLO);
  const srcOf = (dev) => {
    const h = hellos.find((p) => p.msg[16] === dev);
    return h ? h.rinfo.port : 0;
  };
  const pb = srcOf(0xB1);
  const pc = srcOf(0xC1);
  check('both StandbyHellos reach the host, from two DIFFERENT relay ports -- neither the observe socket',
        pb && pc && pb !== pc && pb !== UDP && pc !== UDP, `b=${pb} c=${pc} observe=${UDP}`);
  check('...and they are the ports the heartbeat told the host to punch', facing.some((p) => p.port === pb) && facing.some((p) => p.port === pc));
  check('...at the host-NAT socket the lease pinned (not the heartbeat tuple)',
        hellos.every((p) => hostNat.got.includes(p)), `nat=${hostNat.got.length} media=${host.got.length}`);
  check('each client gets ITS Ack back (entries differ)', !!bAck && !!cAck &&
        bAck.msg.readBigUInt64LE(32) !== cAck.msg.readBigUInt64LE(32));
  const eb = bAck ? bAck.msg.readBigUInt64LE(32) : 0n;
  const ec = cAck ? cAck.msg.readBigUInt64LE(32) : 0n;

  // ------------------------------------------------------------ pings, activation, session traffic
  B.got.length = 0;
  C.got.length = 0;
  for (let i = 0; i < 3; ++i) {
    B.sock.send(buildStandby(KIND_SB_PING, { entryId: eb }), RELAY_PORT, '127.0.0.1');
    C.sock.send(buildStandby(KIND_SB_PING, { entryId: ec }), RELAY_PORT, '127.0.0.1');
  }
  await waitFor(() => B.got.length >= 3 && C.got.length >= 3);
  check('pings: each client gets only its own pongs',
        B.got.filter((p) => kindOf(p.msg) === KIND_SB_PONG).every((p) => p.msg.readBigUInt64LE(32) === eb) &&
        C.got.filter((p) => kindOf(p.msg) === KIND_SB_PONG).every((p) => p.msg.readBigUInt64LE(32) === ec) &&
        B.got.length >= 3 && C.got.length >= 3, `b=${B.got.length} c=${C.got.length}`);
  B.got.length = 0;
  C.got.length = 0;
  B.sock.send(buildStandby(KIND_SB_ACTIVATE, { entryId: eb, gen: 1 }), RELAY_PORT, '127.0.0.1');
  const bHelloAck = await waitFor(() => B.got.find((p) => kindOf(p.msg) === KIND_HELLO_ACK));
  check('B activates over the SAME lease (no new connect): its HelloAck comes back to B', !!bHelloAck);
  hostNat.got.length = 0;
  host.got.length = 0;
  const media = (tag) => { const b = Buffer.alloc(60, tag); b.writeUInt32LE(MAGIC, 0); b.writeUInt16LE(305, 4); return b; };
  for (let i = 0; i < 4; ++i) {
    B.sock.send(media(0xB5), RELAY_PORT, '127.0.0.1');
    C.sock.send(media(0xC5), RELAY_PORT, '127.0.0.1');
  }
  await waitFor(() => hostNat.got.length >= 8);
  const bMedia = hostNat.got.filter((p) => p.msg.length === 60 && p.msg[10] === 0xB5);
  const cMedia = hostNat.got.filter((p) => p.msg.length === 60 && p.msg[10] === 0xC5);
  check('session packets: B\'s arrive from B\'s lease port only, C\'s from C\'s -- the host\'s address gate can tell them apart',
        bMedia.length === 4 && cMedia.length === 4 && bMedia.every((p) => p.rinfo.port === pb) && cMedia.every((p) => p.rinfo.port === pc),
        `b=${bMedia.length} c=${cMedia.length}`);
  B.got.length = 0;
  C.got.length = 0;
  hostNat.sock.send(media(0xD5), pb, '127.0.0.1');
  hostNat.sock.send(media(0xE5), pc, '127.0.0.1');
  await waitFor(() => B.got.length && C.got.length);
  check('host -> client: what the host sends to B\'s lease reaches B only, C\'s lease C only',
        B.got.length === 1 && B.got[0].msg[10] === 0xD5 && C.got.length === 1 && C.got[0].msg[10] === 0xE5);

  // ------------------------------------------------------------ a stranger on a lease's host-facing port
  B.got.length = 0;
  const stranger = recorder();
  await bind(stranger.sock);
  stranger.sock.send(media(0xF5), pb, '127.0.0.1');
  await sleep(200);
  check('a datagram to B\'s host-facing port from another port (not the pinned host tuple) reaches nobody', B.got.length === 0);

  // ------------------------------------------------------------ a heartbeat does not move a pinned lease
  await heartbeat();
  hostNat.got.length = 0;
  host.got.length = 0;
  B.sock.send(buildStandby(KIND_SB_PING, { entryId: eb }), RELAY_PORT, '127.0.0.1');
  await waitFor(() => hostNat.got.length || host.got.length);
  check('after another heartbeat, B\'s lease still goes to the tuple it pinned (the NAT socket), not the heartbeat\'s',
        hostNat.got.length === 1 && host.got.length === 0, `nat=${hostNat.got.length} media=${host.got.length}`);

  // ------------------------------------------------------------ the legacy lease beside them
  const L = await mkClient('l');
  const rl = await connect(L, false);
  check('a legacy connect is offered the relay as before (no standbyRelay field)',
        (rl.body.candidates || []).some((x) => x.kind === 'relay') && rl.body.standbyRelay === undefined);
  await heartbeat();
  host.got.length = 0;
  L.sock.send(buildHello(KIND_HELLO, rl.body.punchToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
  const lAck = await waitFor(() => L.got.find((p) => kindOf(p.msg) === KIND_HELLO_ACK));
  const lHello = host.got.find((p) => kindOf(p.msg) === KIND_HELLO);
  check('the legacy Hello reaches the host from the OBSERVE socket, and its HelloAck comes back',
        !!lHello && lHello.rinfo.port === UDP && !!lAck, lHello ? `src=${lHello.rinfo.port}` : 'no hello');
  B.got.length = 0;
  C.got.length = 0;
  B.sock.send(buildStandby(KIND_SB_PING, { entryId: eb }), RELAY_PORT, '127.0.0.1');
  C.sock.send(buildStandby(KIND_SB_PING, { entryId: ec }), RELAY_PORT, '127.0.0.1');
  await waitFor(() => B.got.length && C.got.length);
  check('...and the standby leases were not dropped by it (both still pong)', B.got.length === 1 && C.got.length === 1);
  const L2 = await mkClient('l2');
  const rl2 = await connect(L2, false);
  await heartbeat();
  L2.sock.send(buildHello(KIND_HELLO, rl2.body.punchToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
  await waitFor(() => L2.got.find((p) => kindOf(p.msg) === KIND_HELLO_ACK));
  host.got.length = 0;
  L.sock.send(media(0xA5), RELAY_PORT, '127.0.0.1');
  await sleep(200);
  check('a second legacy connect still takes over the legacy lease (newest wins): L\'s packets are no longer carried',
        !host.got.some((p) => p.msg.length === 60 && p.msg[10] === 0xA5));
  B.got.length = 0;
  B.sock.send(buildStandby(KIND_SB_PING, { entryId: eb }), RELAY_PORT, '127.0.0.1');
  await waitFor(() => B.got.length);
  check('...the standby leases stay through that takeover too', B.got.length === 1);

  // ------------------------------------------------------------ capabilities do not cross classes
  const X = await mkClient('x');
  const rx = await connect(X, true);
  await heartbeat();
  host.got.length = 0;
  hostNat.got.length = 0;
  X.sock.send(buildHello(KIND_HELLO, rx.body.punchToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
  await sleep(250);
  // Punches (the host's to the client's address, the reservation's own to the host) are not what is asked about.
  const notPunch = (list) => list.filter((p) => kindOf(p.msg) !== KIND_PUNCH);
  check('a standby capability in a LEGACY Hello opens nothing (no takeover with it)',
        notPunch(host.got).length === 0 && notPunch(hostNat.got).length === 0 && notPunch(X.got).length === 0,
        `host=${notPunch(host.got).length} nat=${notPunch(hostNat.got).length} x=${notPunch(X.got).length}`);
  const Y = await mkClient('y');
  const ry = await connect(Y, false);
  await heartbeat();
  host.got.length = 0;
  hostNat.got.length = 0;
  Y.sock.send(buildStandby(KIND_SB_HELLO, { token: ry.body.punchToken, device: 0x91, gen: 1 }), RELAY_PORT, '127.0.0.1');
  await sleep(250);
  check('a legacy capability in a StandbyHello opens nothing',
        notPunch(host.got).length === 0 && notPunch(hostNat.got).length === 0 && notPunch(Y.got).length === 0,
        `host=${notPunch(host.got).length} nat=${notPunch(hostNat.got).length} y=${notPunch(Y.got).length}`);
  // Y's Hello now -- the legacy lease moves to Y (newest wins) -- for the count below.

  // ------------------------------------------------------------ bounds: 8 per host, a legacy lease counts
  // On the host now: B, C, X (reserved) = 3 standby + L2's legacy lease = 4.
  const extra = [];
  const outcomes = [];
  for (let i = 0; i < 5; ++i) {
    const e = await mkClient(`e${i}`);
    extra.push(e);
    outcomes.push((await connect(e, true)).body.standbyRelay);
  }
  check('four more standby connects fill the host to eight (3 standby + 1 legacy + 4), the fifth is refused host-full',
        outcomes.slice(0, 4).every((o) => o === 'reserved') && outcomes[4] === 'host-full', JSON.stringify(outcomes));
  const refused = await connect(extra[4], true);
  check('...and a refused standby connect is offered no relay candidate (the direct ones as always)',
        refused.body.standbyRelay === 'host-full' && !(refused.body.candidates || []).some((x) => x.kind === 'relay') &&
        (refused.body.candidates || []).some((x) => x.kind === 'public'));
  // A client endpoint holds one lease: B's socket binding a NEW reservation drops B's old one (frees a place).
  const rb2 = await connect(B, true);
  check('the host is full: B\'s second standby connect is refused too', rb2.body.standbyRelay === 'host-full');

  for (const s of [host, hostNat, B, C, L, L2, X, Y, stranger, ...extra]) s.sock.close();
  console.log(`\nRESULT: ${failures ? 'FAILED' : 'ALL PASS'}  (${failures} failed)`);
  process.exit(failures ? 1 : 0);
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
