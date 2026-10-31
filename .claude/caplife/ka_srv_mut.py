# usage: python -I ka_srv_mut.py <R1..R6> <apply|revert>  -- server.js mutants for relay decision (a)
import sys
P = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\directory\\server.js'
MUTS = {
    # R1: a standby lease goes out of the shared observe socket (one host-facing address, option (b))
    'R1': ('''  const out = session.cls === 'standby' ? session.sock : observeSock;''',
           '''  const out = observeSock;  // MUTANT R1'''),
    # R2: a legacy bind drops every lease of the host (newest wins over all of them)
    'R2': ('''  if (existingLease && !existingLease.dropped) relayDropSession(existingLease, 'newer connect');''',
           '''  if (existingLease && !existingLease.dropped) relayDropSession(existingLease, 'newer connect');
  for (const sb of [...(relayStandbyByHost.get(auth.hostId) || [])]) relayDropSession(sb, 'newer connect');  // MUTANT R2'''),
    # R3: the heartbeat's tuple is written over a pinned lease
    'R3': ('''    if (!standby.hostPinned) {
      standby.hostIp = aim.ip;''',
           '''    if (true) {  // MUTANT R3
      standby.hostIp = aim.ip;'''),
    # R4: a lease takes host-side datagrams from anyone after it pinned
    'R4': ('''  } else if (rinfo.address !== session.hostIp || rinfo.port !== session.hostPort) {
    relayDiag.note('standby-host-unpinned',''',
           '''  } else if (false) {  // MUTANT R4
    relayDiag.note('standby-host-unpinned','''),
    # R5: a standby capability is accepted by a legacy Hello (a takeover with it)
    'R5': ('''  if (auth.cls === 'standby') {
    relayDiag.note('hello-standby-token',''',
           '''  if (false) {  // MUTANT R5
    relayDiag.note('hello-standby-token','''),
    # R6: the legacy lease is not counted toward the host's eight
    'R6': ('''  const onHost = (relayStandbyByHost.get(hostId) || new Set()).size + (legacy && !legacy.dropped ? 1 : 0);''',
           '''  const onHost = (relayStandbyByHost.get(hostId) || new Set()).size;  // MUTANT R6'''),
}
name, op = sys.argv[1], sys.argv[2]
good, bad = MUTS[name]
s = open(P, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a, b = (good, bad) if op == 'apply' else (bad, good)
assert s.count(a) == 1, (name, op, s.count(a))
s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(P, 'wb').write(s.encode('utf-8'))
print(name, op, 'ok')
