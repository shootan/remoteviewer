#pragma once

#include <cstdint>

namespace remote60::native_poc {

// Which Windows session the SYSTEM input agent should be created in.
//
// This used to be WTSGetActiveConsoleSessionId() and nothing else, which is the session attached
// to the PHYSICAL console -- not the caller's, and not the one the host is streaming. Measured on
// the development machine while connected over RDP:
//
//     rdp-tcp#0   shotan   1   Active     <- the host streams this
//     console              8   Conn       <- WTSGetActiveConsoleSessionId() returns this
//
// So clicks aimed at a prompt in the streamed session were injected into the physical console's
// desktop instead. Nothing surfaced it: the pipe write succeeded, the injection result was
// discarded, and the host acknowledged every event as delivered.
//
// The requester's own session is the right answer, because the process on the other end of the
// pipe is the streaming host, and the desktop it is capturing is the desktop the operator is
// looking at. The console session stays as a fallback for the pre-logon case, where there is no
// requester session to speak of.

enum class SessionSource : uint8_t {
  Requester,  // the session of the process that opened the control pipe
  Console,    // physical console, used only when the requester's session is unusable
  None,       // nothing usable; the caller must not start an agent
};

struct SessionChoice {
  uint32_t sessionId = kInvalidSession;
  SessionSource source = SessionSource::None;

  static constexpr uint32_t kInvalidSession = 0xffffffffu;
};

inline constexpr uint32_t kInvalidSessionId = 0xffffffffu;

/**
 * Picks the session to create the input agent in.
 *
 * `requesterSession` is the session of the process holding the control pipe, or kInvalidSessionId
 * when it could not be determined. `consoleSession` is WTSGetActiveConsoleSessionId(), which
 * returns kInvalidSessionId while no session is attached to the console during a transition.
 *
 * Session 0 is rejected from both inputs. It is the services session and has no interactive
 * desktop, so an agent created there could attach to nothing -- and it is exactly what a
 * requester running as a service would report.
 */
inline SessionChoice resolve_target_session(uint32_t requesterSession, bool requesterStateKnown,
                                            bool requesterActive, uint32_t consoleSession) {
  const auto usable = [](uint32_t session) {
    return session != kInvalidSessionId && session != 0u;
  };
  // Requester and console being the same session is itself proof the requester owns the console's
  // attach right now (WTSGetActiveConsoleSessionId named it), so honor it regardless of the
  // connect-state probe -- the ordinary "sitting at the machine" case. (Codex review #362.)
  if (usable(requesterSession) && requesterSession == consoleSession) {
    return {requesterSession, SessionSource::Requester};
  }
  // Otherwise the requester is only the right target while it is ACTIVELY connected. A disconnected
  // requester -- the RDP session after the client drops, which Windows leaves "signed in but not
  // connected, e.g. exited to the lock screen" -- has its desktop off the active input desktop, so
  // SendInput there returns ERROR_ACCESS_DENIED (measured: secure_input.log err=5, requester=1
  // Disconnected while the LogonUI ran on console=13). Fall to the console in that case. When the
  // state probe failed (stateKnown=false) treat the requester as not-active for the same reason: a
  // valid console is the stronger signal, and we must never inject into a stale disconnected session.
  if (usable(requesterSession) && requesterStateKnown && requesterActive) {
    return {requesterSession, SessionSource::Requester};
  }
  if (usable(consoleSession)) return {consoleSession, SessionSource::Console};
  return {kInvalidSessionId, SessionSource::None};
}

inline const char* session_source_name(SessionSource source) {
  switch (source) {
    case SessionSource::Requester: return "requester";
    case SessionSource::Console: return "console";
    default: return "none";
  }
}

}  // namespace remote60::native_poc
