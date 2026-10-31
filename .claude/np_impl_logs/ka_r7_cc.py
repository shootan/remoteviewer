import os
base = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src' + os.sep
NL = chr(92) + 'n'

def edit(name, pairs):
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    for a, c in pairs:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, c)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

edit('viewer_control_client.cpp', [
('''      if (releaseOk && releaseResponse.kind == TcpControlResponseKind::InputAck) {
        gate_end_input(ctx.control.fenceGate, InputOutcome::Acked);
      } else {
        gate_end_input(ctx.control.fenceGate, InputOutcome::Failed);
        std::cout << "[native-video-client][control] a release after the owner fence was not acknowledged; "
                     "no resume (the session is ending)''' + NL + '''";
        break;
      }''', '''      if (releaseOk && releaseResponse.kind == TcpControlResponseKind::InputAck) {
        gate_end_input(ctx.control.fenceGate, InputOutcome::Acked);
      } else {
        // a release is never "cancelled": the gate admits it, so whatever went wrong is a failure (r7, Codex N2)
        const ExchangeFailure why = gate_take_failure(ctx.control.fenceGate);
        gate_end_input(ctx.control.fenceGate, InputOutcome::Failed);
        std::cout << "[native-video-client][control] a release after the owner fence was not acknowledged ("
                  << (why == ExchangeFailure::HookRefused ? "its write was refused by the test hook"
                      : why == ExchangeFailure::Broken     ? "the message was cut"
                                                           : "the exchange failed")
                  << "); no resume (the session is ending)''' + NL + '''";
        break;
      }'''),
('''      const bool actionOk = execute_control_action(gated, action, &response);
      // r5/r6 (Codex R3-a): an input is acknowledged only by the host's InputAck
      admitEnd.outcome = (actionOk && response.kind == TcpControlResponseKind::InputAck) ? InputOutcome::Acked
                                                                                         : InputOutcome::Failed;''',
'''      const bool actionOk = execute_control_action(gated, action, &response);
      // r5/r6/r7 (Codex R3-a, N2): an input is acknowledged only by the host's InputAck. Otherwise WHY it did not go is
      // taken from the gate: refused by the fence before any byte (a down the fence caught between its admission and
      // its write) is a cancellation -- nothing was sent, the drain goes on; anything else is a failure.
      const bool inputAcked = actionOk && response.kind == TcpControlResponseKind::InputAck;
      const ExchangeFailure failure = inputAcked ? ExchangeFailure::Transport : gate_take_failure(ctx.control.fenceGate);
      admitEnd.outcome = inputAcked ? InputOutcome::Acked : outcome_of_failure(failure);'''),
('''        if (gate_is_fenced(ctx.control.fenceGate)) continue;  // r6: the fence refused it -- releases still go
        if (begin_control_resume(qpc_now_us())) {
          workerState = remote60::native_poc::ControlWorkerState::Resuming;
          continue;
        }
        break;
      }
      if (action.kind == ControlOutboundActionKind::InputEvent) {''', '''        // r7 (Codex N2): only a write the fence refused before any byte is a cancellation the drain may go on after;
        // a cut message or a transport failure is what it is (no resume after the fence: the loop ends)
        if (failure == ExchangeFailure::NotStarted) continue;
        if (begin_control_resume(qpc_now_us())) {
          workerState = remote60::native_poc::ControlWorkerState::Resuming;
          continue;
        }
        break;
      }
      if (action.kind == ControlOutboundActionKind::InputEvent) {'''),
('''        if (gate_is_fenced(ctx.control.fenceGate)) continue;  // r6: refused by the fence -- releases still go''',
 '''        if (gate_take_failure(ctx.control.fenceGate) == ExchangeFailure::NotStarted) continue;  // r7: a fence cancellation'''),
('''      if (synced < 0 && gate_is_fenced(ctx.control.fenceGate)) continue;  // r6: refused by the fence''',
 '''      if (synced < 0 && gate_take_failure(ctx.control.fenceGate) == ExchangeFailure::NotStarted) continue;  // r7'''),
('''      if (fetched < 0 && gate_is_fenced(ctx.control.fenceGate)) continue;  // r6: refused by the fence''',
 '''      if (fetched < 0 && gate_take_failure(ctx.control.fenceGate) == ExchangeFailure::NotStarted) continue;  // r7'''),
])

edit('viewer_window_proc.cpp', [(
'''        const FenceVerdict v = fence_verdict(ctx.control.fenceGate, ctx.control.inputQueue);
        const bool confirmed = v == FenceVerdict::Confirmed && !ctx.control.udpControl.TxPending();
        if (confirmed || v == FenceVerdict::Failed || waited >= kOwnerFenceDrainBudgetMs) {
          KillTimer(hwnd, kOwnerFenceDrainTimerId);
          std::cout << "[native-video-client] owner fence: releases "
                    << (confirmed ? "acknowledged by the host"
                                  : v == FenceVerdict::Failed ? "NOT confirmed (an exchange failed)"
                                                              : "NOT confirmed within the budget")
                    << " after " << waited << " ms; closing the session''' + NL + '''";
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }''', '''        FenceTally t{};
        const FenceVerdict v = fence_verdict(ctx.control.fenceGate, ctx.control.inputQueue, &t);
        const bool confirmed = v == FenceVerdict::Confirmed && !ctx.control.udpControl.TxPending();
        if (confirmed || v == FenceVerdict::Failed || waited >= kOwnerFenceDrainBudgetMs) {
          KillTimer(hwnd, kOwnerFenceDrainTimerId);
          // r7 (Codex): what the host acknowledged is said apart from what was deliberately not sent
          std::cout << "[native-video-client] owner fence: releases "
                    << (confirmed ? "confirmed"
                                  : v == FenceVerdict::Failed ? "NOT confirmed (an exchange failed)"
                                                              : "NOT confirmed within the budget")
                    << " after " << waited << " ms -- since the fence: " << t.acked
                    << " acknowledged by the host (InputAck), " << t.skipped << " not sent by the logon-screen gate, "
                    << t.cancelled << " non-release(s) cancelled by the fence before a byte, " << t.failed
                    << " failed, of " << t.taken << " taken; closing the session''' + NL + '''";
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }''')])
print('ok')
