p = 'C:/work/gnlink-wt/ka-r3/apps/directory/server.js'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

# ---------------------------------------------------------------- N3: integer env bounds
rep('''const RELAY_STANDBY_MAX_PER_ACCOUNT = Math.max(1, Number(process.env.REMOTE60_RELAY_STANDBY_MAX_PER_ACCOUNT || 24));
const RELAY_STANDBY_MAX_PER_WIRE_IP = Math.max(1, Number(process.env.REMOTE60_RELAY_STANDBY_MAX_PER_WIRE_IP || 24));
const RELAY_STANDBY_MAX_SOCKETS = Math.max(1, Number(process.env.REMOTE60_RELAY_STANDBY_MAX_SOCKETS || 256));''',
    '''// r4 (Codex N3): a bound is a finite positive integer, or the default -- a value that is not one must not make the
// check vanish (Math.max(1, NaN) is NaN, and every comparison with NaN is false).
function relayEnvBound(name, fallback) {
  const raw = process.env[name];
  if (raw === undefined || raw === '') return fallback;
  const v = Number(raw);
  if (Number.isInteger(v) && v >= 1 && v <= 1000000) return v;
  console.error(`[relay] ${name}=${JSON.stringify(raw)} is not a positive integer; using ${fallback}`);
  return fallback;
}
const RELAY_STANDBY_MAX_PER_ACCOUNT = relayEnvBound('REMOTE60_RELAY_STANDBY_MAX_PER_ACCOUNT', 24);
const RELAY_STANDBY_MAX_PER_WIRE_IP = relayEnvBound('REMOTE60_RELAY_STANDBY_MAX_PER_WIRE_IP', 24);
const RELAY_STANDBY_MAX_SOCKETS = relayEnvBound('REMOTE60_RELAY_STANDBY_MAX_SOCKETS', 256);'''),

# ---------------------------------------------------------------- N3: the legacy bind keeps the host's eight
rep('''  // The newest connect wins. The alternative -- first session keeps the host -- reads as fair but''',
    '''  // r4 (Codex N3): the host's eight (legacy included) hold for a NEW legacy lease too. Replacing the existing
  // legacy lease keeps the count; a new one beside eight standby leases is refused -- before anything is dropped
  // (a standby lease at this very client endpoint would go with "client moved" and is not counted).
  {
    const existing = relayLeaseByHost.get(auth.hostId);
    const replacing = existing && !existing.dropped;
    const atClient = relaySessionByClient.get(endpointKey(rinfo.address, rinfo.port));
    let standbyOnHost = 0;
    for (const sb of relayStandbyByHost.get(auth.hostId) || []) {
      if (!sb.dropped && sb !== atClient) ++standbyOnHost;
    }
    if (!replacing && standbyOnHost >= RELAY_STANDBY_MAX_PER_HOST) {
      relayDiag.note('hello-host-full', rinfo.address,
                     `[relay] connect=${auth.connectId} hello rejected src=${src} reason=host-full standby=${standbyOnHost}`);
      return null;
    }
  }
  // The newest connect wins. The alternative -- first session keeps the host -- reads as fair but'''),

# ---------------------------------------------------------------- N1: a host proof per reservation
rep('''    hostPinned: false,
    hostFacingPort: 0,''', '''    hostPinned: false,
    // r4 (Codex N1): what pins the host's tuple -- a host-only proof, handed to the HOST alone (pendingPunch
    // hostProof) and carried back in its punch to this socket. Not the client's token, not an entry id.
    hostProof: crypto.randomBytes(16).toString('hex'),
    hostFacingPort: 0,'''),

rep('''function relayStandbyFromHost(session, msg, rinfo) {
  if (session.dropped) return;
  if (!session.hostPinned) {
    const host = store.hosts[session.hostId];
    const known = host ? [host.wireIp, host.publicIp, session.hostIp].filter(Boolean) : [];
    if (!known.includes(rinfo.address)) {
      relayDiag.note('standby-host-foreign', rinfo.address,
                     `[relay] connect=${session.connectId} standby host-side datagram refused ` +
                     `src=${rinfo.address}:${rinfo.port} reason=not-the-host`);
      return;
    }
    session.hostPinned = true;''', '''// r4 (Codex N1): the proof a 49 B Punch carries in its token field (32 hex), or ''.
function punchProofOf(msg) {
  if (msg.length !== UDP_HELLO_BYTES || msg.readUInt32LE(0) !== UDP_MAGIC || msg.readUInt16LE(4) !== UDP_KIND_PUNCH) return '';
  const end = msg.indexOf(0, UDP_TOKEN_OFFSET);
  const stop = end < 0 ? msg.length : Math.min(end, UDP_TOKEN_OFFSET + 32);
  const t = msg.toString('latin1', UDP_TOKEN_OFFSET, stop);
  return /^[0-9a-f]{32}$/.test(t) ? t : '';
}

function proofMatches(got, want) {
  if (!got || !want || got.length !== want.length) return false;
  return crypto.timingSafeEqual(Buffer.from(got, 'latin1'), Buffer.from(want, 'latin1'));
}

// r4 (Codex N1): a new host proof for a lease, handed to the host with its next heartbeat (a fresh pendingPunch
// entry aimed at the lease's socket). The previous proof stops pinning anything.
function relayStandbyReprove(session, why) {
  session.hostProof = crypto.randomBytes(16).toString('hex');
  if (!RELAY_HOST_FACING_IP || !session.hostFacingPort) return;
  const list = pendingPunch.get(session.hostId) || [];
  list.push({ ip: RELAY_HOST_FACING_IP, port: session.hostFacingPort, punchToken: crypto.randomBytes(16).toString('hex'),
              hostProof: session.hostProof, expiresAt: Date.now() + PUNCH_TTL_MS, connectId: session.connectId,
              mintedAt: Date.now() });
  pendingPunch.set(session.hostId, list);
  console.log(`[relay] connect=${session.connectId} standby host proof renewed (${why}); hostFacing=${session.hostFacingPort}`);
}

function relayStandbyFromHost(session, msg, rinfo) {
  if (session.dropped) return;
  if (!session.hostPinned) {
    // r4 (Codex N1): only the HOST's punch with this lease's current proof pins -- from one of the host's known
    // addresses. A first datagram from the same IP proves nothing (another socket behind the same NAT); a punch
    // without the proof, with a wrong one or another lease's, changes nothing at all.
    const host = store.hosts[session.hostId];
    const known = host ? [host.wireIp, host.publicIp, session.hostIp].filter(Boolean) : [];
    if (!known.includes(rinfo.address) || !proofMatches(punchProofOf(msg), session.hostProof)) {
      relayDiag.note('standby-host-unproven', rinfo.address,
                     `[relay] connect=${session.connectId} standby host-side datagram refused ` +
                     `src=${rinfo.address}:${rinfo.port} reason=${known.includes(rinfo.address) ? 'no-host-proof' : 'not-the-host'}`);
      return;
    }
    session.hostPinned = true;'''),

rep('''  const moved = !!previousWire.ip && (previousWire.ip !== host.wireIp || previousWire.port !== host.wirePort);
  for (const standby of relayStandbyByHost.get(host.hostId) || []) {
    if (standby.dropped) continue;
    if (standby.hostPinned && moved) {
      standby.hostPinned = false;
      console.log(`[relay] connect=${standby.connectId} standby host moved; pin released ` +
                  `was=${standby.hostIp}:${standby.hostPort}`);
    }''', '''  const moved = !!previousWire.ip && (previousWire.ip !== host.wireIp || previousWire.port !== host.wirePort);
  for (const standby of relayStandbyByHost.get(host.hostId) || []) {
    if (standby.dropped) continue;
    if (moved) {
      // r4 (Codex N1): a moved host re-proves itself -- a fresh proof goes out with THIS heartbeat's pendingPunch
      // (the old one, even replayed, pins nothing) -- and nothing is pinned until that punch arrives.
      if (standby.hostPinned) {
        console.log(`[relay] connect=${standby.connectId} standby host moved; pin released ` +
                    `was=${standby.hostIp}:${standby.hostPort}`);
      }
      standby.hostPinned = false;
      relayStandbyReprove(standby, 'host moved');
    }'''),

rep('''      if (RELAY_HOST_FACING_IP) {
        const current = pendingPunch.get(host.hostId) || [];
        current.push({ ip: RELAY_HOST_FACING_IP, port: reserved.session.hostFacingPort,
                       punchToken: crypto.randomBytes(16).toString('hex'), expiresAt: Date.now() + PUNCH_TTL_MS,
                       connectId, mintedAt: Date.now() });
        pendingPunch.set(host.hostId, current);
      }''', '''      if (RELAY_HOST_FACING_IP) {
        // r4 (Codex N1): with the lease's host proof -- the host carries it back in its punch to that socket.
        const current = pendingPunch.get(host.hostId) || [];
        current.push({ ip: RELAY_HOST_FACING_IP, port: reserved.session.hostFacingPort,
                       punchToken: crypto.randomBytes(16).toString('hex'), hostProof: reserved.session.hostProof,
                       expiresAt: Date.now() + PUNCH_TTL_MS, connectId, mintedAt: Date.now() });
        pendingPunch.set(host.hostId, current);
      }'''),

rep('''    pendingPunch: punches.map((p) => ({ ip: p.ip, port: p.port, punchToken: p.punchToken })),''',
    '''    // r4: hostProof only on a relay lease's own entry (an older host ignores the field).
    pendingPunch: punches.map((p) => Object.assign({ ip: p.ip, port: p.port, punchToken: p.punchToken },
                                                   p.hostProof ? { hostProof: p.hostProof } : {})),''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
