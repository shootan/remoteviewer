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
  test_window_mode_is_zero();
  test_unknown_monitor_is_zero();
  if (gFailures == 0) {
    std::printf("host_input_target_rect_test: PASS\n");
    return 0;
  }
  std::printf("host_input_target_rect_test: FAIL (%d)\n", gFailures);
  return 1;
}
