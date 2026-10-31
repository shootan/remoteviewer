// dxgi-wedge-fix r1/r2 (Codex B6, r2 D1/D2): the restore handoff's wire forms, what a restore selects,
// and when a target counts as seen. pure-logic.
//
// Mutants (same source, one define each; each must FAIL here):
//   REMOTE60_RESTORE_MUTANT_COMMIT_ON_ARM        commit at the reveal (decode), not at present (r1's rule)
//   REMOTE60_RESTORE_MUTANT_MONITOR_AS_DESKTOP   an explicit screen restored as the desktop (r1's primary shortcut)

#include "viewer_restore_target.hpp"

#include <cstdio>
#include <string>

using namespace remote60::native_poc::viewer;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
RestoreTarget of(RestoreTarget::Kind k) {
  RestoreTarget t;
  t.kind = k;
  return t;
}
const RestoreTarget kDesktop = of(RestoreTarget::Kind::Desktop);
const RestoreTarget kMonitor = of(RestoreTarget::Kind::Monitor);
const RestoreTarget kWindow = of(RestoreTarget::Kind::Window);
std::string arg_of(const std::optional<RestoreTarget>& t) { return t ? FormatRestoreArg(*t) : std::string("(none)"); }
}  // namespace

int main() {
  // ---- wire forms
  for (const auto& t : {kDesktop, kMonitor, kWindow}) {
    const auto back = ParseRestoreArg(FormatRestoreArg(t));
    check(("round trip: " + FormatRestoreArg(t)).c_str(), back && *back == t);
  }
  for (const char* bad : {"", "Desktop", "desktop:5c5c:0,0,1,1:1", "monitor:1", "window:1", "desktop ", "monitor\n"}) {
    check((std::string("refused: '") + bad + "'").c_str(), !ParseRestoreArg(bad));
  }
  const auto line = ParseRestoreLine(FormatRestoreLine(kMonitor) + "\r");
  check("the stdout line round-trips (a CR is tolerated)", line && *line == kMonitor);
  const auto real = ParseRestoreLine("10-22 01:35:26.311 " + FormatRestoreLine(kDesktop) + " eventQpcUs=39672159007");
  check("the viewer's real line (timestamp + eventQpcUs) parses", real && *real == kDesktop);
  check("refused: a line with anything before the prefix", !ParseRestoreLine("x[viewer-restore] target=desktop"));
  check("refused: something other than a timestamp before the prefix",
        !ParseRestoreLine("10-22 01:35:26.311 x " + FormatRestoreLine(kDesktop)) &&
            !ParseRestoreLine("ab-cd 01:35:26.311 " + FormatRestoreLine(kDesktop)));

  // ---- D1: what a restore selects
  auto d = DecideRestore(kDesktop, false);
  check("D1 the logical desktop is selected as the desktop (window id 0) -- no screen list needed", d.selectDesktop);
  d = DecideRestore(kMonitor, false);
  check("D1 an explicitly chosen screen is NOT restored (no stable identity): picker",
        !d.selectDesktop && d.failReason && std::string(d.failReason) == "monitor_not_restored");
  // The cases r1 got wrong all reach this same answer: the old primary is now another screen, the list
  // is missing or unsupported, or another output sits under the same DISPLAYn name and rectangle --
  // the descriptor carries no screen identity to be fooled by, and a screen is never guessed.
  d = DecideRestore(kWindow, false);
  check("a window is not restored: picker", !d.selectDesktop && std::string(d.failReason) == "window_not_restored");
  d = DecideRestore(kDesktop, true);
  check("a host that locks the target: picker", !d.selectDesktop && std::string(d.failReason) == "target_locked_by_host");

  // ---- D2: committed only when a frame of the selection reached the screen
  {
    RestoreCommitTracker tr;
    tr.Arm(kDesktop, 5);
    check("D2 armed but nothing presented: nothing committed", !tr.Committed());
    check("D2 a stale frame (older generation) presented: still nothing", !tr.OnPresented(4) && !tr.Committed());
    const auto c = tr.OnPresented(5);
    check("D2 the selection's own frame presented: committed once", c && *c == kDesktop && tr.Committed());
    check("D2 a later present does not report again", !tr.OnPresented(5));
  }
  {
    // Codex D2 counter-case: A presented; then B is picked, acknowledged and decoded (revealed) but its
    // present fails -- and the viewer exits. A reconnect must go back to A.
    RestoreCommitTracker tr;
    tr.Arm(kDesktop, 3);
    tr.OnPresented(3);
    tr.Arm(kWindow, 4);  // B: host confirmed, first frame decoded
    check("D2 A presented, B decoded but never presented: A stays committed",
          arg_of(tr.Committed()) == "desktop", arg_of(tr.Committed()));
    tr.OnPresented(4);
    check("D2 once B presents: B", arg_of(tr.Committed()) == "window", arg_of(tr.Committed()));
  }
  {
    RestoreCommitTracker tr;
    tr.Arm(kDesktop, 2);
    tr.OnPresented(2);
    tr.Arm(kWindow, 3);
    tr.Cancel();  // B's selection cancelled / link lost
    check("D2 a cancelled candidate never commits, even if its generation presents later",
          !tr.OnPresented(3) && arg_of(tr.Committed()) == "desktop", arg_of(tr.Committed()));
  }
  {
    RestoreCommitTracker tr;
    tr.Arm(kDesktop, 7);
    check("a present before any candidate's generation does nothing (fresh viewer, previous target kept by the shell)",
          !tr.OnPresented(6) && !tr.Committed());
  }
  {
    // The toolbar's screen switch: no generation tells it apart, so the descriptor drops to one that is
    // never restored -- a reconnect then gets the picker, not the desktop the user has left.
    RestoreCommitTracker tr;
    tr.Arm(kDesktop, 1);
    tr.OnPresented(1);
    check("toolbar switch: the desktop cannot be 'downgraded' to", !tr.Downgrade(kDesktop));
    check("toolbar switch: committed as 'monitor' at once", tr.Downgrade(kMonitor) && arg_of(tr.Committed()) == "monitor");
    tr.Arm(kDesktop, 2);
    check("then a desktop pick again: still 'monitor' until its frame presents", arg_of(tr.Committed()) == "monitor");
    tr.OnPresented(2);
    check("and 'desktop' once it does", arg_of(tr.Committed()) == "desktop");
  }

  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
