src = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = src + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

# table: CRLF like its neighbours
p = src + 'host_standby_table.hpp'
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n').replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

edit('poc_protocol.hpp', [
    ('''constexpr uint32_t kUdpStandbyStatusSuperseded = 3;  // Activate: an old generation (a late packet), not a new activation''',
     '''constexpr uint32_t kUdpStandbyStatusSuperseded = 3;  // Activate: an old generation (a late packet), not a new activation
// keepalive-r2 A r3 (Codex decision 2): an activation's RESULT names its operation. The host answers an
// accepted StandbyActivate with a StandbyPong {entryId, clientGen = the activation's generation, status Ok,
// features |= this flag} right before the HelloAck; a refused one with a StandbyPong {entryId, clientGen,
// status Unknown/Superseded}. A client takes an activation as done only on the Pong of ITS entry and ITS
// generation -- a late Ack of an earlier activation, or of a cancelled one, carries another generation.
constexpr uint32_t kUdpStandbyPongActivationGranted = 0x1;'''),
])

edit('host_standby_wire.hpp', [
    ('''struct StandbyOutcome {
  StandbyAction action = StandbyAction::NotStandby;
  UdpStandbyPacket reply{};
  StandbyEntry entry;
  bool fresh = false;   // Activate: a new session (false = the same activation again)
  std::string log;      // one line for the host log, or empty
};''',
     '''struct StandbyOutcome {
  StandbyAction action = StandbyAction::NotStandby;
  UdpStandbyPacket reply{};
  StandbyEntry entry;
  bool fresh = false;   // Activate: a new session (false = the same activation again)
  // Activate (r3): the StandbyPong that names this activation's result (entry + generation), sent right
  // before the HelloAck.
  UdpStandbyPacket activationPong{};
  std::string log;      // one line for the host log, or empty
};'''),
    ('''// `authorize` is the capability check (directoryAgent.AuthorizePeer); asked only for a StandbyHello that is
// not a replay.
inline StandbyOutcome standby_handle(StandbyTable& table, const uint8_t* rx, size_t len, const sockaddr_in& peer,
                                     bool preloginMode,
                                     const std::function<bool(const std::string&, const sockaddr_in&)>& authorize,
                                     uint64_t nowMs) {
  StandbyOutcome out;
  if (len != sizeof(UdpStandbyPacket)) return out;''',
     '''// `authorize` is the capability check (directoryAgent.AuthorizePeer); asked only for a StandbyHello that is
// not a replay of its own admission. `endedEpoch` (r3 S3): the control session that last really ended
// (SessionState::backendSessionEnded's epoch) -- the active grant ends with it before anything else is decided.
inline StandbyOutcome standby_handle(StandbyTable& table, const uint8_t* rx, size_t len, const sockaddr_in& peer,
                                     bool preloginMode,
                                     const std::function<bool(const std::string&, const sockaddr_in&)>& authorize,
                                     uint64_t nowMs, uint64_t endedEpoch = 0) {
  StandbyOutcome out;
  if (len != sizeof(UdpStandbyPacket)) return out;'''),
    ('''  const StandbyEndpoint from{peer.sin_addr.s_addr, peer.sin_port};
  out.reply.size = static_cast<uint16_t>(sizeof(out.reply));''',
     '''  const StandbyEndpoint from{peer.sin_addr.s_addr, peer.sin_port};
  if (endedEpoch != 0) table.SessionEnded(endedEpoch, nowMs);
  out.reply.size = static_cast<uint16_t>(sizeof(out.reply));'''),
    ('''    StandbyEntry e;
    // The same capability check a Hello gets, and only that: not the session's token or authorization.
    if (!table.IsReplay(sb.deviceId, sb.clientGen, nowMs, &e) && !authorize(std::string(sb.authToken, tokenLen), peer)) {
      out.log = "[native-video-host][standby] hello refused (capability)";
      return out;
    }
    const auto r = table.Admit(sb.deviceId, sb.clientGen, sb.features, from, nowMs, &e);
    out.action = StandbyAction::Reply;
    out.reply.kind = static_cast<uint16_t>(UdpPacketKind::StandbyAck);
    out.reply.clientGen = sb.clientGen;
    if (r == StandbyTable::AdmitResult::Busy) {
      out.reply.status = kUdpStandbyStatusBusy;
    } else {
      out.reply.status = kUdpStandbyStatusOk;
      out.reply.entryId = e.entryId;
      std::memcpy(out.reply.entrySecret, e.secret.data(), 16);
      out.reply.leaseMs = kStandbyLeaseMs;
    }
    out.log = std::string("[native-video-host][standby] hello ") +
              (r == StandbyTable::AdmitResult::Busy ? "busy" : (r == StandbyTable::AdmitResult::Replayed ? "replayed" : "admitted")) +
              " entries=" + std::to_string(table.size()) + " active=" + (table.activeEntryId() != 0 ? "1" : "0");
    return out;''',
     '''    StandbyEntry e;
    // r3 (Codex S1): the very admission again -- same capability (fingerprint), same return endpoint, valid
    // lease -- is answered as before without a capability (it was spent). Anything else is a NEW admission
    // and needs a fresh capability: a known device id + generation prove nothing, and a refused one gets no
    // secret and changes no entry.
    const uint64_t proofFp = StandbyProofFingerprint(sb.authToken, tokenLen);
    const bool replay = table.ClassifyHello(sb.deviceId, sb.clientGen, proofFp, from, nowMs, &e) ==
                        StandbyTable::HelloClass::Replay;
    StandbyTable::AdmitResult r = StandbyTable::AdmitResult::Admitted;
    if (!replay) {
      // The same capability check a Hello gets, and only that: not the session's token or authorization.
      if (!authorize(std::string(sb.authToken, tokenLen), peer)) {
        out.log = "[native-video-host][standby] hello refused (capability)";
        return out;
      }
      r = table.Admit(sb.deviceId, sb.clientGen, proofFp, sb.features, from, nowMs, &e);
    }
    out.action = StandbyAction::Reply;
    out.reply.kind = static_cast<uint16_t>(UdpPacketKind::StandbyAck);
    out.reply.clientGen = sb.clientGen;
    if (r != StandbyTable::AdmitResult::Admitted) {
      // Busy: the table is full / Live: this device holds the session / Failed: no random draw -- all Busy on
      // the wire (no secret).
      out.reply.status = kUdpStandbyStatusBusy;
    } else {
      out.reply.status = kUdpStandbyStatusOk;
      out.reply.entryId = e.entryId;
      std::memcpy(out.reply.entrySecret, e.secret.data(), 16);
      out.reply.leaseMs = kStandbyLeaseMs;
    }
    const char* what = replay ? "replayed"
                       : r == StandbyTable::AdmitResult::Busy ? "busy"
                       : r == StandbyTable::AdmitResult::Live ? "refused (device holds the session)"
                       : r == StandbyTable::AdmitResult::Failed ? "refused (random draw failed)"
                                                                : "admitted";
    out.log = std::string("[native-video-host][standby] hello ") + what + " entries=" + std::to_string(table.size()) +
              " active=" + (table.activeEntryId() != 0 ? "1" : "0");
    return out;'''),
    ('''  if (kind == static_cast<uint16_t>(UdpPacketKind::StandbyRelease)) {
    if (table.Release(sb.entryId, sb.entrySecret) == StandbyTable::Verdict::Ok) {
      out.log = "[native-video-host][standby] released entries=" + std::to_string(table.size());
    }
    return out;
  }''',
     '''  if (kind == static_cast<uint16_t>(UdpPacketKind::StandbyRelease)) {
    const auto v = table.Release(sb.entryId, sb.entrySecret);
    if (v == StandbyTable::Verdict::Ok) {
      out.log = "[native-video-host][standby] released entries=" + std::to_string(table.size());
    } else if (v == StandbyTable::Verdict::Live) {
      out.log = "[native-video-host][standby] release refused (the entry holds the session)";
    }
    return out;
  }'''),
    ('''  const auto v = table.Activate(sb.entryId, sb.entrySecret, sb.clientGen, from, nowMs, &out.fresh, &out.entry);
  if (v != StandbyTable::Verdict::Ok) {
    out.action = StandbyAction::Reply;
    out.reply.kind = static_cast<uint16_t>(UdpPacketKind::StandbyPong);
    out.reply.entryId = sb.entryId;
    out.reply.status = v == StandbyTable::Verdict::Superseded ? kUdpStandbyStatusSuperseded : kUdpStandbyStatusUnknown;
    out.log = std::string("[native-video-host][standby] activate refused (") +
              (v == StandbyTable::Verdict::Superseded ? "superseded" : "unknown") + ")";
    return out;
  }
  out.action = StandbyAction::Activate;
  return out;''',
     '''  const auto v = table.Activate(sb.entryId, sb.entrySecret, sb.clientGen, from, nowMs, &out.fresh, &out.entry);
  if (v != StandbyTable::Verdict::Ok) {
    out.action = StandbyAction::Reply;
    out.reply.kind = static_cast<uint16_t>(UdpPacketKind::StandbyPong);
    out.reply.entryId = sb.entryId;
    out.reply.clientGen = sb.clientGen;  // r3: the refusal names the activation it refuses
    out.reply.status = v == StandbyTable::Verdict::Superseded ? kUdpStandbyStatusSuperseded : kUdpStandbyStatusUnknown;
    out.log = std::string("[native-video-host][standby] activate refused (") +
              (v == StandbyTable::Verdict::Superseded ? "superseded" : "unknown") + ") gen=" + std::to_string(sb.clientGen);
    return out;
  }
  out.action = StandbyAction::Activate;
  out.activationPong.size = static_cast<uint16_t>(sizeof(out.activationPong));
  out.activationPong.kind = static_cast<uint16_t>(UdpPacketKind::StandbyPong);
  out.activationPong.entryId = sb.entryId;
  out.activationPong.clientGen = sb.clientGen;
  out.activationPong.status = kUdpStandbyStatusOk;
  out.activationPong.features = kUdpStandbyPongActivationGranted;
  out.activationPong.leaseMs = kStandbyLeaseMs;
  return out;'''),
])
print('ok')
