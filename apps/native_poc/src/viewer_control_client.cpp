// See viewer_control_client.hpp. Bodies are the controlThread lambda of native_video_client_main.cpp,
// verbatim (viewer split refactor Phase 2-4).

#include "viewer_control_client.hpp"

#include <iostream>
#include <memory>
#include <sstream>

#include "viewer_constants.hpp"
#include "viewer_env_util.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_log.hpp"
#include "viewer_picker.hpp"

#include "viewer_unlock.hpp"

namespace remote60::native_poc::viewer {

int ControlClient::fetch_one_thumbnail(remote60::native_poc::ControlLink& link) {
  // Routed through the ControlLink, not the raw socket, so a directory session (control
  // tunnelled over the punched UDP socket) fetches previews too. The exchange itself is the
  // shared fetch_window_thumbnail, the same code the Android ClientSessionController runs
  // (F-09); this side only picks the card and stores the pixels -- GDI wants BGRA, which is the
  // wire order. One card per idle action keeps the strict request/response loop from being
  // starved. Only invoked when the host advertised the capability, because an older host would
  // drain the request and never reply. Returns: 1 fetched, 0 nothing to do, -1 link failure
  // (stream desynced).
  if (!ctx.picker.hostSupportsThumbnails.load(std::memory_order_relaxed)) return 0;
  uint64_t id = 0;
  {
    std::lock_guard<std::mutex> lk(ctx.picker.thumbMu);
    if (ctx.picker.thumbFetchQueue.empty()) return 0;
    id = ctx.picker.thumbFetchQueue.front();
    ctx.picker.thumbFetchQueue.pop_front();
  }
  remote60::native_poc::WindowThumbnailReply reply;
  if (!remote60::native_poc::fetch_window_thumbnail(link, id, 256, 160, qpc_now_us(), &reply)) {
    return -1;
  }
  if (reply.present) {
    auto thumb = std::make_shared<WindowThumb>();
    thumb->width = reply.width;
    thumb->height = reply.height;
    thumb->bgra = std::move(reply.bgra);
    thumb->fetchedUs = qpc_now_us();
    {
      std::lock_guard<std::mutex> lk(ctx.picker.thumbMu);
      ctx.picker.thumbs[id] = std::move(thumb);
    }
    // Outside the lock: the paint handler takes ctx.picker.thumbMu, and invalidating while
    // holding it invited a stall on every received preview.
    InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
  }
  return 1;
}

void ControlClient::handle_pong(const ControlOutboundAction& action, const ControlPongMessage& pong) {
  const uint64_t doneUs = qpc_now_us();
  ctx.control.scheduler.OnPingCompleted(doneUs);
  {
    // Say it once per transition rather than every ping. A frozen picture with no
    // explanation is the worst version of this; a line saying a Windows security
    // prompt is on screen turns it into something the operator can act on.
    const bool secure =
        (pong.captureTargetFlags &
         remote60::native_poc::kCaptureFlagSecureDesktopActive) != 0;
    ctx.session.hostImeSupported.store(
        (pong.captureTargetFlags & remote60::native_poc::kCaptureFlagHostImeV1) != 0,
        std::memory_order_relaxed);
    const bool imeV2 =
        (pong.captureTargetFlags & remote60::native_poc::kCaptureFlagHostImePulseStateV2) != 0;
    ctx.session.hostImeV2Supported.store(imeV2, std::memory_order_relaxed);
    // Enter host-IME negotiation once, when the user opted in and the host speaks v2. Disabled ->
    // pending; the control loop's align pump then aligns the host to EN and posts activate. A
    // v1-only host never enters, so it stays on the unchanged legacy client-IME path. (Codex Edge 4.)
    if (host_ime_optin() && imeV2 &&
        ctx.session.imeMode.load(std::memory_order_acquire) == 0 &&
        !ctx.session.imeEnterPending.exchange(true, std::memory_order_acq_rel)) {
      const uint32_t gen = ctx.session.imeGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
      std::cout << "[native-video-client][ime] enter negotiation gen=" << gen << "\n";
    }
    ctx.session.unlockSupported.store(
        (pong.captureTargetFlags & remote60::native_poc::kCaptureFlagUnlockSealedV1) != 0,
        std::memory_order_relaxed);
    if (secure != ctx.control.reportedSecure) {
      ctx.control.reportedSecure = secure;
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
    log_client_line(ctx, telem.str());
  }
}

void ControlClient::handle_window_list(const ControlWindowListMessage& windowList) {
  // Handed to the UI thread rather than applied here: ApplyWindowList sizes the visible grid from
  // the window's client rect and DPI, and the thumbnail queue it fills is consumed against the
  // same layout. A control thread computing UI layout was the thread-affinity smell F-07 names.
  // Falls back to applying inline only when there is no window to post to (headless harness).
  bool posted = false;
  if (ctx.session.hwnd) {
    auto* copy = new ControlWindowListMessage(windowList);
    posted = PostMessageW(ctx.session.hwnd, kMsgApplyWindowList, 0, reinterpret_cast<LPARAM>(copy)) != FALSE;
    if (!posted) delete copy;
  }
  if (!posted) apply_window_list_snapshot(ctx, windowList);
  // The window list is where the host says whether it knows the monitor
  // messages; asking one that does not would stall this loop waiting for a
  // reply that never comes.
  const bool supportsMonitors =
      (windowList.flags &
       remote60::native_poc::kControlWindowListFlagMonitors) != 0;
  const bool monitorsNewlySupported =
      ctx.picker.windowPanel.SetHostSupportsMonitors(supportsMonitors);
  // The stored --monitor is auto-applied only when the session opens straight
  // into the stream. In picker mode the user has not chosen a target yet, so
  // selecting a monitor here would restart the host capture before any pick and
  // fight the first-frame gate; a monitor pick is a follow-up (toolbar) action.
  if (!startInPicker && monitorsNewlySupported && ctx.session.requestedMonitorId > 0) {
    // Only when a screen other than the primary was asked for: selecting monitor
    // zero would restart the capture for no change.
    ctx.picker.windowPanel.RequestMonitorSelect(ctx.session.requestedMonitorId);
  }
  InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
}

void ControlClient::handle_window_selected(const ControlWindowSelectedMessage& windowSelected) {
  apply_window_selected_result(ctx, windowSelected);
  queue_window_list_request(ctx, "window_list_request pending");
  InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
}

void ControlClient::handle_input_ack(const ControlInputAckMessage& inputAck) {
  const uint64_t ackCount = ctx.control.scheduler.RecordInputAck(args.inputLogEvery);
  if (ackCount > 0) {
    std::cout << "[native-video-client][input] ackSeq=" << inputAck.seq
              << " sent=" << ackCount
              << " dropped=" << ctx.control.inputQueue.dropped_count()
              << "\n";
  }
}


void ControlClient::Run() {
  // Built once, not per action: the tunnelled link carries the partially-read inbound
  // message between calls, and a fresh one each time would drop whatever it held.
  std::unique_ptr<remote60::native_poc::ControlLink> controlLink;
  if (ctx.control.overUdp.load(std::memory_order_acquire)) {
    controlLink = std::make_unique<remote60::native_poc::UdpControlLink>(
        &ctx.control.udpControl, kUdpControlReadTimeoutMs);
  } else {
    controlLink = std::make_unique<remote60::native_poc::TcpControlLink>(controlSock);
  }

  // P0 telemetry accumulators (input serialization diagnosis, #351): one line per second.
  // Move-specific (BLOCKER 1) + coalesce delta (BLOCKER 2) + queue age (HIGH 3) + windowUs.
  uint64_t p0MoveSent = 0;       // move InputEvents actually sent (kind==1)
  uint64_t p0MoveRttSumUs = 0;   // send->ack RTT for moves
  uint64_t p0MoveRttMaxUs = 0;
  uint64_t p0MoveQueueAgeSumUs = 0;  // generate->send age for moves
  uint64_t p0MoveQueueAgeMaxUs = 0;
  uint64_t p0LastEmitUs = 0;
  uint64_t p0LastMoveGen = 0;
  uint64_t p0LastCoalesced = 0;

  while (ctx.session.running.load()) {
    // Drives retransmission and gap recovery; cheap when there is nothing outstanding.
    if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
    bool didWork = false;
    const uint64_t nowUs = qpc_now_us();
    // P0: once a second, print mouse-moves the UI generated vs coalesced vs actually sent, with the
    // send->ack RTT and generate->send queue age. moveGen>>moveSent (and coalesced making up the
    // gap) means the drag is serialized 1-per-RTT. windowUs is emitted because the control thread
    // blocks in execute_control_action, so the window is only approximately 1s.
    if (p0LastEmitUs == 0) p0LastEmitUs = nowUs;
    if (nowUs - p0LastEmitUs >= 1000000ULL) {
      const uint64_t windowUs = nowUs - p0LastEmitUs;
      const uint64_t moveGenNow = ctx.input.moveGeneratedCount.load(std::memory_order_relaxed);
      const uint64_t coalescedNow = ctx.control.inputQueue.coalesced_move_count();
      const uint64_t moveGenDelta = moveGenNow - p0LastMoveGen;
      const uint64_t coalescedDelta = coalescedNow - p0LastCoalesced;
      const uint64_t rttAvg = p0MoveSent ? (p0MoveRttSumUs / p0MoveSent) : 0;
      const uint64_t ageAvg = p0MoveSent ? (p0MoveQueueAgeSumUs / p0MoveSent) : 0;
      std::cout << "[native-video-client][input-p0] windowUs=" << windowUs
                << " moveGen=" << moveGenDelta
                << " moveCoalesced=" << coalescedDelta
                << " moveSent=" << p0MoveSent
                << " droppedTotal=" << ctx.control.inputQueue.dropped_count()
                << " moveRttAvgUs=" << rttAvg
                << " moveRttMaxUs=" << p0MoveRttMaxUs
                << " moveQueueAgeAvgUs=" << ageAvg
                << " moveQueueAgeMaxUs=" << p0MoveQueueAgeMaxUs
                << " transport=" << (ctx.control.overUdp.load(std::memory_order_acquire) ? "udp" : "tcp")
                << "\n";
      p0LastMoveGen = moveGenNow;
      p0LastCoalesced = coalescedNow;
      p0MoveSent = 0;
      p0MoveRttSumUs = 0;
      p0MoveRttMaxUs = 0;
      p0MoveQueueAgeSumUs = 0;
      p0MoveQueueAgeMaxUs = 0;
      p0LastEmitUs = nowUs;
    }
    if (ctx.session.unlockRequested.exchange(false, std::memory_order_acq_rel)) {
      if (!ctx.session.unlockSupported.load(std::memory_order_relaxed)) {
        std::cout << "[native-video-client][unlock] host does not support sealed unlock\n";
      } else {
        std::wstring pw;
        if (load_unlock_password(0, &pw)) {
          std::string status;
          bool clearCred = false;
          (void)run_unlock_exchange(*controlLink, 0, pw, &status, &clearCred);
          if (!pw.empty()) SecureZeroMemory(&pw[0], pw.size() * sizeof(wchar_t));
          if (clearCred) {  // wrong/corrupt password: drop it so it is not auto-reused (lockout guard)
            clear_unlock_password(0);
            std::cout << "[native-video-client][unlock] " << status
                      << " (stored password cleared; set it again to retry)\n";
          } else {
            std::cout << "[native-video-client][unlock] " << status << "\n";
          }
        } else {
          clear_unlock_password(0);  // a present-but-unreadable file would block re-prompt forever
          std::cout << "[native-video-client][unlock] no usable stored password (set it again)\n";
        }
      }
    }
    // Host-IME entry: before draining input, align the host IME to English and confirm, so the
    // switch to physical routing (posted below) never lets an English key compose as jamo. Runs at
    // most one round-trip; keeps ordering because it precedes NextAction on the serial link.
    if (ctx.session.imeEnterPending.load(std::memory_order_acquire)) {
      const uint32_t gen = ctx.session.imeGeneration.load(std::memory_order_acquire);
      ControlOutboundAction imeAction{};
      imeAction.kind = ControlOutboundActionKind::ImeStateRequest;
      imeAction.expectedResponseType = MessageType::ControlImeStateResponse;
      imeAction.expectedResponseSize = sizeof(ControlImeStateResponseMessage);
      imeAction.imeStateReq.header.magic = remote60::native_poc::kMagic;
      imeAction.imeStateReq.header.type =
          static_cast<uint16_t>(MessageType::ControlImeStateRequest);
      imeAction.imeStateReq.header.size = sizeof(ControlImeStateRequestMessage);
      imeAction.imeStateReq.seq = gen;
      imeAction.imeStateReq.targetGeneration = gen;
      imeAction.imeStateReq.action = 1;  // set to English, then query
      imeAction.imeStateReq.clientSendQpcUs = qpc_now_us();
      TcpControlResponse imeResp{};
      if (!execute_control_action(*controlLink, imeAction, &imeResp)) {
        std::cout << "[native-video-client][control] action failed kind=ime-align\n";
        break;
      }
      if (imeResp.kind == TcpControlResponseKind::ImeStateResponse &&
          imeResp.imeStateResponse.targetGeneration == gen) {
        const uint16_t st = imeResp.imeStateResponse.status;
        if (st == 2) {
          // Host reported stale-target (focus moved mid-align); keep pending and retry next loop.
          std::cout << "[native-video-client][ime] align stale-target; retrying\n";
        } else {
          const int open = (st == 0) ? static_cast<int>(imeResp.imeStateResponse.open) : 2;  // 2=?
          ctx.session.imeEnterPending.store(false, std::memory_order_release);
          if (ctx.session.hwnd)
            PostMessageW(ctx.session.hwnd, kMsgHostImeActivate, static_cast<WPARAM>(open), 0);
          std::cout << "[native-video-client][ime] align done status=" << st << " open=" << open
                    << "\n";
        }
      } else {
        // Generation mismatch or unexpected reply: drop pending so we do not spin; a later pong
        // re-enters if still applicable.
        ctx.session.imeEnterPending.store(false, std::memory_order_release);
      }
      didWork = true;
      continue;
    }
    ControlOutboundAction action{};
    if (ctx.control.scheduler.NextAction(
            nowUs, capture_client_control_metrics_snapshot(ctx), &ctx.picker.windowPanel,
            &ctx.control.streamState, &ctx.control.captureModeRequests, &ctx.control.keyframeRequests, &ctx.control.runtimeTune,
            &ctx.control.inputQueue, &action)) {
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
                  << (ctx.control.overUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp");
        if (ctx.control.overUdp.load(std::memory_order_acquire)) {
          // Closed means the channel gave up on the peer; open means the exchange came
          // back as something other than the reply this action was waiting for.
          const auto stats = ctx.control.udpControl.GetStats();
          std::cout << " closed=" << (ctx.control.udpControl.IsClosed() ? 1 : 0)
                    << " reason=" << to_string(ctx.control.udpControl.CloseReason())
                    << " sent=" << stats.messagesSent
                    << " received=" << stats.messagesReceived
                    << " retx=" << stats.fragmentRetransmits
                    << " nacks=" << stats.nacksSent;
        }
        std::cout << "\n";
        break;
      }
      if (action.kind == ControlOutboundActionKind::InputEvent) {
        const uint64_t sent = ++ctx.session.inputEventsSent;
        if (args.inputLogEvery > 0 && (sent % args.inputLogEvery) == 0) {
          std::cout << "[native-video-client][input] sent=" << sent
                    << " kind=" << action.inputEvent.kind
                    << " seq=" << action.inputEvent.seq << "\n";
        }
        // P0 telemetry (#351): move-specific only (kind==1). actionUs = send + wait-for-ack, the
        // serial round trip; queue age = send time - generation time (how long it waited behind the
        // one-per-RTT gate). Buttons/keys are excluded so the drag comparison stays clean.
        if (action.inputEvent.kind == 1) {
          ++p0MoveSent;
          if (actionUs > p0MoveRttMaxUs) p0MoveRttMaxUs = actionUs;
          p0MoveRttSumUs += actionUs;
          const uint64_t sendUs = actionStartUs;
          const uint64_t ageUs =
              (action.inputGeneratedUs > 0 && sendUs >= action.inputGeneratedUs)
                  ? (sendUs - action.inputGeneratedUs)
                  : 0;
          p0MoveQueueAgeSumUs += ageUs;
          if (ageUs > p0MoveQueueAgeMaxUs) p0MoveQueueAgeMaxUs = ageUs;
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
          ctx.picker.windowPanel.ApplyMonitorList(response.monitorList);
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

    if (!didWork && ctx.picker.visible.load(std::memory_order_relaxed)) {
      const int fetched = fetch_one_thumbnail(*controlLink);
      if (fetched < 0) break;
      didWork = (fetched > 0);
    }
    if (!didWork) Sleep(2);
  }
  ctx.control.connected.store(false, std::memory_order_relaxed);
  // Host-IME: control is gone, so restore the local IME and stop physical routing on the UI thread.
  // A reconnect renegotiates from scratch (generation bumps, pending re-arms on the next pong).
  ctx.session.imeEnterPending.store(false, std::memory_order_release);
  if (ctx.session.imeMode.load(std::memory_order_acquire) != 0 && ctx.session.hwnd) {
    PostMessageW(ctx.session.hwnd, kMsgHostImeDeactivate, 0, 0);
  }
  ctx.control.runtimeTune.SetEnabled(false);
  // A selection cannot complete once control is gone: drop the pending state so the picker
  // re-enables instead of staying locked on "waiting for first frame". The viewer exits
  // shortly after (the video socket dies too), which returns the shell to the host list.
  clear_pc_target_selection(ctx);
  // Drop the persistent generation filter too: a reconnect renegotiates generations from
  // scratch, so an old value must not silently filter the new stream to nothing.
  ctx.sel.activeStreamGeneration.store(0, std::memory_order_release);
  set_window_panel_status(ctx, "control_disconnected");
  InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
}

}  // namespace remote60::native_poc::viewer
