p = 'C:/work/gnlink-wt/ka-r3/apps/directory/test/relay_standby_r4_test.js'
s = open(p, 'rb').read().decode('utf-8')
a = s.index("    const h = await mkHost('r4-n3a');")
b = s.index("  // ------------------------------------------------------------ N3: legacy + seven standby")
new = """    const h = await mkHost('r4-n3a');
    const clients = [];
    const outcomes = [];
    for (let i = 0; i < 8; ++i) {
      const c = await mkClient('n3a' + i);
      c.token = (await connect(h, c, true)).body;
      clients.push(c);
      outcomes.push(c.token.standbyRelay);
    }
    await h.heartbeat(true);  // the host punches every lease with its proof
    await sleep(300);
    // bind all eight (StandbyHello -> Ack)
    const entries = [];
    for (let i = 0; i < 8; ++i) {
      const c = clients[i];
      c.sock.send(buildStandby(KIND_SB_HELLO, { token: c.token.punchToken, device: 0x60 + i, gen: 1 }), RELAY_PORT, '127.0.0.1');
      const a = await waitFor(() => c.got.find((p) => kindOf(p.msg) === KIND_SB_ACK));
      entries.push(a ? a.msg.readBigUInt64LE(32) : 0n);
    }
    check('N3: eight standby leases fill a host, all bound', outcomes.every((o) => o === 'reserved') && entries.every((e) => e !== 0n),
          JSON.stringify(outcomes));
    const L = await mkClient('n3aL');
    const rl = await connect(h, L, false);
    await h.heartbeat(true);
    h.media.got.length = 0;
    L.sock.send(buildHello(KIND_HELLO, rl.body.punchToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
    await sleep(500);
    check('N3: a NEW legacy Hello beside eight standby leases is refused: it never reaches the host, no HelloAck',
          !h.media.got.some((p) => kindOf(p.msg) === KIND_HELLO) && !L.got.some((p) => kindOf(p.msg) === KIND_HELLO_ACK));
    // nothing was dropped: every one of the eight still answers its ping
    let pongs = 0;
    for (let i = 0; i < 8; ++i) {
      const c = clients[i];
      c.got.length = 0;
      c.sock.send(buildStandby(KIND_SB_PING, { entryId: entries[i] }), RELAY_PORT, '127.0.0.1');
      if (await waitFor(() => c.got.find((p) => kindOf(p.msg) === KIND_SB_PONG), 1000)) ++pongs;
    }
    check('N3: ...and the refusal dropped nothing: all eight standby leases still carry their pings', pongs === 8, String(pongs));
  }

"""
s = s[:a] + new + s[b:]
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
