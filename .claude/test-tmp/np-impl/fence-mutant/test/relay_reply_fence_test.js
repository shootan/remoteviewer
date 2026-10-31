// np-impl r2 (Codex N1): a scheduled relay reply belongs to the connect it was scheduled for.
//
// relayScheduleReply is taken from server.js as it stands and run on a virtual clock -- its timers, Date.now and
// the relay socket are stand-ins that record, nothing goes on a network. That aims at the exact instants the
// finding is about: a connect replaced 50 ms into its early replies, an entry that expires between two replies,
// the grace replies of a legacy connect -- orderings a real server only produces by luck. The same-tuple case is
// also run against a real server in relay_early_test.js.
const fs = require('fs');
const path = require('path');
const vm = require('vm');

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

const source = fs.readFileSync(path.join(__dirname, '..', 'server.js'), 'utf8');
const m = source.match(/\r?\nfunction relayScheduleReply\([\s\S]*?\r?\n\}\r?\n/);  // a checkout may be CRLF
if (!m) {
  console.log('FAIL  relayScheduleReply not found in server.js');
  process.exit(1);
}

const GRACE = 2500;

function harness() {
  let now = 1000000;
  const timers = [];
  const sends = [];
  const ctx = {
    RELAY_GRACE_MS: GRACE,
    RELAY_PUNCH_REPLIES: 3,
    relayEligibleByWireIp: new Map(),
    relaySock: { send: (pkt, port, ip) => sends.push({ at: now, ip, port, bytes: pkt.length }) },
    endpointKey: (ip, port) => `${ip}:${port}`,
    buildPunchPacket: () => Buffer.alloc(49),
    setTimeout: (fn, ms) => { timers.push({ at: now + ms, fn }); return { unref() {} }; },
    Date: { now: () => now },
    console: { log() {}, error() {} },
  };
  vm.createContext(ctx);
  vm.runInContext(m[0], ctx);
  const advanceTo = (t) => {
    for (;;) {
      timers.sort((a, b) => a.at - b.at);
      const next = timers[0];
      if (!next || next.at > t) break;
      timers.shift();
      now = next.at;
      next.fn();
    }
    now = t;
  };
  return { ctx, sends, advanceTo, start: now, at: () => now };
}

const record = (over) => Object.assign({ connectId: 'c', expiresAt: 1000000 + 60000, replied: new Set() }, over);

// 1. an accepted early connect: three replies at 0/120/240
{
  const h = harness();
  const a = record({ connectId: 'A', early: true, earlyWirePort: 42001 });
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', a);
  h.ctx.relayScheduleReply('10.0.0.1', 42001, a);
  h.advanceTo(h.start + 1000);
  check('early, undisturbed: three replies at 0/120/240',
        h.sends.length === 3 && h.sends.map((s) => s.at - h.start).join() === '0,120,240',
        h.sends.map((s) => s.at - h.start).join());
}

// 2. the finding: A early, then at 50 ms B (no flag) takes the SAME tuple
{
  const h = harness();
  const a = record({ connectId: 'A', early: true, earlyWirePort: 42001 });
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', a);
  h.ctx.relayScheduleReply('10.0.0.1', 42001, a);
  h.advanceTo(h.start + 50);
  const b = record({ connectId: 'B' });
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', b);
  const before = h.sends.length;
  h.advanceTo(h.start + 1000);
  check('A replaced at 50 ms by B on the same tuple: nothing more of A is sent',
        before === 1 && h.sends.length === 1, `before=${before} after=${h.sends.length}`);
  // B punches at 300 ms from the same tuple: its own replies come after its grace, not before
  h.advanceTo(h.start + 300);
  h.ctx.relayScheduleReply('10.0.0.1', 42001, b);
  h.advanceTo(h.start + 300 + GRACE + 1000);
  const bSends = h.sends.slice(1).map((s) => s.at - (h.start + 300));
  check("B's replies are its own, after its grace", bSends.length === 3 && bSends[0] === GRACE,
        `B sends at +${bSends.join(',+')}`);
}

// 3. the entry expires between the first and second early reply
{
  const h = harness();
  const a = record({ connectId: 'A', early: true, earlyWirePort: 42001, expiresAt: 1000000 + 60 });
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', a);
  h.ctx.relayScheduleReply('10.0.0.1', 42001, a);
  h.advanceTo(h.start + 1000);
  check('expired after the first reply: one sent, then none', h.sends.length === 1, `sends=${h.sends.length}`);
}

// 4. replaced by a connect from another tuple on the same wire IP: A's pending replies stop too
{
  const h = harness();
  const a = record({ connectId: 'A', early: true, earlyWirePort: 42001 });
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', a);
  h.ctx.relayScheduleReply('10.0.0.1', 42001, a);
  h.advanceTo(h.start + 50);
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', record({ connectId: 'C', early: true, earlyWirePort: 42002 }));
  h.advanceTo(h.start + 1000);
  check('replaced by another tuple on the same IP: the old record sends nothing more', h.sends.length === 1,
        `sends=${h.sends.length}`);
  // A's client keeps punching; the next punch is answered under the record that replaced it (the grace,
  // its port is not C's), so A is not left without a relay.
  const current = h.ctx.relayEligibleByWireIp.get('10.0.0.1');
  h.ctx.relayScheduleReply('10.0.0.1', 42001, current);
  h.advanceTo(h.start + 1050 + GRACE + 500);
  const later = h.sends.slice(1).map((s) => s.at - (h.start + 1000));
  check("A's next punch is answered under the new record, after the grace", later.length === 3 && later[0] === GRACE,
        `+${later.join(',+')}`);
}

// 5. a legacy connect, undisturbed: the grace as always
{
  const h = harness();
  const a = record({ connectId: 'L' });
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', a);
  h.ctx.relayScheduleReply('10.0.0.1', 42001, a);
  h.advanceTo(h.start + GRACE + 1000);
  check('legacy, undisturbed: three replies at the grace', h.sends.map((s) => s.at - h.start).join() === '2500,2620,2740',
        h.sends.map((s) => s.at - h.start).join());
}

// 6. a legacy connect replaced before its grace: none of its replies is sent into the newer race
{
  const h = harness();
  const a = record({ connectId: 'L' });
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', a);
  h.ctx.relayScheduleReply('10.0.0.1', 42001, a);
  h.advanceTo(h.start + 2000);
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', record({ connectId: 'M' }));
  h.advanceTo(h.start + GRACE + 1000);
  check('legacy replaced at 2000 ms: none of its grace replies is sent', h.sends.length === 0, `sends=${h.sends.length}`);
}

// 7. the record removed by the expiry sweep: nothing sent
{
  const h = harness();
  const a = record({ connectId: 'S', early: true, earlyWirePort: 42001 });
  h.ctx.relayEligibleByWireIp.set('10.0.0.1', a);
  h.ctx.relayScheduleReply('10.0.0.1', 42001, a);
  h.advanceTo(h.start + 10);
  h.ctx.relayEligibleByWireIp.delete('10.0.0.1');
  h.advanceTo(h.start + 1000);
  check('record swept away after the first reply: nothing more', h.sends.length === 1, `sends=${h.sends.length}`);
}

console.log(`\nrelay_reply_fence_test: ${failures === 0 ? 'ALL PASS' : failures + ' FAILED'}`);
process.exit(failures === 0 ? 0 : 1);
