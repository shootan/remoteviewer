'use strict';

/**
 * remote60 directory service.
 *
 * Lets a phone find and reach a PC without any port forwarding. Both sides connect *outbound*
 * to this server, which is allowed through corporate firewalls, and it introduces them to each
 * other. Media never passes through here — the server only exchanges addresses and coordinates
 * the UDP hole punch — so it stays cheap enough for a small instance.
 *
 * Configuration (environment only; never commit credentials):
 *   REMOTE60_DIR_PORT        HTTP port                       (default 8080)
 *   REMOTE60_DIR_UDP_PORT    UDP address-observation port    (default 8081)
 *   REMOTE60_DIR_DATA        account/host store path         (default ./directory-data.json)
 *   REMOTE60_DIR_TLS_KEY     PEM key  — enables HTTPS when both are set
 *   REMOTE60_DIR_TLS_CERT    PEM cert
 *   REMOTE60_DIR_SIGNUP_KEY  shared secret that allows account creation; unset = no signup
 *   REMOTE60_DIR_MIN_PASSWORD  shortest password signup will accept (default 8)
 *
 * Temporary NAT diagnostics (default off; remove once the company-network question is settled):
 *   REMOTE60_NAT_DIAG_ENABLED  1 = run the silent probe listener and offer its candidate
 *   REMOTE60_NAT_DIAG_IP       this server's public IPv4, as the phone must dial it
 *   REMOTE60_NAT_DIAG_PORT     port the probe listens on               (default 43000)
 *   REMOTE60_NAT_DIAG_WAKE     1 = poke the host over UDP the moment a connect is requested
 */

const http = require('http');
const https = require('https');
const dgram = require('dgram');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const HTTP_PORT = Number(process.env.REMOTE60_DIR_PORT || 8080);
const UDP_PORT = Number(process.env.REMOTE60_DIR_UDP_PORT || 8081);
const DATA_PATH = process.env.REMOTE60_DIR_DATA || path.join(__dirname, 'directory-data.json');
const TLS_KEY = process.env.REMOTE60_DIR_TLS_KEY || '';
const TLS_CERT = process.env.REMOTE60_DIR_TLS_CERT || '';
// Account creation is off unless a key is configured: this server is reachable from the
// internet, and an open signup endpoint would let anyone register on it.
const SIGNUP_KEY = process.env.REMOTE60_DIR_SIGNUP_KEY || '';
// The operator's call, not ours. Short passwords are genuinely weak on a server that
// grants remote control of a PC, so the default stays at 8 and lowering it is explicit.
const MIN_PASSWORD = Math.max(1, Number(process.env.REMOTE60_DIR_MIN_PASSWORD || 8));

// ---------------------------------------------------------------- nat diagnostics (temporary)
//
// Two questions have to be separated before any more traversal work is worth doing: whether the
// phone's network refuses UDP to the port the host actually listens on, and whether our own
// signalling is simply too late. Both answers have to land in a log we can read -- Android 11
// keeps the client's log inside Android/data, where no file manager can reach it -- so the
// evidence is produced here and in the host log, never on the phone.
//
// Off unless explicitly enabled, and the probe never transmits, so with the flags unset this
// block cannot influence a connection at all.
const NAT_DIAG_ENABLED = process.env.REMOTE60_NAT_DIAG_ENABLED === '1';
const NAT_DIAG_IP = String(process.env.REMOTE60_NAT_DIAG_IP || '').trim();
const NAT_DIAG_PORT = Number(process.env.REMOTE60_NAT_DIAG_PORT || 43000);
const NAT_DIAG_WAKE = process.env.REMOTE60_NAT_DIAG_WAKE === '1';
const NAT_DIAG_TTL_MS = 2 * 60 * 1000;

// Correlates a probe packet back to the connect that provoked it. Keyed by source IP rather than
// ip:port on purpose: a mapping whose port differs from the one seen on the observe socket is
// itself a finding (endpoint-dependent NAT), and keying on the port would hide exactly that.
const natDiagByIp = new Map();

// The observe socket, kept so the wake can be sent from the exact address the host already has a
// NAT mapping toward. Sending from any other socket would need a mapping that does not exist.
let observeSock = null;

const SESSION_TTL_MS = 12 * 60 * 60 * 1000;   // 12 h
const HOST_OFFLINE_MS = 90 * 1000;            // no heartbeat for 90 s = offline
const PUNCH_TTL_MS = 30 * 1000;
const OBSERVE_TTL_MS = 5 * 60 * 1000;
const MAX_BODY_BYTES = 16 * 1024;

// ---------------------------------------------------------------- persistence

/** @type {{accounts: Object, hosts: Object}} */
let store = { accounts: {}, hosts: {} };

function loadStore() {
  try {
    store = JSON.parse(fs.readFileSync(DATA_PATH, 'utf8'));
    if (!store.accounts) store.accounts = {};
    if (!store.hosts) store.hosts = {};
  } catch (err) {
    if (err.code !== 'ENOENT') console.error('[directory] store read failed:', err.message);
    store = { accounts: {}, hosts: {} };
  }
  indexHostTokens();
}

/**
 * Host tokens have to survive a restart. They used to live only in memory, so every deploy or
 * reboot invalidated them; each PC would then be told its token was unknown and would need a
 * password typed in again, which the host app deliberately does not keep. The store holds only
 * the hash, so a stolen store file still yields no usable credential.
 */
function indexHostTokens() {
  hostTokens.clear();
  for (const host of Object.values(store.hosts)) {
    if (host.tokenHash) hostTokens.set(host.tokenHash, host.hostId);
  }
}

let saveTimer = null;
function saveStoreSoon() {
  if (saveTimer) return;
  saveTimer = setTimeout(() => {
    saveTimer = null;
    const tmp = DATA_PATH + '.tmp';
    try {
      fs.writeFileSync(tmp, JSON.stringify(store, null, 2));
      fs.renameSync(tmp, DATA_PATH);   // atomic: never leave a half-written store
    } catch (err) {
      console.error('[directory] store write failed:', err.message);
    }
  }, 200);
}

/** For changes where losing the last 200 ms means a manual re-sign-in on the host. */
function saveStoreNow() {
  if (saveTimer) {
    clearTimeout(saveTimer);
    saveTimer = null;
  }
  const tmp = DATA_PATH + '.tmp';
  try {
    fs.writeFileSync(tmp, JSON.stringify(store, null, 2));
    fs.renameSync(tmp, DATA_PATH);
  } catch (err) {
    console.error('[directory] store write failed:', err.message);
  }
}

// ---------------------------------------------------------------- credentials

function hashPassword(password, saltHex) {
  const salt = saltHex ? Buffer.from(saltHex, 'hex') : crypto.randomBytes(16);
  const derived = crypto.scryptSync(password, salt, 32);
  return { salt: salt.toString('hex'), hash: derived.toString('hex') };
}

function verifyPassword(password, account) {
  if (!account || !account.salt || !account.hash) return false;
  const { hash } = hashPassword(password, account.salt);
  const a = Buffer.from(hash, 'hex');
  const b = Buffer.from(account.hash, 'hex');
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

function randomToken() {
  return crypto.randomBytes(32).toString('hex');
}

function hashToken(token) {
  return crypto.createHash('sha256').update(String(token)).digest('hex');
}

// ---------------------------------------------------------------- volatile state

const sessions = new Map();      // sessionToken -> {accountId, expiresAt}
const hostTokens = new Map();    // sha256(hostToken) -> hostId, rebuilt from the store
const pendingPunch = new Map();  // hostId       -> [{ip, port, punchToken, expiresAt}]
const observed = new Map();      // observeToken -> {ip, port, at}
const loginFailures = new Map(); // accountId    -> {count, nextAllowedAt}

function sweep() {
  const now = Date.now();
  for (const [token, s] of sessions) if (s.expiresAt <= now) sessions.delete(token);
  for (const [hostId, list] of pendingPunch) {
    const live = list.filter((p) => p.expiresAt > now);
    if (live.length) pendingPunch.set(hostId, live);
    else pendingPunch.delete(hostId);
  }
  for (const [token, o] of observed) if (now - o.at > OBSERVE_TTL_MS) observed.delete(token);
}
setInterval(sweep, 30 * 1000).unref();

// ---------------------------------------------------------------- http helpers

function sendJson(res, status, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(body),
    'cache-control': 'no-store',
  });
  res.end(body);
}

function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', (c) => {
      size += c.length;
      if (size > MAX_BODY_BYTES) {
        reject(new Error('body too large'));
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on('end', () => {
      if (!chunks.length) return resolve({});
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')));
      } catch (err) {
        reject(new Error('invalid json'));
      }
    });
    req.on('error', reject);
  });
}

/** Client address as seen from here; honours a trusted proxy header only when configured. */
function remoteIp(req) {
  const raw = req.socket.remoteAddress || '';
  return raw.startsWith('::ffff:') ? raw.slice(7) : raw;
}

function bearerToken(req) {
  const auth = req.headers['authorization'] || '';
  return auth.startsWith('Bearer ') ? auth.slice(7) : '';
}

function sessionFor(req) {
  const token = bearerToken(req);
  if (!token) return null;
  const s = sessions.get(token);
  if (!s || s.expiresAt <= Date.now()) return null;
  return s;
}

// ---------------------------------------------------------------- routes

/**
 * Creates an account so people can choose their own id and password instead of asking whoever
 * runs the server to make one for them. Gated by a shared key rather than left open, and it
 * refuses to overwrite an existing account so the key alone cannot take one over.
 */
async function handleSignup(req, res) {
  if (!SIGNUP_KEY) {
    return sendJson(res, 403, { error: 'account creation is disabled on this server' });
  }
  const body = await readJsonBody(req);
  const key = String(body.signupKey || '');
  const keyBuf = Buffer.from(key);
  const expected = Buffer.from(SIGNUP_KEY);
  if (keyBuf.length !== expected.length || !crypto.timingSafeEqual(keyBuf, expected)) {
    return sendJson(res, 403, { error: 'signup key is not correct' });
  }

  const id = String(body.id || '').trim().toLowerCase();
  const pw = String(body.pw || '');
  if (!/^[a-z0-9._-]{3,32}$/.test(id)) {
    return sendJson(res, 400, { error: 'id must be 3-32 characters: letters, digits, . _ -' });
  }
  if (pw.length < MIN_PASSWORD) {
    return sendJson(res, 400, { error: `password must be at least ${MIN_PASSWORD} characters` });
  }
  if (store.accounts[id]) {
    return sendJson(res, 409, { error: 'that id is already taken' });
  }

  const { salt, hash } = hashPassword(pw);
  store.accounts[id] = { id, salt, hash, createdAt: Date.now() };
  saveStoreNow();
  console.log(`[directory] account '${id}' created via signup`);
  sendJson(res, 200, { ok: true, id });
}

async function handleLogin(req, res) {
  const body = await readJsonBody(req);
  const id = String(body.id || '').trim().toLowerCase();
  const pw = String(body.pw || '');
  if (!id || !pw) return sendJson(res, 400, { error: 'id and pw are required' });

  const fail = loginFailures.get(id);
  if (fail && fail.nextAllowedAt > Date.now()) {
    const waitSec = Math.ceil((fail.nextAllowedAt - Date.now()) / 1000);
    return sendJson(res, 429, { error: `too many attempts, retry in ${waitSec}s` });
  }

  const account = store.accounts[id];
  // Deliberately identical response for "no such account" and "wrong password" so the API
  // cannot be used to enumerate which accounts exist.
  if (!account || !verifyPassword(pw, account)) {
    // Let a few honest typos through before throttling; only sustained guessing gets delayed.
    const count = (fail ? fail.count : 0) + 1;
    const FREE_ATTEMPTS = 3;
    const delayMs = count <= FREE_ATTEMPTS
      ? 0
      : Math.min(30000, 500 * 2 ** Math.min(count - FREE_ATTEMPTS, 6));
    loginFailures.set(id, { count, nextAllowedAt: Date.now() + delayMs });
    return sendJson(res, 401, { error: 'invalid id or password' });
  }

  loginFailures.delete(id);
  const token = randomToken();
  const expiresAt = Date.now() + SESSION_TTL_MS;
  sessions.set(token, { accountId: id, expiresAt });
  sendJson(res, 200, { sessionToken: token, expiresAt });
}

async function handleHostRegister(req, res) {
  const body = await readJsonBody(req);
  const id = String(body.id || '').trim().toLowerCase();
  const pw = String(body.pw || '');
  const hostName = String(body.hostName || '').trim().slice(0, 64) || 'PC';
  const machineId = String(body.machineId || '').trim().slice(0, 128);
  if (!id || !pw || !machineId) {
    return sendJson(res, 400, { error: 'id, pw and machineId are required' });
  }
  const account = store.accounts[id];
  if (!account || !verifyPassword(pw, account)) {
    return sendJson(res, 401, { error: 'invalid id or password' });
  }

  // Keyed by machine so reinstalling does not pile up duplicate entries.
  let hostId = Object.keys(store.hosts).find(
    (h) => store.hosts[h].accountId === id && store.hosts[h].machineId === machineId,
  );
  if (!hostId) hostId = crypto.randomBytes(8).toString('hex');

  // Re-registering issues a new token; the previous one must stop working.
  const previous = store.hosts[hostId];
  if (previous && previous.tokenHash) hostTokens.delete(previous.tokenHash);

  const hostToken = randomToken();
  const tokenHash = hashToken(hostToken);
  store.hosts[hostId] = {
    hostId,
    accountId: id,
    machineId,
    hostName,
    tokenHash,
    lastSeen: previous ? previous.lastSeen : 0,
    publicIp: previous ? previous.publicIp : '',
    publicUdpPort: previous ? previous.publicUdpPort : 0,
    localIps: previous ? previous.localIps || [] : [],
    localUdpPort: previous ? previous.localUdpPort || 0 : 0,
    alternateUdpPort: previous ? previous.alternateUdpPort || 0 : 0,
  };
  hostTokens.set(tokenHash, hostId);
  saveStoreNow();
  sendJson(res, 200, { hostId, hostToken, hostName });
}

// Whatever a host reports about itself is untrusted input that ends up in another client's
// connect list, so it is bounded and shape-checked rather than passed through.
const MAX_LOCAL_IPS = 6;

function sanitizePort(value) {
  const port = Number(value);
  return Number.isInteger(port) && port > 0 && port <= 65535 ? port : 0;
}

function sanitizeIpv4List(value) {
  if (!Array.isArray(value)) return [];
  const out = [];
  for (const entry of value) {
    const text = String(entry || '');
    // Dotted quad only. Anything else would become an address some other client dials.
    const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(text);
    if (!m) continue;
    if (m.slice(1).some((part) => Number(part) > 255)) continue;
    // Loopback and link-local can never reach a peer; link-local in particular means DHCP failed.
    if (text.startsWith('127.') || text.startsWith('169.254.') || text === '0.0.0.0') continue;
    if (!out.includes(text)) out.push(text);
    if (out.length >= MAX_LOCAL_IPS) break;
  }
  return out;
}

/**
 * The addresses a client should try, most-preferred first.
 *
 * Private candidates lead because when one works the traffic never leaves the LAN -- faster, and
 * the only route at all when the router will not hairpin. The public candidate is the observed
 * mapping. The alternate port exists because a network that filters the first may pass the
 * second, which is the whole reason a single address was not enough.
 */
function connectCandidatesFor(host) {
  const out = [];
  const add = (ip, port, kind) => {
    if (!ip || !port) return;
    if (out.some((c) => c.ip === ip && c.port === port)) return;
    out.push({ ip, port, kind });
  };
  const lanPort = host.localUdpPort || host.publicUdpPort;
  for (const ip of host.localIps || []) add(ip, lanPort, 'private');
  add(host.publicIp, host.publicUdpPort, 'public');
  if (host.alternateUdpPort && host.alternateUdpPort !== host.publicUdpPort) {
    add(host.publicIp, host.alternateUdpPort, 'public-alt');
  }
  // A candidate that exists only to be punched at, never to be used.
  //
  // The client punches every candidate before waiting on any of them, so offering this one makes
  // the phone send a packet to a port we control, on a network we otherwise cannot instrument.
  // Whether it arrives answers the destination-port question outright.
  //
  // It is appended last and the listener never replies, so it cannot be nominated: selection
  // requires an answer, and the no-answer fallback takes the first candidate, not this one.
  if (NAT_DIAG_ENABLED && NAT_DIAG_IP) add(NAT_DIAG_IP, NAT_DIAG_PORT, 'diag-silent');
  return out;
}

/**
 * Silent probe listener. Records what arrives and answers nothing.
 *
 * The silence is the safety property, not an oversight: a reply would make this endpoint eligible
 * to win the client's candidate race, and it would win often, because a server with no NAT in
 * front of it answers faster than the peer we actually want.
 */
function startNatDiagListener() {
  if (!NAT_DIAG_ENABLED) return null;
  const sock = dgram.createSocket('udp4');
  sock.on('message', (msg, rinfo) => {
    const rec = natDiagByIp.get(rinfo.address);
    const age = rec ? Date.now() - rec.at : -1;
    const connectId = rec ? rec.connectId : 'unknown';
    // Same source port as the observe socket saw means one mapping serves both destinations;
    // a different one means the NAT allocates per destination, which changes what a published
    // "observed" address is even worth.
    const samePort = rec ? String(rinfo.port === rec.observedPort) : 'unknown';
    console.log(`[natdiag] connect=${connectId} rx dport=${NAT_DIAG_PORT} ` +
                `src=${rinfo.address}:${rinfo.port} observedPort=${rec ? rec.observedPort : '?'} ` +
                `samePort=${samePort} ageMs=${age} bytes=${msg.length}`);
  });
  sock.on('error', (err) => console.error('[natdiag] listener error:', err.message));
  sock.bind(NAT_DIAG_PORT, () => {
    console.log(`[natdiag] silent probe listening on udp ${NAT_DIAG_PORT}; never replies`);
  });
  return sock;
}

/**
 * Builds the packet the host already treats as "a peer is trying to reach you".
 *
 * Layout is dictated by UdpHelloPacket in poc_protocol.hpp, which sits inside a
 * `#pragma pack(1)` region, so the 49 bytes below are exact rather than approximate. The host
 * checks only the magic and the kind, and does not check who sent it, which is what lets the
 * directory stand in for the peer here.
 */
function buildPunchPacket() {
  const buf = Buffer.alloc(49);
  buf.writeUInt32LE(0x31435052, 0);  // kMagic, "RPC1"
  buf.writeUInt16LE(303, 4);         // UdpPacketKind::Punch
  buf.writeUInt16LE(49, 6);          // size
  buf.writeUInt32LE(2, 8);           // kUdpProtocolVersion
  buf.writeUInt32LE(0, 12);          // features -- unread on this path
  return buf;                         // authToken stays zeroed
}

/**
 * Pokes the host the instant a connect is requested, instead of waiting for its next heartbeat.
 *
 * The host learns about a waiting peer either from that heartbeat -- up to 25 seconds away -- or
 * from the peer's own punch reaching it. The second only happens if the punch already crossed the
 * host's NAT, which is precisely what fails on a restrictive one, so the two paths are circular
 * exactly where it matters. Sending from the observe socket sidesteps that: the host has already
 * opened a mapping toward this address and port.
 *
 * Three sends because a single datagram is a single chance, and this is a diagnostic that has to
 * distinguish "the host was told and still could not connect" from "the host was never told".
 */
function sendWakePunch(sock, host, connectId) {
  if (!sock || !host.publicIp || !host.publicUdpPort) return;
  const packet = buildPunchPacket();
  console.log(`[natdiag] connect=${connectId} wakeTx host=${host.publicIp}:${host.publicUdpPort}`);
  for (const delay of [0, 100, 300]) {
    setTimeout(() => {
      sock.send(packet, host.publicUdpPort, host.publicIp, (err) => {
        if (err) console.error(`[natdiag] connect=${connectId} wakeTx failed: ${err.message}`);
      });
    }, delay).unref();
  }
}

async function handleHostHeartbeat(req, res) {
  const body = await readJsonBody(req);
  const hostToken = String(body.hostToken || '');
  const hostId = hostToken ? hostTokens.get(hashToken(hostToken)) : undefined;
  const host = hostId ? store.hosts[hostId] : null;
  if (!host) return sendJson(res, 401, { error: 'unknown host token' });

  // The observation token ties this heartbeat to the UDP packet that came from the very
  // socket the host will stream on, so the port we hand out is the one NAT actually mapped.
  const obs = observed.get(String(body.observeToken || ''));
  host.publicIp = obs ? obs.ip : remoteIp(req);
  host.publicUdpPort = obs ? obs.port : Number(body.udpPort || 0);
  host.hostName = String(body.hostName || host.hostName).slice(0, 64);
  // Where else this host can be reached. The public address is ours to determine -- we see the
  // mapping NAT actually made -- but a LAN address and a second listening port are only knowable
  // at the host, and one published address cannot serve two networks whose filtering points in
  // opposite directions.
  host.localIps = sanitizeIpv4List(body.localIps);
  host.alternateUdpPort = sanitizePort(body.alternateUdpPort);
  host.localUdpPort = sanitizePort(body.localUdpPort);
  host.lastSeen = Date.now();
  saveStoreSoon();

  const punches = pendingPunch.get(hostId) || [];
  pendingPunch.delete(hostId);
  sendJson(res, 200, {
    ok: true,
    observedIp: host.publicIp,
    observedPort: host.publicUdpPort,
    pendingPunch: punches.map((p) => ({ ip: p.ip, port: p.port, punchToken: p.punchToken })),
  });
}

function handleHosts(req, res) {
  const session = sessionFor(req);
  if (!session) return sendJson(res, 401, { error: 'login required' });
  const now = Date.now();
  const list = Object.values(store.hosts)
    .filter((h) => h.accountId === session.accountId)
    .map((h) => ({
      hostId: h.hostId,
      hostName: h.hostName,
      online: now - h.lastSeen < HOST_OFFLINE_MS,
      lastSeen: h.lastSeen,
    }))
    .sort((a, b) => Number(b.online) - Number(a.online) || a.hostName.localeCompare(b.hostName));
  sendJson(res, 200, { hosts: list });
}

async function handleConnect(req, res) {
  const session = sessionFor(req);
  if (!session) return sendJson(res, 401, { error: 'login required' });
  const body = await readJsonBody(req);
  const host = store.hosts[String(body.hostId || '')];
  if (!host || host.accountId !== session.accountId) {
    return sendJson(res, 404, { error: 'host not found' });
  }
  if (Date.now() - host.lastSeen >= HOST_OFFLINE_MS) {
    return sendJson(res, 409, { error: 'host is offline' });
  }

  const obs = observed.get(String(body.observeToken || ''));
  const clientIp = obs ? obs.ip : remoteIp(req);
  const clientPort = obs ? obs.port : Number(body.udpPort || 0);
  if (!clientPort) return sendJson(res, 400, { error: 'client udp port unknown' });

  const punchToken = crypto.randomBytes(16).toString('hex');
  const list = pendingPunch.get(host.hostId) || [];
  list.push({ ip: clientIp, port: clientPort, punchToken, expiresAt: Date.now() + PUNCH_TTL_MS });
  pendingPunch.set(host.hostId, list);

  if (NAT_DIAG_ENABLED) {
    // One short id ties together the three places this attempt shows up: here, the probe
    // listener, and the host log. Without it, concurrent attempts are indistinguishable.
    const connectId = crypto.randomBytes(4).toString('hex');
    natDiagByIp.set(clientIp, { connectId, observedPort: clientPort, at: Date.now() });
    for (const [ip, rec] of natDiagByIp) {
      if (Date.now() - rec.at > NAT_DIAG_TTL_MS) natDiagByIp.delete(ip);
    }
    console.log(`[natdiag] connect=${connectId} host=${host.hostName} ` +
                `hostPublic=${host.publicIp}:${host.publicUdpPort} ` +
                `clientObserved=${clientIp}:${clientPort} ` +
                `diag=${NAT_DIAG_IP || '(unset)'}:${NAT_DIAG_PORT}`);
    if (NAT_DIAG_WAKE) sendWakePunch(observeSock, host, connectId);
  }

  sendJson(res, 200, {
    // Kept for clients that predate candidates; they dial this one and behave as before.
    hostPublicIp: host.publicIp,
    hostPublicUdpPort: host.publicUdpPort,
    candidates: connectCandidatesFor(host),
    punchToken,
  });
}

const routes = {
  'POST /api/signup': handleSignup,
  'POST /api/login': handleLogin,
  'POST /api/host/register': handleHostRegister,
  'POST /api/host/heartbeat': handleHostHeartbeat,
  'GET /api/hosts': handleHosts,
  'POST /api/connect': handleConnect,
};

async function onRequest(req, res) {
  const url = (req.url || '').split('?')[0];
  if (url === '/healthz') return sendJson(res, 200, { ok: true });
  const handler = routes[`${req.method} ${url}`];
  if (!handler) return sendJson(res, 404, { error: 'not found' });
  try {
    await handler(req, res);
  } catch (err) {
    sendJson(res, 400, { error: err.message || 'bad request' });
  }
}

// ---------------------------------------------------------------- udp observation

/**
 * Minimal STUN-like endpoint. A peer sends "OBSERVE <token>" from the same socket it will
 * stream on; we reply with the source address we saw, which is the address its NAT presents
 * to the outside world and therefore the one the other peer must punch towards.
 */
function startUdp() {
  const sock = dgram.createSocket('udp4');
  sock.on('message', (msg, rinfo) => {
    const text = msg.toString('utf8', 0, Math.min(msg.length, 128)).trim();
    if (!text.startsWith('OBSERVE ')) return;
    const token = text.slice(8).trim().slice(0, 64);
    if (!token) return;
    observed.set(token, { ip: rinfo.address, port: rinfo.port, at: Date.now() });
    const reply = Buffer.from(JSON.stringify({ ip: rinfo.address, port: rinfo.port }));
    sock.send(reply, rinfo.port, rinfo.address);
  });
  sock.on('error', (err) => console.error('[directory] udp error:', err.message));
  sock.bind(UDP_PORT, () => console.log(`[directory] udp observe on ${UDP_PORT}`));
  return sock;
}

// ---------------------------------------------------------------- account admin

function addAccountFromCli() {
  const args = process.argv.slice(2);
  const i = args.indexOf('--add-account');
  if (i < 0) return false;
  const id = String(args[i + 1] || '').trim().toLowerCase();
  const pw = String(args[i + 2] || '');
  if (!id || !pw) {
    console.error('usage: node server.js --add-account <id> <password>');
    process.exit(2);
  }
  loadStore();
  const { salt, hash } = hashPassword(pw);
  store.accounts[id] = { id, salt, hash, createdAt: Date.now() };
  fs.writeFileSync(DATA_PATH, JSON.stringify(store, null, 2));
  console.log(`[directory] account '${id}' saved to ${DATA_PATH}`);
  return true;
}

// ---------------------------------------------------------------- boot

if (!addAccountFromCli()) {
  loadStore();
  const useTls = TLS_KEY && TLS_CERT;
  const server = useTls
    ? https.createServer({ key: fs.readFileSync(TLS_KEY), cert: fs.readFileSync(TLS_CERT) }, onRequest)
    : http.createServer(onRequest);
  server.listen(HTTP_PORT, () => {
    console.log(`[directory] ${useTls ? 'https' : 'http'} on ${HTTP_PORT}`);
    if (!useTls) {
      console.warn('[directory] TLS is off — tokens travel in clear. Set REMOTE60_DIR_TLS_KEY/CERT for anything but local testing.');
    }
  });
  observeSock = startUdp();
  startNatDiagListener();
  if (NAT_DIAG_ENABLED) {
    console.log(`[natdiag] ENABLED ip=${NAT_DIAG_IP || '(unset -- candidate NOT offered)'} ` +
                `port=${NAT_DIAG_PORT} wake=${NAT_DIAG_WAKE ? 'on' : 'off'}`);
  }
}
