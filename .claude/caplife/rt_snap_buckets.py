"""old-ref r1: the relay trace's periodic snap carries the buckets of the seconds completed since the previous snap (the
fixture kills Node by its handle: no close line then). Applied to a TRACED server.js in place (either base)."""
import sys

p = sys.argv[1]
raw = open(p, 'rb').read()
s = raw.decode('utf-8')
assert b'\r\n' not in raw


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''      last: { c2hRx: 0, c2hSent: 0, h2cRx: 0, h2cSent: 0 }, sendErr: { c2h: 0, h2c: 0 }, lastErr: '', events: [],
    };''', '''      last: { c2hRx: 0, c2hSent: 0, h2cRx: 0, h2cSent: 0 }, sendErr: { c2h: 0, h2c: 0 }, lastErr: '', events: [],
      snapThroughSec: -1,  // old-ref r1: the last COMPLETED second a snap already wrote
    };''')
rep('''  if (withBuckets) {
    const parts = [];
    for (const [sec, b] of t.buckets) parts.push(`${sec}:` + Object.entries(b).map(([k, v]) => `${k}=${v}`).join(','));
    line += ` buckets=[${parts.join(' ')}]`;
  }
  return line;''', '''  if (withBuckets) {
    const parts = [];
    for (const [sec, b] of t.buckets) parts.push(`${sec}:` + Object.entries(b).map(([k, v]) => `${k}=${v}`).join(','));
    line += ` buckets=[${parts.join(' ')}]`;
  } else {
    // old-ref r1 (Codex codex_old_apk_lane_peer_lost_decision_r1.md): the fixture ends Node by its handle -- no close
    // line. Each snap writes the buckets of the seconds COMPLETED since the previous snap (header kinds only, never a
    // payload), so they are on disk before Node goes. "sent" is the socket's word only, not the far end's receipt.
    const nowSec = Math.floor((mono - t.mono0) / 1000);
    const parts = [];
    for (const [sec, b] of t.buckets) {
      if (sec > t.snapThroughSec && sec < nowSec) {
        parts.push(`${sec}:` + Object.entries(b).map(([k, v]) => `${k}=${v}`).join(','));
      }
    }
    line += ` buckets=[${parts.join(' ')}] bucketsThrough=${nowSec - 1}`;
    t.snapThroughSec = nowSec - 1;
  }
  return line;''')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
