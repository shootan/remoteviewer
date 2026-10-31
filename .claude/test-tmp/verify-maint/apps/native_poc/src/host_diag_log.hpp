#pragma once

// The decisions inside the connect diagnostics. (pc2-connect-diag r1, scope B)
//
// Role:    how a refusal is named, and how two log sites stay bounded when their rate is set by
//          whatever arrives on a UDP port.
// Thread:  none of its own; callers hold whatever lock their own state needs.
// Input:   an authorisation result, or a counter and a clock.
// Output:  a reason string, or whether to emit this line and how many were skipped.
// Callers: HostAgent::LogPunchArrival, the hello refusal in host_startup_control.cpp, the
//          heartbeat cycle in HostAgent::Run.
//
// Here rather than inline because a diagnostic that is wrong is worse than none: it is read as
// evidence. The naming below is the part that decides what a reader concludes, and inline it could
// only be checked by causing the failure it describes.

#include <cstdint>

namespace remote60::native_poc {

/** What an authorisation had to say for itself. Mirrors HostAgent::PeerAuthDiag. */
struct RejectFacts {
  bool matched = false;       // a capability with this value was found
  bool endpointMoved = false; // ...but it arrived from a tuple other than the expected one
  size_t held = 0;            // capabilities in hand after the expiry sweep
  size_t expiredNow = 0;      // how many that sweep dropped
};

/**
 * Why a hello was refused, in one word.
 *
 * "invalid directory capability" was the whole vocabulary, and it covered every case below. The
 * distinction that matters most is the cheapest one: a host holding nothing at all has not been
 * given the capability yet, which is a delivery problem; a host holding two that do not match has
 * been given something else, which is not.
 */
inline const char* reject_reason(const RejectFacts& f) {
  if (f.matched) return f.endpointMoved ? "peer-moved" : "consumed";
  if (f.held > 0) return "no-match";
  // Nothing in hand. Whether anything was just thrown away separates "it expired before it was
  // used" from "it never arrived", and those point at different halves of the system.
  return f.expiredNow > 0 ? "expired" : "none-held";
}

/**
 * A run of identical refusals, thinned.
 *
 * A client repeats its hello several times a second, and a host that cannot serve it refuses every
 * one. Printing them all buries the log; printing only the first hides that it is still going. So:
 * the first, and then every `every`-th, each carrying the run length.
 */
inline bool reject_should_emit(uint64_t sameRun, uint64_t every = 32) {
  if (sameRun == 0) return false;
  if (sameRun == 1) return true;
  return every > 0 && (sameRun % every) == 0;
}

/**
 * At most `perSecond` lines in any one second, with the skipped ones counted for the next.
 *
 * Driven by arriving datagrams, so it needs a ceiling that does not depend on the sender behaving.
 * The window is deliberately a reset rather than a sliding average: a burst then shows up as one
 * second of lines plus a count, which is what a reader wants to see.
 */
struct PunchLogWindow {
  uint64_t windowStartUs = 0;
  uint32_t inWindow = 0;
  uint64_t skipped = 0;
  uint64_t total = 0;
};

struct PunchLogDecision {
  bool emit = false;
  uint64_t total = 0;        // every arrival counts, emitted or not
  uint64_t skippedSince = 0; // reported on the line that follows them, then cleared
};

inline PunchLogDecision punch_log_decide(PunchLogWindow* w, uint64_t nowUs,
                                         uint32_t perSecond = 10) {
  PunchLogDecision out;
  if (!w) return out;
  ++w->total;
  out.total = w->total;
  if (w->windowStartUs == 0 || nowUs - w->windowStartUs >= 1000000ULL) {
    w->windowStartUs = nowUs;
    w->inWindow = 0;
  }
  if (w->inWindow < perSecond) {
    ++w->inWindow;
    out.emit = true;
    out.skippedSince = w->skipped;
    w->skipped = 0;
  } else {
    ++w->skipped;
  }
  return out;
}

/** Why the heartbeat loop stopped sleeping. Named so a stalled cycle reads differently to a woken one. */
inline const char* cycle_wake_cause(bool refreshFired, bool stillRunning) {
  if (!stillRunning) return "stop";
  return refreshFired ? "refresh" : "elapsed";
}

}  // namespace remote60::native_poc
