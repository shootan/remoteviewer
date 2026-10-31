  // apk-decoder-inbox r2: the bytes that arrived, per direction -- KNOWN lengths: relay_test.js relays only 49-byte
  // datagrams (Hello / HelloAck / punch, HELLO_BYTES) and 1200-byte opaque media, so each flow's last snapshot must say
  // bytes = 49 x a + 1200 x b with a + b = packets (a, b >= 0 whole numbers) -- and media (b >= 1) was counted
  const byFlow = new Map();
  for (const l of flows) {
    const m = / connect=(\S+) .* rxBytes c2h=(\d+)\/(\d+) h2c=(\d+)\/(\d+) /.exec(l);
    if (m) byFlow.set(m[1], m.slice(2).map(Number));
  }
  const totals = [...byFlow.values()].reduce((a, v) => a.map((x, i) => x + v[i]), [0, 0, 0, 0]);
  check('each flow\'s arrived bytes are counted per direction (rxBytes c2h=B/n h2c=B/n)', byFlow.size > 0 && totals[1] + totals[3] > 0,
        `flows=${byFlow.size} c2h=${totals[0]}/${totals[1]} h2c=${totals[2]}/${totals[3]}`);
  const media = (bytes, n) => {
    const extra = bytes - 49 * n;  // what the 1200-byte packets add over 49 each
    return extra >= 0 && extra % 1151 === 0 && extra / 1151 <= n ? extra / 1151 : -1;
  };
  const mc = media(totals[0], totals[1]);
  const mh = media(totals[2], totals[3]);
  check('the count is exact for the known lengths (49 / 1200): whole Hello + media packets, media seen both ways',
        mc >= 1 && mh >= 1, `c2h=${totals[0]}/${totals[1]} (media ${mc}) h2c=${totals[2]}/${totals[3]} (media ${mh})`);
