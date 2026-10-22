#pragma once

// dxgi-wedge-fix r1 (Codex B6, Bⓑ1): what an automatic reconnect brings back.
//
// Field (10-21 15:17): the host's streaming child wedged and restarted; the viewer closed (session-dead,
// exit 43) and the shell reconnected it a second later -- onto the PICKER. The user, who had been on the
// desktop, saw a window list instead of their screen and left. This is the handoff that lets the
// reconnected viewer go straight back to what was on screen.
//
// The rules (Codex B6, r2 D1/D2):
//   * only a target whose frame was actually PRESENTED is reported -- not a pick still in flight, not
//     one that was acknowledged or decoded but never reached the screen, never a value from an older
//     session (RestoreCommitTracker below);
//   * the viewer reports it on its stdout, which only the shell that launched it reads; the shell keeps
//     it per operation (child, owner epoch, host) and hands it to the next AUTOMATIC reconnect of that
//     same host only. No capability, token or password is in it -- the reconnect authenticates afresh;
//   * "desktop" is the LOGICAL desktop: what the desktop card selects, which a host captures as its
//     current primary screen. It is restored with that same meaning (a desktop pick), never turned into
//     a particular physical screen;
//   * "monitor" is a screen the user chose explicitly. It is NOT restored automatically: the only names
//     the host gives a screen today are its GDI device name ("\\.\DISPLAYn") and rectangle, and another
//     output can come back under the same name and rectangle -- that is not a stable identity, and a
//     wrong screen must never be shown on its own. The picker, with the reason (r2 D1, the "safe
//     minimum" Codex allows; a stable output identity would be a wire change of its own);
//   * a window is not restored either (minimum implementation allowed by B6): the picker, with the reason.
//
// Wire forms (ASCII):
//   viewer stdout:  [viewer-restore] target=<arg>
//   viewer arg:     --restore-target <arg>
//   <arg> = "desktop" | "monitor" | "window"

#include <cstdint>
#include <optional>
#include <string>

namespace remote60::native_poc::viewer {

struct RestoreTarget {
  enum class Kind { Desktop, Monitor, Window };
  Kind kind = Kind::Desktop;
  bool operator==(const RestoreTarget& o) const { return kind == o.kind; }
};

std::string FormatRestoreArg(const RestoreTarget& t);
std::optional<RestoreTarget> ParseRestoreArg(const std::string& arg);
// The stdout line (no newline) and its strict parse (the shell side).
std::string FormatRestoreLine(const RestoreTarget& t);
std::optional<RestoreTarget> ParseRestoreLine(const std::string& line);

// r2 D1: what a restore does with a descriptor once the new host's window list is in. Only the logical
// desktop is selected (window id 0, the desktop card's own pick); it needs no screen list, so a list that
// is missing, late or unsupported changes nothing. Everything else is the picker with the reason.
struct RestoreDecision {
  bool selectDesktop = false;
  const char* failReason = nullptr;  // set when !selectDesktop
};
inline RestoreDecision DecideRestore(const RestoreTarget& t, bool selectionLockedByHost) {
  if (t.kind == RestoreTarget::Kind::Window) return {false, "window_not_restored"};
#ifndef REMOTE60_RESTORE_MUTANT_MONITOR_AS_DESKTOP  // test-only mutant: the r1 "primary -> desktop" shortcut
  if (t.kind == RestoreTarget::Kind::Monitor) return {false, "monitor_not_restored"};
#endif
  if (selectionLockedByHost) return {false, "target_locked_by_host"};
  return {true, nullptr};
}

// r2 D2: the last target the user actually SAW. Pure; the viewer drives it from the UI thread.
//
//   Arm(t, g)       a selection the host confirmed (the picker's reveal: its ack is in and the first
//                   frame of generation g decoded). It is only a candidate.
//   OnPresented(g)  a frame of generation g reached the screen. A candidate of that generation (or an
//                   older one -- within one selection the host only moves forward) becomes the committed
//                   target, returned once so it can be reported.
//   Downgrade(t)    the screen changed in a way no generation tells apart (the toolbar's screen switch:
//                   the host answers it before applying it and keeps the generation). Allowed only for a
//                   target that is never restored automatically, so committing it early can only send a
//                   reconnect to the picker -- never to a screen nobody saw. Drops any candidate.
//   Cancel()        the candidate's selection was cancelled or replaced, or the link went.
//
// A candidate that never presents never commits: the previous committed target (or, for a viewer that
// has presented nothing yet, the one the shell already holds) stays what a reconnect goes back to.
class RestoreCommitTracker {
 public:
  void Arm(const RestoreTarget& t, uint64_t generation) {
    candidate_ = t;
    candidateGeneration_ = generation;
#ifdef REMOTE60_RESTORE_MUTANT_COMMIT_ON_ARM  // test-only mutant: the r1 rule (commit at decode/reveal)
    committed_ = t;
#endif
  }
  void Cancel() { candidate_.reset(); }
  std::optional<RestoreTarget> OnPresented(uint64_t generation) {
    if (!candidate_ || generation < candidateGeneration_) return std::nullopt;
    committed_ = *candidate_;
    candidate_.reset();
    return committed_;
  }
  bool Downgrade(const RestoreTarget& t) {
    if (t.kind == RestoreTarget::Kind::Desktop) return false;
    candidate_.reset();
    committed_ = t;
    return true;
  }
  bool HasCandidate() const { return candidate_.has_value(); }
  const std::optional<RestoreTarget>& Committed() const { return committed_; }

 private:
  std::optional<RestoreTarget> candidate_;
  uint64_t candidateGeneration_ = 0;
  std::optional<RestoreTarget> committed_;
};

}  // namespace remote60::native_poc::viewer
