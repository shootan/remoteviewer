// See viewer_restore.hpp (dxgi-wedge-fix r1, Codex B6).

#include "viewer_state.hpp"
#include "viewer_restore.hpp"

#include <iostream>
#include <sstream>
#include <vector>

#include "monitor_select_target.hpp"
#include "viewer_log.hpp"
#include "viewer_picker.hpp"
#include "viewer_restore_target.hpp"
#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {

namespace {

// How long a restore may wait for the new host's lists before it gives up and leaves the picker up.
constexpr uint64_t kRestoreListWaitUs = 15'000'000;

std::vector<RestoreMonitor> listed_monitors(const WindowPanelSnapshot& snap) {
  std::vector<RestoreMonitor> out;
  for (const auto& m : snap.monitors) {
    RestoreMonitor r;
    r.id = m.id;
    r.name = m.name;
    r.x = m.x;
    r.y = m.y;
    r.width = m.width;
    r.height = m.height;
    r.primary = m.primary;
    out.push_back(std::move(r));
  }
  return out;
}

void fail(ViewerState& ctx, const std::string& reason) {
  auto& r = ctx.picker.restore;
  r.phase = RestoreState::Phase::Failed;
  r.failReason = reason;
  log_client_line(ctx, "[native-video-client][restore] result=picker reason=" + reason);
  if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
}

void request(ViewerState& ctx, uint64_t windowId, const std::string& what) {
  auto& r = ctx.picker.restore;
  // The picker's own transaction: the picker stays up until the first frame of the generation the
  // host answers with, exactly as after a click.
  if (begin_pc_target_selection(ctx, windowId, "restore_requested")) {
    r.phase = RestoreState::Phase::Requested;
    log_client_line(ctx, "[native-video-client][restore] requested " + what);
  } else {
    fail(ctx, "select_refused");
  }
}

}  // namespace

void restore_init(ViewerState& ctx, const std::string& restoreArg, bool reconnect) {
  auto& r = ctx.picker.restore;
  r.reconnect = reconnect;
  if (restoreArg.empty()) return;
  r.target = ParseRestoreArg(restoreArg);
  if (!r.target) {
    r.phase = RestoreState::Phase::Failed;
    r.failReason = "restore_arg_invalid";
    std::cout << "[native-video-client][restore] result=picker reason=restore_arg_invalid\n";
    return;
  }
  r.phase = RestoreState::Phase::Waiting;
  r.waitingSinceUs = qpc_now_us();
  std::cout << "[native-video-client][restore] waiting target=" << FormatRestoreArg(*r.target) << "\n";
}

void restore_step(ViewerState& ctx, HWND hwnd) {
  auto& r = ctx.picker.restore;
  if (r.phase != RestoreState::Phase::Waiting || !r.target) {
    // Not restoring: once something has been revealed, a monitor list that arrived since (asked for in
    // restore_on_reveal) turns a bare "desktop" report into the screen's identity. Deduplicated.
    if (r.revealed && !ctx.picker.visible.load(std::memory_order_relaxed)) restore_report_current(ctx, "monitor_list");
    return;
  }
  if (r.target->kind == RestoreTarget::Kind::Window) {
    // B6: a window is not restored automatically -- another window must never be picked by mistake.
    fail(ctx, "window_not_restored");
    return;
  }
  const bool connected = ctx.control.connected.load(std::memory_order_relaxed);
  const bool listSeen = r.windowListSeen.load(std::memory_order_acquire);
  if (!connected || !listSeen) {
    if (qpc_now_us() - r.waitingSinceUs > kRestoreListWaitUs) fail(ctx, connected ? "window_list_timeout" : "not_connected");
    return;
  }
  if (ctx.sel.pending.load(std::memory_order_acquire)) return;  // a pick already in flight
  const WindowPanelSnapshot snap = ctx.picker.windowPanel.Snapshot();
  if (snap.selectionLocked) {
    fail(ctx, "target_locked_by_host");
    return;
  }
  if (!r.target->hasMonitor) {
    request(ctx, 0, "desktop");
    return;
  }
  if (!snap.hostSupportsMonitors) {
    // A host that lists no screens has one desktop; it is the same screen only if that was the primary.
    if (r.target->primary) {
      request(ctx, 0, "desktop (host lists no screens; was the primary)");
    } else {
      fail(ctx, "monitor_list_unsupported");
    }
    return;
  }
  if (snap.monitors.empty()) {
    if (!r.monitorListAsked) {
      r.monitorListAsked = true;
      ctx.picker.windowPanel.RequestMonitorList();
    } else if (qpc_now_us() - r.waitingSinceUs > kRestoreListWaitUs) {
      fail(ctx, "monitor_list_timeout");
    }
    return;
  }
  const RestoreMatch m = MatchRestoreMonitor(*r.target, listed_monitors(snap));
  if (m.kind != RestoreMatchKind::Matched) {
    fail(ctx, m.reason());
    return;
  }
  if (m.primary) {
    // The new host captures its primary screen for "desktop": the same one-transaction pick a click on
    // the desktop card makes.
    request(ctx, 0, "desktop monitor=" + std::to_string(m.monitorId) + " (primary)");
    return;
  }
  if (!snap.hostSupportsMonitorSelect) {
    fail(ctx, "monitor_select_unsupported");
    return;
  }
  request(ctx, encode_monitor_select_target(m.monitorId), "desktop monitor=" + std::to_string(m.monitorId));
  (void)hwnd;
}

void restore_on_reveal(ViewerState& ctx) {
  auto& r = ctx.picker.restore;
  if (r.phase == RestoreState::Phase::Requested) {
    r.phase = RestoreState::Phase::Done;
    log_client_line(ctx, "[native-video-client][restore] result=restored target=" +
                             (r.target ? FormatRestoreArg(*r.target) : std::string("?")));
  } else if (r.phase == RestoreState::Phase::Failed) {
    // The user picked something themselves: the failure line no longer applies.
    r.phase = RestoreState::Phase::Done;
  }
  r.revealed = true;
  restore_report_current(ctx, "reveal");
  // The PC viewer does not fetch the screen list on its own in picker mode; without it the report can
  // only say "desktop" (= the primary on a new host). Ask once, so a secondary screen can be named.
  const WindowPanelSnapshot snap = ctx.picker.windowPanel.Snapshot();
  if (snap.hostSupportsMonitors && snap.monitors.empty()) ctx.picker.windowPanel.RequestMonitorList();
}

void restore_report_current(ViewerState& ctx, const char* why) {
  auto& r = ctx.picker.restore;
  const WindowPanelSnapshot snap = ctx.picker.windowPanel.Snapshot();
  RestoreTarget t;
  if (snap.selectedId != 0) {
    t.kind = RestoreTarget::Kind::Window;
  } else {
    for (const auto& m : snap.monitors) {
      if (m.id != snap.selectedMonitorId || m.name.empty()) continue;
      t.hasMonitor = true;
      t.monitorName = m.name;
      t.x = m.x;
      t.y = m.y;
      t.width = m.width;
      t.height = m.height;
      t.primary = m.primary;
    }
  }
  const std::string line = FormatRestoreLine(t);
  if (line == r.lastReported) return;
  r.lastReported = line;
  // Its own stdout line, unprefixed, so the shell's pump can match it exactly.
  log_client_line(ctx, line);
  log_client_line(ctx, std::string("[native-video-client][restore] reported why=") + why);
}

void restore_note_monitor_switch(ViewerState& ctx, uint64_t /*unused*/) {
  // Arm: the next frame of a NEWER generation than the last decoded one is the switched screen's.
  auto& r = ctx.picker.restore;
  r.reportAfterGenerationPlusOne.store(r.lastDecodedGeneration.load(std::memory_order_acquire) + 1,
                                       std::memory_order_release);
}

}  // namespace remote60::native_poc::viewer
