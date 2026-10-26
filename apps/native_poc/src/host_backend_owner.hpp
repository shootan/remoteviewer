#pragma once
// capture-lifecycle r2 (Codex N2): who owns a viewer's desktop-backend choice -- the logical control session
// (epoch) that made it -- decided in ONE place, from ONE epoch read per main-loop tick. Pure: no host state,
// so the boundary orders Codex listed are unit-tested as they are (host_backend_owner_test.cpp).
//
//   * The owner is the epoch the main loop read at the top of its tick. A new owner starts from the host's
//     own default (its startup request); the same epoch again (a resume, a re-sent Hello, a pause) keeps it.
//   * The owner's real end (its Serve() returned and no resume was pending -- host_control_session.cpp) puts
//     the choice back to the default too, once. A late end of an EARLIER owner changes nothing.
//   * A request is handled only under its own owner: stamped with an older epoch -> dropped; stamped with a
//     NEWER one (posted after this tick read the epoch) -> left where it is, for the tick that has made that
//     epoch the owner first. Its result is published under the request's own epoch, never the one read later.

#include <cstdint>

namespace remote60::native_poc {

struct BackendOwnerState {
  uint64_t ownerEpoch = 0;
  uint64_t endHandledEpoch = 0;  // the owner end already put back (an end is applied once)
};

struct BackendOwnerStep {
  bool ownerChanged = false;  // a new logical session: start from the host default
  bool ownerEnded = false;    // the owner's real end: back to the host default
};

// curEpoch: this tick's one read of SessionState::epoch. endedEpoch: SessionState::backendSessionEndedEpoch.
inline BackendOwnerStep StepBackendOwner(BackendOwnerState& s, uint64_t curEpoch, uint64_t endedEpoch) {
  BackendOwnerStep out;
  if (curEpoch != s.ownerEpoch) {
    s.ownerEpoch = curEpoch;
    out.ownerChanged = true;
  }
  if (endedEpoch != 0 && endedEpoch == s.ownerEpoch && endedEpoch != s.endHandledEpoch) {
    s.endHandledEpoch = endedEpoch;
    out.ownerEnded = true;
  }
  return out;
}

enum class BackendRequestFate : uint8_t { Handle, Drop, Leave };

// reqEpoch 0 = unstamped (older callers): handled under the current owner.
inline BackendRequestFate ClassifyBackendRequest(uint64_t reqEpoch, uint64_t ownerEpoch) {
  if (reqEpoch == 0 || ownerEpoch == 0 || reqEpoch == ownerEpoch) return BackendRequestFate::Handle;
  return reqEpoch < ownerEpoch ? BackendRequestFate::Drop : BackendRequestFate::Leave;
}

// The key a handled request is marked with (backendAppliedKey): its own epoch, never a later read.
inline uint64_t BackendRequestKey(uint64_t reqEpoch, uint64_t ownerEpoch, uint32_t seq) {
  return ((reqEpoch != 0 ? reqEpoch : ownerEpoch) << 32) | seq;
}

}  // namespace remote60::native_poc
