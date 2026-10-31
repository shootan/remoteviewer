import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'

# A. every desktop backend refreshes the primary monitor geometry on restart
edit(S + r'\host_capture_session.cpp', [
(r"""    if (!capture.windowModeActive && backend.active == DesktopCaptureBackend::Dxgi) {
      capture.monitorInfo = primary_monitor_info();
      if (!capture.monitorInfo.has_value()) {
        std::cerr << "[native-video-host] primary monitor query failed on restart\n";
        return false;
      }
      if (capture.monitorInfo->width < capture.monitorInfo->height) {""",
 r"""    if (!capture.windowModeActive) {
      // Every desktop backend opens the primary monitor (DXGI duplicates it, the WGC restart item
      // and the GDI worker read it), so its geometry is refreshed for all of them: the secure
      // input agent's target rect is derived from it right after this restart (P9), and a host
      // that started under RDP must not keep the RDP display's rect once the console is back.
      capture.monitorInfo = primary_monitor_info();
      if (!capture.monitorInfo.has_value()) {
        std::cerr << "[native-video-host] primary monitor query failed on restart\n";
        if (backend.active == DesktopCaptureBackend::Dxgi) return false;
      }
    }
    if (!capture.windowModeActive && backend.active == DesktopCaptureBackend::Dxgi) {
      if (capture.monitorInfo->width < capture.monitorInfo->height) {"""),
])

# B. declaration next to restart_capture_session
edit(S + r'\host_main_loop.hpp', [
(r"""bool restart_capture_session(HostContext& hx);""",
 r"""bool restart_capture_session(HostContext& hx);
// Re-derives the SYSTEM input agent's target rect from the current capture target
// (host_input_target_rect.hpp) and hands it to the broker when it changed, with a log line.
// Called after every capture (re)start and once at startup. Main loop only.
void sync_input_target_rect(CaptureState& capture, InputRouterState& inputRouter, const char* reason);"""),
])

# C. the helper + the call at the restart tail
edit(S + r'\host_loop_helpers.cpp', [
(r"""#include "host_input_router.hpp"
""",
 r"""#include "host_input_router.hpp"
#include "host_input_target_rect.hpp"
"""),
(r"""bool restart_capture_session(HostContext& hx) {""",
 r"""void sync_input_target_rect(CaptureState& capture, InputRouterState& inputRouter, const char* reason) {
  MonitorPhysicalRect monitor;
  if (capture.monitorInfo.has_value()) {
    monitor.originX = capture.monitorInfo->originX;
    monitor.originY = capture.monitorInfo->originY;
    monitor.width = capture.monitorInfo->width;
    monitor.height = capture.monitorInfo->height;
  }
  const InputTargetRect next =
      derive_input_target_rect(capture.windowModeActive.load(std::memory_order_acquire), monitor);
  const bool first = std::string(inputRouter.targetRectSent.source) == "none";
  if (!first && input_target_rect_same(inputRouter.targetRectSent, next)) return;
  inputRouter.broker.SetTargetRect(next.originX, next.originY, next.width, next.height);
  inputRouter.targetRectSent = next;
  std::cout << "[native-video-host] secure-input target rect=" << describe_input_target_rect(next)
            << " reason=" << (reason ? reason : "-") << "\n";
}

bool restart_capture_session(HostContext& hx) {"""),
(r"""  encoder.ApplyConfirmedCaptureGeometry(capture, res, frameGating, inputRouter, sender, finalW, finalH, "capture-restart");
  return true;
}""",
 r"""  encoder.ApplyConfirmedCaptureGeometry(capture, res, frameGating, inputRouter, sender, finalW, finalH, "capture-restart");
  // The agent's rect follows what was just opened: RDP<->console, DXGI<->WGC<->GDI, a monitor
  // select and a geometry change all come through here (P9).
  sync_input_target_rect(capture, inputRouter, "capture-restart");
  return true;
}"""),
])

# D. startup uses the same rule
edit(S + r'\host_startup_graphics.cpp', [
(r"""  // Tell the SYSTEM agent where the captured pixels live. Without it the agent can only assume,
  // and its old assumption -- the primary monitor -- put every click on the wrong screen when the
  // prompt opened somewhere else.
  inputRouter.broker.SetTargetRect(capture.monitorInfo->originX, capture.monitorInfo->originY, capture.monitorInfo->width,
                                  capture.monitorInfo->height);""",
 r"""  // Tell the SYSTEM agent where the captured pixels live. Without it the agent can only assume,
  // and its old assumption -- the primary monitor -- put every click on the wrong screen when the
  // prompt opened somewhere else. This is only the FIRST value: it is re-derived after every
  // capture restart (P9) -- a host started over RDP saw the RDP display here and kept it for the
  // console session it later served, scaling every UAC click by 2236/1920 x 1232/1080.
  sync_input_target_rect(capture, inputRouter, "startup");"""),
])

# E. remember what was sent
edit(S + r'\host_input_router.hpp', [
(r"""#include "host_input_inject.hpp"
#include "host_window_enum.hpp"
#include "secure_input_broker.hpp"
""",
 r"""#include "host_input_inject.hpp"
#include "host_input_target_rect.hpp"
#include "host_window_enum.hpp"
#include "secure_input_broker.hpp"
"""),
(r"""  SecureInputBrokerClient broker;
  std::atomic<uint64_t> events{0};""",
 r"""  SecureInputBrokerClient broker;
  // The rect last handed to the broker (main loop only): a restart on the same monitor neither
  // logs nor churns it. source "none" until the first sync_input_target_rect.
  InputTargetRect targetRectSent;
  std::atomic<uint64_t> events{0};"""),
])

# F. agent: per-episode landing budget
edit(S + r'\secure_input_service_main.cpp', [
(r"""#include "secure_input_session.hpp"
""",
 r"""#include "secure_input_session.hpp"
#include "secure_input_diag_budget.hpp"
"""),
(r"""// Records where a click actually landed, for the first few button events only.""",
 r"""// Records where a click actually landed, for the first few button events of each episode."""),
(r"""void diag_inject_landing(const SecureInputMessage& message, long mapped_x, long mapped_y) {
  static std::atomic<int> remaining{12};
  if (remaining.fetch_sub(1, std::memory_order_relaxed) <= 0) return;""",
 r"""void diag_inject_landing(const SecureInputMessage& message, long mapped_x, long mapped_y) {
  // Per episode, not per process: a 2 s quiet gap refills the 12 lines, 60 per minute at most
  // (secure_input_diag_budget.hpp). The agent outlives a UAC prompt -- it is recreated only when
  // the desktop name changes -- so the old one-shot budget recorded the first prompt of the day
  // and nothing after it (2026-09-07 15:15 left no line). Single message thread.
  static remote60::native_poc::DiagLandingBudget budget;
  if (!remote60::native_poc::diag_landing_budget_take(budget, now_ms() * 1000ULL)) return;"""),
])

# G. mapping test: the field regression + the budget
edit(S + r'\secure_input_mapping_test.cpp', [
(r"""#include "secure_input_mapping.hpp"

#include <cstdio>
#include <string>
""",
 r"""#include "secure_input_mapping.hpp"
#include "secure_input_diag_budget.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
"""),
(r"""void TestDegenerateInputSpace() {
  std::printf("a one-pixel input space does not divide by zero\n");
  const DesktopRect screen{100, 200, 1920, 1080};
  expect("single pixel", 0, 0, 1, 1, screen, 100, 200);
}
""",
 r"""void TestDegenerateInputSpace() {
  std::printf("a one-pixel input space does not divide by zero\n");
  const DesktopRect screen{100, 200, 1920, 1080};
  expect("single pixel", 0, 0, 1, 1, screen, 100, 200);
}

// The 2026-09-07 defect, pinned (P9): the same click in the same 1920x1080 domain lands where it
// was aimed with the live console rect and 138/99 px down-right with the RDP-era rect the host
// kept from its start. The mapper is right both times; the rect it was fed was not.
void TestStaleRdpRectScalesTheClick() {
  std::printf("a stale 2236x1232 rect scales a 1920x1080 click; the live rect does not\n");
  const DesktopRect stale{0, 0, 2236, 1232};
  const DesktopRect live{0, 0, 1920, 1080};
  expect("field click 14:27:25, stale rect", 841, 703, 1920, 1080, stale, 979, 802);
  expect("field click 14:27:25, live rect", 841, 703, 1920, 1080, live, 841, 703);
  expect("field click 14:27:27, stale rect", 808, 704, 1920, 1080, stale, 941, 803);
  expect("field click 14:27:27, live rect", 808, 704, 1920, 1080, live, 808, 704);
}

void check(bool cond, const char* what) {
  if (!cond) {
    std::printf("  FAIL %s\n", what);
    ++gFailures;
  } else {
    std::printf("  ok   %s\n", what);
  }
}

// The landing diag budget: 12 per episode, refilled by a 2 s quiet gap, 60 per minute at most.
void TestLandingBudgetRefillsPerEpisode() {
  std::printf("the landing diag budget refills per episode and caps per minute\n");
  using remote60::native_poc::DiagLandingBudget;
  using remote60::native_poc::diag_landing_budget_take;
  DiagLandingBudget b;
  uint64_t t = 1'000'000;
  int granted = 0;
  for (int i = 0; i < 20; ++i) {
    if (diag_landing_budget_take(b, t)) ++granted;
    t += 50'000;
  }
  check(granted == 12, "12 lines in the first episode, the 13th+ refused");
  // The next UAC prompt, 2.5 s later, gets its own 12 -- the case the one-shot budget lost.
  t += 2'500'000;
  granted = 0;
  for (int i = 0; i < 20; ++i) {
    if (diag_landing_budget_take(b, t)) ++granted;
    t += 50'000;
  }
  check(granted == 12, "a 2 s gap refills the episode");
  // A click every 100 ms for 3 s stays in one episode: no refill inside it.
  granted = 0;
  for (int i = 0; i < 30; ++i) {
    if (diag_landing_budget_take(b, t)) ++granted;
    t += 100'000;
  }
  check(granted == 0, "continuous clicking does not refill");
  // Pathological: 30 prompts 2.5 s apart. Minute one (started at t=1.0 s) already granted 24, so
  // 36 more; minute two grants at most 60; the rest are refused. 96 lines for 360 clicks.
  granted = 0;
  for (int e = 0; e < 30; ++e) {
    t += 2'500'000;
    for (int i = 0; i < 12; ++i) {
      if (diag_landing_budget_take(b, t)) ++granted;
      t += 1'000;
    }
  }
  check(granted == 96, "the per-minute ceiling holds across many episodes");
}
"""),
(r"""  TestOutOfRangeIsClampedNotDropped();
  TestDegenerateInputSpace();
""",
 r"""  TestOutOfRangeIsClampedNotDropped();
  TestDegenerateInputSpace();
  TestStaleRdpRectScalesTheClick();
  TestLandingBudgetRefillsPerEpisode();
"""),
])

# H. CMake target for the rect rule test
edit(ROOT + r'\apps\native_poc\CMakeLists.txt', [
(r"""add_executable(remote60_secure_input_mapping_test
  src/secure_input_mapping_test.cpp
)
target_include_directories(remote60_secure_input_mapping_test PRIVATE src)
""",
 r"""add_executable(remote60_secure_input_mapping_test
  src/secure_input_mapping_test.cpp
)
target_include_directories(remote60_secure_input_mapping_test PRIVATE src)

# The secure-input target rect rule (P9): the agent's rect follows the monitor the capture opened
# and is re-derived on every restart; window mode = no rect; origin-preserving for multi-monitor.
add_executable(remote60_host_input_target_rect_test
  src/host_input_target_rect_test.cpp
)
target_include_directories(remote60_host_input_target_rect_test PRIVATE src)
"""),
])
print('all P9 edits ok')
