base = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\directory'


def edit(name, reps):
    p = base + '\\' + name
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    s = raw.decode('utf-8').replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


edit('server.js', [
("""      last: { c2hRx: 0, c2hSent: 0, h2cRx: 0, h2cSent: 0 }, sendErr: { c2h: 0, h2c: 0 }, lastErr: '', events: [],
    };""", """      last: { c2hRx: 0, c2hSent: 0, h2cRx: 0, h2cSent: 0 }, sendErr: { c2h: 0, h2c: 0 }, lastErr: '', events: [],
      // apk-decoder-inbox r2 (Codex codex_apk_inbox_plan_src_r1.md, traffic): the UDP payload bytes that ARRIVED for this
      // flow, per direction (counted once, at rx -- never again at sent), cumulative and per 1 s bucket (`<key>B`)
      rxBytes: { c2h: 0, h2c: 0 }, rxPkts: { c2h: 0, h2c: 0 },
    };"""),
("""  const key = `${dir}.${what}.${relayTraceKind(msg)}`;
  b[key] = (b[key] || 0) + 1;""", """  const key = `${dir}.${what}.${relayTraceKind(msg)}`;
  b[key] = (b[key] || 0) + 1;
  if (what === 'rx') {
    const len = msg ? msg.length : 0;
    b[`${key}B`] = (b[`${key}B`] || 0) + len;
    t.rxBytes[dir] += len;
    t.rxPkts[dir] += 1;
  }"""),
("""             `lastErr=${t.lastErr || 'none'} events=[${t.events.join(' ')}]`;""",
 """             `lastErr=${t.lastErr || 'none'} rxBytes c2h=${t.rxBytes.c2h}/${t.rxPkts.c2h} h2c=${t.rxBytes.h2c}/${t.rxPkts.h2c} ` +
             `events=[${t.events.join(' ')}]`;"""),
])

edit(r'test\relay_trace_test.js', [
("""  check('no credential in the output', !on.out.includes('test-pass-1234') && !on.out.includes('outsider-pass-1234'));""",
 """  check('no credential in the output', !on.out.includes('test-pass-1234') && !on.out.includes('outsider-pass-1234'));
  // apk-decoder-inbox r2: the bytes that arrived, per direction -- a KNOWN length: every datagram relay_test.js
  // relays is a 49-byte Hello / HelloAck / punch (HELLO_BYTES), so the last snapshot of each flow says bytes = 49 x pkts
  const byFlow = new Map();
  for (const l of flows) {
    const m = / connect=(\\S+) .* rxBytes c2h=(\\d+)\\/(\\d+) h2c=(\\d+)\\/(\\d+) /.exec(l);
    if (m) byFlow.set(m[1], m.slice(2).map(Number));
  }
  const totals = [...byFlow.values()].reduce((a, v) => a.map((x, i) => x + v[i]), [0, 0, 0, 0]);
  check('each flow\\'s arrived bytes are counted per direction (rxBytes c2h=B/n h2c=B/n)', byFlow.size > 0 && totals[1] + totals[3] > 0,
        `flows=${byFlow.size} c2h=${totals[0]}/${totals[1]} h2c=${totals[2]}/${totals[3]}`);
  check('the count is exact for a known length: bytes = 49 x packets in both directions',
        totals[0] === 49 * totals[1] && totals[2] === 49 * totals[3], `c2h=${totals[0]}/${totals[1]} h2c=${totals[2]}/${totals[3]}`);"""),
])
print('ok')
