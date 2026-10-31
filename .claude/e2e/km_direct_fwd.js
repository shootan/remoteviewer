// keepalive-multi r2 V1: a DIRECT path to one fail-closed fixture host for the AVD. The host stays bound to
// 127.0.0.1; this forwards UDP arriving at <lanIp>:<port> (the host's own "private" candidate) to 127.0.0.1:<port>
// and back, so the phone's punch / Hello / session use the private candidate rather than the relay. Accepts only
// sources in the allow list (this PC's own addresses -- the emulator's traffic leaves from them). TEST FIXTURE ONLY.
//   node km_direct_fwd.js <lanIp> <port> <allowCsv> <logFile>
'use strict';
const dgram = require('dgram');
const fs = require('fs');
const [lanIp, portS, allowCsv, logFile] = process.argv.slice(2);
const port = Number(portS);
const allow = new Set(allowCsv.split(','));
const log = (l) => fs.appendFileSync(logFile, `${new Date().toISOString()} ${l}\n`);
const front = dgram.createSocket('udp4');
const peers = new Map();  // "ip:port" -> {up, toHost, toPeer}
let rejected = 0;
front.on('message', (msg, rinfo) => {
  if (!allow.has(rinfo.address)) {
    if (++rejected <= 5) log(`reject ${rinfo.address}:${rinfo.port}`);
    return;
  }
  const key = `${rinfo.address}:${rinfo.port}`;
  let p = peers.get(key);
  if (!p) {
    const up = dgram.createSocket('udp4');
    p = { up, toHost: 0, toPeer: 0 };
    const peer = p;
    up.on('message', (back) => { peer.toPeer++; front.send(back, rinfo.port, rinfo.address); });
    up.bind(0, '127.0.0.1');
    peers.set(key, p);
    log(`peer ${key}`);
  }
  p.toHost++;
  p.up.send(msg, port, '127.0.0.1');
});
front.on('error', (e) => { log(`error ${e.message}`); process.exit(1); });
front.bind(port, lanIp, () => log(`listening ${lanIp}:${port} -> 127.0.0.1:${port} allow=${allowCsv}`));
setInterval(() => {
  for (const [k, p] of peers) log(`counts ${k} toHost=${p.toHost} toPeer=${p.toPeer}`);
}, 10000);
