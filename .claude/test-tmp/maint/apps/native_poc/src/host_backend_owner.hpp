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
//   * r3 (Codex N2 end): a request of an ENDED owner is dropped -- one still queued when its Serve() ended, or
//     one posted late by that Serve() after the end was seen. "Ended" is a Serve(), not an epoch number: each
//     Serve() has its own sequence (SessionState::backendServeSeq), the end carries the one that ended, and a
//     LATER Serve() of the same epoch (a control resume after the end) is live again -- its requests carry
//     its own, newer sequence.

#include <cstdint>

namespace remote60::native_poc {

// The end as SessionState::backendSessionEnded packs it: epoch << 24 | (Serve() sequence & 0xffffff).
constexpr uint32_t kBackendServeSeqBits = 24;
constexpr uint64_t kBackendServeSeqMask = (1ull << kBackendServeSeqBits) - 1;
inline uint64_t PackBackendSessionEnd(uint64_t epoch, uint64_t serveSeq) {
  return (epoch << kBackendServeSeqBits) | (serveSeq & kBackendServeSeqMask);
}

struct BackendOwnerState {
  uint64_t ownerEpoch = 0;
  uint64_t endHandled = 0;  // the packed end already applied (an end is applied once)
  uint64_t endedServe = 0;  // the owner's Serve() sequence that ended (0 = live); requests up to it are dead
};

struct BackendOwnerStep {
  bool ownerChanged = false;  // a new logical session: start from the host default
  bool ownerEnded = false;    // the owner's real end: back to the host default
};

// curEpoch: this tick's one read of SessionState::epoch. endedPacked: SessionState::backendSessionEnded.
inline BackendOwnerStep StepBackendOwner(BackendOwnerState& s, uint64_t curEpoch, uint64_t endedPacked) {
  BackendOwnerStep out;
  if (curEpoch != s.ownerEpoch) {
    s.ownerEpoch = curEpoch;
    s.endedServe = 0;
    out.ownerChanged = true;
  }
  const uint64_t endedEpoch = endedPacked >> kBackendServeSeqBits;
  if (endedPacked != 0 && endedEpoch == s.ownerEpoch && endedPacked != s.endHandled) {
    s.endHandled = endedPacked;
    s.endedServe = endedPacked & kBackendServeSeqMask;
    out.ownerEnded = true;
  }
  return out;
}

enum class BackendRequestFate : uint8_t { Handle, Drop, Leave };

// reqEpoch 0 = unstamped (older callers): judged as the current owner's. reqServe: the Serve() sequence it was
// posted from (0 = unstamped: dead once the owner has ended).
inline BackendRequestFate ClassifyBackendRequest(uint64_t reqEpoch, uint64_t reqServe, const BackendOwnerState& s) {
  const uint64_t epoch = reqEpoch != 0 ? reqEpoch : s.ownerEpoch;
  if (s.ownerEpoch != 0 && epoch < s.ownerEpoch) return BackendRequestFate::Drop;
  if (s.ownerEpoch != 0 && epoch > s.ownerEpoch) return BackendRequestFate::Leave;
  if (s.endedServe != 0 && (reqServe & kBackendServeSeqMask) <= s.endedServe) return BackendRequestFate::Drop;
  return BackendRequestFate::Handle;
}

// The key a handled request is marked with (BackendStatusSnapshot::handledKey): its own epoch, never a later read.
inline uint64_t BackendRequestKey(uint64_t reqEpoch, uint64_t ownerEpoch, uint32_t seq) {
  return ((reqEpoch != 0 ? reqEpoch : ownerEpoch) << 32) | seq;
}

}  // namespace remote60::native_poc
