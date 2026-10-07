#pragma once

// Ctrl+Alt+Del at the remote logon screen (prelogin-ui r9, R5): the two control messages and the
// service's decision. Numbers fixed by the verifier (2026-10-08): 83 ControlPreloginControl and 82
// ControlPreloginStatus, reserved until now; the capability bit kCaptureFlagPreloginSasV1 (0x20000).
//
// Ctrl+Alt+Del is the Secure Attention Sequence: Windows takes it from the keyboard driver only -- a
// SendInput of the three keys is ignored by design (the pre-login input filter drops it too). The one
// software way is SendSAS (sas.dll), from a service, into the target session, and only if the local
// policy SoftwareSASGeneration lets services do it (1 = services, 3 = services and Ease of Access).
// The plan (prelogin_service_2026-10-07 section 4): the service reads that policy and never changes
// it; a policy that does not allow it is reported as such ("PolicyDenied"), not worked around.
// Verifier's decision (r9, A2A 3856): an UNSET policy is not taken as a refusal either -- this PC has it
// unset (A0 measurement) -- SendSAS is tried and the answer is "SentUnconfirmed". SendSAS returns
// nothing, so "Sent" means the call was made -- whether the logon screen reacted is seen on the screen
// (the field check, G). DisableCAD (the logon does not need Ctrl+Alt+Del) is read and said too.
//
// Path: viewer -> ControlPreloginControl{op = Sas} (allowed before login, directory-authenticated link
// only) -> the --prelogin worker -> its admin pipe AdminOp::Sas -> the service: the caller's session must
// be the console's and nobody logged on in it -> policy -> SendSAS(FALSE) impersonating that session.

#include <cstdint>
#include <cstring>
#include <vector>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

constexpr uint16_t kPreloginControlOpSas = 1;

struct ControlPreloginControlMessage {  // 83, viewer -> host
  MessageHeader header{};
  uint32_t requestId = 0;  // never 0; echoed by 82
  uint16_t op = 0;         // kPreloginControlOp*
  uint16_t reserved = 0;
};
static_assert(sizeof(ControlPreloginControlMessage) == 16, "prelogin control wire drift");

enum class PreloginSasResult : uint8_t {
  Sent = 0,           // SendSAS was called for the console session (void: what the screen did is seen there)
  PolicyDenied = 1,   // SoftwareSASGeneration does not let services generate it (win32 = the value, or 0xFFFFFFFF unset)
  NotAtLogon = 2,     // not a pre-login host / a user is logged on / not the console session
  NotAuthorized = 3,  // the link is not the directory-authenticated session
  Failed = 4,         // the service could not be asked, or the call failed (win32)
  SentUnconfirmed = 5,  // the policy is not set: SendSAS was called, its effect is not known (see the screen)
};
constexpr uint8_t kPreloginStatusFlagCadNotRequired = 0x01;  // ControlPreloginStatusMessage::flags: DisableCAD = 1

struct ControlPreloginStatusMessage {  // 82, host -> viewer
  MessageHeader header{};
  uint32_t requestId = 0;
  uint16_t op = 0;
  uint8_t result = 0;  // PreloginSasResult for op Sas
  uint8_t flags = 0;   // kPreloginStatusFlag*
  uint32_t win32 = 0;
  uint32_t reserved1 = 0;
};
static_assert(sizeof(ControlPreloginStatusMessage) == 24, "prelogin status wire drift");

inline ControlPreloginControlMessage make_sas_request(uint32_t requestId) {
  ControlPreloginControlMessage m{};
  m.header.magic = kMagic;
  m.header.type = static_cast<uint16_t>(MessageType::ControlPreloginControl);
  m.header.size = static_cast<uint16_t>(sizeof(m));
  m.requestId = requestId;
  m.op = kPreloginControlOpSas;
  return m;
}

inline ControlPreloginStatusMessage make_prelogin_status(uint32_t requestId, uint16_t op, PreloginSasResult result,
                                                         uint32_t win32, uint8_t flags = 0) {
  ControlPreloginStatusMessage m{};
  m.header.magic = kMagic;
  m.header.type = static_cast<uint16_t>(MessageType::ControlPreloginStatus);
  m.header.size = static_cast<uint16_t>(sizeof(m));
  m.requestId = requestId;
  m.op = op;
  m.result = static_cast<uint8_t>(result);
  m.win32 = win32;
  m.flags = flags;
  return m;
}

}  // namespace remote60::native_poc
