import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'

# ================= sender boundary: input epoch fencing at dequeue =================
edit(S + r'\host_encoded_sender.hpp', [
(r"""  uint64_t mediaEpoch = 0;
};""",
 r"""  uint64_t mediaEpoch = 0;
  // Flush epoch of the input this AU was encoded from (H264AccessUnit::inputEpoch, P11). The
  // sender drops a dequeued item whose epoch is behind CaptureState::inputEpoch: an AU queued
  // before a flush must not start on the wire after it (host_epoch_gate.hpp states the scope --
  // one AU already being chunked when the flush happens completes). 0 = untagged (raw path).
  uint64_t inputEpoch = 0;
};"""),
(r"""  std::atomic<uint64_t> mediaSessionEpoch{1};""",
 r"""  std::atomic<uint64_t> mediaSessionEpoch{1};
  // The host's flush epoch (CaptureState::inputEpoch), read by the sender thread at dequeue to
  // fence queued pre-flush AUs (P11). Set once at startup; nullptr = no fencing (tests / raw).
  const std::atomic<uint64_t>* inputEpochRef = nullptr;
  std::atomic<uint64_t> inputEpochDropCount{0};"""),
])
edit(S + r'\host_encoded_sender.cpp', [
(r"""      if (item.mediaEpoch != sender.mediaSessionEpoch.load(std::memory_order_acquire)) {
        sender.dropCount.fetch_add(1, std::memory_order_relaxed);
        continue;
      }""",
 r"""      if (item.mediaEpoch != sender.mediaSessionEpoch.load(std::memory_order_acquire)) {
        sender.dropCount.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      // Flush epoch fence (P11): an AU encoded from an input the host accepted before the last
      // flush may not START on the wire after it -- the new epoch's IDR must be the first. Queued
      // items are fenced here; an item already being chunked when the flush lands completes (the
      // documented in-flight exception; the viewer's held-resume rule covers its stamp).
      if (sender.inputEpochRef && item.inputEpoch != 0 &&
          item.inputEpoch < sender.inputEpochRef->load(std::memory_order_acquire)) {
        const uint64_t n = sender.inputEpochDropCount.fetch_add(1, std::memory_order_relaxed) + 1;
        sender.dropCount.fetch_add(1, std::memory_order_relaxed);
        if (n <= 5 || (n % 50) == 0) {
          std::cout << "[native-video-host] sender dropped pre-flush AU seq=" << item.udpHdr.seq
                    << " auEpoch=" << item.inputEpoch
                    << " curEpoch=" << sender.inputEpochRef->load(std::memory_order_acquire)
                    << " key=" << (item.keyFrame ? 1 : 0) << " total=" << n << "\n";
        }
        continue;
      }"""),
])
edit(S + r'\host_stage_encode_send_h264_au.cpp', [
(r"""      item.mediaEpoch = sender.mediaSessionEpoch.load(std::memory_order_acquire);
      item.enqueueUs = qpc_now_us();  // AU handed to sender; sender derives queueWaitUs""",
 r"""      item.mediaEpoch = sender.mediaSessionEpoch.load(std::memory_order_acquire);
      item.inputEpoch = au.inputEpoch;  // sender-side fence for AUs queued before a flush (P11)
      item.enqueueUs = qpc_now_us();  // AU handed to sender; sender derives queueWaitUs"""),
(r"""      case EpochVerdict::DropAwaitingKey:
        // The forced IDR did not come (ignored or delayed): ask again; this P would reference a
        // chain the viewer will never have.
        encoder.forceKeyNext = true;""",
 r"""      case EpochVerdict::DropUnknownEpoch:
        // No FIFO provenance (0) or an epoch this host never issued: fail closed, never on the wire.
        std::cout << "[native-video-host] epoch-gate dropped-unknown auEpoch=" << au.inputEpoch << " curEpoch=" << inputEpochNow
                  << " auCaptureUs=" << gateStampUs << " key=" << (au.keyFrame ? 1 : 0)
                  << " total=" << encoder.epochGate.droppedUnknownEpoch << "\n";
        return AuFlow::Continue;
      case EpochVerdict::DropAwaitingKey:
        // The forced IDR did not come (ignored or delayed): ask again; this P would reference a
        // chain the viewer will never have.
        encoder.forceKeyNext = true;"""),
])
edit(S + r'\host_startup_capture.cpp', [
(r"""  sender.StartThread(transport, useH264, args, clientSession, hx.mailbox);""",
 r"""  sender.inputEpochRef = &capture.inputEpoch;  // P11: fence queued pre-flush AUs at dequeue
  sender.StartThread(transport, useH264, args, clientSession, hx.mailbox);"""),
])

# ================= P9: live rect at secure dispatch, fail closed =================
edit(S + r'\host_capture_session.hpp', [
(r"""  std::atomic<uint64_t> inputEpoch{1};""",
 r"""  std::atomic<uint64_t> inputEpoch{1};
  // HMONITOR the capture session opened (0 = window mode / unknown), mirrored for the control
  // thread: the secure-input dispatch re-reads that monitor's live rect before every event (P9).
  std::atomic<uint64_t> captureMonitorHandle{0};"""),
])
edit(S + r'\secure_input_broker.hpp', [
(r"""  void SetTargetRect(int32_t originX, int32_t originY, uint32_t width, uint32_t height);
  bool connected() const;""",
 r"""  void SetTargetRect(int32_t originX, int32_t originY, uint32_t width, uint32_t height);
  /** The rect currently stamped into every message (0x0 = none: the host refuses to send). */
  void GetTargetRect(int32_t* originX, int32_t* originY, uint32_t* width, uint32_t* height) const;
  bool connected() const;"""),
])
edit(S + r'\secure_input_broker.cpp', [
(r"""void SecureInputBrokerClient::apply_target_rect_locked(SecureInputMessage* message) const {""",
 r"""void SecureInputBrokerClient::GetTargetRect(int32_t* originX, int32_t* originY, uint32_t* width,
                                            uint32_t* height) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (originX) *originX = targetOriginX_;
  if (originY) *originY = targetOriginY_;
  if (width) *width = targetWidth_;
  if (height) *height = targetHeight_;
}

void SecureInputBrokerClient::apply_target_rect_locked(SecureInputMessage* message) const {"""),
])
edit(S + r'\host_input_router.hpp', [
(r"""  std::atomic<uint64_t> secureSkipUnauthenticated{0};  // no directory capability to act on""",
 r"""  std::atomic<uint64_t> secureSkipUnauthenticated{0};  // no directory capability to act on
  std::atomic<uint64_t> secureSkipRectUnknown{0};      // captured monitor's rect not knowable at dispatch: refused (P9)
  std::atomic<uint64_t> secureRectUpdatedAtDispatch{0};  // rect changed between the last sync and this event
  uint64_t targetRectQueryFailures = 0;                  // main loop: sync could not read the monitor"""),
])
edit(S + r'\host_main_loop.hpp', [
(r"""void sync_input_target_rect(CaptureState& capture, InputRouterState& inputRouter, const char* reason);""",
 r"""void sync_input_target_rect(CaptureState& capture, InputRouterState& inputRouter, const char* reason);
// Control thread, right before a secure-desktop event goes to the agent: re-reads the captured
// monitor's live rect (host_secure_target_rect.hpp) and updates the broker if it moved; false =
// the rect is not knowable now and the event must NOT be sent (fail closed, counted, logged).
bool secure_target_rect_ready(CaptureState& capture, InputRouterState& inputRouter);"""),
])
edit(S + r'\host_loop_helpers.cpp', [
(r"""#include "host_input_target_rect.hpp"
""",
 r"""#include "host_input_target_rect.hpp"
#include "host_secure_target_rect.hpp"
"""),
(r"""void sync_input_target_rect(CaptureState& capture, InputRouterState& inputRouter, const char* reason) {
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
      derive_input_target_rect(capture.windowModeActive.load(std::memory_order_acquire), monitor);
  const bool first = std::string(inputRouter.targetRectSent.source) == "none";
  if (!first && input_target_rect_same(inputRouter.targetRectSent, next)) return;
  inputRouter.broker.SetTargetRect(next.originX, next.originY, next.width, next.height);
  inputRouter.targetRectSent = next;
  std::cout << "[native-video-host] secure-input target rect=" << describe_input_target_rect(next)
            << " reason=" << (reason ? reason : "-") << "\n";
}""",
 r"""bool query_monitor_physical_rect(HMONITOR monitor, MonitorPhysicalRect* out) {
  if (!monitor || !out) return false;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) return false;
  out->originX = info.rcMonitor.left;
  out->originY = info.rcMonitor.top;
  out->width = static_cast<uint32_t>(std::max<LONG>(0, info.rcMonitor.right - info.rcMonitor.left));
  out->height = static_cast<uint32_t>(std::max<LONG>(0, info.rcMonitor.bottom - info.rcMonitor.top));
  return out->valid();
}

InputTargetRect broker_target_rect(const InputRouterState& inputRouter) {
  InputTargetRect r;
  inputRouter.broker.GetTargetRect(&r.originX, &r.originY, &r.width, &r.height);
  r.source = (r.width && r.height) ? "broker" : "none";
  return r;
}

void sync_input_target_rect(CaptureState& capture, InputRouterState& inputRouter, const char* reason) {
  // The rect of the monitor the capture session opened, read LIVE: a display arrangement change
  // that keeps the resolution (the primary moved, a monitor re-ordered) changes the origin
  // without a size change, so no restart would follow -- the periodic call (stats tick) catches
  // it here, and the secure dispatch re-reads it per event (secure_target_rect_ready). The handle
  // is mirrored for the control thread. A failed query does NOT keep the previous rect: the
  // broker's rect is cleared, so no secure event is sent until the monitor is known again.
  const bool windowMode = capture.windowModeActive.load(std::memory_order_acquire);
  const HMONITOR monitor = (!windowMode && capture.monitorInfo.has_value()) ? capture.monitorInfo->monitor : nullptr;
  capture.captureMonitorHandle.store(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(monitor)), std::memory_order_release);
  MonitorPhysicalRect live;
  const bool queryOk = !windowMode && query_monitor_physical_rect(monitor, &live);
  const InputTargetRect next = windowMode ? derive_input_target_rect(true, live)
                               : queryOk ? derive_input_target_rect(false, live)
                                         : derive_input_target_rect(false, MonitorPhysicalRect{});
  const InputTargetRect current = broker_target_rect(inputRouter);
  if (!windowMode && !queryOk) {
    ++inputRouter.targetRectQueryFailures;
    if (inputRouter.targetRectQueryFailures <= 3 || (inputRouter.targetRectQueryFailures % 60) == 0) {
      std::cout << "[native-video-host] secure-input target rect UNKNOWN (monitor query failed) reason="
                << (reason ? reason : "-") << " failures=" << inputRouter.targetRectQueryFailures
                << " -> secure input refused until known\n";
    }
  }
  const bool first = std::string(inputRouter.targetRectSent.source) == "none";
  if (!first && input_target_rect_same(current, next) && input_target_rect_same(inputRouter.targetRectSent, next)) return;
  inputRouter.broker.SetTargetRect(next.originX, next.originY, next.width, next.height);
  inputRouter.targetRectSent = next;
  std::cout << "[native-video-host] secure-input target rect=" << describe_input_target_rect(next)
            << " reason=" << (reason ? reason : "-") << "\n";
}

bool secure_target_rect_ready(CaptureState& capture, InputRouterState& inputRouter) {
  const bool windowMode = capture.windowModeActive.load(std::memory_order_acquire);
  const HMONITOR monitor = reinterpret_cast<HMONITOR>(
      static_cast<uintptr_t>(capture.captureMonitorHandle.load(std::memory_order_acquire)));
  MonitorPhysicalRect live;
  const bool queryOk = !windowMode && query_monitor_physical_rect(monitor, &live);
  const InputTargetRect current = broker_target_rect(inputRouter);
  const SecureRectDecision d = decide_secure_target_rect(windowMode, queryOk, live, current);
  if (d.send && d.update) {
    inputRouter.broker.SetTargetRect(d.rect.originX, d.rect.originY, d.rect.width, d.rect.height);
    inputRouter.secureRectUpdatedAtDispatch.fetch_add(1, std::memory_order_relaxed);
    std::cout << "[native-video-host] secure-input target rect=" << describe_input_target_rect(d.rect)
              << " reason=dispatch (was " << describe_input_target_rect(current) << ")\n";
  }
  if (!d.send) {
    static std::atomic<uint64_t> lastLogUs{0};
    const uint64_t nowUs = qpc_now_us();
    const uint64_t last = lastLogUs.load(std::memory_order_relaxed);
    if (nowUs - last > 1'000'000) {
      lastLogUs.store(nowUs, std::memory_order_relaxed);
      std::cout << "[native-video-host] secure-input event refused why=" << d.why
                << " (captured monitor rect not knowable; not injecting at an unknown place)\n";
    }
  }
  return d.send;
}"""),
])
edit(S + r'\host_control_session.cpp', [
(r"""            inputRouter.secureSkipUnauthenticated.fetch_add(1, std::memory_order_relaxed);
          } else if (!inputRouter.broker.SendInputEvent(input, domainW, domainH)) {""",
 r"""            inputRouter.secureSkipUnauthenticated.fetch_add(1, std::memory_order_relaxed);
          } else if (!secure_target_rect_ready(capture, inputRouter)) {
            // The captured monitor's rect is not knowable right now (P9): do not aim at the
            // previous rect or at the whole virtual screen. Counted; the next event re-checks.
            inputRouter.secureSkipRectUnknown.fetch_add(1, std::memory_order_relaxed);
          } else if (!inputRouter.broker.SendInputEvent(input, domainW, domainH)) {"""),
(r"""        if (desktopMode && clientSession.directoryAuthenticated.load(std::memory_order_acquire) &&
            !interactive_desktop_is_default() &&
            inputRouter.broker.SendInputText(text,""",
 r"""        if (desktopMode && clientSession.directoryAuthenticated.load(std::memory_order_acquire) &&
            !interactive_desktop_is_default() &&
            secure_target_rect_ready(capture, inputRouter) &&
            inputRouter.broker.SendInputText(text,"""),
])

# ================= viewer pending: budget, valid stamps, lifetime =================
edit(S + r'\viewer_frame_gate.cpp', [
(r"""  const bool congestionHealthy = (gate.congestionState == ClientCongestionState::Normal);
  if (!congestionHealthy) gate.resumeAnchorPending = false;
  // P11: the resume frame must be FRESH to anchor. hostHoldUs is host-clock only (send - capture):
  // a frame the host sat on for longer than resumeAnchorMaxHoldUs is a held picture (the async
  // encoder's pre-flush output after a UAC switch; the last capture kept through a still secure
  // desktop), not where the content is now. It is shown, but the anchor waits for the first fresh
  // frame behind it; while pending the entry estimate treats the incoming stamp as the floor.
  const uint64_t hostHoldUs = (in.sendQpcUs > in.captureQpcUs) ? (in.sendQpcUs - in.captureQpcUs) : 0;
  const bool fresh = (in.sendQpcUs == 0) || (hostHoldUs <= gate.resumeAnchorMaxHoldUs);
  bool anchorHere = false;
  if (in.recvGapUs > 250000 && !in.synthetic && congestionHealthy) {
    if (fresh) {
      anchorHere = true;
      gate.resumeAnchorPending = false;
    } else {
      gate.resumeAnchorPending = true;
      gate.resumeAnchorPendingSinceUs = in.packetNowUs;
      gate.resumeAnchorPendingFrames = 0;
      ++gate.heldResumeFrames;
    }
  } else if (gate.resumeAnchorPending && !in.synthetic && congestionHealthy) {
    ++gate.resumeAnchorPendingFrames;
    if (fresh) {
      anchorHere = true;
      gate.resumeAnchorPending = false;
    } else if (gate.resumeAnchorPendingFrames >= gate.resumeAnchorPendingMaxFrames ||
               in.packetNowUs >= gate.resumeAnchorPendingSinceUs + gate.resumeAnchorPendingMaxUs) {
      // Every frame since the silence was held: a genuine backlog. Back to the old rule.
      gate.resumeAnchorPending = false;
      ++gate.heldResumeExpired;
    }
  }""",
 r"""  const bool congestionHealthy = (gate.congestionState == ClientCongestionState::Normal);
  // Lifetime: pending is a Normal-state, same-episode notion. A state change, a keyframe wait or
  // a decoder resync (note_reference_sync) ends it.
  if (!congestionHealthy || gate.waitForKeyFrame) gate.resumeAnchorPending = false;
  // P11: the resume frame must be provably FRESH to anchor. hostHoldUs is host-clock only (send -
  // capture): the host's own age for this picture, not a network or viewer backlog. A frame the
  // host sat on for longer than resumeAnchorMaxHoldUs is a held picture (the async encoder's
  // pre-flush output after a UAC switch; the last capture kept through a still secure desktop),
  // not where the content is now. It is shown, but the anchor waits for the first fresh frame
  // behind it; while pending the decode-queue ENTRY estimate treats the incoming stamp as the
  // floor -- nothing else (stream-lag emergency, keyframe wait, stale drops) is touched.
  // Stamps that are missing, zero or reversed are no evidence of freshness or of holding: such
  // a frame follows the pre-P11 rule and can neither start nor end a pending.
  const bool stampsValid = in.sendQpcUs != 0 && in.captureQpcUs != 0 && in.sendQpcUs >= in.captureQpcUs;
  const uint64_t hostHoldUs = stampsValid ? (in.sendQpcUs - in.captureQpcUs) : 0;
  const bool heldKnown = stampsValid && hostHoldUs > gate.resumeAnchorMaxHoldUs;
  const bool freshKnown = stampsValid && !heldKnown;
  bool anchorHere = false;
  if (in.recvGapUs > 250000 && !in.synthetic && congestionHealthy) {
    if (heldKnown) {
      if (!gate.resumeAnchorPending) {
        // One budget per episode: a later held gap frame spends it, never renews it.
        gate.resumeAnchorPending = true;
        gate.resumeAnchorPendingSinceUs = in.packetNowUs;
        gate.resumeAnchorPendingFrames = 0;
      } else {
        ++gate.resumeAnchorPendingFrames;
      }
      ++gate.heldResumeFrames;
    } else {
      anchorHere = true;  // fresh, or stamps unknown: the pre-P11 rule
      gate.resumeAnchorPending = false;
    }
  } else if (gate.resumeAnchorPending && !in.synthetic && congestionHealthy) {
    ++gate.resumeAnchorPendingFrames;
    if (freshKnown) {
      anchorHere = true;
      gate.resumeAnchorPending = false;
    }
  }
  if (gate.resumeAnchorPending &&
      (gate.resumeAnchorPendingFrames >= gate.resumeAnchorPendingMaxFrames ||
       in.packetNowUs >= gate.resumeAnchorPendingSinceUs + gate.resumeAnchorPendingMaxUs)) {
    // No fresh frame within the budget: a genuine backlog holds every frame. Back to the old rule.
    gate.resumeAnchorPending = false;
    ++gate.heldResumeExpired;
  }"""),
(r"""void FrameGate::note_reference_sync(const FrameGateInputs& in) {""",
 r"""void FrameGate::note_reference_sync(const FrameGateInputs& in) {
  gate.resumeAnchorPending = false;  // a decoded keyframe is the anchor; any pending ends here (P11)"""),
])

# ================= fake host: the new verdict =================
edit(S + r'\viewer_udp_recovery_test.cpp', [
(r"""        case EpochVerdict::DropOldEpoch: ++gateDroppedOld_; continue;
        case EpochVerdict::DropAwaitingKey: ++gateDroppedNonKey_; forceKeyPending_ = true; continue;""",
 r"""        case EpochVerdict::DropOldEpoch: ++gateDroppedOld_; continue;
        case EpochVerdict::DropUnknownEpoch: ++gateDroppedUnknown_; continue;
        case EpochVerdict::DropAwaitingKey: ++gateDroppedNonKey_; forceKeyPending_ = true; continue;"""),
(r"""  uint64_t gateDroppedOld_ = 0;
  uint64_t gateDroppedNonKey_ = 0;""",
 r"""  uint64_t gateDroppedOld_ = 0;
  uint64_t gateDroppedUnknown_ = 0;
  uint64_t gateDroppedNonKey_ = 0;"""),
])

# ================= tests: rect decision =================
edit(S + r'\host_input_target_rect_test.cpp', [
(r"""// Window mode never routes to the agent; the rect is zero so the agent's own virtual-screen
// fallback applies if it ever is asked.
void test_window_mode_is_zero() {""",
 r"""// The dispatch-time decision (host_secure_target_rect.hpp): the origin moved right after the
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
void test_window_mode_is_zero() {"""),
(r"""  test_origin_only_change_is_a_change();
  test_window_mode_is_zero();""",
 r"""  test_origin_only_change_is_a_change();
  test_dispatch_decision();
  test_window_mode_is_zero();"""),
])

# ================= tests: epoch gate fail-closed + reset cap =================
edit(S + r'\host_encode_epoch_test.cpp', [
(r"""  // Untagged AUs (epoch 0) are never held back.
  CHECK(judge(4, au(0, false)) == EpochVerdict::Emit);""",
 r"""  // Fail closed: no provenance (0) and a future epoch are never sent and never open the gate.
  CHECK(judge(4, au(0, false)) == EpochVerdict::DropUnknownEpoch);
  CHECK(judge(4, au(0, true)) == EpochVerdict::DropUnknownEpoch);
  CHECK(judge(4, au(9, true)) == EpochVerdict::DropUnknownEpoch);
  CHECK(!g.awaitingKey && g.droppedUnknownEpoch == 3);
  CHECK(judge(4, au(4, false)) == EpochVerdict::Emit);
  {
    // ...and while the gate is closed (epoch 5 flush), an unknown/future key does not open it.
    EpochGate h;
    uint64_t t2 = 5000000;
    auto j = [&](uint64_t e, const H264AccessUnit& a) { t2 += 16667; return epoch_gate_judge(h, e, a.inputEpoch, a.keyFrame, a.bytes.size(), t2); };
    CHECK(j(5, au(0, true)) == EpochVerdict::DropUnknownEpoch);
    CHECK(j(5, au(6, true)) == EpochVerdict::DropUnknownEpoch);
    CHECK(h.awaitingKey);
    CHECK(j(5, au(5, true)) == EpochVerdict::AcceptKey);
    CHECK(!h.awaitingKey);
  }"""),
(r"""  // Bound by time: epoch 6, one old AU right away, then one 800 ms later -> reset.
  CHECK(judge(6, au(5, false)) == EpochVerdict::DropOldEpoch);
  CHECK(judge(6, au(5, false), 800000) == EpochVerdict::ResetEncoder);
  CHECK(g.resetsRequested == 2);
}""",
 r"""  // Bound by time: epoch 6, one old AU right away, then one 800 ms later -> reset.
  CHECK(judge(6, au(5, false)) == EpochVerdict::DropOldEpoch);
  CHECK(judge(6, au(5, false), 800000) == EpochVerdict::ResetEncoder);
  CHECK(g.resetsRequested == 2);
  // A valid current-epoch IDR is accepted even when the wait has long run out.
  epoch_gate_note_reset(g, t);
  CHECK(judge(6, au(6, true), 5000000) == EpochVerdict::AcceptKey);
  // Rebuild cap: past kResetMaxPerWindow rebuilds within kResetWindowUs the gate keeps dropping
  // and re-forcing but asks for no more rebuilds (no endless re-initialisation).
  {
    EpochGate c;
    uint64_t t3 = 100000000;
    auto j = [&](uint64_t e, const H264AccessUnit& a, uint64_t dt) { t3 += dt; return epoch_gate_judge(c, e, a.inputEpoch, a.keyFrame, a.bytes.size(), t3); };
    CHECK(j(1, au(1, true), 0) == EpochVerdict::AcceptKey);
    uint32_t resets = 0, suppressed = 0;
    for (int round = 0; round < 5; ++round) {
      EpochVerdict v = EpochVerdict::Emit;
      for (uint32_t i = 0; i < EpochGate::kAwaitMaxDropped + 1 && v != EpochVerdict::ResetEncoder; ++i) {
        v = j(2, au(2, false), 1000);
      }
      if (v == EpochVerdict::ResetEncoder) { ++resets; epoch_gate_note_reset(c, t3); }
      else { ++suppressed; c.awaitDropped = 0; }  // the stage keeps re-forcing; model its next wait
    }
    CHECK(resets == EpochGate::kResetMaxPerWindow);
    CHECK(suppressed == 5 - EpochGate::kResetMaxPerWindow);
    CHECK(c.resetsSuppressed >= 1);
    // The window passes: a rebuild is allowed again.
    EpochVerdict v = EpochVerdict::Emit;
    for (uint32_t i = 0; i < EpochGate::kAwaitMaxDropped + 1 && v != EpochVerdict::ResetEncoder; ++i) {
      v = j(2, au(2, false), i == 0 ? EpochGate::kResetWindowUs : 1000);
    }
    CHECK(v == EpochVerdict::ResetEncoder);
  }
}"""),
])
print('step26 edits ok')
