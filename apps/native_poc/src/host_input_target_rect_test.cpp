// Unit test for the secure-input target rect rule (host_input_target_rect.hpp): the rect the
// SYSTEM agent maps UAC clicks into must be the monitor the capture session actually opened,
// re-derived on every capture (re)start -- the 2026-09-07 defect was a rect read once at process
// start under RDP (2236x1232) and kept after the host moved to the 1920x1080 console (P9).
// Pure; no Win32.
//
// Build: remote60_host_input_target_rect_test (CMake). Run: prints "...: PASS", exit 0.

#include <cstdio>
#include <string>

#include "host_input_target_rect.hpp"
#include "host_secure_target_rect.hpp"
#include "secure_input_mapping.hpp"

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      ++gFailures;                                                     \
    }                                                                  \
  } while (0)

DesktopRect as_desktop(const InputTargetRect& r) {
  return DesktopRect{r.originX, r.originY, static_cast<int32_t>(r.width), static_cast<int32_t>(r.height)};
}

// The field trace: host started under RDP, rect frozen at 2236x1232; after the console came
// back the capture re-fit to 1920x1080 but the agent still scaled by 2236/1920, 1232/1080.
void test_rdp_to_console_transition() {
  std::printf("[1] RDP-era rect must be replaced by the console rect on restart\n");
  const MonitorPhysicalRect rdp{0, 0, 2236, 1232};
  const MonitorPhysicalRect console{0, 0, 1920, 1080};
  const InputTargetRect atStart = derive_input_target_rect(false, rdp);
  CHECK(atStart.width == 2236 && atStart.height == 1232);
  // With the stale rect the measured click (841,703) in the 1920x1080 domain lands at (979,802).
  const MappedPoint stale = map_client_point(841, 703, 1920, 1080, as_desktop(atStart));
  CHECK(stale.x == 979 && stale.y == 802);
  // After the restart the rule yields the console rect and the same click lands where aimed.
  const InputTargetRect afterRestart = derive_input_target_rect(false, console);
  CHECK(!input_target_rect_same(atStart, afterRestart));
  CHECK(afterRestart.width == 1920 && afterRestart.height == 1080 && afterRestart.originX == 0);
  const MappedPoint fixed = map_client_point(841, 703, 1920, 1080, as_desktop(afterRestart));
  CHECK(fixed.x == 841 && fixed.y == 703);
  // Idempotent: a restart on the same monitor is not a change (no log line, no broker churn).
  CHECK(input_target_rect_same(afterRestart, derive_input_target_rect(false, console)));
}

// A selected monitor left of the primary: origin negative, size its own, DPI untouched.
void test_selected_monitor_negative_origin() {
  std::printf("[2] a monitor left of the primary keeps its origin\n");
  const MonitorPhysicalRect left{-1920, 0, 1920, 1080};
  const InputTargetRect r = derive_input_target_rect(false, left);
  CHECK(r.originX == -1920 && r.originY == 0 && r.width == 1920 && r.height == 1080);
  const MappedPoint p = map_client_point(960, 540, 1920, 1080, as_desktop(r));
  CHECK(p.x == -960 && p.y == 540);
  // A 4K monitor above the primary (per-monitor DPI does not scale physical pixels).
  const MonitorPhysicalRect above{0, -2160, 3840, 2160};
  const InputTargetRect r2 = derive_input_target_rect(false, above);
  const MappedPoint q = map_client_point(1919, 1079, 1920, 1080, as_desktop(r2));
  CHECK(q.x == 3839 && q.y == -1);
}

// Same resolution, different origin: the primary moved in the arrangement (or the RDP display
// and the console display happen to share a size). No capture restart follows a pure origin
// change, so the periodic re-derivation must see it as a change and re-push.
void test_origin_only_change_is_a_change() {
  std::printf("[2b] an origin-only change at the same size is a change\n");
  const InputTargetRect before = derive_input_target_rect(false, MonitorPhysicalRect{0, 0, 1920, 1080});
  const InputTargetRect after = derive_input_target_rect(false, MonitorPhysicalRect{1920, 0, 1920, 1080});
  CHECK(!input_target_rect_same(before, after));
  CHECK(after.originX == 1920 && after.width == 1920);
  const MappedPoint p = map_client_point(0, 0, 1920, 1080, as_desktop(after));
  CHECK(p.x == 1920 && p.y == 0);
  // And the y axis alone.
  const InputTargetRect shifted = derive_input_target_rect(false, MonitorPhysicalRect{0, 1080, 1920, 1080});
  CHECK(!input_target_rect_same(before, shifted) && !input_target_rect_same(after, shifted));
}

// The dispatch-time decision (host_secure_target_rect.hpp): the origin moved right after the
// last sync and the first secure click arrives before the next tick -- the click must go with the
// NEW rect; an unreadable monitor refuses the click instead of aiming at the old rect or at the
// whole virtual screen.
void test_dispatch_decision() {
  std::printf("[2c] dispatch: origin moved since the last sync -> updated before the click; unknown -> refused\n");
  const InputTargetRect synced = derive_input_target_rect(false, MonitorPhysicalRect{0, 0, 1920, 1080});
  // (a) the primary moved to the right by one screen; the 1 s tick has not fired yet.
  SecureRectDecision d = decide_secure_target_rect(false, true, MonitorPhysicalRect{1920, 0, 1920, 1080}, synced);
  CHECK(d.send && d.update);
  CHECK(d.rect.originX == 1920 && d.rect.width == 1920);
  CHECK(map_client_point(841, 703, 1920, 1080, as_desktop(d.rect)).x == 1920 + 841);
  // (b) nothing moved: send, no update, no log churn.
  d = decide_secure_target_rect(false, true, MonitorPhysicalRect{0, 0, 1920, 1080}, synced);
  CHECK(d.send && !d.update);
  // (c) the monitor query failed: refuse -- not the old rect, not a zero rect.
  d = decide_secure_target_rect(false, false, MonitorPhysicalRect{}, synced);
  CHECK(!d.send && !d.update);
  CHECK(std::string(d.why) == "monitor-query-failed");
  // (d) the query answered an empty rect: refuse as well.
  d = decide_secure_target_rect(false, true, MonitorPhysicalRect{0, 0, 0, 0}, synced);
  CHECK(!d.send);
  CHECK(std::string(d.why) == "monitor-rect-invalid");
  // (e) window mode: the secure path never routes it.
  d = decide_secure_target_rect(true, true, MonitorPhysicalRect{0, 0, 1920, 1080}, synced);
  CHECK(!d.send);
  // (f) the broker holds no rect yet (0x0): a readable monitor updates and sends.
  d = decide_secure_target_rect(false, true, MonitorPhysicalRect{0, 0, 1920, 1080}, InputTargetRect{});
  CHECK(d.send && d.update);
}

// Window mode never routes to the agent; the rect is zero so the agent's own virtual-screen
// fallback applies if it ever is asked.
void test_window_mode_is_zero() {
  std::printf("[3] window mode hands the agent no rect\n");
  const MonitorPhysicalRect mon{0, 0, 1920, 1080};
  const InputTargetRect r = derive_input_target_rect(true, mon);
  CHECK(r.width == 0 && r.height == 0);
  CHECK(std::string(r.source) == "window-mode");
  const DesktopRect d = as_desktop(r);
  CHECK(!d.valid());
}

// A failed monitor query must not leave the previous rect in place silently: zero + a source
// the caller logs.
void test_unknown_monitor_is_zero() {
  std::printf("[4] an unknown monitor yields the fallback rect\n");
  const InputTargetRect r = derive_input_target_rect(false, MonitorPhysicalRect{});
  CHECK(r.width == 0 && r.height == 0);
  CHECK(std::string(r.source) == "monitor-unknown");
  CHECK(describe_input_target_rect(r) == "(0,0)/0x0 source=monitor-unknown");
}

}  // namespace

int main() {
  test_rdp_to_console_transition();
  test_selected_monitor_negative_origin();
  test_origin_only_change_is_a_change();
  test_dispatch_decision();
  test_window_mode_is_zero();
  test_unknown_monitor_is_zero();
  if (gFailures == 0) {
    std::printf("host_input_target_rect_test: PASS\n");
    return 0;
  }
  std::printf("host_input_target_rect_test: FAIL (%d)\n", gFailures);
  return 1;
}
