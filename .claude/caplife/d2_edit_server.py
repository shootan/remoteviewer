P = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/directory/server.js'
CR = chr(13)
s = open(P, encoding='utf-8', newline='').read()
crlf = (CR + '\n') in s
if crlf:
    s = s.replace(CR + '\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


REPS = [('''function relayDropSession(session, reason) {
  if (!session || session.dropped) return;
  session.dropped = true;''', '''// apk-decoder-liveness r3 (Codex design r3 D2) -- TEST FIXTURE trace of each relay flow, on only with
// REMOTE60_RELAY_TRACE=1 (the local AVD fixture sets it; no deployment does). Per flow and direction: what arrived,
// what was handed to the socket and what the socket said (sent / its error), by packet kind read from the header only
// (video / control data / ACK / NACK / handshake / standby / unknown -- never a payload), in 1 s buckets on this
// process's monotonic clock (mono0 = the flow's first trace, wall0 = the wall clock then), the last time of each, and
// the events that change a flow (a refused host-side datagram, a host tuple change). A send that succeeded is the
// socket's word only -- not that the far end received it. Written at each sweep (5 s, no buckets) and at the close
// (with the buckets). Off: nothing is counted and every send is made exactly as before.
const RELAY_TRACE = process.env.REMOTE60_RELAY_TRACE === '1';
const RELAY_TRACE_MAX_BUCKETS = 600;
const relayTraceUnmatched = { count: 0, lastMono: 0, lastSrc: '' };

function relayTraceMonoMs() {
  return Number(process.hrtime.bigint() / 1000000n);
}

function relayTraceKind(msg) {
  if (!msg || msg.length < 6 || msg.readUInt32LE(0) !== UDP_MAGIC) return 'unknown';
  const k = msg.readUInt16LE(4);
  switch (k) {
    case 300: return 'hello';
    case 301: return 'helloAck';
    case 302: return 'video';
    case 303: return 'punch';
    case 304: return 'ctlData';
    case 305: return 'ctlAck';
    case 306: return 'ctlNack';
    case 307: return 'cursor';
    case 308: return 'videoNack';
    case 309: return 'resume';
    case 310: return 'resumeAck';
    default: return (k >= 311 && k <= 316) ? 'standby' : 'other';
  }
}

function relayTraceOf(session) {
  if (!session.trace) {
    session.trace = {
      mono0: relayTraceMonoMs(), wall0: Date.now(), buckets: new Map(),
      last: { c2hRx: 0, c2hSent: 0, h2cRx: 0, h2cSent: 0 }, sendErr: { c2h: 0, h2c: 0 }, lastErr: '', events: [],
    };
  }
  return session.trace;
}

// what: 'rx' (arrived and taken for this flow), 'sent' / 'sendErr' (the socket's answer to our send).
function relayTraceCount(session, dir, what, msg) {
  if (!RELAY_TRACE || !session) return;
  const t = relayTraceOf(session);
  const mono = relayTraceMonoMs();
  const sec = Math.floor((mono - t.mono0) / 1000);
  let b = t.buckets.get(sec);
  if (!b) {
    b = {};
    t.buckets.set(sec, b);
    if (t.buckets.size > RELAY_TRACE_MAX_BUCKETS) t.buckets.delete(t.buckets.keys().next().value);
  }
  const key = `${dir}.${what}.${relayTraceKind(msg)}`;
  b[key] = (b[key] || 0) + 1;
  if (what === 'rx') t.last[`${dir}Rx`] = mono;
  if (what === 'sent') t.last[`${dir}Sent`] = mono;
}

// The callback for a send of this flow (undefined when the trace is off: the send is then exactly as before).
function relayTraceSendCb(session, dir, msg) {
  if (!RELAY_TRACE) return undefined;
  return (err) => {
    if (err) {
      const t = relayTraceOf(session);
      t.sendErr[dir]++;
      t.lastErr = `${dir}:${err.code || err.message}`;
      relayTraceCount(session, dir, 'sendErr', msg);
    } else {
      relayTraceCount(session, dir, 'sent', msg);
    }
  };
}

function relayTraceEvent(session, what) {
  if (!RELAY_TRACE || !session) return;
  const t = relayTraceOf(session);
  t.events.push(`${relayTraceMonoMs() - t.mono0}:${what}`);
  if (t.events.length > 50) t.events.shift();
}

function relayTraceLine(session, why, withBuckets) {
  const t = relayTraceOf(session);
  const mono = relayTraceMonoMs();
  const age = (x) => (x ? mono - x : -1);
  let line = `[relay-trace] connect=${session.connectId} ${why} wall=${new Date().toISOString()} mono=${mono} ` +
             `mono0=${t.mono0} wall0=${new Date(t.wall0).toISOString()} state=${session.state} ` +
             `ageMs c2hRx=${age(t.last.c2hRx)} c2hSent=${age(t.last.c2hSent)} h2cRx=${age(t.last.h2cRx)} ` +
             `h2cSent=${age(t.last.h2cSent)} sendErr c2h=${t.sendErr.c2h} h2c=${t.sendErr.h2c} ` +
             `lastErr=${t.lastErr || 'none'} events=[${t.events.join(' ')}]`;
  if (withBuckets) {
    const parts = [];
    for (const [sec, b] of t.buckets) parts.push(`${sec}:` + Object.entries(b).map(([k, v]) => `${k}=${v}`).join(','));
    line += ` buckets=[${parts.join(' ')}]`;
  }
  return line;
}

function relayDropSession(session, reason) {
  if (!session || session.dropped) return;
  session.dropped = true;
  if (RELAY_TRACE && session.trace) console.log(relayTraceLine(session, `close reason=${reason}`, true));'''),
    ('''  relayDiag.sweep();
  for (const session of [...relaySessions]) {''', '''  relayDiag.sweep();
  if (RELAY_TRACE) {
    for (const session of relaySessions) {
      if (!session.dropped && session.trace) console.log(relayTraceLine(session, 'snap', false));
    }
    if (relayTraceUnmatched.count > 0) {
      console.log(`[relay-trace] unmatched c2h count=${relayTraceUnmatched.count} lastSrc=${relayTraceUnmatched.lastSrc} ` +
                  `ageMs=${relayTraceMonoMs() - relayTraceUnmatched.lastMono}`);
    }
  }
  for (const session of [...relaySessions]) {'''),
    ('''    if (!known.includes(rinfo.address) || !proofMatches(punchProofOf(msg), session.hostProof)) {''',
     '''    if (!known.includes(rinfo.address) || !proofMatches(punchProofOf(msg), session.hostProof)) {
      relayTraceEvent(session, `h2c-refused:unproven:${relayTraceKind(msg)}`);'''),
    ('''  } else if (rinfo.address !== session.hostIp || rinfo.port !== session.hostPort) {
    relayDiag.note('standby-host-unpinned', rinfo.address,''', '''  } else if (rinfo.address !== session.hostIp || rinfo.port !== session.hostPort) {
    relayTraceEvent(session, `h2c-refused:not-pinned:${rinfo.address}:${rinfo.port}:${relayTraceKind(msg)}`);
    relayDiag.note('standby-host-unpinned', rinfo.address,'''),
    ('''  session.lastHostAt = Date.now();
  const handshake = parseHandshakePacket(msg);
  if (handshake && handshake.kind === UDP_KIND_PUNCH) return;''', '''  session.lastHostAt = Date.now();
  relayTraceCount(session, 'h2c', 'rx', msg);
  const handshake = parseHandshakePacket(msg);
  if (handshake && handshake.kind === UDP_KIND_PUNCH) return;'''),
    ('''  if (!session.clientPort) return;  // a reservation: the host's punch pinned it, nothing to carry yet
  if (relaySock) relaySock.send(msg, session.clientPort, session.clientIp);
}''', '''  if (!session.clientPort) return;  // a reservation: the host's punch pinned it, nothing to carry yet
  if (relaySock) relaySock.send(msg, session.clientPort, session.clientIp, relayTraceSendCb(session, 'h2c', msg));
}'''),
    ('''function relayForwardToHost(session, msg) {
  session.lastClientAt = Date.now();''', '''function relayForwardToHost(session, msg) {
  session.lastClientAt = Date.now();
  relayTraceCount(session, 'c2h', 'rx', msg);'''),
    ('''  if (out) out.send(msg, session.hostPort, session.hostIp);
}''', '''  if (out) out.send(msg, session.hostPort, session.hostIp, relayTraceSendCb(session, 'c2h', msg));
}'''),
    ('''      return;  // anything else opens nothing
    }''', '''      if (RELAY_TRACE) {
        relayTraceUnmatched.count++;
        relayTraceUnmatched.lastMono = relayTraceMonoMs();
        relayTraceUnmatched.lastSrc = `${rinfo.address}:${rinfo.port}:${relayTraceKind(msg)}`;
      }
      return;  // anything else opens nothing
    }'''),
    ('''  session.lastHostAt = Date.now();
  session.pktH2C++;
  session.bytesH2C += msg.length;
  // CONNECT-DIAG: the first sign of life from the host, once per session.''', '''  session.lastHostAt = Date.now();
  relayTraceCount(session, 'h2c', 'rx', msg);
  session.pktH2C++;
  session.bytesH2C += msg.length;
  // CONNECT-DIAG: the first sign of life from the host, once per session.'''),
    ('''  if (relaySock) relaySock.send(msg, session.clientPort, session.clientIp);
  return true;
}''', '''  if (relaySock) relaySock.send(msg, session.clientPort, session.clientIp, relayTraceSendCb(session, 'h2c', msg));
  return true;
}'''),
    ('''    console.log(`[relay] ENABLED ip=${RELAY_IP || '(unset -- candidate NOT offered)'} ` +''',
     '''    if (RELAY_TRACE) console.log('[relay-trace] ENABLED (REMOTE60_RELAY_TRACE=1: a test fixture only)');
    console.log(`[relay] ENABLED ip=${RELAY_IP || '(unset -- candidate NOT offered)'} ` +'''),
]
for a, b in REPS:
    rep(a, b)
s = s.replace('\n', CR + '\n') if crlf else s
open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok server')
