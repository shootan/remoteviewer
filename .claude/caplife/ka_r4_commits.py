import json
R = 'C:/work/gnlink-wt/ka-r3'
D = 'D:/remote/remote/.claude/caplife/'
N = 'apps/native_poc/src/'
T = 'apps/directory/test/'
commits = [
    ('kar4_h', [N + 'host_standby_table.hpp', N + 'host_standby_wire.hpp', N + 'directory_client.hpp', N + 'directory_client.cpp'],
     """fix(host, keepalive-r2 A r4): the replay proof is a SHA-256, a late admission changes nothing, a relay socket gets the host's proof

Codex codex_keepalive_r2_spike_r3.md N1 (host half), N2.
- N2-a: StandbyHello's replay proof is the capability's SHA-256 (BCryptHash), compared whole -- not a 64-bit FNV.
  A digest that cannot be computed is no replay and admits nothing (fail-closed; the capability is not spent).
- N2-b: a device's admission no newer than its entry (gen <= admitGen: an unused older capability arriving late,
  or the same gen with another proof) is Stale -- answered Superseded, no secret, the entry and its secret stay. A
  newer operation replaces it as before. (Clients keep the admission generation monotonic across restarts.)
- N1 (host half): a pendingPunch entry may carry hostProof (a relay lease's own host-facing socket). The host's
  punch toward that target carries the proof in its token field; every other punch stays empty (a client's token
  is never put on the wire). Such a target is not a peer: it is never authorised as a capability.

Product / test / docs: product (standby table and wire, directory agent punch) / test (none in this commit) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""),
    ('kar4_s', ['apps/directory/server.js'],
     """fix(directory, keepalive-r2 (a) r4): a standby lease pins only the host's proof; the host's eight hold either way round; bounds are integers

Codex codex_keepalive_r2_spike_r3.md N1, N3. Local only (NAS unchanged).
- N1: each reservation has a host-only proof (handed to the host alone, as its pendingPunch entry's hostProof). An
  unpinned host-facing socket pins ONLY a 49 B Punch from the host's address carrying that lease's current proof;
  no proof, a wrong one, another lease's, or any other datagram changes nothing. A moved host gets a FRESH proof per
  lease with that heartbeat (the old one, replayed, pins nothing) and nothing is pinned until its punch arrives.
- N3: a NEW legacy lease is refused beside eight standby leases -- before anything is dropped; replacing the legacy
  lease keeps eight. The standby bounds from the environment must be positive integers, else the default is used
  and said (Math.max(1, NaN) made the bound vanish).

Product / test / docs: product (directory relay) / test (none in this commit) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""),
    ('kar4_t', [N + 'host_standby_table_test.cpp', T + 'relay_standby_test.js', T + 'relay_standby_r4_test.js',
                T + 'relay_env_bound_test.js', T + 'run.js'],
     """test(keepalive-r2 A r4): N1 first-pin race, N2 late admission and digest, N3 eight either way round, env bounds

- host_standby_table_test 56/56: SHA-256 digest equality; gen 11 then the late unused gen 10 / same gen another
  proof -> Stale, secret unchanged; the gen-11 retransmit still a replay; a proof differing only in its last byte is
  no replay; gen 12 replaces; wire: Superseded with no secret; a failed digest spends no capability.
- relay_standby_r4_test.js (18): a stranger on the host IP FIRST (no proof / wrong / fake StandbyAck) pins nothing;
  the host's proof pins; another lease's proof pins nothing; a moved host gets fresh proofs, the old one replayed
  pins nothing, the new one does; eight standby -> a legacy Hello refused and all eight still carry pings; legacy +
  seven: an eighth standby host-full, the legacy replaced, still eight.
- relay_env_bound_test.js (4): abc / 2.5 / -3 fall back with a warning. run.js runs both (suite 813 PASS rc0);
  relay_standby_test's fake host punches with the proof.
Mutants: N1a (IP-only first pin) 5 FAIL, N1b (no reproof on a move) 2 FAIL, N3a (legacy uncounted) 1 FAIL, N3b (r3
env parse) 3 FAIL, N2a (8-byte compare) 1 FAIL, N2b (no Stale) 5 FAIL, N2c (digest failure ignored) 1 FAIL.

Product / test / docs: product (none) / test (table, relay r4, env bound, run.js) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""),
]
for key, files, msg in commits:
    json.dump({'repo_path': R, 'files': files}, open(D + key + '_add.json', 'w', encoding='utf-8'))
    json.dump({'repo_path': R, 'message': msg}, open(D + key + '_commit.json', 'w', encoding='utf-8'), ensure_ascii=False)
print('ok')
