// dxgi-wedge-fix r1 (Codex B6): the restore handoff's wire forms and the screen match.
// pure-logic. The device name is hex on the wire, so nothing in it can split a field or a command line;
// a screen is the same screen only by name AND rectangle, exactly once.

#include "viewer_restore_target.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace remote60::native_poc::viewer;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
RestoreMonitor mon(uint32_t id, const char* name, int x, int y, uint32_t w, uint32_t h, bool primary) {
  RestoreMonitor m;
  m.id = id;
  m.name = name;
  m.x = x;
  m.y = y;
  m.width = w;
  m.height = h;
  m.primary = primary;
  return m;
}
}  // namespace

int main() {
  RestoreTarget t;
  t.hasMonitor = true;
  t.monitorName = R"(\\.\DISPLAY2)";
  t.x = -1920;
  t.y = 0;
  t.width = 1920;
  t.height = 1080;
  t.primary = false;
  const std::string arg = FormatRestoreArg(t);
  check("a screen: desktop:<hex name>:<rect>:<primary>", arg == "desktop:5c5c2e5c444953504c415932:-1920,0,1920,1080:0", arg);
  const auto back = ParseRestoreArg(arg);
  check("it parses back to the same screen", back && back->hasMonitor && back->monitorName == t.monitorName &&
                                               back->x == -1920 && back->width == 1920 && !back->primary);
  const auto line = ParseRestoreLine(FormatRestoreLine(t) + "\r");
  check("the stdout line round-trips (a CR is tolerated)", line && line->monitorName == t.monitorName);
  check("the plain forms", ParseRestoreArg("desktop") && !ParseRestoreArg("desktop")->hasMonitor &&
                               ParseRestoreArg("window") && ParseRestoreArg("window")->kind == RestoreTarget::Kind::Window);
  RestoreTarget w;
  w.kind = RestoreTarget::Kind::Window;
  check("a window is reported as just 'window' (no title, no id)", FormatRestoreArg(w) == "window");
  for (const char* bad : {"desktop:zz:0,0,1,1:0", "desktop:41:0,0,1:0", "desktop:41:0,0,0,1:0", "desktop:41:0,0,1,1:2",
                          "desktop:41:0,0,1,1", "desktop:00:0,0,1,1:0", "window:1", "", "desktop:41:0,0,1,1:0:extra",
                          "desktop:41:a,0,1,1:0", "desktop:41:99999999999,0,1,1:0"}) {
    check((std::string("refused: '") + bad + "'").c_str(), !ParseRestoreArg(bad));
  }
  check("refused: a line with anything before the prefix", !ParseRestoreLine("x[viewer-restore] target=desktop"));
  // The form the shell's pump really receives (log_client_line): a timestamp before, fields after.
  const auto real = ParseRestoreLine("10-22 01:35:26.311 " + FormatRestoreLine(t) + " eventQpcUs=39672159007");
  check("the viewer's real line (timestamp + eventQpcUs) parses", real && real->monitorName == t.monitorName &&
                                                                     real->width == 1920);
  check("refused: something other than a timestamp before the prefix",
        !ParseRestoreLine("10-22 01:35:26.311 x " + FormatRestoreLine(t)) &&
            !ParseRestoreLine("ab-cd 01:35:26.311 " + FormatRestoreLine(t)));
  check("refused: a 65-byte name", !ParseRestoreArg("desktop:" + std::string(130, '4') + ":0,0,1,1:0"));

  const std::vector<RestoreMonitor> two = {mon(0, R"(\\.\DISPLAY1)", 0, 0, 2560, 1440, true),
                                           mon(1, R"(\\.\DISPLAY2)", -1920, 0, 1920, 1080, false)};
  auto m = MatchRestoreMonitor(t, two);
  check("matched: same name and rectangle -> that id (not the index it had)", m.kind == RestoreMatchKind::Matched &&
                                                                               m.monitorId == 1 && !m.primary);
  std::vector<RestoreMonitor> reordered = {mon(0, R"(\\.\DISPLAY2)", -1920, 0, 1920, 1080, false),
                                           mon(1, R"(\\.\DISPLAY1)", 0, 0, 2560, 1440, true)};
  m = MatchRestoreMonitor(t, reordered);
  check("the list in another order: still found, by identity, at its NEW id", m.kind == RestoreMatchKind::Matched &&
                                                                               m.monitorId == 0);
  std::vector<RestoreMonitor> moved = {mon(0, R"(\\.\DISPLAY1)", 0, 0, 2560, 1440, true),
                                       mon(1, R"(\\.\DISPLAY2)", 2560, 0, 1920, 1080, false)};
  m = MatchRestoreMonitor(t, moved);
  check("same name, other rectangle -> changed (picker)", m.kind == RestoreMatchKind::Changed &&
                                                            std::string(m.reason()) == "monitor_changed");
  m = MatchRestoreMonitor(t, {two[0]});
  check("not listed -> gone (picker)", m.kind == RestoreMatchKind::Gone && std::string(m.reason()) == "monitor_gone");
  std::vector<RestoreMonitor> twin = two;
  twin.push_back(mon(2, R"(\\.\DISPLAY2)", -1920, 0, 1920, 1080, false));
  m = MatchRestoreMonitor(t, twin);
  check("listed twice -> ambiguous (picker), never a guess", m.kind == RestoreMatchKind::Ambiguous);
  // Negative control: the same index (1) with another screen behind it must not match.
  std::vector<RestoreMonitor> swapped = {mon(0, R"(\\.\DISPLAY1)", 0, 0, 2560, 1440, true),
                                         mon(1, R"(\\.\DISPLAY3)", -1920, 0, 1920, 1080, false)};
  m = MatchRestoreMonitor(t, swapped);
  check("negative control: another screen at the old index is NOT taken", m.kind == RestoreMatchKind::Gone);

  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
