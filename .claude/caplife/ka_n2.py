import re
S = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'

def edit(name, pairs, regex=()):
    p = S + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    for pat, rep_ in regex:
        s, n = re.subn(pat, rep_, s)
        assert n > 0, (name, pat)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('host_standby_table.hpp', [
    ('''//   S3  The active grant ends with its session''', '''//   r4  (Codex codex_keepalive_r2_spike_r3.md N2) the replay proof is the capability's SHA-256 digest, compared in
//       full (no 64-bit non-cryptographic shortcut) -- a digest that cannot be computed is no replay and admits
//       nothing; and a device's admission no newer than its current entry's (gen <= admitGen, an unused older
//       capability arriving late, or the same gen with another proof) is Stale: that entry and its secret stay.
//       (A client keeps its admission generation monotonic across restarts -- the client side's contract.)
//   S3  The active grant ends with its session'''),
    ('''// A fingerprint of the capability an entry was admitted with (FNV-1a 64): identifies a retransmit of the same
// admission. Not a secret and not a credential -- the capability itself was consumed when it was checked.
inline uint64_t StandbyProofFingerprint(const char* token, size_t len) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint8_t>(token[i]);
    h *= 1099511628211ull;
  }
  return h == 0 ? 1 : h;
}''', '''// r4 (N2-a): the capability an entry was admitted with, as its SHA-256 digest -- what decides that a later Hello
// is a retransmit of that admission (and so needs no capability again). Compared whole. The token itself was
// consumed when it was checked and is not kept.
using StandbyProof = std::array<uint8_t, 32>;

inline bool StandbyProofDigest(const char* token, size_t len, StandbyProof* out) {
  if (!out || !token || len == 0) return false;
  out->fill(0);
#if defined(_WIN32)
  return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(token)),
                    static_cast<ULONG>(len), out->data(), static_cast<ULONG>(out->size())) == 0;
#else
  return false;
#endif
}'''),
    ('''  uint64_t proofFp = 0;        // r3 S1: the admitting capability's fingerprint''',
     '''  StandbyProof proof{};        // r3 S1 / r4 N2-a: the admitting capability's SHA-256'''),
    ('''  enum class AdmitResult { Admitted, Busy, Live, Failed };''',
     '''  enum class AdmitResult { Admitted, Busy, Live, Failed, Stale };'''),
    ('''  HelloClass ClassifyHello(const uint8_t deviceId[16], uint32_t gen, uint64_t proofFp, StandbyEndpoint from,''',
     '''  HelloClass ClassifyHello(const uint8_t deviceId[16], uint32_t gen, const StandbyProof& proof, StandbyEndpoint from,'''),
    ('''      if (e.admitGen == gen && e.proofFp == proofFp && e.admitFrom == from && nowMs - e.admittedMs <= kStandbyHelloReplayMs) {''',
     '''      if (e.admitGen == gen && e.proof == proof && e.admitFrom == from && nowMs - e.admittedMs <= kStandbyHelloReplayMs) {'''),
    ('''  // An admission whose capability the caller has just checked. One entry per device: a new admission
  // replaces that device's old entry -- unless that entry holds the live session (Live: the device is the
  // session; nothing changes). A full table is Busy; a failed random draw is Failed (no entry at all).
  AdmitResult Admit(const uint8_t deviceId[16], uint32_t gen, uint64_t proofFp, uint32_t helloFeatures,''',
     '''  // An admission whose capability the caller has just checked. One entry per device: a NEWER admission
  // replaces that device's old entry -- unless that entry holds the live session (Live: the device is the
  // session; nothing changes). r4 N2-b: one no newer than the entry there (gen <= its admitGen) is Stale and
  // changes nothing. A full table is Busy; a failed random draw is Failed (no entry at all).
  AdmitResult Admit(const uint8_t deviceId[16], uint32_t gen, const StandbyProof& proof, uint32_t helloFeatures,'''),
    ('''    if (sameDevice != entries_.size() && IsLive(entries_[sameDevice].entryId)) return AdmitResult::Live;''',
     '''    if (sameDevice != entries_.size() && IsLive(entries_[sameDevice].entryId)) return AdmitResult::Live;
    if (sameDevice != entries_.size() && gen <= entries_[sameDevice].admitGen) return AdmitResult::Stale;'''),
    ('''    e.proofFp = proofFp;''', '''    e.proof = proof;'''),
])

edit('host_standby_wire.hpp', [
    ('''// `authorize` is the capability check (directoryAgent.AuthorizePeer); asked only for a StandbyHello that is
// not a replay of its own admission.''', '''// `authorize` is the capability check (directoryAgent.AuthorizePeer); asked only for a StandbyHello that is
// not a replay of its own admission. `digest` (r4 N2-a) replaces StandbyProofDigest -- tests only (a failure).'''),
    ('''                                     uint64_t nowMs, uint64_t endedEpoch = 0) {''',
     '''                                     uint64_t nowMs, uint64_t endedEpoch = 0,
                                     const std::function<bool(const char*, size_t, StandbyProof*)>& digest = nullptr) {'''),
    ('''    const uint64_t proofFp = StandbyProofFingerprint(sb.authToken, tokenLen);
    const bool replay = table.ClassifyHello(sb.deviceId, sb.clientGen, proofFp, from, nowMs, &e) ==
                        StandbyTable::HelloClass::Replay;''', '''    StandbyProof proof{};
    const bool digested = digest ? digest(sb.authToken, tokenLen, &proof) : StandbyProofDigest(sb.authToken, tokenLen, &proof);
    if (!digested) {
      // r4 N2-a: fail-closed -- no digest, no replay and no admission (the capability is not spent either).
      out.log = "[native-video-host][standby] hello refused (proof digest failed)";
      return out;
    }
    const bool replay = table.ClassifyHello(sb.deviceId, sb.clientGen, proof, from, nowMs, &e) ==
                        StandbyTable::HelloClass::Replay;'''),
    ('''      r = table.Admit(sb.deviceId, sb.clientGen, proofFp, sb.features, from, nowMs, &e);''',
     '''      r = table.Admit(sb.deviceId, sb.clientGen, proof, sb.features, from, nowMs, &e);'''),
    ('''    if (r != StandbyTable::AdmitResult::Admitted) {''', '''    if (r == StandbyTable::AdmitResult::Stale) {
      // r4 N2-b: a late admission of this device, no newer than its entry: Superseded, no secret, entry unchanged.
      out.reply.status = kUdpStandbyStatusSuperseded;
    } else if (r != StandbyTable::AdmitResult::Admitted) {'''),
    ('''                       : r == StandbyTable::AdmitResult::Failed ? "refused (random draw failed)"''',
     '''                       : r == StandbyTable::AdmitResult::Failed ? "refused (random draw failed)"
                       : r == StandbyTable::AdmitResult::Stale ? "refused (stale: not newer than its entry)"'''),
])

edit('host_standby_table_test.cpp', [
    ('''constexpr uint64_t kFp1 = 0x1111, kFp2 = 0x2222;''', '''// r4: the replay proof is a 32-byte digest; tests use distinct fixed values.
StandbyProof Proof(int b) {
  StandbyProof p{};
  p.fill(static_cast<uint8_t>(b));
  p[0] = static_cast<uint8_t>(b >> 8);
  return p;
}
const StandbyProof kFp1 = Proof(0x11), kFp2 = Proof(0x22);'''),
], regex=[
    (r'\.Admit\((Dev\(\w+\)\.id), (\w+), (0x[0-9a-fA-F]+|\d+|d), ', r'.Admit(\1, \2, Proof(\3), '),
])
print('ok')
