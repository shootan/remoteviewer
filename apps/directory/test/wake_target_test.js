// Where a wake is aimed (wake_target.js).
//
// Pins the field failure of 2026-09-18: the one host whose OBSERVE reaches this server through a
// hairpin had wireIp=192.168.0.1 -- the router -- so every wake went to the router and the host
// learned about a waiting peer only at its next 25-second heartbeat, by which time the client had
// given up. It was refused with "invalid directory capability", which says nothing about the real
// cause: the capability was correct and simply had not arrived yet.
//
// Pure: no server, no sockets. The same-LAN predicate is injected, because the real one reads this
// machine's interfaces and a test that depended on those would pass or fail by where it runs.

const { wakeTargetFor } = require('../wake_target');

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

// A stand-in for the server being attached to 192.168.0.0/24, injected rather than measured.
const onLan = (ip) => typeof ip === 'string' && ip.startsWith('192.168.0.');

// The live record, copied from directory-data.json at the time of the failure.
const hairpinned = {
  hostName: 'shotan Desktop',
  publicIp: '175.207.45.151',
  publicUdpPort: 43000,
  wireIp: '192.168.0.1',      // the router, not the host
  wirePort: 43000,
  localIps: ['192.168.0.76', '172.30.192.1', '172.30.80.1'],
  localUdpPort: 43000,
};

// The other host on the same account, which never had this symptom.
const remote = {
  hostName: 'DEV-SHOTAN-L',
  publicIp: '211.218.222.1',
  publicUdpPort: 43000,
  wireIp: '211.218.222.1',
  wirePort: 43000,
  // On ITS lan -- and deliberately inside the same private range as ours, because that is the
  // common case (192.168.0.x is what most home routers hand out) and it is what makes the wire
  // gate load-bearing. Without the gate this address looks local to us and the wake would be
  // redirected to whatever machine actually holds 192.168.0.42 here, which is a stranger.
  localIps: ['192.168.0.42'],
  localUdpPort: 43000,
};

// (a) the fix
{
  const aim = wakeTargetFor(hairpinned, onLan);
  check('a hairpinned host is woken on its own lan address', aim.ip === '192.168.0.76', aim.ip);
  check('...on the port it listens on', aim.port === 43000, String(aim.port));
  check('...and never at the router again', aim.ip !== hairpinned.wireIp);
  check('...reported as a lan aim', aim.via === 'lan', aim.via);
}

// (b) the negative control. If this ever changes, a host that works today stops working -- and
// note that its published lan address LOOKS like ours. Gating on the wire address is what saves it.
{
  const aim = wakeTargetFor(remote, onLan);
  check('a host beyond our nat is aimed exactly where it was before',
        aim.ip === '211.218.222.1' && aim.port === 43000, `${aim.ip}:${aim.port}`);
  check('...even though it publishes an address that looks like ours', aim.via === 'wire', aim.via);
  check('...and is never redirected onto our own lan', aim.ip !== '192.168.0.42', aim.ip);
}

// (c) fallback: on our lan, but nothing better to aim at
{
  const noLocals = Object.assign({}, hairpinned, { localIps: [] });
  const aim = wakeTargetFor(noLocals, onLan);
  check('with no published addresses the old aim is kept', aim.ip === '192.168.0.1', aim.ip);
  check('...and says so', aim.via === 'wire', aim.via);

  const elsewhere = Object.assign({}, hairpinned, { localIps: ['10.1.2.3', '172.30.192.1'] });
  const aim2 = wakeTargetFor(elsewhere, onLan);
  check('published addresses on other networks are not used', aim2.ip === '192.168.0.1', aim2.ip);

  const missing = Object.assign({}, hairpinned);
  delete missing.localIps;
  check('a host that publishes none at all does not crash the wake',
        wakeTargetFor(missing, onLan).ip === '192.168.0.1');

  const noPort = Object.assign({}, hairpinned, { localUdpPort: 0 });
  check('a host with no published port falls back to the wire port',
        wakeTargetFor(noPort, onLan).port === 43000);
}

// (d) the aim is legible. Both branches have to name themselves, or a log cannot tell a wake that
// reached the host from one that went to the router -- which is precisely what went unnoticed.
{
  check('every aim carries a via', ['lan', 'wire'].includes(wakeTargetFor(hairpinned, onLan).via) &&
                                   ['lan', 'wire'].includes(wakeTargetFor(remote, onLan).via));
}

// (e) shapes that must not throw
{
  const bare = { publicIp: '203.0.113.7', publicUdpPort: 29181 };
  const aim = wakeTargetFor(bare, onLan);
  check('a record with no wire tuple falls back to the public one',
        aim.ip === '203.0.113.7' && aim.port === 29181, `${aim.ip}:${aim.port}`);

  const empty = wakeTargetFor({}, onLan);
  check('an empty record yields an empty aim rather than an exception', empty.ip === '');

  const junk = Object.assign({}, hairpinned, { localIps: [null, 42, '192.168.0.76'] });
  check('non-string entries are skipped, not dereferenced',
        wakeTargetFor(junk, onLan).ip === '192.168.0.76');
}

console.log(failures === 0 ? '\nwake_target_test: PASS' : `\nwake_target_test: FAILED (${failures})`);
process.exit(failures === 0 ? 0 : 1);
