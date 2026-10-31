import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'

# ---- test expectation: the first delivery of a fresh assembler has no gap; deliver one AU first ----
edit(SRC + r'\native_video_client_shared_core_test.cpp', [
("""  UdpH264FrameAssembler c;
  c.ConfigureInOrderHold(100000, 8);
  (void)push_chunk(c, 20, 0, 8, 4, false, t0);          // incomplete P
""",
"""  UdpH264FrameAssembler c;
  c.ConfigureInOrderHold(100000, 8);
  (void)push_chunk(c, 19, 0, 4, 4, false, t0 - 1000);   // something delivered before, so a gap is a gap
  if (!expect(c.PopDelivery(t0 - 1000, true, &out) && out.frame.header.seq == 19, "hold: AU 19 delivered")) return false;
  (void)push_chunk(c, 20, 0, 8, 4, false, t0);          // incomplete P
"""),
])

# ---- invariant 1: the IDR deferral for a NACK repair is bounded per loss episode ----
edit(SRC + r'\viewer_constants.hpp', [
("""constexpr uint64_t kKeyRecoveryRetryUsDefault = 500000;      // 500ms
constexpr uint64_t kKeyRecoveryRetryMaxUsDefault = 2000000;  // 2s
""",
"""constexpr uint64_t kKeyRecoveryRetryUsDefault = 500000;      // 500ms
constexpr uint64_t kKeyRecoveryRetryMaxUsDefault = 2000000;  // 2s
// How long a due re-ask may be held back for a NACK repair in progress (the assembler's hold is
// 120 ms and the rounds fit inside it). Measured from the first deferral of that re-ask; a new
// datagram, a new AU or a new NACK round does not restart it, so a repair that never ends cannot
// starve the IDR path. (Codex invariant 1.)
constexpr uint64_t kKeyRecoveryDeferMaxUsDefault = 300000;   // 300ms
"""),
])
edit(SRC + r'\viewer_frame_gate_state.hpp', [
("""  uint64_t recoveryRetryIntervalUs = 0;
  uint64_t recoveryRetryMaxIntervalUs = 0;
""",
"""  uint64_t recoveryRetryIntervalUs = 0;
  uint64_t recoveryRetryMaxIntervalUs = 0;
  uint64_t recoveryRetryDeferMaxUs = 0;  // config: the longest a due re-ask waits for a NACK repair
  uint64_t recoveryDeferSinceUs = 0;     // first deferral of the current due re-ask (0 = none)
"""),
])
edit(SRC + r'\viewer_frame_gate.cpp', [
("""  if (nowUs < gate.recoveryNextRetryUs) return;
  if (repairInProgress) {
    // A retransmit may still complete the missing AU (or the missing IDR itself): asking for a
    // fresh IDR now would have both answers on the wire. Checked again on the next tick.
    ++gate.recoveryRetryDeferred;
    return;
  }
  const bool firstRetry = (gate.recoveryRetryCurrentUs == gate.recoveryRetryIntervalUs);
""",
"""  if (nowUs < gate.recoveryNextRetryUs) return;
  if (repairInProgress) {
    // A retransmit may still complete the missing AU (or the missing IDR itself): asking for a
    // fresh IDR now would have both answers on the wire. Checked again on the next tick -- but
    // only for recoveryRetryDeferMaxUs from the first deferral of THIS re-ask: a repair that keeps
    // finding a new AU to chase must not hold the IDR path back indefinitely.
    if (gate.recoveryDeferSinceUs == 0) gate.recoveryDeferSinceUs = nowUs;
    const uint64_t deferredUs = (nowUs >= gate.recoveryDeferSinceUs) ? (nowUs - gate.recoveryDeferSinceUs) : 0;
    if (deferredUs < gate.recoveryRetryDeferMaxUs) {
      ++gate.recoveryRetryDeferred;
      return;
    }
  }
  gate.recoveryDeferSinceUs = 0;
  const bool firstRetry = (gate.recoveryRetryCurrentUs == gate.recoveryRetryIntervalUs);
"""),
("""    gate.keyWaitSinceUs = 0;
    gate.recoveryNextRetryUs = 0;
    gate.recoveryRetryCurrentUs = 0;
    return;
  }
""",
"""    gate.keyWaitSinceUs = 0;
    gate.recoveryNextRetryUs = 0;
    gate.recoveryRetryCurrentUs = 0;
    gate.recoveryDeferSinceUs = 0;
    return;
  }
"""),
])
edit(SRC + r'\viewer_startup.cpp', [
("""  ctx.gate.recoveryRetryMaxIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEY_RECOVERY_RETRY_MAX_US",
      static_cast<uint32_t>(kKeyRecoveryRetryMaxUsDefault), 100000, 30000000);
""",
"""  ctx.gate.recoveryRetryMaxIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEY_RECOVERY_RETRY_MAX_US",
      static_cast<uint32_t>(kKeyRecoveryRetryMaxUsDefault), 100000, 30000000);
  ctx.gate.recoveryRetryDeferMaxUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEY_RECOVERY_DEFER_MAX_US",
      static_cast<uint32_t>(kKeyRecoveryDeferMaxUsDefault), 0, 5000000);
"""),
])
edit(SRC + r'\viewer_frame_gate_test.cpp', [
("""    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;                // 500 ms
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;          // 2 s
""",
"""    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;                // 500 ms
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;          // 2 s
    gate.recoveryRetryDeferMaxUs = kKeyRecoveryDeferMaxUsDefault;             // 300 ms
"""),
("""  d.fg.tick(start + 525 * kMs, false);
  CHECK(d.sink.requests(7) == 1);
""",
"""  d.fg.tick(start + 525 * kMs, false);
  CHECK(d.sink.requests(7) == 1);
  // ... and the deferral is bounded: a repair that is "in progress" on every tick (a new AU to
  // chase each time) still lets the re-ask out 300 ms after it first fell due (Codex invariant 1).
  Rig e;
  e.gate.waitForKeyFrame = true;
  e.fg.tick(start);
  int asks = 0;
  uint64_t askedAtUs = 0;
  for (uint64_t t = start; t <= start + 2000 * kMs; t += 25 * kMs) {
    e.fg.tick(t, /*repairInProgress=*/true);
    if (e.sink.requests(7) > asks) {
      asks = e.sink.requests(7);
      if (askedAtUs == 0) askedAtUs = t;
    }
  }
  CHECK(asks >= 1);
  CHECK(askedAtUs >= start + 800 * kMs && askedAtUs <= start + 850 * kMs);  // 500 ms due + 300 ms cap
  CHECK(e.gate.recoveryRetryDeferred >= 10);
"""),
])
edit(SRC + r'\viewer_udp_recovery_test.cpp', [
("""    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;
""",
"""    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;
    gate.recoveryRetryDeferMaxUs = kKeyRecoveryDeferMaxUsDefault;
"""),
])

# ---- invariant 2: a wedged thread ends the process; no cleanup runs after a detach ----
edit(SRC + r'\viewer_shutdown.cpp', [
("""constexpr uint32_t kRecvThreadJoinTimeoutMs = 3000;
}  // namespace
""",
"""constexpr uint32_t kRecvThreadJoinTimeoutMs = 3000;
// The control thread is woken by the channel close / socket shutdown above it; the same bound.
constexpr uint32_t kControlThreadJoinTimeoutMs = 3000;

// A thread that did not come back is left alone -- it may be inside a driver call holding
// pointers into ctx -- and the process ends right here, before any of the shared state below is
// torn down (a detach followed by cleanup would be a use-after-free). Logged first so the field
// log names the stage; exit code 44 tells the shell this was an abnormal end. This is a bounded
// termination, not a recovery and not a reconnect. (Codex invariant 2.)
[[noreturn]] void abandon_thread_and_exit(ViewerContext& ctx, const char* which, uint32_t waitedMs) {
  const uint64_t nowUs = qpc_now_us();
  std::cout << "[native-video-client][liveness] " << which << " thread did not exit within " << waitedMs
            << " ms at shutdown stage=" << recv_stage_name(ctx.recvLive.current_stage())
            << " stageAgeUs=" << liveness_age_us(nowUs, ctx.recvLive.stageEnterUs.load(std::memory_order_relaxed))
            << " loops=" << ctx.recvLive.loopIterations.load(std::memory_order_relaxed)
            << " control=" << (ctx.control.connected.load(std::memory_order_relaxed) ? 1 : 0)
            << "; terminating the process (bounded termination, not a recovery)" << std::endl;
  std::cout.flush();
  TerminateProcess(GetCurrentProcess(), 44);
  ExitProcess(44);
}
}  // namespace
"""),
("""  if (ctx.controlThread.joinable()) ctx.controlThread.join();
  if (!join_with_timeout(ctx.recvThread, kRecvThreadJoinTimeoutMs)) {
    // The recv thread is wedged (the liveness watchdog will have logged the stage). Nothing
    // below -- decoder shutdown, MFShutdown -- can be trusted to return either, and the shell
    // can only restart a viewer that has actually gone. End the process: a last resort that
    // reports itself, not a recovery. (Codex condition 4 on history #390 item 5.)
    const uint64_t nowUs = qpc_now_us();
    std::cout << "[native-video-client][liveness] recv thread did not exit within "
              << kRecvThreadJoinTimeoutMs << " ms at shutdown stage="
              << recv_stage_name(ctx.recvLive.current_stage())
              << " stageAgeUs=" << liveness_age_us(nowUs, ctx.recvLive.stageEnterUs.load(std::memory_order_relaxed))
              << " loops=" << ctx.recvLive.loopIterations.load(std::memory_order_relaxed)
              << "; terminating the process\\n";
    std::cout.flush();
    ctx.recvThread.detach();
    TerminateProcess(GetCurrentProcess(), 44);
  }
""",
"""  // Bounded joins. A thread wedged inside a decoder / D3D / socket call never returns; blocking
  // here would turn a dead session into a hung process the shell cannot restart. Only a thread
  // that actually returned lets the teardown below touch the state it shared. (Codex condition
  // 4 / invariant 2 on history #390 item 5.)
  if (!join_with_timeout(ctx.controlThread, kControlThreadJoinTimeoutMs)) {
    abandon_thread_and_exit(ctx, "control", kControlThreadJoinTimeoutMs);
  }
  if (!join_with_timeout(ctx.recvThread, kRecvThreadJoinTimeoutMs)) {
    abandon_thread_and_exit(ctx, "recv", kRecvThreadJoinTimeoutMs);
  }
"""),
])
