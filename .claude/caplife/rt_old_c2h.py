"""old-ref r1: the old (fae0e1f) server's client->host forward -- the same two trace lines 7a79b09 adds to the new one
(its hunk 8 did not apply: the old function sends from observeSock only, there is no standby lease)."""
import sys

p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''function relayForwardToHost(session, msg) {
  session.lastClientAt = Date.now();
  session.pktC2H++;''', '''function relayForwardToHost(session, msg) {
  session.lastClientAt = Date.now();
  relayTraceCount(session, 'c2h', 'rx', msg);
  session.pktC2H++;''')
rep('''  if (observeSock) observeSock.send(msg, session.hostPort, session.hostIp);
}''', '''  if (observeSock) observeSock.send(msg, session.hostPort, session.hostIp, relayTraceSendCb(session, 'c2h', msg));
}''')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
