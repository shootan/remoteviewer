#pragma once

// keepalive-r2 A (spike/host-admission): the part of a Standby* datagram's handling that is the SAME before
// the first session (host_startup_connect.cpp's handshake loop) and during one (host_startup_control.cpp's
// reader): parse, authenticate a StandbyHello, keep/renew/release an entry, and decide an Activate. Neither
// caller's session state is touched here -- what an Activate DOES is the caller's (the first session's
// admission, or a takeover), because those two differ.

#include <winsock2.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

#include "host_standby_table.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

enum class StandbyAction {
  NotStandby,  // not a Standby* datagram: the caller goes on as before
  Silent,      // a Standby* datagram that gets no answer (refused capability, pre-login, a Release)
  Reply,       // send `reply` back to the peer
  Activate,    // an accepted activation: the caller admits the session (fresh) or re-sends its HelloAck
};

struct StandbyOutcome {
  StandbyAction action = StandbyAction::NotStandby;
  UdpStandbyPacket reply{};
  StandbyEntry entry;
  bool fresh = false;   // Activate: a new session (false = the same activation again)
  // Activate (r3): the StandbyPong that names this activation's result (entry + generation), sent right
  // before the HelloAck.
  UdpStandbyPacket activationPong{};
  std::string log;      // one line for the host log, or empty
};

// `authorize` is the capability check (directoryAgent.AuthorizePeer); asked only for a StandbyHello that is
// not a replay of its own admission. `digest` (r4 N2-a) replaces StandbyProofDigest -- tests only (a failure). `endedEpoch` (r3 S3): the control session that last really ended
// (SessionState::backendSessionEnded's epoch) -- the active grant ends with it before anything else is decided.
inline StandbyOutcome standby_handle(StandbyTable& table, const uint8_t* rx, size_t len, const sockaddr_in& peer,
                                     bool preloginMode,
                                     const std::function<bool(const std::string&, const sockaddr_in&)>& authorize,
                                     uint64_t nowMs, uint64_t endedEpoch = 0,
                                     const std::function<bool(const char*, size_t, StandbyProof*)>& digest = nullptr) {
  StandbyOutcome out;
  if (len != sizeof(UdpStandbyPacket)) return out;
  UdpStandbyPacket sb{};
  std::memcpy(&sb, rx, sizeof(sb));
  const uint16_t kind = sb.kind;
  if (sb.magic != kMagic || sb.size != sizeof(sb) || sb.version != kUdpProtocolVersion ||
      kind < static_cast<uint16_t>(UdpPacketKind::StandbyHello) || kind > static_cast<uint16_t>(UdpPacketKind::StandbyRelease)) {
    return out;
  }
  out.action = StandbyAction::Silent;
  // Before login there is one directory session and nothing to keep ready: silent, which a client reads as
  // "not supported" -- what an older host says.
  if (preloginMode) return out;
  const StandbyEndpoint from{peer.sin_addr.s_addr, peer.sin_port};
  if (endedEpoch != 0) table.SessionEnded(endedEpoch, nowMs);
  out.reply.size = static_cast<uint16_t>(sizeof(out.reply));

  if (kind == static_cast<uint16_t>(UdpPacketKind::StandbyHello)) {
    size_t tokenLen = 0;
    while (tokenLen < sizeof(sb.authToken) && sb.authToken[tokenLen] != '\0') ++tokenLen;
    if (tokenLen == 0) return out;  // standby is for directory-authenticated clients only
    StandbyEntry e;
    // r3 (Codex S1): the very admission again -- same capability (fingerprint), same return endpoint, valid
    // lease -- is answered as before without a capability (it was spent). Anything else is a NEW admission
    // and needs a fresh capability: a known device id + generation prove nothing, and a refused one gets no
    // secret and changes no entry.
    StandbyProof proof{};
    const bool digested = digest ? digest(sb.authToken, tokenLen, &proof) : StandbyProofDigest(sb.authToken, tokenLen, &proof);
    if (!digested) {
      // r4 N2-a: fail-closed -- no digest, no replay and no admission (the capability is not spent either).
      out.log = "[native-video-host][standby] hello refused (proof digest failed)";
      return out;
    }
    const bool replay = table.ClassifyHello(sb.deviceId, sb.clientGen, proof, from, nowMs, &e) ==
                        StandbyTable::HelloClass::Replay;
    StandbyTable::AdmitResult r = StandbyTable::AdmitResult::Admitted;
    if (!replay) {
      // The same capability check a Hello gets, and only that: not the session's token or authorization.
      if (!authorize(std::string(sb.authToken, tokenLen), peer)) {
        out.log = "[native-video-host][standby] hello refused (capability)";
        return out;
      }
      r = table.Admit(sb.deviceId, sb.clientGen, proof, sb.features, from, nowMs, &e);
    }
    out.action = StandbyAction::Reply;
    out.reply.kind = static_cast<uint16_t>(UdpPacketKind::StandbyAck);
    out.reply.clientGen = sb.clientGen;
    if (r == StandbyTable::AdmitResult::Stale) {
      // r4 N2-b: a late admission of this device, no newer than its entry: Superseded, no secret, entry unchanged.
      out.reply.status = kUdpStandbyStatusSuperseded;
    } else if (r != StandbyTable::AdmitResult::Admitted) {
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
                       : r == StandbyTable::AdmitResult::Stale ? "refused (stale: not newer than its entry)"
                                                                : "admitted";
    out.log = std::string("[native-video-host][standby] hello ") + what + " entries=" + std::to_string(table.size()) +
              " active=" + (table.activeEntryId() != 0 ? "1" : "0");
    return out;
  }
  if (kind == static_cast<uint16_t>(UdpPacketKind::StandbyPing)) {
    out.action = StandbyAction::Reply;
    out.reply.kind = static_cast<uint16_t>(UdpPacketKind::StandbyPong);
    out.reply.entryId = sb.entryId;
    out.reply.status = table.Ping(sb.entryId, sb.entrySecret, from, nowMs) == StandbyTable::Verdict::Ok
                           ? kUdpStandbyStatusOk
                           : kUdpStandbyStatusUnknown;
    out.reply.leaseMs = kStandbyLeaseMs;
    return out;
  }
  if (kind == static_cast<uint16_t>(UdpPacketKind::StandbyRelease)) {
    const auto v = table.Release(sb.entryId, sb.entrySecret);
    if (v == StandbyTable::Verdict::Ok) {
      out.log = "[native-video-host][standby] released entries=" + std::to_string(table.size());
    } else if (v == StandbyTable::Verdict::Live) {
      out.log = "[native-video-host][standby] release refused (the entry holds the session)";
    }
    return out;
  }
  // StandbyActivate (StandbyAck / StandbyPong arriving here are a peer's mistake: silent).
  if (kind != static_cast<uint16_t>(UdpPacketKind::StandbyActivate)) return out;
  const auto v = table.Activate(sb.entryId, sb.entrySecret, sb.clientGen, from, nowMs, &out.fresh, &out.entry);
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
  return out;
}

// The HelloAck an activated entry gets: what an accepted authenticated Hello with the entry's features gets.
inline UdpHelloPacket standby_hello_ack(uint32_t helloFeatures, bool bulkAvailable) {
  UdpHelloPacket ack{};
  ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
  ack.features = kUdpFeatureVideoFec | (helloFeatures & kUdpFeatureVideoFecInterleaved) | kUdpFeatureVideoNack |
                 kUdpFeatureControlResume | kUdpFeatureBandwidthObserve | kUdpFeatureDirectoryAuth;
  if ((helloFeatures & kUdpFeatureBulkChannel) != 0 && bulkAvailable) ack.features |= kUdpFeatureBulkChannel;
  return ack;
}

}  // namespace remote60::native_poc
