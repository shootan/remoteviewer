// Pins the case that broke in the field: the host on the server's own LAN.
//
// From there its OBSERVE never crosses a NAT, so what the server advertises to remote peers has
// to be its own public address (the correction), while what the server *sends to* has to stay the
// tuple the packet actually came from -- the router's hairpin path, the only one our datagrams
// reach. This test makes the two addresses differ on purpose: the host listens on this machine's
// LAN interface and REMOTE60_PUBLIC_IP is a documentation address nothing answers on. If the
// server ever aims its own wake or relay traffic at the advertised address, nothing arrives and
// the checks below fail.
//
// Run against a server started with REMOTE60_PUBLIC_IP=T_PUBLIC_IP and the relay on 127.0.0.1.
const http = require('http');
const dgram = require('dgram');
const os = require('os');

const HTTP = Number(process.env.T_PORT || 18080);
const UDP = Number(process.env.T_UDP || 18081);
const RELAY_PORT = Number(process.env.T_RELAY || 18443);
const GRACE_MS = Number(process.env.REMOTE60_RELAY_GRACE_MS || 400);
const PUBLIC_IP = process.env.T_PUBLIC_IP || '203.0.113.9';

const MAGIC = 0x31435052;
const HELLO_BYTES = 49;
const KIND_HELLO = 300;
const KIND_HELLO_ACK = 301;
const KIND_PUNCH = 303;
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
        let out = '';
        res.on('data', (c) => (out += c));
        res.on('end', () => {
          let parsed = {};
          try { parsed = JSON.parse(out); } catch { /* keep {} */ }
          resolve({ status: res.statusCode, body: parsed });
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

function recorder() {
  const sock = dgram.createSocket('udp4');
  const got = [];
  sock.on('message', (msg, rinfo) => got.push({ msg, rinfo }));
  return { sock, got };
}

/** OBSERVE from `sock` to the server at `serverIp`; resolves with what the server reported. */
function observe(sock, token, serverIp) {
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
    sock.send(Buffer.from('OBSERVE ' + token), UDP, serverIp);
  });
}

const bindOn = (sock, ip) => new Promise((r) => sock.bind(0, ip, r));

function lanAddress() {
  for (const addrs of Object.values(os.networkInterfaces())) {
    for (const a of addrs || []) {
      if (a.family === 'IPv4' && !a.internal && a.address) return a.address;
    }
  }
  return null;
}

const isPunch = (p) => p.msg.length === HELLO_BYTES && p.msg.readUInt32LE(0) === MAGIC &&
                       p.msg.readUInt16LE(4) === KIND_PUNCH;

/**
 * A LAN address the server will actually store.
 *
 * sanitizeIpv4List drops 169.254.x on purpose -- link-local means DHCP failed and can never
 * reach a peer -- and this machine has one. lanAddress() returns whichever interface comes
 * first, so it can hand back an address the fixture binds to happily while the server discards
 * it, leaving localIps empty and the hairpin case below testing nothing at all. It did.
 */
function publishableLanAddress() {
  for (const addrs of Object.values(os.networkInterfaces())) {
    for (const a of addrs || []) {
      if (a.family !== 'IPv4' || a.internal || !a.address) continue;
      if (a.address.startsWith('169.254.') || a.address.startsWith('127.')) continue;
      return a.address;
    }
  }
  return null;
}

(async () => {
  const lan = lanAddress();
  if (!lan) {
    console.log('SKIP  no non-loopback IPv4 interface on this machine');
    process.exit(0);
  }

  let r = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
  const session = r.body.sessionToken;
  check('login', r.status === 200 && !!session, `status=${r.status}`);

  r = await api('POST', '/api/host/register',
                { id: 'tester', pw: 'test-pass-1234', hostName: 'lan-host', machineId: 'machine-lan' });
  const hostToken = r.body.hostToken;
  const hostId = r.body.hostId;
  check('host registers', r.status === 200 && !!hostToken, `status=${r.status}`);

  // ---------------------------------------------------------------- the host, on our LAN

  const host = recorder();
  await bindOn(host.sock, lan);
  const hostPort = host.sock.address().port;
  const hostObs = await observe(host.sock, 'lan-host-observe', lan);
  host.got.length = 0;
  check('a host on the server\'s lan is told the public address, not its own',
        hostObs.ip === PUBLIC_IP && hostObs.port === hostPort,
        `reported=${hostObs.ip}:${hostObs.port} expected=${PUBLIC_IP}:${hostPort}`);

  await api('POST', '/api/host/heartbeat', {
    hostToken, hostName: 'lan-host', observeToken: 'lan-host-observe',
    localUdpPort: hostPort, localIps: [lan],
  });

  // ---------------------------------------------------------------- the client, elsewhere

  const client = recorder();
  await bindOn(client.sock, '127.0.0.1');
  await observe(client.sock, 'lan-client-observe', '127.0.0.1');
  client.got.length = 0;

  r = await api('POST', '/api/connect', { hostId, observeToken: 'lan-client-observe' }, session);
  const cands = r.body.candidates || [];
  const punchToken = r.body.punchToken;
  check('connect succeeds', r.status === 200 && !!punchToken, `status=${r.status}`);
  const pub = cands.find((c) => c.kind === 'public');
  check('the public candidate a remote client gets is the advertised address',
        !!pub && pub.ip === PUBLIC_IP && pub.port === hostPort,
        pub ? `${pub.ip}:${pub.port}` : 'no public candidate');

  // The wake is the server's own datagram, so it must take the wire path.
  await sleep(500);
  const wake = host.got.find(isPunch);
  check('the wake reaches the host over the wire tuple, not the advertised one', !!wake,
        `hostReceived=${host.got.length}`);
  check('...from the observe socket', !!wake && wake.rinfo.port === UDP,
        wake ? `src=${wake.rinfo.address}:${wake.rinfo.port}` : 'missing');

  // ---------------------------------------------------------------- relay, host leg on the wire

  client.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, '127.0.0.1');
  await sleep(GRACE_MS + 400);

  host.got.length = 0;
  client.got.length = 0;
  const hello = buildHello(KIND_HELLO, punchToken, FEATURE_FEC);
  client.sock.send(hello, RELAY_PORT, '127.0.0.1');
  await sleep(300);
  const forwarded = host.got.find((p) => p.msg.length === HELLO_BYTES &&
                                         p.msg.readUInt16LE(4) === KIND_HELLO);
  check('the relayed Hello reaches the host on its wire tuple', !!forwarded,
        `hostReceived=${host.got.length}`);
  check('...from the observe socket', !!forwarded && forwarded.rinfo.port === UDP,
        forwarded ? `src=${forwarded.rinfo.address}:${forwarded.rinfo.port}` : 'missing');

  if (forwarded) {
    host.sock.send(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH),
                   forwarded.rinfo.port, forwarded.rinfo.address);
    await sleep(250);
    const ack = client.got.find((p) => p.msg.length === HELLO_BYTES &&
                                       p.msg.readUInt16LE(4) === KIND_HELLO_ACK);
    check('the host\'s HelloAck comes back to the client (h2c > 0)', !!ack,
          `clientReceived=${client.got.length}`);
  }

  // ---------------------------------------------------------------- the host moves

  // A later heartbeat from a different socket is what "the wire tuple changed" looks like. The
  // relay session was bound to the first one; both directions must now use the second.
  const host2 = recorder();
  await bindOn(host2.sock, lan);
  await observe(host2.sock, 'lan-host-observe-2', lan);
  host2.got.length = 0;
  await api('POST', '/api/host/heartbeat', {
    hostToken, hostName: 'lan-host', observeToken: 'lan-host-observe-2',
    localUdpPort: host2.sock.address().port, localIps: [lan],
  });

  host.got.length = 0;
  host2.got.length = 0;
  const media = Buffer.from('client media after the move');
  client.sock.send(media, RELAY_PORT, '127.0.0.1');
  await sleep(250);
  check('after the host moves, client media follows it to the new tuple',
        host2.got.some((p) => p.msg.equals(media)) && !host.got.some((p) => p.msg.equals(media)),
        `old=${host.got.length} new=${host2.got.length}`);

  client.got.length = 0;
  const reply = Buffer.from('host media from the new tuple');
  host2.sock.send(reply, UDP, lan);
  await sleep(250);
  check('...and host media from the new tuple still reaches the client',
        client.got.some((p) => p.msg.equals(reply)), `clientReceived=${client.got.length}`);

  // A fresh connect wakes the host where it is now, not where it was.
  await sleep(1100);  // past the per-host wake interval
  host.got.length = 0;
  host2.got.length = 0;
  await api('POST', '/api/connect', { hostId, observeToken: 'lan-client-observe' }, session);
  await sleep(500);
  check('a later wake goes to the moved tuple',
        host2.got.some(isPunch) && !host.got.some(isPunch),
        `old=${host.got.filter(isPunch).length} new=${host2.got.filter(isPunch).length}`);

  // ------------------------------------ the wire tuple is on our lan but is NOT the host
  //
  // The field failure of 2026-09-18, reduced to one machine. A router doing hairpin NAT rewrites
  // the source of the host's OBSERVE, so the wire tuple the server records is on our LAN and yet
  // answers for nobody -- 192.168.0.1, the router, while the host is at 192.168.0.76. Everything
  // above uses a host whose wire tuple IS the host, so it cannot tell the two apart.
  //
  // Here the rewrite is a port rather than an address. That is the only part that would otherwise
  // need a second machine, and it changes nothing about the decision: the host listens where it
  // publishes, not where its OBSERVE came from, and anything aimed at the wire tuple is lost.
  {
    r = await api('POST', '/api/host/register',
                  { id: 'tester', pw: 'test-pass-1234', hostName: 'hairpin-host',
                    machineId: 'machine-hairpin' });
    const hpToken = r.body.hostToken;
    const hpId = r.body.hostId;
    check('the hairpin fixture registers', r.status === 200 && !!hpToken, `status=${r.status}`);

    // Socket A -- where the OBSERVE comes from, and therefore what becomes the wire tuple. It
    // stands in for the router.
    const hpLan = publishableLanAddress();
    check('the hairpin case has an address the server will store', !!hpLan,
          hpLan || 'only link-local addresses on this machine');
    const wireSock = recorder();
    await bindOn(wireSock.sock, hpLan);
    await observe(wireSock.sock, 'hairpin-observe', hpLan);
    wireSock.got.length = 0;

    // Socket B -- where the host actually is, which is what it publishes in its heartbeat.
    const realSock = recorder();
    await bindOn(realSock.sock, hpLan);
    const realPort = realSock.sock.address().port;

    const hb = await api('POST', '/api/host/heartbeat', {
      hostToken: hpToken, hostName: 'hairpin-host', observeToken: 'hairpin-observe',
      localUdpPort: realPort, localIps: [hpLan],
    });
    check('the hairpin heartbeat is accepted', hb.status === 200, `status=${hb.status}`);

    await sleep(1100);  // past the per-host wake interval
    wireSock.got.length = 0;
    realSock.got.length = 0;
    r = await api('POST', '/api/connect',
                  { hostId: hpId, observeToken: 'lan-client-observe' }, session);
    const hpPunchToken = r.body.punchToken;
    check('the hairpin fixture accepts a connect', r.status === 200 && !!hpPunchToken,
          `status=${r.status}`);

    await sleep(500);
    check('the wake goes where the host says it is, not where its observe came from',
          realSock.got.some(isPunch) && !wireSock.got.some(isPunch),
          `published=${realSock.got.filter(isPunch).length} wire=${wireSock.got.filter(isPunch).length}`);

    // The relay leg has to make the same choice. Bound to the wire tuple it relays to nobody,
    // which is what the field log showed: `bound host=192.168.0.1:43000` and then
    // `closed reason=no HelloAck h2c=0/0B`.
    client.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, '127.0.0.1');
    await sleep(GRACE_MS + 400);
    wireSock.got.length = 0;
    realSock.got.length = 0;
    client.got.length = 0;
    client.sock.send(buildHello(KIND_HELLO, hpPunchToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
    await sleep(300);
    const isHello = (p) => p.msg.length === HELLO_BYTES && p.msg.readUInt16LE(4) === KIND_HELLO;
    check('the relayed Hello goes to the published tuple too',
          realSock.got.some(isHello) && !wireSock.got.some(isHello),
          `published=${realSock.got.filter(isHello).length} wire=${wireSock.got.filter(isHello).length}`);

    const relayed = realSock.got.find(isHello);
    if (relayed) {
      realSock.sock.send(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH),
                         relayed.rinfo.port, relayed.rinfo.address);
      await sleep(250);
      check('...so the HelloAck reaches the client instead of h2c staying at zero',
            client.got.some((q) => q.msg.length === HELLO_BYTES &&
                                   q.msg.readUInt16LE(4) === KIND_HELLO_ACK),
            `clientReceived=${client.got.length}`);
    }

    // And the next heartbeat must not put it back on the wire tuple. The re-key reads the
    // addresses the heartbeat has just stored, which is why it happens after they are stored.
    await api('POST', '/api/host/heartbeat', {
      hostToken: hpToken, hostName: 'hairpin-host', observeToken: 'hairpin-observe',
      localUdpPort: realPort, localIps: [hpLan],
    });
    wireSock.got.length = 0;
    realSock.got.length = 0;
    const media = Buffer.from('client media after a heartbeat');
    client.sock.send(media, RELAY_PORT, '127.0.0.1');
    await sleep(250);
    check('a heartbeat does not move the session back onto the wire tuple',
          realSock.got.some((q) => q.msg.equals(media)) &&
              !wireSock.got.some((q) => q.msg.equals(media)),
          `published=${realSock.got.length} wire=${wireSock.got.length}`);

    // And when the published port CHANGES, the re-key has to follow the new one. This is what
    // pins the ordering rather than the choice: if the aim is taken before the heartbeat stores
    // what the host just published, it is computed from the PREVIOUS port, matches where the
    // session already is, and returns early -- so the session stays on an address the host has
    // stopped listening on. The check above cannot see that, because there the two heartbeats
    // publish the same port.
    const movedSock = recorder();
    await bindOn(movedSock.sock, hpLan);
    const movedPort = movedSock.sock.address().port;
    const hbMoved = await api('POST', '/api/host/heartbeat', {
      hostToken: hpToken, hostName: 'hairpin-host', observeToken: 'hairpin-observe',
      localUdpPort: movedPort, localIps: [hpLan],
    });
    check('the moved heartbeat is accepted', hbMoved.status === 200, `status=${hbMoved.status}`);

    realSock.got.length = 0;
    movedSock.got.length = 0;
    const moved = Buffer.from('client media after the published port moved');
    client.sock.send(moved, RELAY_PORT, '127.0.0.1');
    await sleep(250);
    check('a session follows the port the host has just published',
          movedSock.got.some((q) => q.msg.equals(moved)) &&
              !realSock.got.some((q) => q.msg.equals(moved)),
          `moved=${movedSock.got.length} previous=${realSock.got.length}`);

    movedSock.sock.close();
    wireSock.sock.close();
    realSock.sock.close();
  }

  host.sock.close();
  host2.sock.close();
  client.sock.close();
  console.log(failures === 0 ? '\nsame_lan_test: PASS' : `\nsame_lan_test: ${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => {
  console.error('same_lan_test error:', e.message);
  process.exit(1);
});
