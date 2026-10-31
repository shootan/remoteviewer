# usage: python -I ka_r4_mut.py <name> <apply|revert>
import sys
SV = 'C:/work/gnlink-wt/ka-r3/apps/directory/server.js'
TB = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_table.hpp'
WR = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_wire.hpp'
MUTS = {
    # N1a: the r3 shape -- the first datagram from the host's IP pins, proof or not
    'N1a': (SV, '''    if (!known.includes(rinfo.address) || !proofMatches(punchProofOf(msg), session.hostProof)) {''',
                '''    if (!known.includes(rinfo.address)) {  // MUTANT N1a'''),
    # N1b: a moved host keeps its old proof (no fresh one)
    'N1b': (SV, '''      standby.hostPinned = false;
      relayStandbyReprove(standby, 'host moved');''', '''      standby.hostPinned = false;  // MUTANT N1b: no reproof'''),
    # N3a: the legacy bind does not count the standby leases
    'N3a': (SV, '''    if (!replacing && standbyOnHost >= RELAY_STANDBY_MAX_PER_HOST) {''',
                '''    if (false) {  // MUTANT N3a'''),
    # N3b: the r3 env parse (NaN makes the bound vanish; nothing is said)
    'N3b': (SV, '''  if (raw === undefined || raw === '') return fallback;
  const v = Number(raw);''', '''  return Math.max(1, Number(raw || fallback));  // MUTANT N3b
  const v = Number(raw);'''),
    # N2a: a shortened comparison (the first 8 bytes)
    'N2a': (TB, '''      if (e.admitGen == gen && e.proof == proof && e.admitFrom == from''',
                '''      if (e.admitGen == gen && std::memcmp(e.proof.data(), proof.data(), 8) == 0 && e.admitFrom == from'''),
    # N2b: no Stale (the r3 shape: an older admission replaces the entry)
    'N2b': (TB, '''    if (sameDevice != entries_.size() && gen <= entries_[sameDevice].admitGen) return AdmitResult::Stale;''',
                '''    // MUTANT N2b: no Stale'''),
    # N2c: a failed digest is not fail-closed (the zero digest goes on)
    'N2c': (WR, '''    if (!digested) {''', '''    if (false && !digested) {  // MUTANT N2c'''),
}
name, op = sys.argv[1], sys.argv[2]
path, good, bad = MUTS[name]
s = open(path, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a, b = (good, bad) if op == 'apply' else (bad, good)
assert s.count(a) == 1, (name, op, s.count(a))
s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(path, 'wb').write(s.encode('utf-8'))
print(name, op, 'ok')
