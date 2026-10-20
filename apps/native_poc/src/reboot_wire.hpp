#pragma once

// Remote reboot (prelogin-service r2, user decision 2026-10-07): the two fixed control messages.
//
// Role:    ControlRebootRequest (84, viewer -> host) asks the host to restart; ControlRebootReply
//          (85, host -> viewer) answers with the OUTCOME, not a receipt (Accepted = the OS accepted the
//          restart request, NOT reboot complete -- Codex r4 R3). The host performs the actual restart
//          from its SYSTEM service; the baseline is a short grace + non-forced call
//          (InitiateSystemShutdownExW(NULL, msg, 5, FALSE, TRUE, planned-reason); InitiateShutdownW is
//          the alternative compared in the service unit), and a failure is NEVER auto-escalated to a
//          forced restart. The viewer only shows "restarting" and must not treat the ensuing disconnect
//          as an error. One confirm popup -> one requestId; the service dedups so a retransmit causes no
//          second OS call, and the viewer never auto-generates a fresh request on a lost ACK. Gated on
//          kCaptureFlagRemoteRebootV1
//          so an old host (which drains an unknown opcode without replying) is never sent one -- its
//          menu item stays disabled with a reason.
// Auth:    the host accepts this ONLY on a directory-authenticated session; a token-less LAN Hello
//          peer is answered NotAuthorized (or dropped per the session gate). It works in prelogin too.
// Thread:  none; pure codec.
//
// Layout: both messages are fixed-size (no trailing payload). Fields are ordered so neither struct has
// implicit padding; sizes are pinned below (MessageHeader is 8 bytes).

#include <cstdint>
#include <cstring>
#include <vector>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

struct ControlRebootRequestMessage {
  MessageHeader header{};   // size = sizeof(*this); no payload follows
  uint32_t requestId = 0;   // random, never 0; echoed by 85
  uint32_t reserved = 0;
};
static_assert(sizeof(ControlRebootRequestMessage) == 16, "reboot request wire drift");

enum class RebootResult : uint8_t {
  Accepted = 0,       // the OS ACCEPTED the restart request -- NOT "reboot complete" (Codex r4 R3)
  Refused = 1,        // declined (feature off / host policy / a restart is already in flight)
  NotAuthorized = 2,  // the session is not directory-authenticated
  Failed = 3,         // the shutdown call failed; win32 says why
};
constexpr uint8_t kRebootResultMax = 3;

struct ControlRebootReplyMessage {
  MessageHeader header{};
  uint32_t requestId = 0;  // echo of 84
  uint8_t result = 0;      // RebootResult
  uint8_t reserved0 = 0;
  uint16_t reserved1 = 0;
  uint32_t win32 = 0;      // GetLastError from InitiateShutdownW when result == Failed, else 0
  uint32_t reserved2 = 0;
};
static_assert(sizeof(ControlRebootReplyMessage) == 24, "reboot reply wire drift");

inline std::vector<uint8_t> build_reboot_request(uint32_t requestId) {
  ControlRebootRequestMessage msg{};
  msg.header.magic = kMagic;
  msg.header.type = static_cast<uint16_t>(MessageType::ControlRebootRequest);
  msg.header.size = static_cast<uint16_t>(sizeof(msg));
  msg.requestId = requestId;
  std::vector<uint8_t> out(sizeof(msg));
  std::memcpy(out.data(), &msg, sizeof(msg));
  return out;
}

inline ControlRebootReplyMessage make_reboot_reply(uint32_t requestId, RebootResult result, uint32_t win32) {
  ControlRebootReplyMessage msg{};
  msg.header.magic = kMagic;
  msg.header.type = static_cast<uint16_t>(MessageType::ControlRebootReply);
  msg.header.size = static_cast<uint16_t>(sizeof(msg));
  msg.requestId = requestId;
  msg.result = static_cast<uint8_t>(result);
  msg.win32 = (result == RebootResult::Failed) ? win32 : 0u;
  return msg;
}

// The host's check of a request's fixed part. A 0 requestId is malformed (never echoable). The
// directory-auth decision is the caller's, not this codec's.
inline bool reboot_request_valid(const ControlRebootRequestMessage& m) {
  return m.header.magic == kMagic && m.header.type == static_cast<uint16_t>(MessageType::ControlRebootRequest) &&
         m.header.size == sizeof(ControlRebootRequestMessage) && m.requestId != 0;
}

// The viewer's check of an answer: type, exact size, echoed id, enum in range. Anything else is a
// desynchronised link, not a "failed" reboot.
inline bool reboot_reply_valid(const ControlRebootReplyMessage& m, uint32_t requestId) {
  return m.header.magic == kMagic && m.header.type == static_cast<uint16_t>(MessageType::ControlRebootReply) &&
         m.header.size == sizeof(ControlRebootReplyMessage) && m.requestId == requestId && m.result <= kRebootResultMax;
}

}  // namespace remote60::native_poc
