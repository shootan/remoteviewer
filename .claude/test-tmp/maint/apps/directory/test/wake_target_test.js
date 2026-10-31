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

const { hostSendTargetFor, wakeTargetFor } = require('../wake_target');

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
  const aim = hostSendTargetFor(hairpinned, onLan);
  check('a hairpinned host is woken on its own lan address', aim.ip === '192.168.0.76', aim.ip);
  check('...on the port it listens on', aim.port === 43000, String(aim.port));
  check('...and never at the router again', aim.ip !== hairpinned.wireIp);
  check('...reported as a lan aim', aim.via === 'lan', aim.via);
}

// (b) the negative control. If this ever changes, a host that works today stops working -- and
// note that its published lan address LOOKS like ours. Gating on the wire address is what saves it.
{
  const aim = hostSendTargetFor(remote, onLan);
  check('a host beyond our nat is aimed exactly where it was before',
        aim.ip === '211.218.222.1' && aim.port === 43000, `${aim.ip}:${aim.port}`);
  check('...even though it publishes an address that looks like ours', aim.via === 'wire', aim.via);
  check('...and is never redirected onto our own lan', aim.ip !== '192.168.0.42', aim.ip);
}

// (c) fallback: on our lan, but nothing better to aim at
{
  const noLocals = Object.assign({}, hairpinned, { localIps: [] });
  const aim = hostSendTargetFor(noLocals, onLan);
  check('with no published addresses the old aim is kept', aim.ip === '192.168.0.1', aim.ip);
  check('...and says so', aim.via === 'wire', aim.via);

  const elsewhere = Object.assign({}, hairpinned, { localIps: ['10.1.2.3', '172.30.192.1'] });
  const aim2 = hostSendTargetFor(elsewhere, onLan);
  check('published addresses on other networks are not used', aim2.ip === '192.168.0.1', aim2.ip);

  const missing = Object.assign({}, hairpinned);
  delete missing.localIps;
  check('a host that publishes none at all does not crash the wake',
        hostSendTargetFor(missing, onLan).ip === '192.168.0.1');

  const noPort = Object.assign({}, hairpinned, { localUdpPort: 0 });
  check('a host with no published port falls back to the wire port',
        hostSendTargetFor(noPort, onLan).port === 43000);
}

// (d) the aim is legible. Both branches have to name themselves, or a log cannot tell a wake that
// reached the host from one that went to the router -- which is precisely what went unnoticed.
{
  check('every aim carries a via', ['lan', 'wire'].includes(hostSendTargetFor(hairpinned, onLan).via) &&
                                   ['lan', 'wire'].includes(hostSendTargetFor(remote, onLan).via));
}

// (e) shapes that must not throw
{
  const bare = { publicIp: '203.0.113.7', publicUdpPort: 29181 };
  const aim = hostSendTargetFor(bare, onLan);
  check('a record with no wire tuple falls back to the public one',
        aim.ip === '203.0.113.7' && aim.port === 29181, `${aim.ip}:${aim.port}`);

  const empty = hostSendTargetFor({}, onLan);
  check('an empty record yields an empty aim rather than an exception', empty.ip === '');

  const junk = Object.assign({}, hairpinned, { localIps: [null, 42, '192.168.0.76'] });
  check('non-string entries are skipped, not dereferenced',
        hostSendTargetFor(junk, onLan).ip === '192.168.0.76');
}

// ------------------------------------------------------------------ the relay uses the same answer
//
// Found in the field once the wake was fixed: a client relaying to the hairpinned host bound to
// `host=192.168.0.1:43000` and the host never answered -- `closed reason=no HelloAck h2c=0/0B`.
// Only half the bug had been fixed. The relay binding and the heartbeat re-key read this same
// function now, so these are the cases above restated as the relay asks them.
{
  // (a) what a relay session binds for the hairpinned host
  const bind = hostSendTargetFor(hairpinned, onLan);
  check('a relay session binds the hairpinned host to its lan address',
        bind.ip === '192.168.0.76' && bind.port === 43000, `${bind.ip}:${bind.port}`);
  check('...not to the router, which relays to nobody', bind.ip !== '192.168.0.1');

  // (b) and leaves a host beyond the nat exactly where it was
  const remoteBind = hostSendTargetFor(remote, onLan);
  check('a relay session for a host beyond our nat is unchanged',
        remoteBind.ip === '211.218.222.1' && remoteBind.port === 43000,
        `${remoteBind.ip}:${remoteBind.port}`);

  // (c) the re-key must not put a session back on the router at the next heartbeat. It is the same
  // function, so what matters is WHEN it is called: the aim has to be taken after the heartbeat has
  // stored what the host just published. These two records are the before and after of that
  // ordering and they disagree -- which is why the call takes the aim as an argument now instead of
  // reaching for the fields itself.
  const beforeFieldsStored = Object.assign({}, hairpinned);
  delete beforeFieldsStored.localIps;
  delete beforeFieldsStored.localUdpPort;
  check('re-keying before the published addresses are stored would pick the router',
        hostSendTargetFor(beforeFieldsStored, onLan).ip === '192.168.0.1',
        'the ordering the heartbeat must not use');
  check('re-keying after them picks the host',
        hostSendTargetFor(hairpinned, onLan).ip === '192.168.0.76');

  // (d) the fallback, restated as the relay's question
  const noLocals = Object.assign({}, hairpinned, { localIps: [] });
  check('with nothing published the relay binding keeps the wire tuple',
        hostSendTargetFor(noLocals, onLan).ip === '192.168.0.1');

  // The previous export name still resolves, so a server.js and a wake_target.js that land out of
  // step still run.
  check('the previous export name still works',
        wakeTargetFor(hairpinned, onLan).ip === hostSendTargetFor(hairpinned, onLan).ip);
}

console.log(failures === 0 ? '\nwake_target_test: PASS' : `\nwake_target_test: FAILED (${failures})`);
process.exit(failures === 0 ? 0 : 1);
