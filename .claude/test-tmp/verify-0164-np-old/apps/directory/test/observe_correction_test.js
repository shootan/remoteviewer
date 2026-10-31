// Pins the own-LAN observation correction.
//
// An OBSERVE that arrives without crossing the sender's NAT tells the server nothing about how
// the internet would reach that sender. From the loopback the source is simply local; from the
// server's own LAN it is worse, because a router doing hairpin NAT rewrites the source to its own
// interface -- an address that answers for nobody. Either way the host would advertise it as its
// public candidate and no outside client could use it.
//
// So the server substitutes its own public address for peers on its own subnets, keeping the
// observed port. What is checked here is that the substitution fires for a LAN source, leaves the
// port alone, and does not fire for loopback (which is where every other test speaks from -- if
// it fired there, the rest of the suite would be measuring a fiction).
//
// Run against a server started with REMOTE60_PUBLIC_IP set to T_PUBLIC_IP.
const dgram = require('dgram');
const os = require('os');

const UDP = Number(process.env.T_UDP || 18081);
const PUBLIC_IP = process.env.T_PUBLIC_IP || '203.0.113.9';
let failures = 0;

function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

/** The first non-internal IPv4 address of this machine, or null when there is none. */
function lanAddress() {
  for (const addrs of Object.values(os.networkInterfaces())) {
    for (const a of addrs || []) {
      if (a.family === 'IPv4' && !a.internal && a.address) return a.address;
    }
  }
  return null;
}

/** Sends one OBSERVE from `bindIp` to `targetIp` and resolves with what the server reported. */
function observe(bindIp, targetIp) {
  return new Promise((resolve, reject) => {
    const sock = dgram.createSocket('udp4');
    const timer = setTimeout(() => {
      sock.close();
      reject(new Error(`no observe reply from ${targetIp}:${UDP}`));
    }, 3000);
    sock.on('message', (msg) => {
      clearTimeout(timer);
      const sourcePort = sock.address().port;
      sock.close();
      try {
        resolve({ reply: JSON.parse(msg.toString('utf8')), sourcePort });
      } catch (e) {
        reject(e);
      }
    });
    sock.on('error', (e) => { clearTimeout(timer); reject(e); });
    sock.bind(0, bindIp, () => {
      const token = 'observe-correction-' + Math.random().toString(36).slice(2);
      const payload = Buffer.from('OBSERVE ' + token);
      sock.send(payload, UDP, targetIp);
    });
  });
}

(async () => {
  const lan = lanAddress();
  if (!lan) {
    console.log('SKIP  no non-loopback IPv4 interface on this machine');
    process.exit(0);
  }

  const fromLan = await observe(lan, lan);
  check('a peer on the server\'s own lan is reported as the server\'s public address',
        fromLan.reply.ip === PUBLIC_IP, `ip=${fromLan.reply.ip} expected=${PUBLIC_IP}`);
  check('the observed port survives the substitution',
        fromLan.reply.port === fromLan.sourcePort,
        `port=${fromLan.reply.port} sent-from=${fromLan.sourcePort}`);

  const fromLoopback = await observe('127.0.0.1', '127.0.0.1');
  check('loopback is left alone, so the rest of the suite still measures the real source',
        fromLoopback.reply.ip === '127.0.0.1', `ip=${fromLoopback.reply.ip}`);

  console.log(failures === 0 ? '\nobserve_correction_test: PASS' : `\nobserve_correction_test: ${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => {
  console.error('observe_correction_test error:', e.message);
  process.exit(1);
});
