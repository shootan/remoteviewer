import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'

# live monitor query + periodic re-check
edit(S + r'\host_loop_helpers.cpp', [
(r"""void sync_input_target_rect(CaptureState& capture, InputRouterState& inputRouter, const char* reason) {
  MonitorPhysicalRect monitor;
  if (capture.monitorInfo.has_value()) {
    monitor.originX = capture.monitorInfo->originX;
    monitor.originY = capture.monitorInfo->originY;
    monitor.width = capture.monitorInfo->width;
    monitor.height = capture.monitorInfo->height;
  }
  const InputTargetRect next =
      derive_input_target_rect(capture.windowModeActive.load(std::memory_order_acquire), monitor);""",
 r"""void sync_input_target_rect(CaptureState& capture, InputRouterState& inputRouter, const char* reason) {
  // The rect of the monitor the capture session opened, read LIVE: a display arrangement change
  // that keeps the resolution (the primary moved, a monitor re-ordered) changes the origin
  // without a size change, so no restart would follow -- the periodic call (stats tick) catches
  // it here. The handle comes from the capture (primary_monitor_info at (re)start); if Windows
  // no longer knows it the last known geometry stands and the source says so.
  MonitorPhysicalRect monitor;
  if (capture.monitorInfo.has_value()) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (capture.monitorInfo->monitor && GetMonitorInfoW(capture.monitorInfo->monitor, &info)) {
      monitor.originX = info.rcMonitor.left;
      monitor.originY = info.rcMonitor.top;
      monitor.width = static_cast<uint32_t>(std::max<LONG>(0, info.rcMonitor.right - info.rcMonitor.left));
      monitor.height = static_cast<uint32_t>(std::max<LONG>(0, info.rcMonitor.bottom - info.rcMonitor.top));
    } else {
      monitor.originX = capture.monitorInfo->originX;
      monitor.originY = capture.monitorInfo->originY;
      monitor.width = capture.monitorInfo->width;
      monitor.height = capture.monitorInfo->height;
    }
  }
  const InputTargetRect next =
      derive_input_target_rect(capture.windowModeActive.load(std::memory_order_acquire), monitor);"""),
])

edit(S + r'\host_stage_stats.cpp', [
(r"""  if (t >= stats.nextAtUs) {
    ++stats.ticks;
    const bool statsPrintDue = (stats.ticks % stats.printEverySec) == 0;""",
 r"""  if (t >= stats.nextAtUs) {
    ++stats.ticks;
    // Once a second: the secure-input agent's target rect follows the captured monitor's live
    // geometry (P9). Origin-only changes never restart the capture, so the restart-time sync is
    // not enough on its own; this is one GetMonitorInfo and a compare, and logs only on change.
    sync_input_target_rect(capture, inputRouter, "periodic");
    const bool statsPrintDue = (stats.ticks % stats.printEverySec) == 0;"""),
])

edit(S + r'\host_input_target_rect_test.cpp', [
(r"""// Window mode never routes to the agent; the rect is zero so the agent's own virtual-screen
// fallback applies if it ever is asked.""",
 r"""// Same resolution, different origin: the primary moved in the arrangement (or the RDP display
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

// Window mode never routes to the agent; the rect is zero so the agent's own virtual-screen
// fallback applies if it ever is asked."""),
(r"""  test_selected_monitor_negative_origin();
  test_window_mode_is_zero();""",
 r"""  test_selected_monitor_negative_origin();
  test_origin_only_change_is_a_change();
  test_window_mode_is_zero();"""),
])
print('P9 follow-up edits ok')
