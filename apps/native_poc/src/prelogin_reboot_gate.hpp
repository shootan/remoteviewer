#pragma once

// Remote reboot host-side authorization + dedup gate (prelogin-service r3, Codex r4 R2/R3).
//
// The pure decision between receiving a ControlRebootRequest and making the ONE OS restart call:
//   - R2: only a directory-authenticated link may reboot (a token-less LAN Hello peer is NotAuthorized);
//     the client cannot name a machine/flags/text -- the gate only ever says "initiate the fixed local
//     restart", the caller supplies the constant parameters.
//   - R3: exactly one restart. The first valid request is accepted and marked in flight; a RETRANSMIT of
//     the same requestId is answered Accepted again WITHOUT a second OS call (idempotent dedup); a
//     DIFFERENT request while one is in flight is refused. Accepted means "the OS accepted the request",
//     never "reboot complete".
//
// Pure and portable; the actual InitiateSystemShutdownExW call is the caller's seam.

#include <cstdint>

#include "reboot_wire.hpp"  // RebootResult

namespace remote60::native_poc {

struct RebootGateState {
  bool inFlight = false;          // a restart has already been accepted/initiated
  uint32_t acceptedRequestId = 0; // the requestId that was accepted (idempotent dedup key)
};

struct RebootRequestInputs {
  bool directoryAuthenticated = false;  // the link is directory-authenticated (token-less LAN -> false)
  bool featureAllowsReboot = false;     // remote reboot enabled + owner-permitted on this host
  uint32_t requestId = 0;               // from ControlRebootRequest (non-zero; validated upstream)
};

enum class RebootDecision {
  NotAuthorized,      // not directory-authenticated -> no OS call
  Refused,            // feature not allowed, or a different request is already in flight
  AcceptAndInitiate,  // first valid request -> make the ONE fixed OS restart call, mark in flight
  DuplicateAccepted,  // same requestId retransmit while in flight -> answer Accepted, NO second OS call
};

// Evaluation order: authorization first (so an unauthorized retransmit never dedups into an Accept),
// then feature, then the single-flight dedup.
inline RebootDecision DecideReboot(RebootGateState& s, const RebootRequestInputs& in) {
  if (!in.directoryAuthenticated) return RebootDecision::NotAuthorized;
  if (!in.featureAllowsReboot) return RebootDecision::Refused;
  if (s.inFlight) {
    return (in.requestId == s.acceptedRequestId) ? RebootDecision::DuplicateAccepted : RebootDecision::Refused;
  }
  s.inFlight = true;
  s.acceptedRequestId = in.requestId;
  return RebootDecision::AcceptAndInitiate;
}

// Map the gate decision to the wire result the host sends in ControlRebootReply.
inline RebootResult RebootResultFor(RebootDecision d) {
  switch (d) {
    case RebootDecision::AcceptAndInitiate:
    case RebootDecision::DuplicateAccepted:
      return RebootResult::Accepted;  // the OS accepted the request (NOT reboot complete)
    case RebootDecision::NotAuthorized:
      return RebootResult::NotAuthorized;
    case RebootDecision::Refused:
      return RebootResult::Refused;
  }
  return RebootResult::Refused;
}

// Does this decision require the caller to actually make the OS restart call? Only the first accept.
inline bool RebootShouldCallOs(RebootDecision d) { return d == RebootDecision::AcceptAndInitiate; }

}  // namespace remote60::native_poc
