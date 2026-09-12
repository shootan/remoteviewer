// See viewer_session_watchdog.hpp.

#include "viewer_session_watchdog.hpp"

#include <sstream>

#include "viewer_log.hpp"
#include "viewer_picker.hpp"

namespace remote60::native_poc::viewer {

namespace {

constexpr uint64_t kPollIntervalUs = 1000000;   // 1 s
constexpr uint64_t kRepeatLogUs = 5000000;      // a persisting stall / silence is repeated every 5 s

}  // namespace

void poll_session_liveness(ViewerState& ctx, HWND hwnd) {
  RecvLiveness& live = ctx.recvLive;
  SessionWatchdogState& w = live.watch;
  const uint64_t nowUs = qpc_now_us();
  if (nowUs < w.nextPollUs) return;
  w.nextPollUs = nowUs + kPollIntervalUs;

  const bool connected = ctx.control.connected.load(std::memory_order_relaxed);
  const bool overUdp = ctx.control.overUdp.load(std::memory_order_acquire);
  const bool tunnelClosed = overUdp && ctx.control.udpControl.IsClosed();
  if (connected && !tunnelClosed) {
    w.controlEverConnected = true;
    w.controlGoneSinceUs = 0;
  } else if ((w.controlEverConnected || ctx.session.controlRequired) && w.controlGoneSinceUs == 0) {
    w.controlGoneSinceUs = nowUs;
  }

  SessionLivenessSample s;
  s.nowUs = nowUs;
  s.stage = live.current_stage();
  s.stageEnterUs = live.stageEnterUs.load(std::memory_order_relaxed);
  s.loopIterations = live.loopIterations.load(std::memory_order_relaxed);
  s.lastDatagramUs = live.lastDatagramUs.load(std::memory_order_relaxed);
  s.lastAssembledUs = live.lastAssembledUs.load(std::memory_order_relaxed);
  s.lastDecodeReturnUs = live.lastDecodeReturnUs.load(std::memory_order_relaxed);
  s.lastPublishUs = live.lastPublishUs.load(std::memory_order_relaxed);
  s.controlConnected = connected;
  s.tunnelClosed = tunnelClosed;
  s.controlGoneSinceUs = w.controlGoneSinceUs;
  s.streamExpected = (!ctx.picker.visible.load() || ctx.sel.pending.load()) &&
                     !ctx.control.reportedSecure.load() &&
                     (ctx.session.hostFrameHeartbeat.load() || s.lastPublishUs == 0 || ctx.sel.pending.load());
  s.controlRequired = ctx.session.controlRequired;
  if (!s.streamExpected) w.streamExpectedSinceUs = 0;
  else if (!w.streamExpectedSinceUs) w.streamExpectedSinceUs = nowUs;
  s.streamExpectedSinceUs = w.streamExpectedSinceUs;
  SessionLivenessConfig cfg;
  cfg.deadSessionUs = static_cast<uint64_t>(ctx.session.deadSessionUs);
  const SessionLivenessVerdict v = evaluate_session_liveness(s, cfg);

  const auto describe = [&](std::ostringstream& os) {
    os << " stage=" << recv_stage_name(s.stage) << " stageAgeUs=" << v.stageAgeUs
       << " loops=" << s.loopIterations << " datagramAgeUs=" << v.datagramAgeUs
       << " assembledAgeUs=" << liveness_age_us(nowUs, s.lastAssembledUs)
       << " decodeAgeUs=" << liveness_age_us(nowUs, s.lastDecodeReturnUs)
       << " publishAgeUs=" << v.publishAgeUs << " control=" << (connected ? 1 : 0)
       << " tunnelClosed=" << (tunnelClosed ? 1 : 0);
    if (overUdp) os << " tunnelReason=" << to_string(ctx.control.udpControl.CloseReason());
    os << " controlGoneUs=" << v.controlGoneUs;
  };

  if (v.recvStalled && nowUs >= w.lastStallLogUs + kRepeatLogUs) {
    // The thread itself is not moving: a decoder / D3D call or a lock holds it. This is the
    // signature the 11:20 freeze could not show -- the stats lines stop, and so does control.
    w.lastStallLogUs = nowUs;
    std::ostringstream os;
    os << "[native-video-client][liveness] recv-thread stalled";
    describe(os);
    log_client_line(ctx, os.str());
  }
  if (v.linkSilent && nowUs >= w.lastSilentLogUs + kRepeatLogUs) {
    // The loop cycles on its receive timeout and nothing arrives while control looks connected:
    // delivery stopped (NAT, relay, host send path). The keyframe recovery timer keeps asking.
    w.lastSilentLogUs = nowUs;
    std::ostringstream os;
    os << "[native-video-client][liveness] link-silent";
    describe(os);
    log_client_line(ctx, os.str());
  }
  if (v.sessionDead && !w.deadReported) {
    w.deadReported = true;
    const bool exitSession = ctx.session.deadSessionExit;
    std::ostringstream os;
    os << "[native-video-client][liveness] session-dead action=" << (exitSession ? "close" : "notify");
    describe(os);
    log_client_line(ctx, os.str());
    set_window_panel_status(ctx, "session_lost");
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    if (exitSession && hwnd) {
      ctx.session.recoveryExitCode.store(43);
      // Nothing on this session can recover: the control thread has exited and the tunnel (if
      // any) is closed for good, and no frame has been published for deadSessionUs. Ending the
      // viewer hands the shell back its host list, where one click reconnects -- the same thing
      // the user did by hand ("had to restart the PC client"), minus the guessing.
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
  }
}

}  // namespace remote60::native_poc::viewer
