// See viewer_control_client.hpp. Bodies are the controlThread lambda of native_video_client_main.cpp,
// verbatim (viewer split refactor Phase 2-4).

#include "viewer_control_client.hpp"

#include <iostream>
#include <memory>
#include <sstream>

#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_picker.hpp"

namespace remote60::native_poc::viewer {

int ControlClient::fetch_one_thumbnail(remote60::native_poc::ControlLink& link) {
  // Routed through the ControlLink, not the raw socket, so a directory session (control
  // tunnelled over the punched UDP socket) fetches previews too -- modelled on the
  // Android ClientSessionController::FetchOneThumbnailLocked. One card per idle action
  // keeps the strict request/response loop from being starved. Only invoked when the
  // host advertised the capability, because an older host would drain the request and
  // never reply. Returns: 1 fetched, 0 nothing to do, -1 link failure (stream desynced).
  if (!gPicker.hostSupportsThumbnails.load(std::memory_order_relaxed)) return 0;
  uint64_t id = 0;
  {
    std::lock_guard<std::mutex> lk(gPicker.thumbMu);
    if (gPicker.thumbFetchQueue.empty()) return 0;
    id = gPicker.thumbFetchQueue.front();
    gPicker.thumbFetchQueue.pop_front();
  }
  remote60::native_poc::ControlWindowThumbnailRequestMessage req{};
  req.header.magic = remote60::native_poc::kMagic;
  req.header.type =
      static_cast<uint16_t>(MessageType::ControlWindowThumbnailRequest);
  req.header.size = static_cast<uint16_t>(sizeof(req));
  req.seq = 0;
  req.windowId = id;
  req.maxWidth = 256;
  req.maxHeight = 160;
  req.clientSendQpcUs = qpc_now_us();
  // One request is one message; EndMessage() draws the boundary UDP needs and TCP ignores.
  if (!link.Write(&req, sizeof(req)) || !link.EndMessage()) return -1;
  remote60::native_poc::ControlWindowThumbnailHeader rsp{};
  if (!link.Read(&rsp, sizeof(rsp))) return -1;
  if (rsp.header.magic != remote60::native_poc::kMagic ||
      rsp.header.type != static_cast<uint16_t>(MessageType::ControlWindowThumbnail) ||
      rsp.payloadSize > remote60::native_poc::kWindowThumbnailMaxPayloadBytes) {
    return -1;
  }
  std::vector<uint8_t> payload(rsp.payloadSize);
  if (rsp.payloadSize > 0 && !link.Read(payload.data(), payload.size())) {
    return -1;
  }
  if ((rsp.flags & 0x1u) != 0 && rsp.width > 0 && rsp.height > 0 &&
      payload.size() == static_cast<size_t>(rsp.width) * rsp.height * 4u) {
    auto thumb = std::make_shared<WindowThumb>();
    thumb->width = rsp.width;
    thumb->height = rsp.height;
    thumb->bgra = std::move(payload);
    thumb->fetchedUs = qpc_now_us();
    {
      std::lock_guard<std::mutex> lk(gPicker.thumbMu);
      gPicker.thumbs[id] = std::move(thumb);
    }
    // Outside the lock: the paint handler takes gPicker.thumbMu, and invalidating while
    // holding it invited a stall on every received preview.
    InvalidateRect(gSession.hwnd, nullptr, FALSE);
  }
  return 1;
}

void ControlClient::handle_pong(const ControlOutboundAction& action, const ControlPongMessage& pong) {
  const uint64_t doneUs = qpc_now_us();
  gControl.scheduler.OnPingCompleted(doneUs);
  {
    // Say it once per transition rather than every ping. A frozen picture with no
    // explanation is the worst version of this; a line saying a Windows security
    // prompt is on screen turns it into something the operator can act on.
    const bool secure =
        (pong.captureTargetFlags &
         remote60::native_poc::kCaptureFlagSecureDesktopActive) != 0;
    if (secure != gControl.reportedSecure) {
      gControl.reportedSecure = secure;
      std::cout << "[native-video-client] secure-desktop-active="
                << (secure ? 1 : 0)
                << (secure ? "  (a Windows security prompt is on screen; it "
                             "cannot be captured, so the picture is paused)"
                           : "  (picture resumes)")
                << std::endl;
    }
  }
  const uint64_t rttUs =
      (doneUs >= action.ping.clientSendQpcUs) ? (doneUs - action.ping.clientSendQpcUs) : 0;
  std::cout << "[native-video-client][control] seq=" << pong.seq
            << " rttUs=" << rttUs
            << " hostQueueUs=" << ((pong.hostSendQpcUs >= pong.hostRecvQpcUs)
                                        ? (pong.hostSendQpcUs - pong.hostRecvQpcUs)
                                        : 0)
            << " hostCapPid=" << pong.captureTargetPid
            << " hostCapProc=" << fixed_cstr_to_string(
                   pong.captureTargetProcess, sizeof(pong.captureTargetProcess))
            << " hostCapRebind=" << pong.captureRebindCount
            << "\n";
  // GNLink stream telemetry (diagnostics only): a periodic NTP-style clock offset
  // (host QPC minus client QPC) plus RTT, so the seq-joined host/client logs can
  // also be roughly aligned on an absolute timeline. Runs once per pong (~control
  // interval); no new control traffic is introduced.
  {
    const int64_t t1 = static_cast<int64_t>(action.ping.clientSendQpcUs);
    const int64_t t2 = static_cast<int64_t>(pong.hostRecvQpcUs);
    const int64_t t3 = static_cast<int64_t>(pong.hostSendQpcUs);
    const int64_t t4 = static_cast<int64_t>(doneUs);
    const int64_t clockOffsetUs = ((t2 - t1) + (t3 - t4)) / 2;
    std::ostringstream telem;
    telem << "[native-video-client][telemetry] stage=clock"
          << " pingSeq=" << pong.seq
          << " rttUs=" << rttUs
          << " clockOffsetUs=" << clockOffsetUs
          << " clientSendUs=" << action.ping.clientSendQpcUs
          << " hostRecvUs=" << pong.hostRecvQpcUs
          << " hostSendUs=" << pong.hostSendQpcUs
          << " clientRecvUs=" << doneUs;
    log_client_line(telem.str());
  }
}

void ControlClient::handle_window_list(const ControlWindowListMessage& windowList) {
  apply_window_list_snapshot(windowList);
  // The window list is where the host says whether it knows the monitor
  // messages; asking one that does not would stall this loop waiting for a
  // reply that never comes.
  const bool supportsMonitors =
      (windowList.flags &
       remote60::native_poc::kControlWindowListFlagMonitors) != 0;
  const bool monitorsNewlySupported =
      gPicker.windowPanel.SetHostSupportsMonitors(supportsMonitors);
  // The stored --monitor is auto-applied only when the session opens straight
  // into the stream. In picker mode the user has not chosen a target yet, so
  // selecting a monitor here would restart the host capture before any pick and
  // fight the first-frame gate; a monitor pick is a follow-up (toolbar) action.
  if (!startInPicker && monitorsNewlySupported && gSession.requestedMonitorId > 0) {
    // Only when a screen other than the primary was asked for: selecting monitor
    // zero would restart the capture for no change.
    gPicker.windowPanel.RequestMonitorSelect(gSession.requestedMonitorId);
  }
  InvalidateRect(gSession.hwnd, nullptr, FALSE);
}

void ControlClient::handle_window_selected(const ControlWindowSelectedMessage& windowSelected) {
  apply_window_selected_result(windowSelected);
  queue_window_list_request("window_list_request pending");
  InvalidateRect(gSession.hwnd, nullptr, FALSE);
}

void ControlClient::handle_input_ack(const ControlInputAckMessage& inputAck) {
  const uint64_t ackCount = gControl.scheduler.RecordInputAck(args.inputLogEvery);
  if (ackCount > 0) {
    std::cout << "[native-video-client][input] ackSeq=" << inputAck.seq
              << " sent=" << ackCount
              << " dropped=" << gControl.inputQueue.dropped_count()
              << "\n";
  }
}


void ControlClient::Run() {
  // Built once, not per action: the tunnelled link carries the partially-read inbound
  // message between calls, and a fresh one each time would drop whatever it held.
  std::unique_ptr<remote60::native_poc::ControlLink> controlLink;
  if (gControl.overUdp.load(std::memory_order_acquire)) {
    controlLink = std::make_unique<remote60::native_poc::UdpControlLink>(
        &gControl.udpControl, kUdpControlReadTimeoutMs);
  } else {
    controlLink = std::make_unique<remote60::native_poc::TcpControlLink>(controlSock);
  }

  while (gSession.running.load()) {
    // Drives retransmission and gap recovery; cheap when there is nothing outstanding.
    if (gControl.overUdp.load(std::memory_order_acquire)) gControl.udpControl.Tick();
    bool didWork = false;
    const uint64_t nowUs = qpc_now_us();
    ControlOutboundAction action{};
    if (gControl.scheduler.NextAction(
            nowUs, capture_client_control_metrics_snapshot(), &gPicker.windowPanel,
            &gControl.streamState, &gControl.captureModeRequests, &gControl.keyframeRequests, &gControl.runtimeTune,
            &gControl.inputQueue, &action)) {
      TcpControlResponse response{};
      const uint64_t actionStartUs = qpc_now_us();
      const bool actionOk = execute_control_action(*controlLink, action, &response);
      // One exchange that never gets its reply stalls every later one behind it,
      // including input. Naming the slow action is the only way to see which.
      const uint64_t actionUs = qpc_now_us() - actionStartUs;
      if (actionUs > 1000000ULL) {
        std::cout << "[native-video-client][control] slow action kind="
                  << static_cast<int>(action.kind) << " tookUs=" << actionUs
                  << " ok=" << (actionOk ? 1 : 0) << "\n";
      }
      if (!actionOk) {
        // A failed exchange ends the session's control, so it has to say which one and
        // on what transport. This used to break out silently, which made a control
        // channel that died on one bad message look identical to one that never
        // connected.
        std::cout << "[native-video-client][control] action failed kind="
                  << static_cast<int>(action.kind) << " transport="
                  << (gControl.overUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp");
        if (gControl.overUdp.load(std::memory_order_acquire)) {
          // Closed means the channel gave up on the peer; open means the exchange came
          // back as something other than the reply this action was waiting for.
          const auto stats = gControl.udpControl.GetStats();
          std::cout << " closed=" << (gControl.udpControl.IsClosed() ? 1 : 0)
                    << " reason=" << to_string(gControl.udpControl.CloseReason())
                    << " sent=" << stats.messagesSent
                    << " received=" << stats.messagesReceived
                    << " retx=" << stats.fragmentRetransmits
                    << " nacks=" << stats.nacksSent;
        }
        std::cout << "\n";
        break;
      }
      if (action.kind == ControlOutboundActionKind::InputEvent) {
        const uint64_t sent = ++gSession.inputEventsSent;
        if (args.inputLogEvery > 0 && (sent % args.inputLogEvery) == 0) {
          std::cout << "[native-video-client][input] sent=" << sent
                    << " kind=" << action.inputEvent.kind
                    << " seq=" << action.inputEvent.seq << "\n";
        }
      }
      didWork = true;

      if (action.kind == ControlOutboundActionKind::CaptureMode) {
        std::cout << "[native-video-client][control] capture-mode-request seq=" << action.captureMode.seq
                  << " mode=" << action.captureMode.mode
                  << " xPermille=" << action.captureMode.xPermille
                  << " yPermille=" << action.captureMode.yPermille
                  << "\n";
      } else if (action.kind == ControlOutboundActionKind::KeyframeRequest) {
        std::cout << "[native-video-client][control] keyframe-request seq=" << action.keyframe.seq
                  << " reason=" << action.keyframe.reason << "\n";
      } else if (action.kind == ControlOutboundActionKind::StreamState) {
        std::cout << "[native-video-client][control] stream-state seq="
                  << action.streamState.seq
                  << " active=" << ((action.streamState.flags & 0x1u) ? 1 : 0) << "\n";
      } else if (action.kind == ControlOutboundActionKind::RuntimeTune) {
        std::cout << "[native-video-client][control] runtime-config seq=" << action.runtimeTune.seq
                  << " bitrate=" << action.runtimeTune.bitrate
                  << " keyint=" << action.runtimeTune.keyint
                  << " flags=" << action.runtimeTune.flags
                  << "\n";
      }

      switch (response.kind) {
        case TcpControlResponseKind::Pong: {
          handle_pong(action, response.pong);
          break;
        }
        case TcpControlResponseKind::WindowList: {
          handle_window_list(response.windowList);
          break;
        }
        case TcpControlResponseKind::MonitorList:
          gPicker.windowPanel.ApplyMonitorList(response.monitorList);
          break;
        case TcpControlResponseKind::WindowSelected:
          handle_window_selected(response.windowSelected);
          break;
        case TcpControlResponseKind::InputAck: {
          handle_input_ack(response.inputAck);
          break;
        }
        case TcpControlResponseKind::None:
        default:
          break;
      }
    }

    if (!didWork && gPicker.visible.load(std::memory_order_relaxed)) {
      const int fetched = fetch_one_thumbnail(*controlLink);
      if (fetched < 0) break;
      didWork = (fetched > 0);
    }
    if (!didWork) Sleep(2);
  }
  gControl.connected.store(false, std::memory_order_relaxed);
  gControl.runtimeTune.SetEnabled(false);
  // A selection cannot complete once control is gone: drop the pending state so the picker
  // re-enables instead of staying locked on "waiting for first frame". The viewer exits
  // shortly after (the video socket dies too), which returns the shell to the host list.
  clear_pc_target_selection();
  // Drop the persistent generation filter too: a reconnect renegotiates generations from
  // scratch, so an old value must not silently filter the new stream to nothing.
  gSel.activeStreamGeneration.store(0, std::memory_order_release);
  set_window_panel_status("control_disconnected");
  InvalidateRect(gSession.hwnd, nullptr, FALSE);
}

}  // namespace remote60::native_poc::viewer
