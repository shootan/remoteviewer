// Loaded into the directory server by wake_resend_test.js with --require, and by nothing else.
//
// Two things the resend's stop paths depend on that a test cannot otherwise reach in seconds:
//   - time: a capability lives 30 s and a host goes stale after 90 s, so "expired" and "stale"
//     are reached by moving Date.now forward rather than by waiting
//   - a failing send: the tick's try/catch is only exercised by an exception inside it
// Both are driven by a small JSON file the test rewrites (WAKE_TEST_CONTROL), read every 25 ms.
// server.js itself is not changed for this: it has no test switch, so the deployed file cannot
// have one either. (RV-10)

const fs = require('fs');
const dgram = require('dgram');

const controlPath = process.env.WAKE_TEST_CONTROL;
if (controlPath) {
  let state = { offsetMs: 0, throwOnPunch: false };
  const read = () => {
    try { state = JSON.parse(fs.readFileSync(controlPath, 'utf8')); } catch { /* keep the last */ }
  };
  read();
  setInterval(read, 25).unref();

  const realNow = Date.now;
  Date.now = () => realNow() + (state.offsetMs || 0);

  // Only the 49-byte punch datagram, so observe replies and everything else keep working.
  const realSend = dgram.Socket.prototype.send;
  dgram.Socket.prototype.send = function patchedSend(msg, ...rest) {
    if (state.throwOnPunch && Buffer.isBuffer(msg) && msg.length === 49 &&
        msg.readUInt16LE(4) === 303) {
      throw new Error('test: punch send failure injected');
    }
    return realSend.call(this, msg, ...rest);
  };
}
