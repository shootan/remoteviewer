p = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\directory\\server.js'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

# ---------------------------------------------------------------- constants and state
rep('''const relayLeaseByHost = new Map();      // hostId -> session; one at a time, like the host itself
let relaySock = null;
''', '''const relayLeaseByHost = new Map();      // hostId -> session; one at a time, like the host itself
let relaySock = null;

// keepalive-r2 relay decision (a) (Codex codex_keepalive_r2_spike_r1.md): STANDBY leases. A client that keeps a
// host ready asks /api/connect for a standby capability; the relay then gives that one connect its OWN
// host-facing UDP socket, reserved at mint time. The host tells such clients apart by address as well as by
// entry -- behind one shared host-facing address its control/ACK/NACK/resume/bulk gate (the adopted IP:port)
// could not. Several standby leases for one host coexist; a new one never drops another, and a legacy Hello's
// lease (relayLeaseByHost, observeSock, newest-wins) is untouched by all of it and does not drop them either.
//
// One standby lease per client endpoint: the client-facing socket is shared, so client->host routing is by
// the client's address -- a client keeps one UDP socket per host lease (contract for the APK/Viewer side).
//
// The NAT path to the reserved socket uses what every host already does: the heartbeat hands the host one
// extra pendingPunch entry aimed at RELAY_HOST_FACING_IP:<reserved port> under a token nobody else holds, and
// the host punches it from its media socket. The reserved socket pins the first datagram from the host's
// address (any port: a symmetric NAT maps each destination anew) and takes nothing from anyone else.
const UDP_STANDBY_BYTES = 104;            // sizeof(UdpStandbyPacket), #pragma pack(1)
const UDP_KIND_STANDBY_HELLO = 311;
const UDP_KIND_STANDBY_ACK = 312;
const UDP_KIND_STANDBY_RELEASE = 316;
const UDP_STANDBY_STATUS_OK = 0;
const UDP_STANDBY_TOKEN_OFFSET = 68;      // char authToken[33]
const RELAY_STANDBY_MAX_PER_HOST = 8;     // the host's own entry limit; a legacy lease counts toward it
const RELAY_STANDBY_MAX_PER_ACCOUNT = Math.max(1, Number(process.env.REMOTE60_RELAY_STANDBY_MAX_PER_ACCOUNT || 24));
const RELAY_STANDBY_MAX_PER_WIRE_IP = Math.max(1, Number(process.env.REMOTE60_RELAY_STANDBY_MAX_PER_WIRE_IP || 24));
const RELAY_STANDBY_MAX_SOCKETS = Math.max(1, Number(process.env.REMOTE60_RELAY_STANDBY_MAX_SOCKETS || 256));
// Where a host reaches this server's reserved sockets, and what they bind to (tests: loopback).
const RELAY_HOST_FACING_IP = String(process.env.REMOTE60_RELAY_HOST_FACING_IP || RELAY_IP || '').trim();
const RELAY_HOST_FACING_BIND = String(process.env.REMOTE60_RELAY_HOST_FACING_BIND || '').trim();
const relayStandbyByHost = new Map();     // hostId -> Set of standby sessions (reserved .. active)
let relayStandbySockets = 0;              // reserved sockets open now (counted before their bind completes)
''')

# ---------------------------------------------------------------- the standby packet
rep('''// Indexes are removed only when they still point at this session.''',
    '''// A Standby* datagram (104 B UdpStandbyPacket) as far as the relay reads one: the kind, and for a StandbyHello the
// capability; plus a StandbyAck's status (the host's answer that makes a lease ready). Everything else is opaque.
function parseStandbyPacket(msg) {
  if (msg.length !== UDP_STANDBY_BYTES) return null;
  if (msg.readUInt32LE(0) !== UDP_MAGIC) return null;
  if (msg.readUInt16LE(6) !== UDP_STANDBY_BYTES) return null;
  if (msg.readUInt32LE(8) !== UDP_PROTOCOL_VERSION) return null;
  const kind = msg.readUInt16LE(4);
  if (kind < UDP_KIND_STANDBY_HELLO || kind > UDP_KIND_STANDBY_RELEASE) return null;
  let token = '';
  if (kind === UDP_KIND_STANDBY_HELLO) {
    const end = msg.indexOf(0, UDP_STANDBY_TOKEN_OFFSET);
    const stop = end < 0 ? msg.length : Math.min(end, UDP_STANDBY_TOKEN_OFFSET + 32);
    token = msg.toString('latin1', UDP_STANDBY_TOKEN_OFFSET, stop);
    if (!/^[0-9a-f]{32}$/.test(token)) token = '';
  }
  return { kind, token, status: msg.readUInt32LE(12) };
}

// Indexes are removed only when they still point at this session.''')

# ---------------------------------------------------------------- drop
rep('''  relaySessions.delete(session);
  relayUnindex(relaySessionByClient, endpointKey(session.clientIp, session.clientPort), session);
  relayUnindex(relaySessionByHost, endpointKey(session.hostIp, session.hostPort), session);
  relayUnindex(relayLeaseByHost, session.hostId, session);
''', '''  relaySessions.delete(session);
  if (session.clientPort) relayUnindex(relaySessionByClient, endpointKey(session.clientIp, session.clientPort), session);
  if (session.cls === 'standby') {
    // Its own socket goes with it -- the reservation, the NAT path and every index are this one object's.
    const set = relayStandbyByHost.get(session.hostId);
    if (set) {
      set.delete(session);
      if (set.size === 0) relayStandbyByHost.delete(session.hostId);
    }
    if (relayAuthByToken.get(session.token) && relayAuthByToken.get(session.token).reservation === session) {
      relayAuthByToken.delete(session.token);
    }
    try { session.sock.close(); } catch { /* already closed */ }
    relayStandbySockets--;
  } else {
    relayUnindex(relaySessionByHost, endpointKey(session.hostIp, session.hostPort), session);
    relayUnindex(relayLeaseByHost, session.hostId, session);
  }
''')
rep('''  console.log(`[relay] connect=${session.connectId} closed reason=${reason} ` +
              `state=${session.state} c2h=''',
    '''  console.log(`[relay] connect=${session.connectId} closed reason=${reason} ` +
              (session.cls === 'standby' ? `standby hostFacing=${session.hostFacingPort} ` : '') +
              `state=${session.state} c2h=''')

# ---------------------------------------------------------------- follow the host's wire (unpinned standby leases)
rep('''function relayFollowHostWire(host, previousWire, aim) {
  const session = relayLeaseByHost.get(host.hostId);''',
    '''function relayFollowHostWire(host, previousWire, aim) {
  // Standby leases: one that has not heard the host yet aims where the host now is. One that has is PINNED to
  // the tuple the host really used toward its own socket -- the global heartbeat tuple is another destination's
  // mapping and is not written over it. If the host itself moved, the pin is let go: the next datagram from the
  // host's address on that socket pins it again (and a lease that no longer reaches it lapses with its client).
  const moved = !!previousWire.ip && (previousWire.ip !== host.wireIp || previousWire.port !== host.wirePort);
  for (const standby of relayStandbyByHost.get(host.hostId) || []) {
    if (standby.dropped) continue;
    if (standby.hostPinned && moved) {
      standby.hostPinned = false;
      console.log(`[relay] connect=${standby.connectId} standby host moved; pin released ` +
                  `was=${standby.hostIp}:${standby.hostPort}`);
    }
    if (!standby.hostPinned) {
      standby.hostIp = aim.ip;
      standby.hostPort = aim.port;
    }
  }
  const session = relayLeaseByHost.get(host.hostId);''')

# ---------------------------------------------------------------- sweep
rep('''    if (session.state === 'provisional' && now - session.createdAt > RELAY_HANDSHAKE_TTL_MS) {''',
    '''    if (session.state === 'reserved') {
      // A standby reservation no client has bound: it lives as long as its capability.
      if (now > session.expiresAt) relayDropSession(session, 'reservation expired');
      continue;
    }
    if (session.state === 'provisional' && now - session.createdAt > RELAY_HANDSHAKE_TTL_MS) {''')
rep('''      relayDropSession(session, 'no HelloAck');''',
    '''      relayDropSession(session, session.cls === 'standby' ? 'no StandbyAck' : 'no HelloAck');''')

# ---------------------------------------------------------------- reserve / bind / host side
rep('''// Forwards one client packet to the host and counts it.''',
    '''// Why a new standby reservation is refused, or '' -- every bound checked before anything is allocated.
function relayStandbyRefusal(hostId, accountId, clientWireIp) {
  if (relayStandbySockets >= RELAY_STANDBY_MAX_SOCKETS) return 'server-full';
  const legacy = relayLeaseByHost.get(hostId);
  const onHost = (relayStandbyByHost.get(hostId) || new Set()).size + (legacy && !legacy.dropped ? 1 : 0);
  if (onHost >= RELAY_STANDBY_MAX_PER_HOST) return 'host-full';
  let byAccount = 0;
  let byIp = 0;
  for (const s of relaySessions) {
    if (s.cls !== 'standby' || s.dropped) continue;
    if (s.accountId === accountId) ++byAccount;
    if (s.clientWireIp === clientWireIp) ++byIp;
  }
  if (byAccount >= RELAY_STANDBY_MAX_PER_ACCOUNT) return 'account-full';
  if (byIp >= RELAY_STANDBY_MAX_PER_WIRE_IP) return 'ip-full';
  return '';
}

/**
 * A standby connect's own host-facing socket, reserved when its capability is minted. Counted and indexed
 * before the bind completes, so two connects racing cannot both pass the bounds; a bind that fails takes
 * the whole reservation back (no capability, no relay candidate for that connect).
 */
async function relayReserveStandby(host, accountId, clientWireIp, token, connectId, expiresAt) {
  const why = relayStandbyRefusal(host.hostId, accountId, clientWireIp);
  if (why) {
    console.log(`[relay] connect=${connectId} standby refused reason=${why}`);
    return { error: why };
  }
  const aim = hostSendTargetFor(host, onServerLan);
  const now = Date.now();
  const session = {
    cls: 'standby',
    connectId,
    token,
    hostId: host.hostId,
    accountId,
    clientWireIp,
    clientIp: null,
    clientPort: 0,
    hostIp: aim.ip,
    hostPort: aim.port,
    hostPinned: false,
    hostFacingPort: 0,
    sock: dgram.createSocket('udp4'),
    state: 'reserved',
    reservedAt: now,
    expiresAt,
    createdAt: now,
    lastClientAt: now,
    lastHostAt: now,
    ackMs: null,
    pktC2H: 0, pktH2C: 0, bytesC2H: 0, bytesH2C: 0,
    dropped: false,
    firstC2HAt: 0, firstH2CAt: 0, silentReported: true,  // a reservation's silence is not a fault
  };
  relayStandbySockets++;
  relaySessions.add(session);
  if (!relayStandbyByHost.has(host.hostId)) relayStandbyByHost.set(host.hostId, new Set());
  relayStandbyByHost.get(host.hostId).add(session);
  try {
    await new Promise((resolve, reject) => {
      session.sock.once('error', reject);
      if (RELAY_HOST_FACING_BIND) session.sock.bind(0, RELAY_HOST_FACING_BIND, resolve);
      else session.sock.bind(0, resolve);
    });
  } catch (err) {
    relayDropSession(session, `bind failed: ${err.message}`);
    return { error: 'bind-failed' };
  }
  if (session.dropped) return { error: 'dropped' };
  session.sock.on('error', (err) => console.error(`[relay] connect=${connectId} standby socket error: ${err.message}`));
  session.sock.on('message', (msg, rinfo) => relayStandbyFromHost(session, msg, rinfo));
  // An activated lease carries video: the same buffers as the client-facing socket.
  try { session.sock.setRecvBufferSize(4 * 1024 * 1024); } catch { /* best effort */ }
  try { session.sock.setSendBufferSize(4 * 1024 * 1024); } catch { /* best effort */ }
  session.hostFacingPort = session.sock.address().port;
  // One punch toward the host from the reserved socket: enough where the host's NAT filters by address only.
  // A port-restricted one needs the host's own punch (the extra pendingPunch entry), which pins this socket.
  if (session.hostIp && session.hostPort) session.sock.send(buildPunchPacket(), session.hostPort, session.hostIp);
  console.log(`[relay] connect=${connectId} standby reserved hostFacing=${session.hostFacingPort} ` +
              `host=${host.hostId.slice(0, 8)} onHost=${relayStandbyByHost.get(host.hostId).size}`);
  return { session };
}

/**
 * A StandbyHello with a standby capability binds its reservation to the client endpoint it came from. Other
 * leases of the host are not touched; the same rules as a legacy Hello for the token and the wire address.
 */
function relayBindStandby(rinfo, sb) {
  const src = `${rinfo.address}:${rinfo.port}`;
  const auth = relayAuthByToken.get(sb.token);
  if (!auth || Date.now() > auth.expiresAt) {
    relayDiag.note('standby-unknown-token', rinfo.address,
                   `[relay] standby hello rejected src=${src} reason=unknown-or-expired-token`);
    return null;
  }
  if (auth.cls !== 'standby' || !auth.reservation) {
    // A legacy capability does not open a standby lease (it would take no reserved socket and share the
    // host's observe address -- exactly what decision (a) excludes).
    relayDiag.note('standby-legacy-token', rinfo.address,
                   `[relay] connect=${auth.connectId} standby hello rejected src=${src} reason=legacy-token`);
    return null;
  }
  if (auth.clientWireIp !== rinfo.address) {
    relayDiag.note('standby-wire-mismatch', rinfo.address,
                   `[relay] connect=${auth.connectId} standby hello rejected src=${src} reason=wire-mismatch ` +
                   `expectedWire=${auth.clientWireIp}`);
    return null;
  }
  const session = auth.reservation;
  if (session.dropped || session.clientPort) {
    // Bound already (from another endpoint: the same one would have been found by its address) or gone.
    relayDiag.note('standby-reservation-taken', rinfo.address,
                   `[relay] connect=${auth.connectId} standby hello rejected src=${src} ` +
                   `reason=${session.dropped ? 'reservation-gone' : 'reservation-bound'}`);
    return null;
  }
  const host = store.hosts[auth.hostId];
  if (!host || !accounts.hostIsActive(store, host)) {
    relayDiag.note('standby-host-inactive', rinfo.address,
                   `[relay] connect=${auth.connectId} standby hello rejected src=${src} reason=host-inactive`);
    return null;
  }
  const clientKey = endpointKey(rinfo.address, rinfo.port);
  const existingClient = relaySessionByClient.get(clientKey);
  if (existingClient && !existingClient.dropped) relayDropSession(existingClient, 'client moved');
  const now = Date.now();
  session.clientIp = rinfo.address;
  session.clientPort = rinfo.port;
  session.state = 'provisional';
  session.createdAt = now;
  session.lastClientAt = now;
  session.silentReported = false;
  relaySessionByClient.set(clientKey, session);
  console.log(`[relay] connect=${session.connectId} standby bound client=${src} ` +
              `hostFacing=${session.hostFacingPort} host=${session.hostIp}:${session.hostPort}` +
              (session.hostPinned ? ' (pinned)' : ''));
  return session;
}

/**
 * The host's side of one standby lease, on that lease's own socket. The first datagram from the host's
 * address pins the tuple; after that nothing else is taken. A StandbyAck Ok makes the lease ready, a HelloAck
 * with DirectoryAuth (its activation, over this same lease) active. Punches only pin -- they carry nothing.
 */
function relayStandbyFromHost(session, msg, rinfo) {
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
    session.hostPinned = true;
    session.hostIp = rinfo.address;
    session.hostPort = rinfo.port;
    console.log(`[relay] connect=${session.connectId} standby host pinned ${rinfo.address}:${rinfo.port} ` +
                `hostFacing=${session.hostFacingPort}`);
  } else if (rinfo.address !== session.hostIp || rinfo.port !== session.hostPort) {
    relayDiag.note('standby-host-unpinned', rinfo.address,
                   `[relay] connect=${session.connectId} standby host-side datagram refused ` +
                   `src=${rinfo.address}:${rinfo.port} reason=not-the-pinned-tuple`);
    return;
  }
  session.lastHostAt = Date.now();
  const handshake = parseHandshakePacket(msg);
  if (handshake && handshake.kind === UDP_KIND_PUNCH) return;
  session.pktH2C++;
  session.bytesH2C += msg.length;
  if (!session.firstH2CAt) {
    session.firstH2CAt = Date.now();
    console.log(`[relay] connect=${session.connectId} first h2c (standby) <- ${rinfo.address}:${rinfo.port} ` +
                `afterBindMs=${session.firstH2CAt - session.createdAt}`);
  }
  const sb = parseStandbyPacket(msg);
  if (session.state === 'provisional' && sb && sb.kind === UDP_KIND_STANDBY_ACK && sb.status === UDP_STANDBY_STATUS_OK) {
    session.state = 'ready';
    session.ackMs = Date.now() - session.createdAt;
    console.log(`[relay] connect=${session.connectId} standby ready; standbyAck in ${session.ackMs}ms`);
  } else if (session.state === 'ready' && handshake && handshake.kind === UDP_KIND_HELLO_ACK &&
             (handshake.features & UDP_FEATURE_DIRECTORY_AUTH) !== 0) {
    session.state = 'active';
    console.log(`[relay] connect=${session.connectId} standby active (activation over the same lease)`);
  }
  if (!session.clientPort) return;  // a reservation: the host's punch pinned it, nothing to carry yet
  if (relaySock) relaySock.send(msg, session.clientPort, session.clientIp);
}

// Forwards one client packet to the host and counts it.''')

rep('''  if (observeSock) observeSock.send(msg, session.hostPort, session.hostIp);
}''', '''  // A standby lease goes out of its OWN host-facing socket; a legacy one out of the observe socket.
  const out = session.cls === 'standby' ? session.sock : observeSock;
  if (out) out.send(msg, session.hostPort, session.hostIp);
}''')

# ---------------------------------------------------------------- client-facing listener
rep('''      const session = relaySessionByClient.get(endpointKey(rinfo.address, rinfo.port));
      // Hello is read before''', '''      const session = relaySessionByClient.get(endpointKey(rinfo.address, rinfo.port));
      // keepalive-r2 (a): Standby* from a client. Its own lease carries it; a StandbyHello with a standby
      // capability binds one; nothing else is opened by it, and a legacy lease does not carry it.
      const standby = parseStandbyPacket(msg);
      if (standby) {
        if (session && !session.dropped) {
          if (session.cls === 'standby') relayForwardToHost(session, msg);
          return;
        }
        if (standby.kind === UDP_KIND_STANDBY_HELLO && standby.token) {
          const bound = relayBindStandby(rinfo, standby);
          if (bound) relayForwardToHost(bound, msg);
        }
        return;
      }
      // Hello is read before''')
rep('''        if (!parsed.token) return;
        if (session && !session.dropped && session.token === parsed.token) {''',
    '''        if (!parsed.token) return;
        // A standby lease is activated by its StandbyActivate, never by a legacy Hello -- not from its own
        // endpoint, and not as a takeover with its capability (relayBindSession refuses that one).
        if (session && !session.dropped && session.cls === 'standby') return;
        if (session && !session.dropped && session.token === parsed.token) {''')
rep('''  if (auth.clientWireIp !== rinfo.address) {
    relayDiag.note('hello-wire-mismatch', rinfo.address,''',
    '''  if (auth.cls === 'standby') {
    relayDiag.note('hello-standby-token', rinfo.address,
                   `[relay] connect=${auth.connectId} hello rejected src=${src} reason=standby-token`);
    return null;
  }
  if (auth.clientWireIp !== rinfo.address) {
    relayDiag.note('hello-wire-mismatch', rinfo.address,''')

# ---------------------------------------------------------------- connect
rep('''  if (relayEligible) {
    const expiresAt = Date.now() + RELAY_AUTH_TTL_MS;''',
    '''  // keepalive-r2 (a): a standby connect -- additive, off unless asked -- gets its own reserved host-facing
  // socket instead of the host's newest-wins legacy lease. Refused (any bound, a failed bind): this connect
  // simply has no relay candidate, and says why; the direct candidates are as always.
  let relayOffered = relayEligible;
  let standbyRelay = '';
  if (relayEligible && body.standby === true) {
    const expiresAt = Date.now() + RELAY_AUTH_TTL_MS;
    const reserved = await relayReserveStandby(host, session.accountId, clientWireIp, punchToken, connectId, expiresAt);
    if (!reserved.session) {
      relayOffered = false;
      standbyRelay = reserved.error;
    } else {
      standbyRelay = 'reserved';
      relayAuthByToken.set(punchToken, { hostId: host.hostId, clientWireIp, clientAdvertisedIp: clientIp,
                                         connectId, expiresAt, cls: 'standby', reservation: reserved.session });
      relayEligibleByWireIp.set(clientWireIp, { connectId, expiresAt, replied: new Set() });
      // The NAT path: the host punches every pendingPunch entry from its media socket. This one is aimed at
      // the reserved socket under a token nobody else is given (the client's own token must not be listed
      // twice: a second listing would let that one capability admit twice).
      if (RELAY_HOST_FACING_IP) {
        list.push({ ip: RELAY_HOST_FACING_IP, port: reserved.session.hostFacingPort,
                    punchToken: crypto.randomBytes(16).toString('hex'), expiresAt: Date.now() + PUNCH_TTL_MS,
                    connectId, mintedAt: Date.now() });
      }
      console.log(`[relay] connect=${connectId} standby eligible client=${clientIp}:${clientPort} ` +
                  `wire=${clientWireIp} hostFacing=${reserved.session.hostFacingPort}`);
    }
  } else if (relayEligible) {
    const expiresAt = Date.now() + RELAY_AUTH_TTL_MS;''')
rep('''    candidates: connectCandidatesFor(host, relayEligible),
    punchToken,''', '''    candidates: connectCandidatesFor(host, relayOffered),
    punchToken,
    // keepalive-r2 (a), only when a standby connect was asked for: 'reserved' or why not.
    ...(standbyRelay ? { standbyRelay } : {}),''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
