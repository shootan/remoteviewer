// See viewer_restore.hpp (dxgi-wedge-fix r1, Codex B6; r2 D1/D2).

#include "viewer_state.hpp"
#include "viewer_restore.hpp"

#include <iostream>

#include "monitor_select_target.hpp"
#include "viewer_log.hpp"
#include "viewer_picker.hpp"
#include "viewer_restore_target.hpp"

namespace remote60::native_poc::viewer {

namespace {

// How long a restore may wait for the new host's window list before it gives up and leaves the picker up.
constexpr uint64_t kRestoreListWaitUs = 15'000'000;

void fail(ViewerState& ctx, const std::string& reason) {
  auto& r = ctx.picker.restore;
  r.phase = RestoreState::Phase::Failed;
  r.failReason = reason;
  log_client_line(ctx, "[native-video-client][restore] result=picker reason=" + reason);
  if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
}

void report(ViewerState& ctx, const RestoreTarget& t, const char* why) {
  // Its own stdout line, unprefixed, so the shell's pump can match it exactly.
  log_client_line(ctx, FormatRestoreLine(t));
  log_client_line(ctx, std::string("[native-video-client][restore] reported why=") + why);
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
  (void)hwnd;
  auto& r = ctx.picker.restore;
  if (r.phase != RestoreState::Phase::Waiting || !r.target) return;
  // Decided before waiting on anything: a target that is never restored does not keep the user waiting.
  if (r.target->kind != RestoreTarget::Kind::Desktop) {
    fail(ctx, DecideRestore(*r.target, false).failReason);
    return;
  }
  const bool connected = ctx.control.connected.load(std::memory_order_relaxed);
  const bool listSeen = r.windowListSeen.load(std::memory_order_acquire);
  if (!connected || !listSeen) {
    if (qpc_now_us() - r.waitingSinceUs > kRestoreListWaitUs) fail(ctx, connected ? "window_list_timeout" : "not_connected");
    return;
  }
  if (ctx.sel.pending.load(std::memory_order_acquire)) return;  // a pick already in flight
  const RestoreDecision d = DecideRestore(*r.target, ctx.picker.windowPanel.Snapshot().selectionLocked);
  if (!d.selectDesktop) {
    fail(ctx, d.failReason);
    return;
  }
  // The desktop card's own transaction: the picker stays up until the first frame of the generation
  // the host answers with, exactly as after a click; the host captures its current primary screen.
  if (begin_pc_target_selection(ctx, 0, "restore_requested")) {
    r.phase = RestoreState::Phase::Requested;
    log_client_line(ctx, "[native-video-client][restore] requested desktop");
  } else {
    fail(ctx, "select_refused");
  }
}

void restore_note_pick(ViewerState& ctx, uint64_t windowId) {
  auto& r = ctx.picker.restore;
  RestoreTarget t;
  if (windowId == 0) {
    t.kind = RestoreTarget::Kind::Desktop;
  } else if (classify_select_target(windowId).kind != SelectTargetKind::Window) {
    t.kind = RestoreTarget::Kind::Monitor;
  } else {
    t.kind = RestoreTarget::Kind::Window;
  }
  r.picked = t;
  // A new pick replaces whatever was waiting to be seen.
  r.commit.Cancel();
}

void restore_on_reveal(ViewerState& ctx, uint64_t generation) {
  auto& r = ctx.picker.restore;
  if (r.phase == RestoreState::Phase::Failed) {
    // The user picked something themselves: the failure line no longer applies.
    r.phase = RestoreState::Phase::Done;
  }
  if (r.picked) r.commit.Arm(*r.picked, generation);
  r.picked.reset();
  log_client_line(ctx, "[native-video-client][restore] revealed gen=" + std::to_string(generation) +
                           " (reported once presented)");
}

void restore_frame_presented(ViewerState& ctx, uint64_t generation) {
  auto& r = ctx.picker.restore;
  const auto committed = r.commit.OnPresented(generation);
  if (!committed) return;
  report(ctx, *committed, "presented");
  if (r.phase == RestoreState::Phase::Requested) {
    r.phase = RestoreState::Phase::Done;
    log_client_line(ctx, "[native-video-client][restore] result=restored target=" + FormatRestoreArg(*committed) +
                             " gen=" + std::to_string(generation));
  }
}

void restore_note_toolbar_monitor(ViewerState& ctx) {
  // The legacy screen switch is answered before it is applied and keeps the stream generation, so no
  // frame can be told to be the new screen's. What is on screen is then no longer known to be the
  // logical desktop: report a target a reconnect never restores (the picker), never the desktop.
  RestoreTarget t;
  t.kind = RestoreTarget::Kind::Monitor;
  auto& r = ctx.picker.restore;
  const bool changed = r.commit.Committed() != std::optional<RestoreTarget>(t);
  r.commit.Downgrade(t);
  if (changed) report(ctx, t, "toolbar_monitor_switch");
}

}  // namespace remote60::native_poc::viewer
