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
  std::string log;      // one line for the host log, or empty
};

// `authorize` is the capability check (directoryAgent.AuthorizePeer); asked only for a StandbyHello that is
// not a replay.
inline StandbyOutcome standby_handle(StandbyTable& table, const uint8_t* rx, size_t len, const sockaddr_in& peer,
                                     bool preloginMode,
                                     const std::function<bool(const std::string&, const sockaddr_in&)>& authorize,
                                     uint64_t nowMs) {
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
  out.reply.size = static_cast<uint16_t>(sizeof(out.reply));

  if (kind == static_cast<uint16_t>(UdpPacketKind::StandbyHello)) {
    size_t tokenLen = 0;
    while (tokenLen < sizeof(sb.authToken) && sb.authToken[tokenLen] != '\0') ++tokenLen;
    if (tokenLen == 0) return out;  // standby is for directory-authenticated clients only
    StandbyEntry e;
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
    if (table.Release(sb.entryId, sb.entrySecret) == StandbyTable::Verdict::Ok) {
      out.log = "[native-video-host][standby] released entries=" + std::to_string(table.size());
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
    out.reply.status = v == StandbyTable::Verdict::Superseded ? kUdpStandbyStatusSuperseded : kUdpStandbyStatusUnknown;
    out.log = std::string("[native-video-host][standby] activate refused (") +
              (v == StandbyTable::Verdict::Superseded ? "superseded" : "unknown") + ")";
    return out;
  }
  out.action = StandbyAction::Activate;
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
