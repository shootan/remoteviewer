// keepalive r2 K2: the fixture directory behind a proxy that can hold back one answer -- POST /api/connect
// -- for as long as <delayFile> says (ms), so the APK's late connect answer can be made to arrive after a
// sign-out. Everything else passes straight through. TEST FIXTURE ONLY.
//
//   node ka_delay_proxy.js <listenPort> <targetPort> <delayFile> <logFile>
'use strict';
const http = require('http');
const fs = require('fs');

const listenPort = Number(process.argv[2] || 18320);
const targetPort = Number(process.argv[3] || 18310);
const delayFile = process.argv[4];
const logFile = process.argv[5];
const log = (line) => fs.appendFileSync(logFile, `${new Date().toISOString()} ${line}\n`);

http.createServer((req, res) => {
  const chunks = [];
  req.on('data', (c) => chunks.push(c));
  req.on('end', () => {
    const body = Buffer.concat(chunks);
    const up = http.request({ host: '127.0.0.1', port: targetPort, path: req.url, method: req.method,
                              headers: req.headers }, (ur) => {
      const out = [];
      ur.on('data', (c) => out.push(c));
      ur.on('end', () => {
        let delay = 0;
        if (req.method === 'POST' && (req.url || '').startsWith('/api/connect')) {
          try { delay = Number(fs.readFileSync(delayFile, 'utf8').trim()) || 0; } catch { delay = 0; }
        }
        log(`${req.method} ${req.url} -> ${ur.statusCode} delay=${delay}`);
        setTimeout(() => {
          res.writeHead(ur.statusCode, ur.headers);
          res.end(Buffer.concat(out));
          if (delay) log(`released ${req.url} after ${delay} ms`);
        }, delay);
      });
    });
    up.on('error', (e) => { log(`upstream error ${e.message}`); res.writeHead(502); res.end(); });
    up.end(body);
  });
}).listen(listenPort, '127.0.0.1', () => log(`proxy :${listenPort} -> :${targetPort}`));
