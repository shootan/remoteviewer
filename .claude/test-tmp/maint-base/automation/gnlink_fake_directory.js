// The smallest directory a GNLinkViewer will talk to, for capturing what the viewer draws.
//
// Not a test double for the server's behaviour -- apps/directory/test has those. This exists so a
// screenshot run has something to point --directory-url at: it answers the four calls the viewer
// makes on the way into a connect, and names a host at an address nothing answers on, so the
// viewer sits in the punch and then the hello. That waiting state is the thing being photographed.
//
//   node automation/gnlink_fake_directory.js [httpPort]
//
// Prints the url it is listening on, then stays up until it is killed.

'use strict';

const http = require('http');
const dgram = require('dgram');

const httpPort = Number(process.argv[2] || 18200);
const udpPort = httpPort + 1;

// The observe socket. The viewer sends "OBSERVE <token>" and expects its own address back; that
// is what it publishes as its candidate, and the punch that follows goes nowhere on purpose.
const udp = dgram.createSocket('udp4');
udp.on('message', (msg, rinfo) => {
  const text = msg.toString();
  if (!text.startsWith('OBSERVE ')) return;
  udp.send(JSON.stringify({ ok: true, ip: rinfo.address, port: rinfo.port }), rinfo.port,
           rinfo.address);
});
udp.bind(udpPort, '127.0.0.1');

const json = (res, body) => {
  const payload = JSON.stringify(body);
  res.writeHead(200, { 'content-type': 'application/json',
                       'content-length': Buffer.byteLength(payload) });
  res.end(payload);
};

http.createServer((req, res) => {
  const path = (req.url || '').split('?')[0];
  let body = '';
  req.on('data', (c) => (body += c));
  req.on('end', () => {
    if (path === '/healthz') {
      return json(res, { ok: true, observe: { port: udpPort } });
    }
    if (path === '/api/login') {
      return json(res, { ok: true, sessionToken: 'fake-session-token' });
    }
    if (path === '/api/hosts') {
      return json(res, { ok: true, hosts: [{ hostId: 'h-cancel', hostName: 'cancel-pc',
                                             online: true }] });
    }
    if (path === '/api/connect') {
      // Port 9 (discard) on loopback: it resolves, nothing answers, and the viewer therefore
      // spends its punch budget and then its hello budget -- which is the state being captured.
      return json(res, {
        ok: true,
        hostPublicIp: '127.0.0.1',
        hostPublicUdpPort: 9,
        candidates: [{ ip: '127.0.0.1', port: 9, kind: 'private' }],
        punchToken: 'c'.repeat(32),
        connectId: 'shots',
      });
    }
    json(res, { ok: true });
  });
}).listen(httpPort, '127.0.0.1', () => {
  console.log('http://127.0.0.1:' + httpPort);
});
