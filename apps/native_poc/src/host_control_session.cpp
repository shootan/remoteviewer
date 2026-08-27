// See host_control_session.hpp for the module summary. Serve() below is the former
// serve_control_session lambda of native_video_host_main.cpp, moved verbatim (host split refactor
// Phase 2-2 step 1); step 2 splits it into one Handle* per message type.

#include <winsock2.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "host_bgra_scale.hpp"
#include "host_capture_device.hpp"
#include "host_control_session.hpp"
#include "host_input_inject.hpp"
#include "host_net_io.hpp"
#include "host_string_util.hpp"
#include "host_window_enum.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

namespace {

// Closes the outbound message when the dispatch arm returns, however it returns.
struct FlushControlMessageOnExit {
  ControlLink* link = nullptr;
  ~FlushControlMessageOnExit() {
    if (link) (void)link->EndMessage();
  }
};

}  // namespace

ControlSessionServer::ControlSessionServer(const Args& args, std::atomic<bool>& stop,
                                           SessionState& clientSession, CaptureState& capture,
                                           ClientMetricsSnapshot& clientMetrics, EncoderState& encoder,
                                           InputRouterState& inputRouter, DesktopBackendState& backend,
                                           WindowSelectionTxn& windowSelectionTxn)
    : args(args),
      stop(stop),
      clientSession(clientSession),
      capture(capture),
      clientMetrics(clientMetrics),
      encoder(encoder),
      inputRouter(inputRouter),
      backend(backend),
      windowSelectionTxn(windowSelectionTxn) {}

void ControlSessionServer::Serve(ControlLink& link) {
  // A new session starts with the stream on, exactly like the first client of a fresh
  // process. The previous session's disconnect turned it off, and a client that never
  // sends stream-state (the Windows client) would otherwise stare at a black screen
  // forever after any reconnect. Clients that manage the state explicitly still can.
  if (!clientSession.streamControlActive.exchange(true, std::memory_order_acq_rel)) {
    std::cout << "[native-video-host][control] stream restored for new session\n";
  }
  auto send_window_list = [&](uint32_t seq) -> bool {
    ControlWindowListMessage rsp{};
    rsp.header.magic = remote60::native_poc::kMagic;
    rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowList);
    rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
    rsp.seq = seq;
    if (capture.windowSelectionLocked.load(std::memory_order_relaxed)) {
      rsp.flags |= remote60::native_poc::kControlWindowListFlagSelectionLocked;
    }
    // Tells the client it may ask for previews; older hosts leave this clear and
    // older clients ignore the bit, so both directions stay compatible.
    rsp.flags |= remote60::native_poc::kControlWindowListFlagThumbnails;
    // Says the monitor messages exist here. A client that asked an older host would wait for a
    // reply that never comes, since unknown opcodes are drained silently.
    rsp.flags |= remote60::native_poc::kControlWindowListFlagMonitors;
    rsp.selectedWindowId = capture.selectedWindowId.load(std::memory_order_relaxed);
    const auto windows = enumerate_shareable_windows();
    rsp.itemCount = std::min<uint32_t>(
        static_cast<uint32_t>(windows.size()), remote60::native_poc::kControlWindowListMaxEntries);
    for (uint32_t i = 0; i < rsp.itemCount; ++i) {
      const auto& src = windows[i];
      auto& dst = rsp.items[i];
      dst.id = src.id;
      dst.pid = src.pid;
      dst.width = static_cast<uint32_t>(std::max<int>(0, src.width));
      dst.height = static_cast<uint32_t>(std::max<int>(0, src.height));
      if (src.minimized) dst.flags |= 0x1u;
      std::snprintf(dst.title, sizeof(dst.title), "%s", src.title.c_str());
    }
    std::cout << "[native-video-host][control] window-list seq=" << seq
              << " count=" << rsp.itemCount
              << " selectedId=" << rsp.selectedWindowId
              << "\n";
    return link.Write(&rsp, sizeof(rsp));
  };
  auto send_monitor_list = [&](uint32_t seq) -> bool {
    ControlMonitorListMessage rsp{};
    rsp.header.magic = remote60::native_poc::kMagic;
    rsp.header.type = static_cast<uint16_t>(MessageType::ControlMonitorList);
    rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
    rsp.seq = seq;
    rsp.selectedMonitorId = capture.selectedMonitorId.load(std::memory_order_acquire);
    const auto monitors = enumerate_monitors();
    rsp.itemCount = std::min<uint32_t>(static_cast<uint32_t>(monitors.size()),
                                       remote60::native_poc::kControlMonitorListMaxEntries);
    for (uint32_t i = 0; i < rsp.itemCount; ++i) {
      const auto& src = monitors[i];
      auto& dst = rsp.items[i];
      dst.id = i;
      dst.x = src.x;
      dst.y = src.y;
      dst.width = src.width;
      dst.height = src.height;
      if (src.primary) dst.flags |= remote60::native_poc::kControlMonitorFlagPrimary;
      std::snprintf(dst.name, sizeof(dst.name), "%s", src.name.c_str());
    }
    std::cout << "[native-video-host][control] monitor-list seq=" << seq
              << " count=" << rsp.itemCount << " selectedId=" << rsp.selectedMonitorId << "\n";
    return link.Write(&rsp, sizeof(rsp));
  };
  auto send_window_thumbnail =
      [&](const ControlWindowThumbnailRequestMessage& req) -> bool {
    const uint32_t maxW = std::clamp<uint32_t>(
        req.maxWidth == 0 ? 256u : req.maxWidth, 16u,
        remote60::native_poc::kWindowThumbnailMaxWidth);
    const uint32_t maxH = std::clamp<uint32_t>(
        req.maxHeight == 0 ? 160u : req.maxHeight, 16u,
        remote60::native_poc::kWindowThumbnailMaxHeight);

    std::vector<uint8_t> bgra;
    uint32_t tw = 0;
    uint32_t th = 0;
    bool ok = false;
    if (req.windowId == 0) {
      ok = capture_window_thumbnail(nullptr, maxW, maxH, &bgra, &tw, &th);
    } else {
      HWND hwnd = window_id_to_hwnd(req.windowId);
      if (should_include_window(hwnd)) {
        ok = capture_window_thumbnail(hwnd, maxW, maxH, &bgra, &tw, &th);
      }
    }
    if (bgra.size() > remote60::native_poc::kWindowThumbnailMaxPayloadBytes) {
      ok = false;
    }

    ControlWindowThumbnailHeader rsp{};
    rsp.header.magic = remote60::native_poc::kMagic;
    rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowThumbnail);
    rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
    rsp.seq = req.seq;
    rsp.windowId = req.windowId;
    if (ok) {
      rsp.flags |= 0x1u;
      rsp.width = tw;
      rsp.height = th;
      rsp.stride = tw * 4u;
      rsp.payloadSize = static_cast<uint32_t>(bgra.size());
      rsp.version = qpc_now_us();
    }
    if (!link.Write(&rsp, sizeof(rsp))) return false;
    if (rsp.payloadSize == 0) return true;
    return link.Write(bgra.data(), bgra.size());
  };
  auto send_input_ack = [&](uint32_t seq) -> bool {
    ControlInputAckMessage ack{};
    ack.header.magic = remote60::native_poc::kMagic;
    ack.header.type = static_cast<uint16_t>(MessageType::ControlInputAck);
    ack.header.size = static_cast<uint16_t>(sizeof(ack));
    ack.seq = seq;
    ack.hostRecvQpcUs = qpc_now_us();
    ack.hostSendQpcUs = qpc_now_us();
    return link.Write(&ack, sizeof(ack));
  };

  while (!stop.load()) {
    MessageHeader header{};
    if (!link.Read(&header, sizeof(header))) break;
    // Marks the response boundary the UDP transport needs; a no-op over TCP.
    const FlushControlMessageOnExit flushResponse{&link};
    if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
    const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
    const auto type = static_cast<MessageType>(header.type);

    if (type == MessageType::ControlPing && header.size == sizeof(ControlPingMessage)) {
      ControlPingMessage ping{};
      ping.header = header;
      if (!link.Read(&ping.seq, sizeof(ping) - sizeof(MessageHeader))) break;
      ControlPongMessage pong{};
      pong.header.magic = remote60::native_poc::kMagic;
      pong.header.type = static_cast<uint16_t>(MessageType::ControlPong);
      pong.header.size = static_cast<uint16_t>(sizeof(pong));
      pong.seq = ping.seq;
      pong.clientSendQpcUs = ping.clientSendQpcUs;
      pong.hostRecvQpcUs = qpc_now_us();
      pong.hostSendQpcUs = qpc_now_us();
      // One snapshot, so the pid / hwnd / process / title the viewer receives all describe the
      // same instant. (Phase 4: SnapshotTarget.)
      const CaptureTargetSnapshot target = capture.SnapshotTarget();
      pong.captureTargetPid = target.pid;
      pong.captureTargetFlags = target.flags;
      // The probe is cached for 250ms, so asking it per ping is cheap. Telling the viewer that
      // a security prompt is up is the difference between an explained pause and an apparent
      // freeze, and it costs one bit in a word that is already on the wire.
      if (!interactive_desktop_is_default()) {
        pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagSecureDesktopActive;
      }
      pong.captureRebindCount = target.rebindCount;
      pong.captureTargetHwnd = target.targetHwnd;
      std::snprintf(pong.captureTargetProcess, sizeof(pong.captureTargetProcess), "%s",
                    target.process.c_str());
      std::snprintf(pong.captureTargetTitle, sizeof(pong.captureTargetTitle), "%s",
                    target.title.c_str());
      if (!link.Write(&pong, sizeof(pong))) break;
      continue;
    }

    if (type == MessageType::ControlInputEvent && header.size == sizeof(ControlInputEventMessage)) {
      ControlInputEventMessage input{};
      input.header = header;
      if (!link.Read(&input.seq, sizeof(input) - sizeof(MessageHeader))) break;
      std::string resolvedTarget;
      if (inputRouter.injectionEnabled) {
        const bool desktopMode =
            !inputRouter.targetCriteria.enabled() &&
            (capture.selectedWindowId.load(std::memory_order_acquire) == 0);
        const uint32_t domainW = inputRouter.domainW.load(std::memory_order_acquire);
        const uint32_t domainH = inputRouter.domainH.load(std::memory_order_acquire);
        InputInjectResult injectResult = InputInjectResult::Failed;
        // Prefer the SYSTEM agent, which is the only way into elevated windows and the lock
        // screen -- but fall back when it is unavailable. The service is registered by the
        // installer, so a host running from a build tree (or before installation) has no
        // broker at all, and treating that as a hard failure left the session with no input
        // whatsoever instead of the ordinary desktop injection that still works fine.
        //
        // The conjuncts are evaluated separately so a failure can name itself. Clicks that go
        // nowhere on a consent prompt look identical to clicks that work: the fallback path
        // below reports Injected either way, because SendInput on the Default desktop succeeds
        // whether or not anything is there to receive it. Counting which branch ran, and why,
        // is the difference between "input is broken" and a specific cause.
        const bool secureDesktopActive = !interactive_desktop_is_default();
        bool routedToAgent = false;
        if (secureDesktopActive) {
          inputRouter.secureAttempts.fetch_add(1, std::memory_order_relaxed);
          if (!desktopMode) {
            inputRouter.secureSkipWindowMode.fetch_add(1, std::memory_order_relaxed);
          } else if (!clientSession.directoryAuthenticated.load(std::memory_order_acquire)) {
            // A plain-LAN session has no capability token, and the agent will not act without
            // one. Nothing about the click is wrong; it simply cannot be authorised.
            inputRouter.secureSkipUnauthenticated.fetch_add(1, std::memory_order_relaxed);
          } else if (!inputRouter.broker.SendInputEvent(input, domainW, domainH)) {
            inputRouter.secureBrokerFailed.fetch_add(1, std::memory_order_relaxed);
          } else {
            inputRouter.secureDelivered.fetch_add(1, std::memory_order_relaxed);
            routedToAgent = true;
          }
        }
        // Set once the outcome has already been tallied (the secure-desktop path), so the result
        // switch below does not also count it -- e.g. as an inject failure.
        bool injectAccounted = false;
        if (routedToAgent) {
          injectResult = InputInjectResult::Injected;
          resolvedTarget = " secure-system-agent";
        } else if (secureDesktopActive) {
          // The cached check says the desktop is secure and the broker path did not route (window
          // mode, unauthenticated, or broker failure -- all already tallied). Ordinary SendInput on
          // a secure desktop would just fail, so do not fall through to it, and do not double-count
          // the miss as an inject failure.
          injectAccounted = true;
        } else {
          InputFailStage directFailStage = InputFailStage::None;
          DWORD directFailError = 0;
          injectResult =
              inject_background_input_event(input, inputRouter.targetCriteria, capture.targetHwnd,
                                            desktopMode, domainW, domainH,
                                            &inputRouter.desktopState, &resolvedTarget,
                                            &directFailStage, &directFailError);
          if (injectResult == InputInjectResult::Failed) {
            switch (directFailStage) {
              case InputFailStage::SetCursorPos:
                inputRouter.failSetCursorPos.fetch_add(1, std::memory_order_relaxed);
                break;
              case InputFailStage::SendInputMouse:
                inputRouter.failSendInputMouse.fetch_add(1, std::memory_order_relaxed);
                break;
              case InputFailStage::SendInputKey:
                inputRouter.failSendInputKey.fetch_add(1, std::memory_order_relaxed);
                break;
              case InputFailStage::PostMessage:
                inputRouter.failPostMessage.fetch_add(1, std::memory_order_relaxed);
                break;
              default:
                break;
            }
            // Stage travels with the target description so the existing inject-fail line needs
            // no format change downstream tooling would have to relearn.
            resolvedTarget += std::string(" stage=") + input_fail_stage_name(directFailStage) +
                              " err=" + std::to_string(directFailError);
          }
          if (injectResult == InputInjectResult::Failed) {
            // The cached check said default but injection failed: the 250ms cache may be stale
            // because a UAC prompt or lock screen rose since the last refresh. Pay for ONE uncached
            // probe on this specific failing event (never per event -- that would be far too costly
            // on a 100+/s pointer stream) to find out which it is.
            if (!interactive_desktop_is_default_uncached()) {
              inputRouter.freshProbeSecure.fetch_add(1, std::memory_order_relaxed);
              // Actually secure now. Retry THIS event through the SYSTEM broker exactly once.
              if (desktopMode &&
                  clientSession.directoryAuthenticated.load(std::memory_order_acquire) &&
                  inputRouter.broker.SendInputEvent(input, domainW, domainH)) {
                injectResult = InputInjectResult::Injected;
                resolvedTarget = " secure-system-agent(reprobe)";
                inputRouter.freshProbeReroute.fetch_add(1, std::memory_order_relaxed);
                inputRouter.secureDelivered.fetch_add(1, std::memory_order_relaxed);
              }
              // else: genuinely secure but not broker-eligible (window mode / unauthenticated) or
              // the broker failed -- stays Failed, but inputRouter.freshProbeSecure distinguishes it from
              // a real default-desktop failure below.
            } else {
              // Uncached re-probe still says default: a genuine failure on the interactive desktop
              // (UIPI, no target, a transient block). Counted separately so it is not confused
              // with the stale-cache case.
              inputRouter.injectFailDefault.fetch_add(1, std::memory_order_relaxed);
              // Field case (14:51 freeze): direct default-desktop injection FAILED at
              // SetCursorPos while the target resolved to a CoreWindow. SetCursorPos failing only
              // tells us one of its required conditions was unmet -- the host thread MAY lack the
              // current-input-desktop association or the window-station access it needs; the new
              // stage/error plus the agent log pin which on the next repro. The SYSTEM agent
              // reattaches to the current input desktop (SetThreadDesktop) before
              // SetCursorPos+SendInput, so retry this one event through it. Only stages that are
              // an actual OS injection API failure are worth retrying -- a coordinate/mapping
              // error (MapPoint) would just repeat in the agent.
              const bool brokerRetryableStage =
                  directFailStage == InputFailStage::SetCursorPos ||
                  directFailStage == InputFailStage::SendInputMouse ||
                  directFailStage == InputFailStage::SendInputKey;
              if (brokerRetryableStage && desktopMode &&
                  clientSession.directoryAuthenticated.load(std::memory_order_acquire)) {
                inputRouter.defaultBrokerFallback.fetch_add(1, std::memory_order_relaxed);
                if (inputRouter.broker.SendInputEvent(input, domainW, domainH)) {
                  // Queued to the agent, not confirmed landed (the broker does not ACK). Mark
                  // Injected so the host stops re-reporting inject-fail, but the honest signal
                  // is inputRouter.defaultBrokerQueued + the service log, not this result.
                  injectResult = InputInjectResult::Injected;
                  resolvedTarget += " default-broker-fallback(queued)";
                  inputRouter.defaultBrokerQueued.fetch_add(1, std::memory_order_relaxed);
                } else {
                  inputRouter.defaultBrokerPipeFail.fetch_add(1, std::memory_order_relaxed);
                }
              }
            }
          }
        }
        if (injectAccounted) {
          // Already tallied on the secure path; nothing more to record.
        } else if (injectResult == InputInjectResult::Injected) {
          const uint64_t n = inputRouter.events.fetch_add(1) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input] injected seq=" << input.seq
                      << " kind=" << input.kind
                      << " x=" << input.x
                      << " y=" << input.y
                      << " buttons=" << input.buttons
                      << " key=" << input.keyCode
                      << " mode=" << (desktopMode ? "desktop" : "window")
                      << resolvedTarget
                      << "\n";
          }
        } else if (injectResult == InputInjectResult::IgnoredMove) {
          inputRouter.ignoredMove.fetch_add(1, std::memory_order_relaxed);
        } else if (injectResult == InputInjectResult::NoTarget) {
          const uint64_t n = inputRouter.noTarget.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input] no-target seq=" << input.seq
                      << " kind=" << input.kind
                      << " filterPid=" << args.inputTargetPid
                      << " filterProc=" << trim_ascii(args.inputTargetProcess)
                      << " filterTitle=" << trim_ascii(args.inputTargetTitle)
                      << resolvedTarget
                      << "\n";
          }
        } else if (injectResult == InputInjectResult::Unsupported) {
          const uint64_t n = inputRouter.unsupported.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input] unsupported seq=" << input.seq
                      << " kind=" << input.kind
                      << " key=" << input.keyCode
                      << "\n";
          }
        } else {
          const uint64_t n = inputRouter.injectFail.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input] inject-fail seq=" << input.seq
                      << " kind=" << input.kind
                      << " key=" << input.keyCode
                      << resolvedTarget
                      << "\n";
          }
        }
      } else if (args.inputLogEvery > 0 && (input.seq % args.inputLogEvery) == 0) {
        std::cout << "[native-video-host][input] blocked seq=" << input.seq
                  << " key=" << input.keyCode
                  << " kind=" << input.kind
                  << "\n";
      }
      if (!send_input_ack(input.seq)) break;
      continue;
    }

    if (type == MessageType::ControlInputText && header.size == sizeof(ControlInputTextMessage)) {
      ControlInputTextMessage text{};
      text.header = header;
      if (!link.Read(&text.seq, sizeof(text) - sizeof(MessageHeader))) break;
      std::string resolvedTarget;
      if (inputRouter.injectionEnabled) {
        const bool desktopMode =
            !inputRouter.targetCriteria.enabled() &&
            (capture.selectedWindowId.load(std::memory_order_acquire) == 0);
        InputInjectResult injectResult = InputInjectResult::Failed;
        if (desktopMode && clientSession.directoryAuthenticated.load(std::memory_order_acquire) &&
            !interactive_desktop_is_default() &&
            inputRouter.broker.SendInputText(text,
                                            inputRouter.domainW.load(std::memory_order_acquire),
                                            inputRouter.domainH.load(std::memory_order_acquire))) {
          injectResult = InputInjectResult::Injected;
          resolvedTarget = " secure-system-agent";
        } else {
          injectResult = apply_input_text_message(text, capture.targetHwnd, desktopMode,
                                                  &inputRouter.desktopState, &resolvedTarget);
        }
        if (injectResult == InputInjectResult::Injected) {
          const uint64_t n = inputRouter.events.fetch_add(1) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input-text] injected seq=" << text.seq
                      << " utf16Count=" << text.utf16Count
                      << " mode=" << (desktopMode ? "desktop" : "window")
                      << resolvedTarget
                      << "\n";
          }
        } else if (injectResult == InputInjectResult::NoTarget) {
          const uint64_t n = inputRouter.noTarget.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input-text] no-target seq=" << text.seq
                      << " utf16Count=" << text.utf16Count
                      << resolvedTarget
                      << "\n";
          }
        } else if (injectResult == InputInjectResult::Unsupported) {
          const uint64_t n = inputRouter.unsupported.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input-text] unsupported seq=" << text.seq
                      << " utf16Count=" << text.utf16Count
                      << "\n";
          }
        } else {
          const uint64_t n = inputRouter.injectFail.fetch_add(1, std::memory_order_relaxed) + 1;
          if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
            std::cout << "[native-video-host][input-text] inject-fail seq=" << text.seq
                      << " utf16Count=" << text.utf16Count
                      << resolvedTarget
                      << "\n";
          }
        }
      }
      if (!send_input_ack(text.seq)) break;
      continue;
    }

    if (type == MessageType::ControlWindowListRequest &&
        header.size == sizeof(ControlWindowListRequestMessage)) {
      ControlWindowListRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (!send_window_list(req.seq)) break;
      continue;
    }

    if (type == MessageType::ControlMonitorListRequest &&
        header.size == sizeof(ControlMonitorListRequestMessage)) {
      ControlMonitorListRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (!send_monitor_list(req.seq)) break;
      continue;
    }

    if (type == MessageType::ControlMonitorSelect &&
        header.size == sizeof(ControlMonitorSelectMessage)) {
      ControlMonitorSelectMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      // Applied by the render loop, which owns the capture item; answered with the list so the
      // client sees the selection that actually took effect rather than the one it asked for.
      capture.monitorSelectRequested.store(req.monitorId, std::memory_order_release);
      capture.monitorSelectPending.store(true, std::memory_order_release);
      if (!send_monitor_list(req.seq)) break;
      continue;
    }

    if (type == MessageType::ControlWindowThumbnailRequest &&
        header.size == sizeof(ControlWindowThumbnailRequestMessage)) {
      ControlWindowThumbnailRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (!send_window_thumbnail(req)) break;
      continue;
    }

    if (type == MessageType::ControlWindowSelect &&
        header.size == sizeof(ControlWindowSelectMessage)) {
      ControlWindowSelectMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;

      ControlWindowSelectedMessage rsp{};
      rsp.header.magic = remote60::native_poc::kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowSelected);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      rsp.seq = req.seq;
      rsp.windowId = req.windowId;
      rsp.streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
      rsp.hostSendQpcUs = qpc_now_us();

      if (capture.windowSelectionLocked.load(std::memory_order_acquire)) {
        rsp.flags |= 0x2u;
        std::snprintf(rsp.reason, sizeof(rsp.reason), "%s", "selection_locked_by_config");
        if (req.windowId == 0) {
          std::snprintf(rsp.title, sizeof(rsp.title), "%s", "desktop");
        }
      } else {
        {
          std::lock_guard<std::mutex> lk(windowSelectionTxn.mu);
          windowSelectionTxn.pending = true;
          windowSelectionTxn.completed = false;
          windowSelectionTxn.reqSeq = req.seq;
          windowSelectionTxn.requestedWindowId = req.windowId;
          windowSelectionTxn.responseFlags = 0;
          windowSelectionTxn.responseWindowId = req.windowId;
          windowSelectionTxn.responseStreamGeneration = 0;
          windowSelectionTxn.responseReason.clear();
          windowSelectionTxn.responseTitle.clear();
        }
        windowSelectionTxn.cv.notify_all();

        std::unique_lock<std::mutex> lk(windowSelectionTxn.mu);
        // Also give up when the link dies. The client that asked for this selection may be the
        // one that just went away, and the next client cannot be served until this returns --
        // so waiting only for completion would hold the whole session handover behind a reply
        // nobody is left to read. Polled, because a rollover closes the channel rather than
        // touching this transaction.
        while (!stop.load() && !windowSelectionTxn.completed && link.Alive()) {
          windowSelectionTxn.cv.wait_for(lk, std::chrono::milliseconds(100));
        }
        rsp.flags = windowSelectionTxn.responseFlags;
        rsp.windowId = windowSelectionTxn.responseWindowId;
        rsp.streamGeneration = windowSelectionTxn.responseStreamGeneration;
        rsp.hostSendQpcUs = qpc_now_us();
        std::snprintf(rsp.reason, sizeof(rsp.reason), "%s", windowSelectionTxn.responseReason.c_str());
        std::snprintf(rsp.title, sizeof(rsp.title), "%s", windowSelectionTxn.responseTitle.c_str());
      }

      if (!link.Write(&rsp, sizeof(rsp))) break;
      continue;
    }

    if (type == MessageType::ControlClientMetrics &&
        header.size == sizeof(ControlClientMetricsMessage)) {
      ControlClientMetricsMessage metrics{};
      metrics.header = header;
      if (!link.Read(&metrics.seq, sizeof(metrics) - sizeof(MessageHeader))) break;
      // Published as one record: these describe a single instant on the viewer and the ABR/M9
      // decision reads them together. Storing them field by field let the main loop act on a mix
      // of two reports. (Phase 4: ClientMetricsSnapshot.)
      ViewerMetrics reported;
      reported.width = metrics.width;
      reported.height = metrics.height;
      reported.recvFpsX100 = metrics.recvFpsX100;
      reported.decodedFpsX100 = metrics.decodedFpsX100;
      reported.recvMbpsX1000 = metrics.recvMbpsX1000;
      reported.skippedFrames = metrics.skippedFrames;
      reported.avgLatencyUs = metrics.avgLatencyUs;
      reported.maxLatencyUs = metrics.maxLatencyUs;
      reported.avgDecodeTailUs = metrics.avgDecodeTailUs;
      reported.maxDecodeTailUs = metrics.maxDecodeTailUs;
      reported.congestionState = metrics.congestionState;
      reported.congestionTransitions = metrics.congestionTransitions;
      reported.congestionRecoveryCount = metrics.congestionRecoveryCount;
      reported.congestionRecoveryReq = metrics.congestionRecoveryReq;
      reported.congestionRecoveryMaxUs = metrics.congestionRecoveryMaxUs;
      reported.queueDepthMax = metrics.queueDepthMax;
      reported.queueDepthH4p = metrics.queueDepthH4p;
      reported.udpAssemblyDropPm = metrics.udpAssemblyDropPm;
      reported.updatedUs = qpc_now_us();
      clientMetrics.Publish(reported);
      // Logged as it arrives rather than folded into the per-second stat line: this is the
      // only view the host gets of what the remote display is actually doing, and a viewer
      // reporting stutter needs it visible without attaching to the device.
      if (metrics.presentSampleCount > 0) {
        std::cout << "[native-video-host][client-present]"
                  << " fps=" << (metrics.presentFpsX100 / 100.0)
                  << " targetUs=" << metrics.presentTargetIntervalUs
                  << " gapP50Us=" << metrics.presentGapP50Us
                  << " gapP95Us=" << metrics.presentGapP95Us
                  << " gapMaxUs=" << metrics.presentGapMaxUs
                  << " over1_5x=" << metrics.presentOver1_5xCount
                  << " over2x=" << metrics.presentOver2xCount
                  << " samples=" << metrics.presentSampleCount
                  << " sched=" << metrics.presentScheduledCount
                  << " immediate=" << metrics.presentImmediateCount
                  << " reanchor=" << metrics.presentReanchorCount
                  << " displayed=" << metrics.presentDisplayedCount
                  << "\n";
      }
      continue;
    }

    if (type == MessageType::ControlRequestKeyFrame &&
        header.size == sizeof(ControlRequestKeyFrameMessage)) {
      ControlRequestKeyFrameMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      const uint64_t nowUs = qpc_now_us();
      // Refill + minimum-interval check + consume, as one transaction under encoder.keyReqMu.
      // These three fields are plain (a double and two uint64s) and the main loop resets them on
      // reconnect, so doing this open-coded here was a data race on the throttle that decides how
      // often the stream is forced to an IDR. (Ledger H-04.)
      double keyReqTokensNow = 0.0;
      if (encoder.TryTakeKeyRequestToken(nowUs, &keyReqTokensNow)) {
        clientMetrics.requestedKeyFrame = true;
        clientMetrics.keyFrameReason = req.reason;
        const uint64_t reqCount = clientMetrics.keyFrameRequestCount.fetch_add(1) + 1;
        std::cout << "[native-video-host][control] keyframe-request seq=" << req.seq
                  << " reason=" << req.reason
                  << " total=" << reqCount
                  << "\n";
      } else {
        const uint64_t dropCount = clientMetrics.keyFrameRequestDropped.fetch_add(1) + 1;
        if ((dropCount % 60) == 1) {
          std::cout << "[native-video-host][control] keyframe-request-throttled seq=" << req.seq
                    << " reason=" << req.reason
                    << " dropped=" << dropCount
                    << " tokens=" << keyReqTokensNow
                    << "\n";
        }
      }
      continue;
    }

    if (type == MessageType::ControlRuntimeEncoderConfig &&
        header.size == sizeof(ControlRuntimeEncoderConfigMessage)) {
      ControlRuntimeEncoderConfigMessage tune{};
      tune.header = header;
      if (!link.Read(&tune.seq, sizeof(tune) - sizeof(MessageHeader))) break;
      const bool hasBitrate = ((tune.flags & 0x1u) != 0) && tune.bitrate >= 100000;
      const bool hasKeyint = ((tune.flags & 0x2u) != 0) && tune.keyint >= 1;
      const bool hasFps = ((tune.flags & 0x4u) != 0) && tune.fps >= 1;
      if (hasBitrate || hasKeyint || hasFps) {
        if (hasBitrate) encoder.tuneBitrate.store(tune.bitrate, std::memory_order_release);
        if (hasKeyint) encoder.tuneKeyint.store(tune.keyint, std::memory_order_release);
        if (hasFps) encoder.tuneFps.store(tune.fps, std::memory_order_release);
        encoder.tuneSeq.store(tune.seq, std::memory_order_release);
        encoder.tunePending.store(true, std::memory_order_release);
        std::cout << "[native-video-host][control] runtime-config seq=" << tune.seq
                  << " bitrate=" << (hasBitrate ? tune.bitrate : 0)
                  << " keyint=" << (hasKeyint ? tune.keyint : 0)
                  << " fps=" << (hasFps ? tune.fps : 0)
                  << " flags=" << tune.flags
                  << "\n";
      }
      continue;
    }

    if (type == MessageType::ControlDesktopBackendRequest &&
        header.size == sizeof(ControlDesktopBackendRequestMessage)) {
      ControlDesktopBackendRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (req.backend == 1 || req.backend == 2 || req.backend == 3) {
        backend.reqSeq.store(req.seq, std::memory_order_release);
        backend.reqValue.store(req.backend, std::memory_order_release);
        backend.reqPending.store(true, std::memory_order_release);
        std::cout << "[native-video-host][control] desktop-backend-request seq=" << req.seq
                  << " backend="
                  << (req.backend == 2 ? "wgc" : (req.backend == 3 ? "gdi" : "dxgi"))
                  << "\n";
      }
      continue;
    }

    if (type == MessageType::ControlStreamState &&
        header.size == sizeof(ControlStreamStateMessage)) {
      ControlStreamStateMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      const bool active = ((req.flags & 0x1u) != 0);
      clientSession.streamControlActive.store(active, std::memory_order_release);
      std::cout << "[native-video-host][control] stream-state seq=" << req.seq
                << " active=" << (active ? 1 : 0)
                << "\n";
      continue;
    }

    if (type == MessageType::ControlCaptureModeRequest &&
        header.size == sizeof(ControlCaptureModeRequestMessage)) {
      ControlCaptureModeRequestMessage req{};
      req.header = header;
      if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
      if (req.mode == 1 || req.mode == 2) {
        capture.modeReqSeq.store(req.seq, std::memory_order_release);
        capture.modeReqMode.store(req.mode, std::memory_order_release);
        capture.modeReqXPermille.store(std::min<uint32_t>(10000u, req.xPermille), std::memory_order_release);
        capture.modeReqYPermille.store(std::min<uint32_t>(10000u, req.yPermille), std::memory_order_release);
        capture.modeReqPending.store(true, std::memory_order_release);
        std::cout << "[native-video-host][control] capture-mode-request seq=" << req.seq
                  << " mode=" << req.mode
                  << " xPermille=" << req.xPermille
                  << " yPermille=" << req.yPermille
                  << "\n";
      }
      continue;
    }

    if (bodySize > 0 && !link.Discard(bodySize)) break;
  }
  clientSession.streamControlActive.store(false, std::memory_order_release);
}

}  // namespace remote60::native_poc
