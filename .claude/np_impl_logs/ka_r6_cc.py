p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\viewer_control_client.cpp'
NL = chr(92) + 'n'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:100])
    s = s.replace(a, c)

# clipboard: a send the gate refused leaves nothing of the text behind
rep('''  if (haveOutbound) {
    if (!remote60::native_poc::send_clipboard_update(link, seq, outText, outHash, qpc_now_us())) {
      return -1;
    }
    return 1;
  }''', '''  if (haveOutbound) {
    const bool sent = remote60::native_poc::send_clipboard_update(link, seq, outText, outHash, qpc_now_us());
    // r6 (Codex F2): taken off the shared pending already -- whether it went or the owner fence refused its write,
    // this copy is zeroed now
    if (!outText.empty()) SecureZeroMemory(&outText[0], outText.size() * sizeof(char16_t));
    outText.clear();
    if (!sent) return -1;
    return 1;
  }''')

# resume: never after the fence
rep('''bool ControlClient::begin_control_resume(uint64_t nowUs) {
  // Only the tunnelled path.''', '''bool ControlClient::begin_control_resume(uint64_t nowUs) {
  // ka-pc-impl r6 (Codex F2): after the owner fence nothing is resumed -- the session is ending; only releases go.
  if (gate_is_fenced(ctx.control.fenceGate)) return false;
  // Only the tunnelled path.''')
rep('''  hooks.cancelled = [this] { return !ctx.session.running.load(); };''',
    '''  hooks.cancelled = [this] { return !ctx.session.running.load() || gate_is_fenced(ctx.control.fenceGate); };''')
rep('''    link = std::make_unique<remote60::native_poc::UdpControlLink>(&ctx.control.udpControl,
                                                                 kUdpControlReadTimeoutMs);
    return control_round_trip(*link);''', '''    link = std::make_unique<remote60::native_poc::UdpControlLink>(&ctx.control.udpControl,
                                                                 kUdpControlReadTimeoutMs);
    FenceGatedLink gatedProbe(link, ctx.control.fenceGate);  // r6: no probe write after the owner fence
    return control_round_trip(gatedProbe);''')

# the one link every exchange of the loop writes through
rep('''    controlLink = std::make_unique<remote60::native_poc::TcpControlLink>(controlSock);
  }
''', '''    controlLink = std::make_unique<remote60::native_poc::TcpControlLink>(controlSock);
  }
  // ka-pc-impl r6 (Codex F2): every write of this loop asks the owner-fence gate first (viewer_owner_fence.hpp): after
  // the fence only a counted release's exchange starts a write -- whichever branch of the loop it comes from.
  FenceGatedLink gated(controlLink, ctx.control.fenceGate);
''')
s = s.replace('*controlLink', 'gated')

rep('''    if (workerState == remote60::native_poc::ControlWorkerState::Resuming) {
      if (!pump_control_resume(controlLink, workerState)) break;
      continue;
    }''', '''    if (workerState == remote60::native_poc::ControlWorkerState::Resuming) {
      // r6 (Codex F2): a resume already under way stops at the owner fence (the session is ending)
      if (gate_is_fenced(ctx.control.fenceGate)) {
        std::cout << "[native-video-client][control] owner fence during a resume: not resumed''' + NL + '''";
        break;
      }
      if (!pump_control_resume(controlLink, workerState)) break;
      continue;
    }''')

# the fenced branch
i = s.index('    // ka-pc-impl r5 (Codex R3-b): after the owner fence this thread sends RELEASES ONLY')
j = s.index('    // P0: once a second, print mouse-moves the UI generated')
s = s[:i] + '''    // ka-pc-impl r5/r6 (Codex R3-b, F1, F2): after the owner fence this thread sends RELEASES ONLY -- each taken off the
    // queue and counted in one critical section of the gate (gate_take_release), through the same logon-screen gates
    // as any input, written through the gated link (only a counted release may write now). No ping-scheduled work, no
    // pick, no stream state, no clipboard, no credential, no IME align. A release whose exchange fails is not resumed.
    if (gate_is_fenced(ctx.control.fenceGate)) {
#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
      // TEST BUILD ONLY (GNLinkViewerKaTest): REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS=N holds this sender for N ms
      // after the fence -- a sender slow at exactly the wrong moment. (A refused release write is
      // REMOTE60_VIEWER_TEST_REFUSE_RELEASE_WRITE_AFTER_FENCE, in the gate itself.)
      {
        static const uint64_t holdMs = [] {
          const char* v = std::getenv("REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS");
          return v ? std::strtoull(v, nullptr, 10) : 0ull;
        }();
        if (holdMs && GetTickCount64() - ctx.session.ownerFenceAtMs.load() < holdMs) {
          Sleep(5);
          continue;
        }
      }
#endif
      ControlOutboundAction release{};
      if (!gate_take_release(ctx.control.fenceGate, ctx.control.inputQueue, nowUs, &release)) {
        ctx.control.inputQueue.WaitForInput(5);
        continue;
      }
      // the same last look as any input (r7 H6 / r9): a release the logon-screen gates would not send is not sent
      const bool keyUpOfHeldBackDown = release.kind == ControlOutboundActionKind::InputEvent &&
                                       release.inputEvent.kind == 6 && release.inputEvent.keyCode < 256 &&
                                       heldDownAtSend[release.inputEvent.keyCode];
      if (keyUpOfHeldBackDown) heldDownAtSend[release.inputEvent.keyCode] = false;
      const bool mayGo =
          !keyUpOfHeldBackDown &&
          remote60::native_poc::prelogin::ViewerMaySendAction(
              remote60::native_poc::prelogin::EffectiveHostFlags(ctx.session.hostRoleKnown.load(std::memory_order_acquire),
                                                                 ctx.session.hostCaptureFlags.load(std::memory_order_relaxed)),
              release) &&
          !(ctx.session.hostRoleKnown.load(std::memory_order_acquire) &&
            (ctx.session.hostCaptureFlags.load(std::memory_order_relaxed) & remote60::native_poc::kCaptureFlagPreloginV1) != 0 &&
            !remote60::native_poc::prelogin::IsControlAllowedInPrelogin(remote60::native_poc::outbound_message_type(release)));
      if (!mayGo) {
        gate_end_input(ctx.control.fenceGate, InputOutcome::Skipped);
        continue;
      }
      TcpControlResponse releaseResponse{};
      const bool releaseOk = execute_control_action(gated, release, &releaseResponse);
      if (releaseOk && releaseResponse.kind == TcpControlResponseKind::InputAck) {
        gate_end_input(ctx.control.fenceGate, InputOutcome::Acked);
      } else {
        gate_end_input(ctx.control.fenceGate, InputOutcome::Failed);
        std::cout << "[native-video-client][control] a release after the owner fence was not acknowledged; "
                     "no resume (the session is ending)''' + NL + '''";
        break;
      }
      continue;
    }
''' + s[j:]

# the ordinary branch: the scheduler's pick admitted (or refused) in the gate's critical section
rep('''    // r4 (Codex R3): raised before the dequeue (see ViewerControlState::inputInFlight), lowered after the exchange.
    if (!ctx.control.inputQueue.Empty()) ctx.control.inputInFlight.store(true);
    struct InFlightDone {
      std::atomic<bool>* f;
      ~InFlightDone() { f->store(false); }
    } inFlightDone{&ctx.control.inputInFlight};
    if (ctx.control.scheduler.NextAction(
            nowUs, capture_client_control_metrics_snapshot(ctx), &ctx.picker.windowPanel,
            &ctx.control.streamState, &ctx.control.captureModeRequests, &ctx.control.keyframeRequests, &ctx.control.runtimeTune,
            &ctx.control.inputQueue, &action, nullptr,
            bandwidthObserve ? &bandwidthSnapshot : nullptr)) {''', '''    // r6 (Codex F1/F2): the scheduler's pick -- which may take an input off the queue -- is made and admitted in ONE
    // critical section of the owner-fence gate: an input taken is counted at once; after a fence anything but a
    // release is refused there and dropped, never written.
    const GateAdmit admit = gate_begin_action(ctx.control.fenceGate, &action, [&](ControlOutboundAction* out) {
      return ctx.control.scheduler.NextAction(
          nowUs, capture_client_control_metrics_snapshot(ctx), &ctx.picker.windowPanel,
          &ctx.control.streamState, &ctx.control.captureModeRequests, &ctx.control.keyframeRequests, &ctx.control.runtimeTune,
          &ctx.control.inputQueue, out, nullptr,
          bandwidthObserve ? &bandwidthSnapshot : nullptr);
    });
    if (admit == GateAdmit::Refused) {
      std::cout << "[native-video-client][control] owner fence: a picked action (kind=" << static_cast<int>(action.kind)
                << ") dropped, not sent''' + NL + '''";
      continue;
    }
    if (admit == GateAdmit::Admitted) {
      // Every way out of this block settles the admitted exchange in the gate (an input: acked / failed / not sent).
      struct AdmitEnd {
        OwnerFenceGate& g;
        bool input;
        InputOutcome outcome = InputOutcome::Skipped;
        ~AdmitEnd() {
          if (input) gate_end_input(g, outcome);
          else gate_end_other(g);
        }
      } admitEnd{ctx.control.fenceGate, action_is_input(action)};''')
rep('''      const bool countedInput = action.kind == ControlOutboundActionKind::InputEvent ||
                                action.kind == ControlOutboundActionKind::InputText ||
                                action.kind == ControlOutboundActionKind::PhysicalKey;
      if (countedInput) ctx.control.inputExchanges.taken.fetch_add(1);
      const bool actionOk = execute_control_action(gated, action, &response);
      if (countedInput) {
        // r5 (Codex R3-a): acknowledged only by the host's InputAck
        if (actionOk && response.kind == TcpControlResponseKind::InputAck) ctx.control.inputExchanges.acked.fetch_add(1);
        else ctx.control.inputExchanges.failed.fetch_add(1);
      }''', '''      const bool actionOk = execute_control_action(gated, action, &response);
      // r5/r6 (Codex R3-a): an input is acknowledged only by the host's InputAck
      admitEnd.outcome = (actionOk && response.kind == TcpControlResponseKind::InputAck) ? InputOutcome::Acked
                                                                                         : InputOutcome::Failed;''')
# failures caused by a refused write after the fence: back to the release-only branch, never a resume or the end
rep('''        if (begin_control_resume(qpc_now_us())) {
          workerState = remote60::native_poc::ControlWorkerState::Resuming;
          continue;
        }
        break;
      }
      if (action.kind == ControlOutboundActionKind::InputEvent) {''', '''        if (gate_is_fenced(ctx.control.fenceGate)) continue;  // r6: the fence refused it -- releases still go
        if (begin_control_resume(qpc_now_us())) {
          workerState = remote60::native_poc::ControlWorkerState::Resuming;
          continue;
        }
        break;
      }
      if (action.kind == ControlOutboundActionKind::InputEvent) {''')
rep('''      const int synced = pump_clipboard_sync(gated);
      if (synced < 0) {''', '''      const int synced = pump_clipboard_sync(gated);
      if (synced < 0 && gate_is_fenced(ctx.control.fenceGate)) continue;  // r6: refused by the fence
      if (synced < 0) {''')
rep('''      const int fetched = fetch_one_thumbnail(gated);
      if (fetched < 0) {''', '''      const int fetched = fetch_one_thumbnail(gated);
      if (fetched < 0 && gate_is_fenced(ctx.control.fenceGate)) continue;  // r6: refused by the fence
      if (fetched < 0) {''')
rep('''      if (!execute_control_action(gated, imeAction, &imeResp)) {
        std::cout << "[native-video-client][control] action failed kind=ime-align''' + NL + '''";
        break;
      }''', '''      if (!execute_control_action(gated, imeAction, &imeResp)) {
        if (gate_is_fenced(ctx.control.fenceGate)) continue;  // r6: refused by the fence -- releases still go
        std::cout << "[native-video-client][control] action failed kind=ime-align''' + NL + '''";
        break;
      }''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
