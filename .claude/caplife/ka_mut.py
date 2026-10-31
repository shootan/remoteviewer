# usage: python -I ka_mut.py <K1..K7> <apply|revert>
# keepalive-r2 A r3 mutants: each puts back ONE shape Codex named (codex_keepalive_r2_spike_r1.md S1-S3).
import sys
S = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\native_poc\\src\\'
T = S + 'host_standby_table.hpp'
MUTS = {
    # K1 (S1, r2 shape): a replay is recognised by device id + generation alone (no proof, no endpoint)
    'K1': [(T,
            '''      if (e.admitGen == gen && e.proofFp == proofFp && e.admitFrom == from && nowMs - e.admittedMs <= kStandbyHelloReplayMs) {''',
            '''      if (e.admitGen == gen && nowMs - e.admittedMs <= kStandbyHelloReplayMs) {  // MUTANT K1''')],
    # K2 (S1): the replay is classified before expired leases are swept
    'K2': [(T,
            '''                           uint64_t nowMs, StandbyEntry* out) {
    Expire(nowMs);
    for (const StandbyEntry& e : entries_) {''',
            '''                           uint64_t nowMs, StandbyEntry* out) {
    // MUTANT K2: no Expire
    for (const StandbyEntry& e : entries_) {''')],
    # K3 (S2, r2 shape): the generation is checked only against ANOTHER entry's activation, and goes down
    'K3': [(T,
            '''    if (gen == 0 || gen < e->activateHigh) return Verdict::Superseded;  // never a generation / stale''',
            '''    if (activeEntryId_ != entryId && gen != 0 && gen < e->activateHigh) return Verdict::Superseded;  // MUTANT K3''')],
    # K4 (S2): 0 accepted as a generation
    'K4': [(T,
            '''    if (gen == 0 || gen < e->activateHigh) return Verdict::Superseded;  // never a generation / stale''',
            '''    if (gen < e->activateHigh) return Verdict::Superseded;  // MUTANT K4''')],
    # K5 (S3, r2 shape): the session's real end does not end the grant
    'K5': [(T,
            '''    if (!grantLive_ || activeEpoch_ == 0 || epoch != activeEpoch_) return;''',
            '''    return;  // MUTANT K5''')],
    # K6 (S3, r2 shape): the CSPRNG's result is ignored
    'K6': [(T,
            '''    if (!Draw(&a) || !Draw(&b)) return AdmitResult::Failed;''',
            '''    Draw(&a); Draw(&b);  // MUTANT K6''')],
    # K7 (S3): a device holding the live session is re-admitted underneath it
    'K7': [(T,
            '''    if (sameDevice != entries_.size() && IsLive(entries_[sameDevice].entryId)) return AdmitResult::Live;''',
            '''    // MUTANT K7: no Live guard''')],
    # KS4 (S4, option (b)): a parked client's SESSION packets leave the relay through the active client's
    # host-facing socket (one shared host address); its Standby* datagrams keep their own leg, so admission,
    # pings and replies still work and only the host's address gate is left to tell them apart
    'KS4': [(S + 'host_standby_admission_e2e_test.cpp',
             '''        sendto(leg->up, reinterpret_cast<const char*>(buf), n, 0, reinterpret_cast<const sockaddr*>(&hostAddr),
               sizeof(hostAddr));''',
             '''        uint16_t kMut = 0;  // MUTANT KS4
        std::memcpy(&kMut, buf + 4, 2);
        const bool sbMut = kMut >= static_cast<uint16_t>(UdpPacketKind::StandbyHello) &&
                           kMut <= static_cast<uint16_t>(UdpPacketKind::StandbyRelease);
        sendto(sbMut || legs.size() < 2 ? leg->up : legs.front().up, reinterpret_cast<const char*>(buf), n, 0,
               reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));''')],
}
name, op = sys.argv[1], sys.argv[2]
for path, good, bad in MUTS[name]:
    s = open(path, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    a, b = (good, bad) if op == 'apply' else (bad, good)
    assert s.count(a) == 1, (name, op, path, s.count(a))
    s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'wb').write(s.encode('utf-8'))
print(name, op, 'ok')
