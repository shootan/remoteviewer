// Exercises the relay end to end, with a fake host and a fake phone talking real packets.
//
// The relay's whole claim is that neither peer needs to know it exists: the client keeps whichever
// candidate answers, the host binds to whoever sent the Hello it authorised. Nothing here mocks
// those two behaviours -- the fake peers do what the real ones do at the wire, and the test asserts
// that a session forms and bytes cross. What is checked just as hard is the other half of the
// claim: that a client who was not listed is never offered the relay, and that a punch or a bad
// token opens nothing.
//
// Run against a server started with REMOTE60_RELAY_ENABLED=1.
const http = require('http');
const dgram = require('dgram');

const HTTP = Number(process.env.T_PORT || 18080);
const UDP = Number(process.env.T_UDP || 18081);
const RELAY_PORT = Number(process.env.T_RELAY || 18443);
const GRACE_MS = Number(process.env.REMOTE60_RELAY_GRACE_MS || 400);

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

// Same layout as UdpHelloPacket in poc_protocol.hpp, which sits in a #pragma pack(1) region.
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

// A socket that records everything it receives, so assertions can be made after the fact rather
// than racing the packet they are about.
function recorder() {
  const sock = dgram.createSocket('udp4');
  const got = [];
  sock.on('message', (msg, rinfo) => got.push({ msg, rinfo }));
  return { sock, got };
}

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

const bind = (sock) => new Promise((r) => sock.bind(0, r));

(async () => {
  let r = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
  const session = r.body.sessionToken;
  check('login', r.status === 200 && !!session, `status=${r.status}`);

  r = await api('POST', '/api/host/register',
                { id: 'tester', pw: 'test-pass-1234', hostName: 'relay-host', machineId: 'machine-relay' });
  const hostToken = r.body.hostToken;
  const hostId = r.body.hostId;
  check('host registers', r.status === 200 && !!hostToken, `status=${r.status}`);

  // The fake host: one socket that observes, heartbeats, and then serves as the media endpoint --
  // exactly the arrangement the real host has, and the reason the relay can reach it at all.
  const host = recorder();
  await bind(host.sock);
  const hostObs = await observe(host.sock, 'relay-host-observe');
  host.got.length = 0;
  await api('POST', '/api/host/heartbeat', {
    hostToken, hostName: 'relay-host', observeToken: 'relay-host-observe',
    localUdpPort: host.sock.address().port, localIps: ['192.168.20.50'],
  });
  check('host is online at its observed port', hostObs.port === host.sock.address().port,
        `observed=${hostObs.port} local=${host.sock.address().port}`);

  // ---------------------------------------------------------------- eligibility

  const client = recorder();
  await bind(client.sock);
  await observe(client.sock, 'relay-client-observe');
  client.got.length = 0;

  host.got.length = 0;
  r = await api('POST', '/api/connect', { hostId, observeToken: 'relay-client-observe' }, session);
  const cands = r.body.candidates || [];
  const punchToken = r.body.punchToken;

  // The relay answers the client's punch instead of forwarding it, so the host never hears the
  // peer knocking and would otherwise learn of it from its next heartbeat -- up to 25 seconds
  // away, against a client that gives up on Hello in about three. The wake closes that gap, which
  // makes it load-bearing rather than diagnostic, and it must not drift back behind the diag flag.
  await sleep(300);
  const wake = host.got.find((p) => p.msg.length === HELLO_BYTES &&
                                    p.msg.readUInt32LE(0) === MAGIC &&
                                    p.msg.readUInt16LE(4) === KIND_PUNCH);
  check('a relay connect wakes the host immediately', !!wake, `hostReceived=${host.got.length}`);
  check('the wake comes from the observe socket, which the host can answer',
        !!wake && wake.rinfo.port === UDP,
        wake ? `src=${wake.rinfo.port} expected=${UDP}` : 'missing');
  const relayCands = cands.filter((c) => c.kind === 'relay');
  check('connect succeeds', r.status === 200, `status=${r.status}`);
  check('exactly one relay candidate is offered', relayCands.length === 1, JSON.stringify(cands));
  check('the relay candidate is last, never first',
        cands.length > 1 && cands[cands.length - 1].kind === 'relay' && cands[0].kind !== 'relay',
        `first=${cands[0] && cands[0].kind} last=${cands[cands.length - 1] && cands[cands.length - 1].kind}`);
  check('the direct candidates are untouched',
        cands.some((c) => c.kind === 'private') && cands.some((c) => c.kind === 'public'),
        JSON.stringify(cands.map((c) => c.kind)));

  // The host's copy of the capability must survive the relay's copy, or the host cannot authorise
  // the very session the relay is carrying.
  r = await api('POST', '/api/host/heartbeat', {
    hostToken, hostName: 'relay-host', observeToken: 'relay-host-observe',
    localUdpPort: host.sock.address().port,
  });
  const pending = r.body.pendingPunch || [];
  check('the host still receives its own punch capability',
        pending.length === 1 && pending[0].punchToken === punchToken,
        JSON.stringify(pending));

  // An account outside the allowlist must not even learn the relay exists. This is the property
  // that makes "the relay cannot disturb the working paths" structural: the candidate is the only
  // way a client could ever reach it, and an unlisted client is never given one.
  await api('POST', '/api/signup',
            { id: 'outsider', pw: 'outsider-pass-1234', signupKey: 'test-signup-key' });
  const outsider = (await api('POST', '/api/login',
                              { id: 'outsider', pw: 'outsider-pass-1234' })).body.sessionToken;
  r = await api('POST', '/api/host/register',
                { id: 'outsider', pw: 'outsider-pass-1234', hostName: 'other-host',
                  machineId: 'machine-outsider' });
  const otherHostId = r.body.hostId;
  await api('POST', '/api/host/heartbeat', {
    hostToken: r.body.hostToken, hostName: 'other-host', observeToken: 'relay-host-observe',
  });
  r = await api('POST', '/api/connect',
                { hostId: otherHostId, observeToken: 'relay-client-observe' }, outsider);
  const outsiderCands = r.body.candidates || [];
  check('an account outside the allowlist is offered no relay',
        r.status === 200 && !outsiderCands.some((c) => c.kind === 'relay'),
        `status=${r.status} kinds=${JSON.stringify(outsiderCands.map((c) => c.kind))}`);

  // ---------------------------------------------------------------- punch timing

  client.got.length = 0;
  client.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, '127.0.0.1');
  await sleep(Math.max(80, GRACE_MS - 250));
  check('the relay stays silent while a direct path could still answer', client.got.length === 0,
        `replies=${client.got.length}`);

  await sleep(400);
  check('the relay answers once the grace has passed', client.got.length > 0,
        `replies=${client.got.length}`);

  // ---------------------------------------------------------------- a bad token opens nothing

  host.got.length = 0;
  client.sock.send(buildHello(KIND_HELLO, 'f'.repeat(32), FEATURE_FEC), RELAY_PORT, '127.0.0.1');
  await sleep(300);
  check('a Hello with an unknown capability is not forwarded', host.got.length === 0,
        `hostReceived=${host.got.length}`);

  // ---------------------------------------------------------------- the session

  host.got.length = 0;
  client.got.length = 0;
  const hello = buildHello(KIND_HELLO, punchToken, FEATURE_FEC);
  client.sock.send(hello, RELAY_PORT, '127.0.0.1');
  await sleep(300);

  const forwarded = host.got.find((p) => p.msg.length === HELLO_BYTES &&
                                         p.msg.readUInt16LE(4) === KIND_HELLO);
  check('a Hello with a valid capability reaches the host', !!forwarded,
        `hostReceived=${host.got.length}`);
  check('the Hello arrives byte for byte as the client sent it',
        !!forwarded && forwarded.msg.equals(hello));
  // This is the property the whole design rests on: the host answers whoever sent the Hello, so
  // the relay must speak from the socket the host already has a NAT mapping toward.
  check('the Hello comes from the observe socket, the one address the host can answer',
        !!forwarded && forwarded.rinfo.port === UDP,
        forwarded ? `src=${forwarded.rinfo.address}:${forwarded.rinfo.port} expected=${UDP}` : 'missing');

  if (forwarded) {
    const ack = buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH);
    host.sock.send(ack, forwarded.rinfo.port, forwarded.rinfo.address);
    await sleep(250);
    const gotAck = client.got.find((p) => p.msg.length === HELLO_BYTES &&
                                          p.msg.readUInt16LE(4) === KIND_HELLO_ACK);
    check('the host\'s HelloAck comes back to the client', !!gotAck,
          `clientReceived=${client.got.length}`);
    check('the Ack still carries the directory-auth bit the client checks for',
          !!gotAck && (gotAck.msg.readUInt32LE(12) & FEATURE_DIRECTORY_AUTH) !== 0);
    check('the Ack appears to come from the relay, as the client expects',
          !!gotAck && gotAck.rinfo.port === RELAY_PORT,
          gotAck ? `src=${gotAck.rinfo.port} expected=${RELAY_PORT}` : 'missing');

    // Opaque payloads, both directions: the relay must not care what the bytes mean.
    host.got.length = 0;
    client.got.length = 0;
    const upstream = Buffer.alloc(1200, 0xab);
    upstream.writeUInt32LE(MAGIC, 0);
    client.sock.send(upstream, RELAY_PORT, '127.0.0.1');
    const downstream = Buffer.alloc(1200, 0xcd);
    downstream.writeUInt32LE(MAGIC, 0);
    host.sock.send(downstream, UDP, '127.0.0.1');
    await sleep(300);
    check('client media reaches the host unchanged',
          host.got.some((p) => p.msg.equals(upstream)), `hostReceived=${host.got.length}`);
    check('host media reaches the client unchanged',
          client.got.some((p) => p.msg.equals(downstream)), `clientReceived=${client.got.length}`);
  }

  // ---------------------------------------------------------------- restart takes the host back

  // The case that matters most in practice and is easiest to get wrong: the app is killed and
  // reopened. Nobody released the old session -- the phone that owned it is gone -- and the host
  // may still be sending video to it, so a lease held by "whoever got there first" would lock the
  // user out of their own machine until a timeout nobody wants to wait for.
  const reborn = recorder();
  await bind(reborn.sock);
  await observe(reborn.sock, 'relay-client-observe-2');
  reborn.got.length = 0;

  r = await api('POST', '/api/connect', { hostId, observeToken: 'relay-client-observe-2' }, session);
  const secondToken = r.body.punchToken;
  check('a restarted app gets a fresh capability',
        r.status === 200 && !!secondToken && secondToken !== punchToken, `status=${r.status}`);

  // The host keeps streaming to the endpoint it last knew, exactly as it would in the field.
  const stale = Buffer.alloc(1200, 0xee);
  stale.writeUInt32LE(MAGIC, 0);
  host.sock.send(stale, UDP, '127.0.0.1');
  await sleep(120);

  host.got.length = 0;
  const secondHello = buildHello(KIND_HELLO, secondToken, FEATURE_FEC);
  reborn.sock.send(secondHello, RELAY_PORT, '127.0.0.1');
  await sleep(300);
  const secondForward = host.got.find((p) => p.msg.length === HELLO_BYTES &&
                                             p.msg.readUInt16LE(4) === KIND_HELLO);
  check('the new capability takes the host from the abandoned session', !!secondForward,
        `hostReceived=${host.got.length}`);

  if (secondForward) {
    const ack2 = buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH);
    host.sock.send(ack2, secondForward.rinfo.port, secondForward.rinfo.address);
    await sleep(250);
    check('the restarted app receives the Ack',
          reborn.got.some((p) => p.msg.length === HELLO_BYTES &&
                                 p.msg.readUInt16LE(4) === KIND_HELLO_ACK),
          `received=${reborn.got.length}`);
  }

  // And the session it replaced must not be able to take it back by retransmitting.
  host.got.length = 0;
  client.sock.send(hello, RELAY_PORT, '127.0.0.1');
  await sleep(250);
  check('the superseded capability cannot reclaim the host', host.got.length === 0,
        `hostReceived=${host.got.length}`);

  // The old phone's media must stop crossing too -- otherwise two clients drive one host.
  host.got.length = 0;
  const orphan = Buffer.alloc(300, 0x11);
  orphan.writeUInt32LE(MAGIC, 0);
  client.sock.send(orphan, RELAY_PORT, '127.0.0.1');
  await sleep(200);
  check('media from the superseded session is dropped',
        !host.got.some((p) => p.msg.equals(orphan)), `hostReceived=${host.got.length}`);

  // The live session must be undamaged by all of that.
  reborn.got.length = 0;
  const afterward = Buffer.alloc(1200, 0x77);
  afterward.writeUInt32LE(MAGIC, 0);
  host.sock.send(afterward, UDP, '127.0.0.1');
  await sleep(250);
  check('the current session still carries host media',
        reborn.got.some((p) => p.msg.equals(afterward)), `received=${reborn.got.length}`);

  reborn.sock.close();

  // ---------------------------------------------------------------- the host is still findable

  // A host mid-relay keeps observing from the same tuple the relay forwards media on. If those
  // observations were read as media, the host would silently drop out of the directory.
  const stillObserved = await observe(host.sock, 'relay-host-observe-2').catch(() => null);
  check('the host can still observe while relaying', !!stillObserved,
        stillObserved ? `${stillObserved.ip}:${stillObserved.port}` : 'no reply');

  host.sock.close();
  client.sock.close();
  console.log(failures === 0 ? '\nRESULT: ALL PASS' : `\nRESULT: ${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
