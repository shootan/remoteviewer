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
 *   REMOTE60_PUBLIC_IP       this server's own public IPv4; lets it correct the observation
 *                            made for a peer that sits on its own LAN (default: the relay ip)
 *
 * Wake (on by default; a connection often depends on it):
 *   REMOTE60_WAKE_DISABLED     1 = do not poke the host when a connect is requested
 *
 * Temporary NAT diagnostics (default off; remove once the company-network question is settled):
 *   REMOTE60_NAT_DIAG_ENABLED  1 = run the silent probe listener and offer its candidate
 *   REMOTE60_NAT_DIAG_IP       this server's public IPv4, as the phone must dial it
 *   REMOTE60_NAT_DIAG_PORT     port the probe listens on               (default 43000)
 *
 * Relay (default off; for networks where no direct path exists at all):
 *   REMOTE60_RELAY_ENABLED     1 = offer a relay candidate and forward for it
 *   REMOTE60_RELAY_IP          this server's public IPv4, as the phone must dial it
 *   REMOTE60_RELAY_PORT        client-facing relay port                (default 43000)
 *   REMOTE60_RELAY_GRACE_MS    how long a direct path gets alone       (default 2500)
 *   REMOTE60_RELAY_ALLOW_IPS   client public IPv4s allowed to relay; "*" for any, empty = none
 *   REMOTE60_RELAY_ALLOW_ACCOUNTS  account ids allowed to relay; "*" for any, empty = none
 */

const http = require('http');
const https = require('https');
const dgram = require('dgram');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const os = require('os');

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
const NAT_DIAG_TTL_MS = 2 * 60 * 1000;

// ---------------------------------------------------------------- wake
//
// Poking the host the instant a connect is requested, instead of leaving it to find out from its
// next heartbeat.
//
// This began as a diagnostic and turned out to be load-bearing. The host learns that a peer is
// waiting either from that heartbeat -- up to 25 seconds away -- or from the peer's own punch
// arriving, which a restrictive NAT drops precisely when it matters. Against a client that gives
// up on Hello in about three seconds, that leaves success to coincidence: measured at roughly one
// attempt in six on mobile data with the wake off, and first-try every time with it on.
//
// So it defaults to on, and turning it off is the explicit act. It was the other way round for
// one afternoon -- the flag was left out of a deploy and mobile connections quietly stopped
// working, with nothing in any log to say why.
const WAKE_ENABLED = process.env.REMOTE60_WAKE_DISABLED !== '1';
const WAKE_PACKETS = [0, 100, 300];        // one datagram is one chance
const WAKE_MIN_INTERVAL_MS = 1000;         // per host; a retrying client must not become a flood
// Only ever aimed at an address a heartbeat confirmed recently. An older one is a mapping that
// has probably closed, and firing at it is at best noise to a stranger who now holds that port.
const WAKE_HOST_FRESH_MS = 90 * 1000;
// hostId -> last send, so repeated connects for one host collapse into a single burst.
const wakeLastSentByHost = new Map();
const wakeStats = { sent: 0, suppressed: 0, skippedStale: 0, failed: 0 };

// Correlates a probe packet back to the connect that provoked it. Keyed by source IP rather than
// ip:port on purpose: a mapping whose port differs from the one seen on the observe socket is
// itself a finding (endpoint-dependent NAT), and keying on the port would hide exactly that.
const natDiagByIp = new Map();

// The observe socket, kept so the wake can be sent from the exact address the host already has a
// NAT mapping toward. Sending from any other socket would need a mapping that does not exist.
let observeSock = null;

// ---------------------------------------------------------------- relay
//
// For networks that carry UDP outbound but have no path between the two peers at all. Measured
// on one company network: the phone's punches reach this server within milliseconds while not a
// single packet crosses between the guest Wi-Fi and the wired segment, in either direction. No
// amount of timing or port selection fixes that, so the traffic has to go through here.
//
// The whole thing lives in the server. Nothing in the APK or the host knows a relay exists:
//   - the client punches every candidate and keeps whichever answers first, so a candidate that
//     answers is a candidate it will use
//   - the host binds its session to whatever endpoint sent the Hello it authorised, so a Hello
//     forwarded from the observe socket makes this server the peer
// Both are existing behaviours, relied on rather than added.
//
// Two properties keep it from touching the paths that already work. The relay answers only after
// a grace period, so a direct candidate that answers at all answers first; and the candidate is
// offered only to explicitly listed clients, so no one else is ever handed the option.
const RELAY_ENABLED = process.env.REMOTE60_RELAY_ENABLED === '1';
const RELAY_IP = String(process.env.REMOTE60_RELAY_IP || '').trim();
const RELAY_PORT = Number(process.env.REMOTE60_RELAY_PORT || 43000);
const RELAY_GRACE_MS = Number(process.env.REMOTE60_RELAY_GRACE_MS || 2500);

// Fail closed: an unset allowlist offers the relay to nobody. Getting this backwards would put
// every session in the country through this one server, so "unset" must not mean "everyone".
function parseAllowList(raw) {
  const items = String(raw || '').split(',').map((s) => s.trim()).filter(Boolean);
  return { any: items.includes('*'), set: new Set(items) };
}
const RELAY_ALLOW_IPS = parseAllowList(process.env.REMOTE60_RELAY_ALLOW_IPS);
const RELAY_ALLOW_ACCOUNTS = parseAllowList(process.env.REMOTE60_RELAY_ALLOW_ACCOUNTS);

// This server's own public address, used to repair observations that never crossed a NAT.
const PUBLIC_IP = process.env.REMOTE60_PUBLIC_IP || RELAY_IP || '';

function ipToInt(ip) {
  const parts = String(ip).split('.');
  if (parts.length !== 4) return null;
  let v = 0;
  for (const p of parts) {
    const n = Number(p);
    if (!Number.isInteger(n) || n < 0 || n > 255) return null;
    v = ((v << 8) | n) >>> 0;
  }
  return v >>> 0;
}

/** The IPv4 subnets this machine is directly attached to. */
const LOCAL_SUBNETS = (() => {
  const out = [];
  for (const addrs of Object.values(os.networkInterfaces())) {
    for (const a of addrs || []) {
      if (a.family !== 'IPv4' || a.internal || !a.netmask) continue;
      const ip = ipToInt(a.address);
      const mask = ipToInt(a.netmask);
      if (ip === null || mask === null) continue;
      out.push({ net: (ip & mask) >>> 0, mask: mask >>> 0 });
    }
  }
  return out;
})();

/** True when `ip` is on one of the subnets this server is attached to. */
function onServerLan(ip) {
  const v = ipToInt(ip);
  if (v === null) return false;
  return LOCAL_SUBNETS.some((s) => ((v & s.mask) >>> 0) === s.net);
}

const correctionLogged = new Set();

/**
 * What to record as a peer's public address.
 *
 * The observation means something only when the packet crossed the peer's NAT on the way here:
 * the source we then see is the mapping the rest of the internet would use. A peer on our own LAN
 * never crosses it. Worse, a router doing hairpin NAT rewrites the source to its own LAN address
 * -- measured here as 192.168.0.1 -- so what we would record is not merely local, it is a router
 * interface that answers for nobody, and the host would advertise it as its public candidate.
 *
 * For those we substitute our own public address and keep the observed port. The port is the part
 * that has to survive, and it does when the router preserves ports: 구현계획.md E2 measured that
 * for this router (bindPort=43000 -> public=...:43000) and the hairpin probe showed it again
 * (source port 60765 came back unchanged). Where it does not hold, the peer ends up on the relay
 * -- which is where an unusable candidate was sending it anyway, so this cannot be worse.
 */
function observedAddressFor(rinfo) {
  if (!PUBLIC_IP || !onServerLan(rinfo.address)) {
    return { ip: rinfo.address, port: rinfo.port };
  }
  if (!correctionLogged.has(rinfo.address)) {
    correctionLogged.add(rinfo.address);
    console.log(`[observe] ${rinfo.address} is on our own lan; reporting ${PUBLIC_IP} instead ` +
                `(port ${rinfo.port} kept)`);
  }
  return { ip: PUBLIC_IP, port: rinfo.port };
}

const RELAY_AUTH_TTL_MS = 60 * 1000;      // how long a connect stays relay-eligible
const RELAY_IDLE_TTL_MS = 60 * 1000;      // a session with no traffic either way is finished
const RELAY_HANDSHAKE_TTL_MS = 30 * 1000; // provisional binding waits this long for a HelloAck
const RELAY_PUNCH_REPLIES = 3;            // one datagram is one chance; the client may miss it

// The capability minted by /api/connect, kept separately from `pendingPunch`. That map is the
// host's copy and heartbeat deletes it on read -- consuming it here would leave the host unable
// to authorise the very session this is for.
const relayAuthByToken = new Map();
// The newest capability minted for each host. A phone that is restarted comes back with a new
// token, and it has to be able to take the host from the session it just abandoned -- the old
// session cannot be asked to release anything, because from the server's side a killed app and a
// quiet one look identical.
const relayLatestTokenByHost = new Map();
// Punch carries no token (the client sends a bare packet), so the punch stage can only be gated
// on "this address asked for a relay-eligible connect recently". Identity comes later, from the
// token inside Hello.
const relayEligibleByIp = new Map();
const relaySessions = new Set();         // authoritative; the maps below are only indexes into it
const relaySessionByClient = new Map();  // "ip:port" of the phone  -> session
const relaySessionByHost = new Map();    // "ip:port" of the host   -> session
const relayLeaseByHost = new Map();      // hostId -> session; one at a time, like the host itself
let relaySock = null;

const UDP_MAGIC = 0x31435052;             // kMagic, "RPC1"
const UDP_HELLO_BYTES = 49;               // sizeof(UdpHelloPacket) under #pragma pack(1)
const UDP_KIND_HELLO = 300;
const UDP_KIND_HELLO_ACK = 301;
const UDP_KIND_PUNCH = 303;
const UDP_PROTOCOL_VERSION = 2;
const UDP_FEATURE_VIDEO_FEC = 0x2;        // the host will not read a Hello without it
const UDP_FEATURE_DIRECTORY_AUTH = 0x4;   // set in HelloAck only when the host authorised us
const UDP_TOKEN_OFFSET = 16;              // char authToken[33] follows the five header words

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
function connectCandidatesFor(host, relayEligible) {
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
  // Last, and only for a client that has been listed. A relay that is merely available would win
  // races it should lose -- a server with no NAT in front of it answers faster than the peer we
  // actually want -- so it is held back both in the list and in time (see relayScheduleReply).
  if (relayEligible && RELAY_IP) {
    add(RELAY_IP, RELAY_PORT, 'relay');
  } else if (NAT_DIAG_ENABLED && NAT_DIAG_IP && !RELAY_ENABLED) {
    // The diagnostic that established the relay was needed. It shares the port, so the two can
    // never run at once; the relay supersedes it.
    //
    // A candidate that exists only to be punched at, never to be used: it is appended last and
    // the listener never replies, so it cannot be nominated -- selection requires an answer, and
    // the no-answer fallback takes the first candidate, not this one.
    add(NAT_DIAG_IP, NAT_DIAG_PORT, 'diag-silent');
  }
  return out;
}

/**
 * Whether this connect may be told about the relay.
 *
 * Both lists must pass. During the POC that means one account on one network, which is what makes
 * "the relay cannot affect the direct paths" a structural claim rather than a hopeful one: nobody
 * else is ever handed the candidate, so nobody else's race can be won by it.
 */
function relayEligibleFor(accountId, clientIp) {
  if (!RELAY_ENABLED || !RELAY_IP) return false;
  const ipOk = RELAY_ALLOW_IPS.any || RELAY_ALLOW_IPS.set.has(clientIp);
  const accountOk = RELAY_ALLOW_ACCOUNTS.any || RELAY_ALLOW_ACCOUNTS.set.has(accountId);
  return ipOk && accountOk;
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
 * Sent from the observe socket because that is the one address the host has an open NAT mapping
 * toward -- it has been sending its own observations there every heartbeat. Any other socket of
 * ours would need a mapping that was never created.
 *
 * Three datagrams because one is a single chance, and the whole point is to beat a client that
 * stops asking after about three seconds.
 */
function sendWakePunch(sock, host, connectId) {
  if (!WAKE_ENABLED || !sock) return false;
  if (!host.publicIp || !host.publicUdpPort) return false;
  // Aim only at an address a heartbeat confirmed recently.
  if (Date.now() - host.lastSeen > WAKE_HOST_FRESH_MS) {
    wakeStats.skippedStale++;
    return false;
  }
  // A client that retries three times in a second means one host to wake, not three bursts at it.
  const last = wakeLastSentByHost.get(host.hostId) || 0;
  if (Date.now() - last < WAKE_MIN_INTERVAL_MS) {
    wakeStats.suppressed++;
    return false;
  }
  wakeLastSentByHost.set(host.hostId, Date.now());
  wakeStats.sent++;

  const packet = buildPunchPacket();
  console.log(`[wake] connect=${connectId} tx host=${host.publicIp}:${host.publicUdpPort}`);
  for (const delay of WAKE_PACKETS) {
    setTimeout(() => {
      sock.send(packet, host.publicUdpPort, host.publicIp, (err) => {
        if (err) {
          wakeStats.failed++;
          console.error(`[wake] connect=${connectId} tx failed: ${err.message}`);
        }
      });
    }, delay).unref();
  }
  return true;
}

// Cheap enough to keep forever, and the only way to notice the wake quietly stopping. Silence
// here is how one afternoon's mobile connections were lost without a single error line.
function logWakeStats() {
  if (!WAKE_ENABLED || wakeStats.sent === 0) return;
  console.log(`[wake] sent=${wakeStats.sent} suppressed=${wakeStats.suppressed} ` +
              `skippedStale=${wakeStats.skippedStale} failed=${wakeStats.failed}`);
}

// ---------------------------------------------------------------- relay engine

const endpointKey = (ip, port) => `${ip}:${port}`;

/**
 * Reads the header of a packet that claims to be one of ours.
 *
 * Only the fixed 49-byte Hello/Punch shape is parsed, and only before a session exists. Once one
 * does, packets are forwarded as opaque bytes -- the relay has no business understanding video.
 */
function parseHandshakePacket(msg) {
  if (msg.length !== UDP_HELLO_BYTES) return null;
  if (msg.readUInt32LE(0) !== UDP_MAGIC) return null;
  // The same fields the host checks before it will treat this as a handshake. Accepting a shape
  // the host would reject only lets a malformed packet take a lease and then strand it.
  if (msg.readUInt16LE(6) !== UDP_HELLO_BYTES) return null;
  if (msg.readUInt32LE(8) !== UDP_PROTOCOL_VERSION) return null;
  const kind = msg.readUInt16LE(4);
  const features = msg.readUInt32LE(12);
  // Both ends require FEC on both halves of the handshake, so requiring it here keeps the relay's
  // idea of the session identical to theirs rather than merely compatible with it.
  if (kind === UDP_KIND_HELLO_ACK && (features & UDP_FEATURE_VIDEO_FEC) === 0) return null;
  let token = '';
  if (kind === UDP_KIND_HELLO) {
    if ((features & UDP_FEATURE_VIDEO_FEC) === 0) return null;
    const end = msg.indexOf(0, UDP_TOKEN_OFFSET);
    const stop = end < 0 ? msg.length : Math.min(end, UDP_TOKEN_OFFSET + 32);
    token = msg.toString('latin1', UDP_TOKEN_OFFSET, stop);
    if (!/^[0-9a-f]{32}$/.test(token)) token = '';
  }
  return { kind, features, token };
}

// Indexes are removed only when they still point at this session. Two sessions can briefly share
// an endpoint -- a phone that reconnects to a different host reuses its address -- and an
// unconditional delete would then evict whichever one arrived second.
function relayUnindex(map, key, session) {
  if (map.get(key) === session) map.delete(key);
}

function relayDropSession(session, reason) {
  if (!session || session.dropped) return;
  session.dropped = true;
  relaySessions.delete(session);
  relayUnindex(relaySessionByClient, endpointKey(session.clientIp, session.clientPort), session);
  relayUnindex(relaySessionByHost, endpointKey(session.hostIp, session.hostPort), session);
  relayUnindex(relayLeaseByHost, session.hostId, session);
  console.log(`[relay] connect=${session.connectId} closed reason=${reason} ` +
              `state=${session.state} c2h=${session.pktC2H}/${session.bytesC2H}B ` +
              `h2c=${session.pktH2C}/${session.bytesH2C}B ` +
              `ackMs=${session.ackMs === null ? 'never' : session.ackMs}`);
}

// One sweep for every expiring thing here. Called on a timer rather than per packet so the hot
// path stays a Map lookup and a send.
function relaySweep() {
  const now = Date.now();
  for (const [token, rec] of relayAuthByToken) {
    if (now > rec.expiresAt) relayAuthByToken.delete(token);
  }
  for (const [ip, rec] of relayEligibleByIp) {
    if (now > rec.expiresAt) relayEligibleByIp.delete(ip);
  }
  for (const session of [...relaySessions]) {
    if (session.state === 'provisional' && now - session.createdAt > RELAY_HANDSHAKE_TTL_MS) {
      relayDropSession(session, 'no HelloAck');
    } else if (now - session.lastClientAt > RELAY_IDLE_TTL_MS) {
      // Deliberately the client's silence, not the session's. A host goes on sending video to the
      // endpoint it last knew about, so counting that as activity would keep a session alive long
      // after the phone that owned it stopped existing -- and while it lived, the phone's next
      // attempt would find the host already leased. The client pings every 1s (10s at the
      // outside), so a minute of nothing means it is gone.
      relayDropSession(session, 'client silent');
    }
  }
}

/**
 * Answers a punch, but only once the direct candidates have had the field to themselves.
 *
 * The client keeps whichever candidate answers first, not whichever is best, so the only way to
 * express "prefer direct" is to be slower than direct. The grace has to sit inside the client's
 * punch budget (4s) with enough room for the Hello that follows, and comfortably above the round
 * trip a working direct path takes.
 */
function relayScheduleReply(clientIp, clientPort, eligible) {
  const key = endpointKey(clientIp, clientPort);
  if (eligible.replied.has(key)) return;
  eligible.replied.add(key);
  const packet = buildPunchPacket();
  for (let i = 0; i < RELAY_PUNCH_REPLIES; ++i) {
    setTimeout(() => {
      if (!relaySock) return;
      relaySock.send(packet, clientPort, clientIp, (err) => {
        if (err) console.error(`[relay] punch reply failed: ${err.message}`);
      });
    }, RELAY_GRACE_MS + i * 120).unref();
  }
  console.log(`[relay] connect=${eligible.connectId} punch from ${key}; ` +
              `answering in ${RELAY_GRACE_MS}ms x${RELAY_PUNCH_REPLIES}`);
}

/**
 * A Hello with a valid capability is what actually creates a session.
 *
 * Punch cannot do this: it carries no token, and a company NAT puts every phone on one address,
 * so the source address alone identifies nothing. The token does -- it is 128 bits, minted for
 * this one connect, and the host holds its own copy.
 */
function relayBindSession(rinfo, parsed) {
  const auth = relayAuthByToken.get(parsed.token);
  if (!auth || Date.now() > auth.expiresAt) return null;
  if (auth.clientIp !== rinfo.address) return null;
  // Only the newest capability for a host may take it. Without this, a token that lost the host
  // could win it back the moment its owner retransmitted an old Hello.
  if (relayLatestTokenByHost.get(auth.hostId) !== parsed.token) return null;
  const host = store.hosts[auth.hostId];
  if (!host || !host.publicIp || !host.publicUdpPort) return null;

  // The newest connect wins. The alternative -- first session keeps the host -- reads as fair but
  // means a phone whose app was killed cannot come back: nobody is left to release the lease, and
  // the server cannot tell a dead session from a quiet one until its client falls silent.
  const existingLease = relayLeaseByHost.get(auth.hostId);
  if (existingLease && !existingLease.dropped) relayDropSession(existingLease, 'newer connect');
  // The phone's address is just as exclusive: reusing it for a second host would leave the first
  // session indexed under a key that no longer finds it.
  const clientKey = endpointKey(rinfo.address, rinfo.port);
  const existingClient = relaySessionByClient.get(clientKey);
  if (existingClient && !existingClient.dropped) relayDropSession(existingClient, 'client moved');

  const now = Date.now();
  const session = {
    connectId: auth.connectId,
    token: parsed.token,
    hostId: auth.hostId,
    clientIp: rinfo.address,
    clientPort: rinfo.port,
    hostIp: host.publicIp,
    hostPort: host.publicUdpPort,
    state: 'provisional',
    createdAt: now,
    lastClientAt: now,
    lastHostAt: now,
    ackMs: null,
    pktC2H: 0, pktH2C: 0, bytesC2H: 0, bytesH2C: 0,
    dropped: false,
  };
  relaySessions.add(session);
  relaySessionByClient.set(clientKey, session);
  relaySessionByHost.set(endpointKey(session.hostIp, session.hostPort), session);
  relayLeaseByHost.set(session.hostId, session);
  console.log(`[relay] connect=${session.connectId} bound client=${session.clientIp}:` +
              `${session.clientPort} host=${session.hostIp}:${session.hostPort}`);
  return session;
}

// Forwards one client packet to the host and counts it.
function relayForwardToHost(session, msg) {
  session.lastClientAt = Date.now();
  session.pktC2H++;
  session.bytesC2H += msg.length;
  if (observeSock) observeSock.send(msg, session.hostPort, session.hostIp);
}

/**
 * Client-facing listener. Also the diagnostic probe when the relay is off, because they share a
 * port and the diagnostic is what proved this port reaches the phone's network.
 */
function startRelayListener() {
  if (!RELAY_ENABLED && !NAT_DIAG_ENABLED) return null;
  const port = RELAY_ENABLED ? RELAY_PORT : NAT_DIAG_PORT;
  const sock = dgram.createSocket('udp4');

  sock.on('message', (msg, rinfo) => {
    if (RELAY_ENABLED) {
      const session = relaySessionByClient.get(endpointKey(rinfo.address, rinfo.port));
      // Hello is read before the established-session path, not after. A phone that restarts often
      // comes back on the same address with a new capability, and treating that Hello as ordinary
      // media would forward it under the old session's identity -- the connection might even
      // survive, while every counter and log line here described the session it replaced.
      const parsed = parseHandshakePacket(msg);
      if (parsed && parsed.kind === UDP_KIND_HELLO) {
        // Dropped here rather than left to fall through: a Hello whose capability we cannot read
        // is not media, and forwarding it under an existing session would attribute one client's
        // handshake to another's.
        if (!parsed.token) return;
        if (session && !session.dropped && session.token === parsed.token) {
          relayForwardToHost(session, msg);   // a retransmission, not a new session
          return;
        }
        const bound = relayBindSession(rinfo, parsed);
        if (bound) relayForwardToHost(bound, msg);
        return;
      }
      if (session && !session.dropped) {
        // Established: opaque bytes, host leg, no inspection. The observe socket is not a choice
        // here -- it is the only address the host has a NAT mapping toward.
        relayForwardToHost(session, msg);
        return;
      }
      if (parsed && parsed.kind === UDP_KIND_PUNCH) {
        const eligible = relayEligibleByIp.get(rinfo.address);
        if (eligible && Date.now() <= eligible.expiresAt) {
          relayScheduleReply(rinfo.address, rinfo.port, eligible);
        }
        return;
      }
      return;  // anything else opens nothing
    }

    // Diagnostic mode: record and answer nothing. Silence is the safety property -- a reply would
    // make this endpoint eligible to win the client's candidate race.
    const rec = natDiagByIp.get(rinfo.address);
    const age = rec ? Date.now() - rec.at : -1;
    const connectId = rec ? rec.connectId : 'unknown';
    const samePort = rec ? String(rinfo.port === rec.observedPort) : 'unknown';
    console.log(`[natdiag] connect=${connectId} rx dport=${port} ` +
                `src=${rinfo.address}:${rinfo.port} observedPort=${rec ? rec.observedPort : '?'} ` +
                `samePort=${samePort} ageMs=${age} bytes=${msg.length}`);
  });

  sock.on('error', (err) => console.error('[relay] listener error:', err.message));
  sock.bind(port, () => {
    if (RELAY_ENABLED) {
      // A keyframe is a burst of a hundred-odd datagrams; the default receive buffer loses the
      // tail of one and the client spends the next second asking for another.
      try { sock.setRecvBufferSize(4 * 1024 * 1024); } catch { /* best effort */ }
      try { sock.setSendBufferSize(4 * 1024 * 1024); } catch { /* best effort */ }
      console.log(`[relay] listening on udp ${port}; grace=${RELAY_GRACE_MS}ms`);
    } else {
      console.log(`[natdiag] silent probe listening on udp ${port}; never replies`);
    }
  });
  return sock;
}

/**
 * The host side of an established relay session, arriving on the observe socket.
 *
 * Returns true when the packet was relay traffic. The caller checks OBSERVE first: a host in an
 * active session keeps sending those from the very same tuple, and reading them as media would
 * drop it out of the directory.
 */
function relayHandleHostPacket(msg, rinfo) {
  if (!RELAY_ENABLED) return false;
  const session = relaySessionByHost.get(endpointKey(rinfo.address, rinfo.port));
  if (!session || session.dropped) return false;
  session.lastHostAt = Date.now();
  session.pktH2C++;
  session.bytesH2C += msg.length;
  if (session.state === 'provisional') {
    const ack = parseHandshakePacket(msg);
    // The host sets this bit only after authorising the capability, and the client refuses an Ack
    // without it. So this one bit is the proof that both ends accepted the relayed handshake.
    if (ack && ack.kind === UDP_KIND_HELLO_ACK &&
        (ack.features & UDP_FEATURE_DIRECTORY_AUTH) !== 0) {
      session.state = 'active';
      session.ackMs = Date.now() - session.createdAt;
      console.log(`[relay] connect=${session.connectId} active; helloAck in ${session.ackMs}ms`);
    }
  }
  if (relaySock) relaySock.send(msg, session.clientPort, session.clientIp);
  return true;
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

  // One short id ties together the places this attempt shows up: here, the listener, the relay,
  // and the host log. Without it, concurrent attempts are indistinguishable.
  const connectId = crypto.randomBytes(4).toString('hex');
  const relayEligible = relayEligibleFor(session.accountId, clientIp);

  if (NAT_DIAG_ENABLED) {
    natDiagByIp.set(clientIp, { connectId, observedPort: clientPort, at: Date.now() });
    for (const [ip, rec] of natDiagByIp) {
      if (Date.now() - rec.at > NAT_DIAG_TTL_MS) natDiagByIp.delete(ip);
    }
    console.log(`[natdiag] connect=${connectId} host=${host.hostName} ` +
                `hostPublic=${host.publicIp}:${host.publicUdpPort} ` +
                `clientObserved=${clientIp}:${clientPort} ` +
                `diag=${NAT_DIAG_IP || '(unset)'}:${NAT_DIAG_PORT}`);
  }

  if (relayEligible) {
    const expiresAt = Date.now() + RELAY_AUTH_TTL_MS;
    // The host's copy of this capability lives in `pendingPunch` and heartbeat deletes it on
    // read. This is a separate copy for the same token, so verifying a relayed Hello never costs
    // the host the authorisation it needs to accept that same Hello.
    relayAuthByToken.set(punchToken, { hostId: host.hostId, clientIp, connectId, expiresAt });
    // Newest wins, so the previous capability for this host stops being able to claim it. A phone
    // that was killed mid-session is the ordinary case, and its old token must not outrank the
    // one the user is holding now.
    const previous = relayLatestTokenByHost.get(host.hostId);
    if (previous && previous !== punchToken) relayAuthByToken.delete(previous);
    relayLatestTokenByHost.set(host.hostId, punchToken);
    relayEligibleByIp.set(clientIp, { connectId, expiresAt, replied: new Set() });
    console.log(`[relay] connect=${connectId} eligible client=${clientIp}:${clientPort} ` +
                `host=${host.hostName}@${host.publicIp}:${host.publicUdpPort}`);
  }

  // Every connect, not just the relayed ones. On the relay the host would otherwise never hear
  // that anyone was waiting -- the relay answers punches rather than forwarding them -- and on the
  // direct path the peer's own punch is dropped by exactly the restrictive NATs where it matters.
  sendWakePunch(observeSock, host, connectId);

  sendJson(res, 200, {
    // Kept for clients that predate candidates; they dial this one and behave as before.
    hostPublicIp: host.publicIp,
    hostPublicUdpPort: host.publicUdpPort,
    candidates: connectCandidatesFor(host, relayEligible),
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
    // OBSERVE is tested first and by shape. A host in a relay session sends both kinds from the
    // same tuple, and reading its observations as media would drop it out of the directory --
    // while a 49-byte binary packet can never be mistaken for this prefix.
    if (msg.length <= 128 && msg[0] === 0x4f /* 'O' */) {
      const text = msg.toString('utf8').trim();
      if (text.startsWith('OBSERVE ')) {
        const token = text.slice(8).trim().slice(0, 64);
        if (!token) return;
        const seen = observedAddressFor(rinfo);
        observed.set(token, { ip: seen.ip, port: seen.port, at: Date.now() });
        // The reply goes back to where the packet actually came from; only its contents are
        // corrected, because that is what the peer will advertise about itself.
        const reply = Buffer.from(JSON.stringify({ ip: seen.ip, port: seen.port }));
        sock.send(reply, rinfo.port, rinfo.address);
        return;
      }
    }
    // Only a tuple already bound to a session is forwarded; an unknown sender opens nothing.
    relayHandleHostPacket(msg, rinfo);
  });
  sock.on('error', (err) => console.error('[directory] udp error:', err.message));
  sock.bind(UDP_PORT, () => {
    if (RELAY_ENABLED) {
      try { sock.setRecvBufferSize(4 * 1024 * 1024); } catch { /* best effort */ }
      try { sock.setSendBufferSize(4 * 1024 * 1024); } catch { /* best effort */ }
    }
    console.log(`[directory] udp observe on ${UDP_PORT}` +
                (PUBLIC_IP ? `; own-lan observations reported as ${PUBLIC_IP}` : ''));
  });
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
  relaySock = startRelayListener();
  if (RELAY_ENABLED) {
    const ips = RELAY_ALLOW_IPS.any ? '*' : ([...RELAY_ALLOW_IPS.set].join(',') || '(none)');
    const accounts = RELAY_ALLOW_ACCOUNTS.any
      ? '*' : ([...RELAY_ALLOW_ACCOUNTS.set].join(',') || '(none)');
    console.log(`[relay] ENABLED ip=${RELAY_IP || '(unset -- candidate NOT offered)'} ` +
                `port=${RELAY_PORT} grace=${RELAY_GRACE_MS}ms allowIps=${ips} ` +
                `allowAccounts=${accounts}`);
    if (!RELAY_IP || ips === '(none)' || accounts === '(none)') {
      console.warn('[relay] no client can be offered the relay until IP and account are listed');
    }
    setInterval(relaySweep, 5000).unref();
  }
  // Said at boot whichever way it is set, because the failure mode of the wake being off is
  // silent: connections simply stop working on the networks that need it.
  console.log(`[wake] ${WAKE_ENABLED ? 'on' : 'OFF (REMOTE60_WAKE_DISABLED=1)'}`);
  if (WAKE_ENABLED) setInterval(logWakeStats, 5 * 60 * 1000).unref();
  if (NAT_DIAG_ENABLED) {
    console.log(`[natdiag] ENABLED ip=${NAT_DIAG_IP || '(unset -- candidate NOT offered)'} ` +
                `port=${NAT_DIAG_PORT}` +
                `${RELAY_ENABLED ? ' (probe superseded by relay)' : ''}`);
  }
}
