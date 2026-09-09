// What the directory publishes when it cannot see the client.
//
// Every HTTP request here arrives through a TCP proxy, so `req.socket.remoteAddress` is the
// proxy's, not the client's. The server used to fall back to exactly that address whenever a
// request carried no address observation -- and to a port the client claimed for itself in the
// body. Behind a proxy, a load balancer or a TLS terminator that means every host is published at
// the same address: punch packets go to the middle box, which is not listening, and two unrelated
// accounts become indistinguishable at the point where relay eligibility is decided.
//
// There is no fallback now. A request that needs an address and has no observation is refused with
// 409 -- not 401, because nothing is wrong with the caller's credentials and it must not sign out
// over this -- and the refusal changes nothing on the server.
//
// This test runs its own server on its own ports with its own data file, and its own proxy in
// front. It starts and stops what it starts, touches nothing installed, and listens on loopback
// only.

const http = require('http');
const net = require('net');
const dgram = require('dgram');
const path = require('path');
const os = require('os');
const fs = require('fs');
const { spawn, spawnSync } = require('child_process');

const SERVER_HTTP = 18190;
const SERVER_UDP = 18191;
const PROXY_PORT = 18192;
const OBSERVE_TTL_MS = 1500;

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/** Every request goes through here, so the server never sees the client's own socket. */
function startProxy() {
  const usedPorts = new Set();
  const server = net.createServer((client) => {
    const upstream = net.connect(SERVER_HTTP, '127.0.0.1', () => {
      // The port the server will see for this request. Recorded so the test can show that the
      // address it published was not this one.
      usedPorts.add(upstream.localPort);
      client.pipe(upstream);
      upstream.pipe(client);
    });
    const drop = () => { client.destroy(); upstream.destroy(); };
    client.on('error', drop);
    upstream.on('error', drop);
  });
  return new Promise((resolve) => {
    server.listen(PROXY_PORT, '127.0.0.1', () => resolve({ server, usedPorts }));
  });
}

function api(method, pathname, body, token, extraHeaders) {
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : null;
    const req = http.request(
      { host: '127.0.0.1', port: PROXY_PORT, path: pathname, method,
        headers: Object.assign(
          payload ? { 'content-type': 'application/json',
                      'content-length': Buffer.byteLength(payload) } : {},
          token ? { authorization: 'Bearer ' + token } : {},
          extraHeaders || {}) },
      (res) => {
        let data = '';
        res.on('data', (c) => (data += c));
        res.on('end', () => {
          try { resolve({ status: res.statusCode, body: JSON.parse(data || '{}') }); }
          catch { resolve({ status: res.statusCode, body: {}, raw: data }); }
        });
      });
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

/** Sends the UDP packet the observation is made from, and reports the socket's own port. */
function observe(token) {
  return new Promise((resolve, reject) => {
    const sock = dgram.createSocket('udp4');
    const timer = setTimeout(() => { sock.close(); reject(new Error('observe timeout')); }, 3000);
    sock.on('message', (msg) => {
      clearTimeout(timer);
      const seen = JSON.parse(msg.toString());
      const localPort = sock.address().port;
      sock.close();
      resolve({ seen, localPort });
    });
    sock.on('error', (e) => { clearTimeout(timer); reject(e); });
    sock.send(Buffer.from('OBSERVE ' + token), SERVER_UDP, '127.0.0.1');
  });
}

function waitForServer() {
  return new Promise((resolve, reject) => {
    const deadline = Date.now() + 8000;
    const attempt = () => {
      const req = http.request({ host: '127.0.0.1', port: SERVER_HTTP, path: '/healthz' },
                               (res) => { res.resume(); resolve(); });
      req.on('error', () => {
        if (Date.now() > deadline) return reject(new Error('server did not come up'));
        setTimeout(attempt, 100);
      });
      req.end();
    };
    attempt();
  });
}

(async () => {
  const serverPath = path.join(__dirname, '..', 'server.js');
  const dataPath = path.join(os.tmpdir(), `remote60-proxy-test-${process.pid}.json`);
  fs.rmSync(dataPath, { force: true });
  const env = { ...process.env,
                REMOTE60_DIR_DATA: dataPath,
                REMOTE60_DIR_PORT: String(SERVER_HTTP),
                REMOTE60_DIR_UDP_PORT: String(SERVER_UDP),
                REMOTE60_DIR_SIGNUP_KEY: 'proxy-signup-key',
                REMOTE60_DIR_OBSERVE_TTL_MS: String(OBSERVE_TTL_MS) };
  spawnSync(process.execPath, [serverPath, '--add-account', 'proxied', 'proxy-pass-1234'],
            { env, stdio: 'ignore' });
  const server = spawn(process.execPath, [serverPath], { env, stdio: 'ignore' });
  const proxy = await startProxy();

  try {
    await waitForServer();

    // ---- the routes that do not need an address work through a proxy unchanged
    let r = await api('GET', '/healthz');
    check('healthz answers through the proxy', r.status === 200, `status=${r.status}`);

    r = await api('POST', '/api/signup',
                  { id: 'proxied-2', pw: 'another-password', signupKey: 'proxy-signup-key' });
    check('signup works through the proxy', r.status === 200, `status=${r.status}`);

    r = await api('POST', '/api/login', { id: 'proxied', pw: 'proxy-pass-1234' });
    check('login works through the proxy', r.status === 200 && !!r.body.sessionToken,
          `status=${r.status}`);
    const session = r.body.sessionToken;

    r = await api('POST', '/api/host/register',
                  { id: 'proxied', pw: 'proxy-pass-1234', hostName: 'Proxied PC',
                    machineId: 'machine-proxy' });
    check('host registration works through the proxy', r.status === 200 && !!r.body.hostToken,
          `status=${r.status}`);
    const hostToken = r.body.hostToken;
    const hostId = r.body.hostId;

    // ---- a heartbeat with no observation is refused, and refused as a state problem
    //
    // The body carries a port the host claims for itself and a header a proxy would set. Both were
    // once believed: the port was taken from the body, the address from the socket. Neither is
    // evidence of where a datagram would actually arrive.
    r = await api('POST', '/api/host/heartbeat',
                  { hostToken, udpPort: 59999 }, null, { 'x-forwarded-for': '203.0.113.9' });
    check('a heartbeat without an observation is refused', r.status === 409, `status=${r.status}`);
    check('...and says which state is missing', r.body.error === 'observation_required',
          `error=${r.body.error}`);
    check('...and it is not 401, which would make the host sign in again', r.status !== 401,
          `status=${r.status}`);

    // ---- and it happened before anything was written
    r = await api('GET', '/api/hosts', null, session);
    const beforeEntry = (r.body.hosts || []).find((h) => h.hostId === hostId);
    check('the host is not marked seen by a refused heartbeat',
          !!beforeEntry && !beforeEntry.lastSeen, `lastSeen=${beforeEntry && beforeEntry.lastSeen}`);

    // ---- with an observation everything works, and the address is the observed one
    const hostObs = await observe('proxy-host-1');
    r = await api('POST', '/api/host/heartbeat', { hostToken, observeToken: 'proxy-host-1' });
    check('a heartbeat with an observation is accepted', r.status === 200 && r.body.ok === true,
          `status=${r.status}`);
    check('the published port is the observed one', r.body.observedPort === hostObs.localPort,
          `published=${r.body.observedPort} observed=${hostObs.localPort}`);
    check('...and not a port the proxy connected on',
          !proxy.usedPorts.has(r.body.observedPort),
          `published=${r.body.observedPort} proxyPorts=${[...proxy.usedPorts].join(',')}`);
    check('...and not the port the body claimed', r.body.observedPort !== 59999,
          `published=${r.body.observedPort}`);

    const seenAt = (await api('GET', '/api/hosts', null, session))
      .body.hosts.find((h) => h.hostId === hostId).lastSeen;
    check('an accepted heartbeat does mark the host seen', seenAt > 0, `lastSeen=${seenAt}`);

    // ---- a connect with no observation is refused, and leaves no punch behind
    r = await api('POST', '/api/connect', { hostId, udpPort: 59998 }, session);
    check('a connect without an observation is refused', r.status === 409, `status=${r.status}`);
    check('...for the same stated reason', r.body.error === 'observation_required',
          `error=${r.body.error}`);

    // The proof that nothing was written: the next heartbeat consumes pending punches, and there
    // must be none to consume. A punch aimed at the proxy would arrive here.
    await observe('proxy-host-2');
    r = await api('POST', '/api/host/heartbeat', { hostToken, observeToken: 'proxy-host-2' });
    check('the refused connect left no punch behind',
          Array.isArray(r.body.pendingPunch) && r.body.pendingPunch.length === 0,
          `pendingPunch=${JSON.stringify(r.body.pendingPunch)}`);

    // ---- a connect with an observation works, and carries the client's own address
    const clientObs = await observe('proxy-client-1');
    r = await api('POST', '/api/connect', { hostId, observeToken: 'proxy-client-1' }, session);
    check('a connect with an observation is accepted', r.status === 200, `status=${r.status}`);

    await observe('proxy-host-3');
    r = await api('POST', '/api/host/heartbeat', { hostToken, observeToken: 'proxy-host-3' });
    const punch = (r.body.pendingPunch || [])[0];
    check('the punch carries the client port that was observed',
          !!punch && punch.port === clientObs.localPort,
          `punch=${punch && punch.port} observed=${clientObs.localPort}`);
    check('...and not a port belonging to the proxy',
          !!punch && !proxy.usedPorts.has(punch.port), `punch=${punch && punch.port}`);

    // ---- an expired observation is its own answer
    //
    // "You never sent one" and "you sent one too long ago" call for different things from the
    // client: the first is a bug in the client, the second is an ordinary retry. They were the
    // same absent entry before, because the sweep deleted expired ones on sight.
    await observe('proxy-host-stale');
    await sleep(OBSERVE_TTL_MS + 300);
    r = await api('POST', '/api/host/heartbeat', { hostToken, observeToken: 'proxy-host-stale' });
    check('an expired observation is refused', r.status === 409, `status=${r.status}`);
    check('...and named as expired, not as missing', r.body.error === 'observation_expired',
          `error=${r.body.error}`);

    r = await api('POST', '/api/connect', { hostId, observeToken: 'proxy-host-stale' }, session);
    check('connect says the same about an expired observation',
          r.status === 409 && r.body.error === 'observation_expired',
          `status=${r.status} error=${r.body.error}`);

    // ---- an unknown token is still missing, not expired
    r = await api('POST', '/api/host/heartbeat', { hostToken, observeToken: 'never-sent-this' });
    check('a token that was never observed is missing, not expired',
          r.status === 409 && r.body.error === 'observation_required',
          `status=${r.status} error=${r.body.error}`);

    // ---- the host list still works, and the host is still the one that registered
    r = await api('GET', '/api/hosts', null, session);
    const entry = (r.body.hosts || []).find((h) => h.hostId === hostId);
    check('the host survived every refusal', !!entry && entry.online === true,
          `entry=${JSON.stringify(entry)}`);
  } catch (err) {
    check('the proxy exercise ran to the end', false, String(err && err.message));
  } finally {
    proxy.server.close();
    await new Promise((resolve) => { server.on('exit', resolve); server.kill(); });
    fs.rmSync(dataPath, { force: true });
  }

  console.log(failures ? `\n${failures} FAILED` : '\nall proxy checks passed');
  process.exit(failures ? 1 : 0);
})();
