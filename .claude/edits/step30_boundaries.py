import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'

# ================= sender: permission point right before the first datagram =================
edit(S + r'\host_encoded_sender.hpp', [
(r"""  // The host's flush epoch (CaptureState::inputEpoch), read by the sender thread at dequeue to
  // fence queued pre-flush AUs (P11). Set once at startup; nullptr = no fencing (tests / raw).
  const std::atomic<uint64_t>* inputEpochRef = nullptr;
  std::atomic<uint64_t> inputEpochDropCount{0};""",
 r"""  // The host's flush epoch (CaptureState::inputEpoch), read by the sender thread to fence AUs of
  // another epoch (P11). nullptr = the fence is inactive (legacy / tests), the only case in which
  // an untagged item passes. With the fence active an item is sent only if its epoch EQUALS the
  // current one at the permission point -- old, untagged (0) and future epochs are all dropped.
  //
  // Permission point: right before the first datagram, after pacing. What the fence guarantees is
  // therefore: no AU whose first datagram was not yet permitted when the flush landed starts on
  // the wire after it. The exception is one AU per flush whose first datagram WAS permitted before
  // the flush (the window between that check and the first sendto is a few microseconds and is not
  // closed; the AU then completes with the metadata it was encoded with -- nothing is relabelled).
  // A single sender thread means at most one such AU exists at a time.
  const std::atomic<uint64_t>* inputEpochRef = nullptr;
  std::atomic<uint64_t> inputEpochDropCount{0};
  // Test seam: called after pacing, right before the permission check for the first datagram, so a
  // test can hold the sender there, move the epoch and resume. Never set in production.
  std::function<void(const EncodedSendItem&)> beforeFirstDatagramHook;"""),
])
edit(S + r'\host_encoded_sender.cpp', [
(r"""      // Flush epoch fence (P11): an AU encoded from an input the host accepted before the last
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
      }""",
 r"""      // Flush epoch fence (P11), early pass: an item of another epoch is dropped here without
      // costing a pacing slot. The decisive check is the permission point right before the first
      // datagram (below) -- a flush can land during pacing, and an AU whose first datagram was not
      // yet permitted then must not start. See SenderState::inputEpochRef for the exact contract.
      auto epoch_fence_drops = [&](const EncodedSendItem& it, const char* where) -> bool {
        if (!sender.inputEpochRef) return false;  // fence inactive: legacy pass-through
        const uint64_t cur = sender.inputEpochRef->load(std::memory_order_acquire);
        if (it.inputEpoch == cur) return false;
        const uint64_t n = sender.inputEpochDropCount.fetch_add(1, std::memory_order_relaxed) + 1;
        sender.dropCount.fetch_add(1, std::memory_order_relaxed);
        if (n <= 5 || (n % 50) == 0) {
          std::cout << "[native-video-host] sender dropped AU of another epoch at " << where << " seq=" << it.udpHdr.seq
                    << " auEpoch=" << it.inputEpoch << " curEpoch=" << cur
                    << " key=" << (it.keyFrame ? 1 : 0) << " total=" << n << "\n";
        }
        return true;
      };
      if (epoch_fence_drops(item, "dequeue")) continue;"""),
(r"""      const uint64_t sendStartUs = qpc_now_us();
      item.udpHdr.sendQpcUs = sendStartUs;
      SendPathStats pathStats{};
      const UdpSendOutcome outcome =
          send_udp_chunks_timed(clientSession.clientSock, peer, item.bytes.data(), item.bytes.size(),
                                item.udpHdr, args.udpMtu, &pathStats, &sender.mediaSessionEpoch,
                                item.mediaEpoch, egress);""",
 r"""      if (sender.beforeFirstDatagramHook) sender.beforeFirstDatagramHook(item);
      // Permission point (P11): the epoch is read once more right before the first datagram. An
      // item that passed the dequeue check but whose epoch moved during pacing is dropped here.
      if (epoch_fence_drops(item, "first-datagram")) {
        cadenceScheduledUs = 0;
        continue;
      }
      const uint64_t sendStartUs = qpc_now_us();
      item.udpHdr.sendQpcUs = sendStartUs;
      SendPathStats pathStats{};
      const UdpSendOutcome outcome =
          send_udp_chunks_timed(clientSession.clientSock, peer, item.bytes.data(), item.bytes.size(),
                                item.udpHdr, args.udpMtu, &pathStats, &sender.mediaSessionEpoch,
                                item.mediaEpoch, egress);"""),
])
# <functional> for the hook
p = S + r'\host_encoded_sender.hpp'
t = open(p, 'rb').read().decode('utf-8')
if '#include <functional>' not in t:
    t = t.replace('#include <deque>', '#include <deque>\n#include <functional>', 1)
    open(p, 'wb').write(t.encode('utf-8'))
    print('functional include added')

# ================= P9: versioned snapshot + one dispatch path =================
edit(S + r'\host_capture_session.hpp', [
(r"""  // HMONITOR the capture session opened (0 = window mode / unknown), mirrored for the control
  // thread: the secure-input dispatch re-reads that monitor's live rect before every event (P9).
  std::atomic<uint64_t> captureMonitorHandle{0};""",
 r"""  // What the capture is targeting, as one record for the control thread's secure-input dispatch
  // (P9, host_secure_target_rect.hpp): window mode + the HMONITOR the session opened + the
  // stream generation, with a version that moves whenever any of them changes. Written by the
  // main loop (sync_input_target_rect), read under inputTargetMu; a reader's copy is valid for
  // one event only -- the dispatch re-reads the version before it sends.
  struct InputTargetSnapshot {
    uint64_t version = 0;
    bool windowMode = false;
    uint64_t monitorHandle = 0;
    uint64_t streamGeneration = 0;
  };
  std::mutex inputTargetMu;
  InputTargetSnapshot inputTarget;"""),
])
edit(S + r'\secure_input_broker.hpp', [
(r"""  /** The rect currently stamped into every message (0x0 = none: the host refuses to send). */
  void GetTargetRect(int32_t* originX, int32_t* originY, uint32_t* width, uint32_t* height) const;""",
 r"""  /** The rect currently stamped into every message (0x0 = none: the host refuses to send). */
  void GetTargetRect(int32_t* originX, int32_t* originY, uint32_t* width, uint32_t* height) const;
  /**
   * Send with an explicit rect stamped into THIS message under the same lock as the write, so
   * a concurrent SetTargetRect can neither replace nor race it (P9 dispatch path).
   */
  bool SendInputEventWithRect(const ControlInputEventMessage& input, uint32_t inputWidth, uint32_t inputHeight,
                              int32_t originX, int32_t originY, uint32_t width, uint32_t height);
  bool SendInputTextWithRect(const ControlInputTextMessage& text, uint32_t inputWidth, uint32_t inputHeight,
                             int32_t originX, int32_t originY, uint32_t width, uint32_t height);"""),
])
edit(S + r'\secure_input_broker.cpp', [
(r"""void SecureInputBrokerClient::GetTargetRect(int32_t* originX, int32_t* originY, uint32_t* width,
                                            uint32_t* height) const {""",
 r"""bool SecureInputBrokerClient::SendInputEventWithRect(const ControlInputEventMessage& input, uint32_t inputWidth,
                                                     uint32_t inputHeight, int32_t originX, int32_t originY,
                                                     uint32_t width, uint32_t height) {
  SecureInputMessage message{};
  message.kind = static_cast<uint16_t>(SecureInputKind::InputEvent);
  message.inputWidth = inputWidth;
  message.inputHeight = inputHeight;
  message.eventKind = input.kind;
  message.buttons = input.buttons;
  message.x = input.x;
  message.y = input.y;
  message.wheelDelta = input.wheelDelta;
  message.keyCode = input.keyCode;
  message.targetOriginX = originX;
  message.targetOriginY = originY;
  message.targetWidth = width;
  message.targetHeight = height;
  std::lock_guard<std::mutex> lock(mu_);
  return WriteLocked(message);
}

bool SecureInputBrokerClient::SendInputTextWithRect(const ControlInputTextMessage& text, uint32_t inputWidth,
                                                    uint32_t inputHeight, int32_t originX, int32_t originY,
                                                    uint32_t width, uint32_t height) {
  SecureInputMessage message{};
  message.kind = static_cast<uint16_t>(SecureInputKind::InputText);
  message.inputWidth = inputWidth;
  message.inputHeight = inputHeight;
  message.textCount = std::min<uint16_t>(text.utf16Count, kSecureInputTextMax);
  std::copy_n(text.utf16, message.textCount, message.text);
  message.targetOriginX = originX;
  message.targetOriginY = originY;
  message.targetWidth = width;
  message.targetHeight = height;
  std::lock_guard<std::mutex> lock(mu_);
  return WriteLocked(message);
}

void SecureInputBrokerClient::GetTargetRect(int32_t* originX, int32_t* originY, uint32_t* width,
                                            uint32_t* height) const {"""),
])
edit(S + r'\host_input_router.hpp', [
(r"""struct CaptureState;
struct InputRouterState;
// Control thread, right before a secure-desktop event goes to the agent: re-reads the captured
// monitor's live rect (host_secure_target_rect.hpp) and updates the broker if it moved; false =
// the rect is not knowable now and the event must NOT be sent (fail closed, counted, logged).
// Defined in host_loop_helpers.cpp. (P9)
bool secure_target_rect_ready(CaptureState& capture, InputRouterState& inputRouter);""",
 r"""struct CaptureState;
struct InputRouterState;
// The ONE path by which a secure-desktop event / text reaches the SYSTEM agent (P9,
// host_secure_target_rect.hpp): capture-target snapshot -> live monitor rect -> stamped into this
// message -> written. Refuses (returns sent=false, counted, logged) when the rect is not knowable
// or the target changed meanwhile. Defined in host_loop_helpers.cpp.
struct SecureDispatchOutcome {
  bool sent = false;
  bool writeFailed = false;  // the broker write failed (the pipe), as opposed to a refusal
  const char* why = "";
};
SecureDispatchOutcome secure_dispatch_event(CaptureState& capture, InputRouterState& inputRouter,
                                            const ControlInputEventMessage& input, uint32_t domainW, uint32_t domainH);
SecureDispatchOutcome secure_dispatch_text(CaptureState& capture, InputRouterState& inputRouter,
                                           const ControlInputTextMessage& text, uint32_t domainW, uint32_t domainH);"""),
(r"""  std::atomic<uint64_t> secureSkipRectUnknown{0};      // captured monitor's rect not knowable at dispatch: refused (P9)
  std::atomic<uint64_t> secureRectUpdatedAtDispatch{0};  // rect changed between the last sync and this event
  uint64_t targetRectQueryFailures = 0;                  // main loop: sync could not read the monitor""",
 r"""  std::atomic<uint64_t> secureSkipRectUnknown{0};      // captured monitor's rect not knowable / target changed at dispatch: refused (P9)
  std::atomic<uint64_t> secureRectUpdatedAtDispatch{0};  // the dispatched rect differed from the last one dispatched
  uint64_t targetRectQueryFailures = 0;                  // main loop: sync could not read the monitor
  InputTargetRect lastDispatchedRect;                    // control thread only: for the change log"""),
])
edit(S + r'\host_main_loop.hpp', [
(r"""// Control thread, right before a secure-desktop event goes to the agent: re-reads the captured
// monitor's live rect (host_secure_target_rect.hpp) and updates the broker if it moved; false =
// the rect is not knowable now and the event must NOT be sent (fail closed, counted, logged).
bool secure_target_rect_ready(CaptureState& capture, InputRouterState& inputRouter);""",
 r"""// secure_dispatch_event / secure_dispatch_text: declared in host_input_router.hpp (the control
// session includes that), defined in host_loop_helpers.cpp."""),
])
edit(S + r'\host_loop_helpers.cpp', [
(r"""  const bool windowMode = capture.windowModeActive.load(std::memory_order_acquire);
  const HMONITOR monitor = (!windowMode && capture.monitorInfo.has_value()) ? capture.monitorInfo->monitor : nullptr;
  capture.captureMonitorHandle.store(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(monitor)), std::memory_order_release);
  MonitorPhysicalRect live;""",
 r"""  const bool windowMode = capture.windowModeActive.load(std::memory_order_acquire);
  const HMONITOR monitor = (!windowMode && capture.monitorInfo.has_value()) ? capture.monitorInfo->monitor : nullptr;
  {
    // The control thread's one-record view of the capture target (P9): version moves only when
    // something changed, so a dispatch that straddles a target switch can tell.
    std::lock_guard<std::mutex> lk(capture.inputTargetMu);
    const uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(monitor));
    const uint64_t gen = capture.streamGenerationState.load(std::memory_order_acquire);
    if (capture.inputTarget.windowMode != windowMode || capture.inputTarget.monitorHandle != handle ||
        capture.inputTarget.streamGeneration != gen) {
      capture.inputTarget.windowMode = windowMode;
      capture.inputTarget.monitorHandle = handle;
      capture.inputTarget.streamGeneration = gen;
      ++capture.inputTarget.version;
    }
  }
  MonitorPhysicalRect live;"""),
(r"""bool secure_target_rect_ready(CaptureState& capture, InputRouterState& inputRouter) {
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
}""",
 r"""namespace {

SecureDispatchOutcome secure_dispatch_impl(CaptureState& capture, InputRouterState& inputRouter,
                                           const std::function<bool(const InputTargetRect&)>& send) {
  SecureDispatchPorts ports;
  ports.snapshot = [&capture]() {
    std::lock_guard<std::mutex> lk(capture.inputTargetMu);
    InputTargetSnapshotView v;
    v.version = capture.inputTarget.version;
    v.windowMode = capture.inputTarget.windowMode;
    v.monitorHandle = capture.inputTarget.monitorHandle;
    v.streamGeneration = capture.inputTarget.streamGeneration;
    return v;
  };
  ports.queryMonitor = [](uint64_t handle, MonitorPhysicalRect* out) {
    return query_monitor_physical_rect(reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(handle)), out);
  };
  ports.send = send;
  const SecureDispatchResult r = secure_dispatch(ports);
  SecureDispatchOutcome out;
  out.sent = r.sent;
  out.writeFailed = r.writeFailed;
  out.why = r.why;
  if (r.sent) {
    if (!input_target_rect_same(inputRouter.lastDispatchedRect, r.rect)) {
      inputRouter.secureRectUpdatedAtDispatch.fetch_add(1, std::memory_order_relaxed);
      std::cout << "[native-video-host] secure-input target rect=" << describe_input_target_rect(r.rect)
                << " reason=dispatch gen=" << r.streamGeneration << " (was "
                << describe_input_target_rect(inputRouter.lastDispatchedRect) << ")\n";
      inputRouter.lastDispatchedRect = r.rect;
    }
  } else if (!r.writeFailed) {
    inputRouter.secureSkipRectUnknown.fetch_add(1, std::memory_order_relaxed);
    static std::atomic<uint64_t> lastLogUs{0};
    const uint64_t nowUs = qpc_now_us();
    const uint64_t last = lastLogUs.load(std::memory_order_relaxed);
    if (nowUs - last > 1'000'000) {
      lastLogUs.store(nowUs, std::memory_order_relaxed);
      std::cout << "[native-video-host] secure-input event refused why=" << r.why
                << " (rect not knowable for this event; not injecting at an unknown place)\n";
    }
  }
  return out;
}

}  // namespace

SecureDispatchOutcome secure_dispatch_event(CaptureState& capture, InputRouterState& inputRouter,
                                            const ControlInputEventMessage& input, uint32_t domainW, uint32_t domainH) {
  return secure_dispatch_impl(capture, inputRouter, [&](const InputTargetRect& rect) {
    return inputRouter.broker.SendInputEventWithRect(input, domainW, domainH, rect.originX, rect.originY, rect.width,
                                                     rect.height);
  });
}

SecureDispatchOutcome secure_dispatch_text(CaptureState& capture, InputRouterState& inputRouter,
                                           const ControlInputTextMessage& text, uint32_t domainW, uint32_t domainH) {
  return secure_dispatch_impl(capture, inputRouter, [&](const InputTargetRect& rect) {
    return inputRouter.broker.SendInputTextWithRect(text, domainW, domainH, rect.originX, rect.originY, rect.width,
                                                    rect.height);
  });
}"""),
])
edit(S + r'\host_control_session.cpp', [
(r"""            inputRouter.secureSkipUnauthenticated.fetch_add(1, std::memory_order_relaxed);
          } else if (!secure_target_rect_ready(capture, inputRouter)) {
            // The captured monitor's rect is not knowable right now (P9): do not aim at the
            // previous rect or at the whole virtual screen. Counted; the next event re-checks.
            inputRouter.secureSkipRectUnknown.fetch_add(1, std::memory_order_relaxed);
          } else if (!inputRouter.broker.SendInputEvent(input, domainW, domainH)) {
            inputRouter.secureBrokerFailed.fetch_add(1, std::memory_order_relaxed);
          } else {
            inputRouter.secureDelivered.fetch_add(1, std::memory_order_relaxed);
            routedToAgent = true;
          }""",
 r"""            inputRouter.secureSkipUnauthenticated.fetch_add(1, std::memory_order_relaxed);
          } else {
            // The one dispatch path (P9): the rect is verified for this event and stamped into
            // it; a refusal (rect not knowable, target changed) is counted inside.
            const SecureDispatchOutcome d = secure_dispatch_event(capture, inputRouter, input, domainW, domainH);
            if (d.sent) {
              inputRouter.secureDelivered.fetch_add(1, std::memory_order_relaxed);
              routedToAgent = true;
            } else if (d.writeFailed) {
              inputRouter.secureBrokerFailed.fetch_add(1, std::memory_order_relaxed);
            }
          }"""),
(r"""              if (desktopMode &&
                  clientSession.directoryAuthenticated.load(std::memory_order_acquire) &&
                  inputRouter.broker.SendInputEvent(input, domainW, domainH)) {""",
 r"""              if (desktopMode &&
                  clientSession.directoryAuthenticated.load(std::memory_order_acquire) &&
                  secure_dispatch_event(capture, inputRouter, input, domainW, domainH).sent) {"""),
(r"""                inputRouter.defaultBrokerFallback.fetch_add(1, std::memory_order_relaxed);
                if (inputRouter.broker.SendInputEvent(input, domainW, domainH)) {""",
 r"""                inputRouter.defaultBrokerFallback.fetch_add(1, std::memory_order_relaxed);
                if (secure_dispatch_event(capture, inputRouter, input, domainW, domainH).sent) {"""),
(r"""        if (desktopMode && clientSession.directoryAuthenticated.load(std::memory_order_acquire) &&
            !interactive_desktop_is_default() &&
            secure_target_rect_ready(capture, inputRouter) &&
            inputRouter.broker.SendInputText(text,
                                            inputRouter.domainW.load(std::memory_order_acquire),
                                            inputRouter.domainH.load(std::memory_order_acquire))) {""",
 r"""        if (desktopMode && clientSession.directoryAuthenticated.load(std::memory_order_acquire) &&
            !interactive_desktop_is_default() &&
            secure_dispatch_text(capture, inputRouter, text,
                                 inputRouter.domainW.load(std::memory_order_acquire),
                                 inputRouter.domainH.load(std::memory_order_acquire)).sent) {"""),
])
print('step30 edits ok')
