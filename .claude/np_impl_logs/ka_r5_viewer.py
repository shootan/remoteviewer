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

# the counters live with the control state
edit('viewer_owner_fence.hpp', [(
'''// The control thread's count of input exchanges (UI thread reads; control thread writes).
struct InputExchangeCounters {
  std::atomic<uint64_t> taken{0};   // an input action whose exchange was started
  std::atomic<uint64_t> acked{0};   // ... that ended with the host's InputAck
  std::atomic<uint64_t> failed{0};  // ... that ended any other way (a failed exchange, a wrong reply)
};

''', '''// InputExchangeCounters (viewer_control_state.hpp): the control thread's count of input exchanges.

''')])
edit('viewer_control_state.hpp', [(
'''namespace remote60::native_poc::viewer {
''', '''namespace remote60::native_poc::viewer {

// ka-pc-impl r5 (Codex R3-a): the control thread's count of the input exchanges it RUNS (the UI thread's owner fence
// reads them; viewer_owner_fence.hpp fence_release_verdict). An acknowledgement is the host's InputAck, nothing less.
struct InputExchangeCounters {
  std::atomic<uint64_t> taken{0};   // an input action whose exchange was started
  std::atomic<uint64_t> acked{0};   // ... that ended with the host's InputAck
  std::atomic<uint64_t> failed{0};  // ... that ended any other way (a failed exchange, a wrong reply)
};
'''), (
'''  std::atomic<bool> inputInFlight{false};
''', '''  std::atomic<bool> inputInFlight{false};
  InputExchangeCounters inputExchanges;
''')])
edit('viewer_session_state.hpp', [(
'''  std::atomic<uint64_t> ownerFenceAtMs{0};''', '''  std::atomic<uint64_t> ownerFenceAtMs{0};
  // r5 (Codex R3-a): the input-exchange counters as the fence found them, before its releases (UI thread only)
  uint64_t fenceTaken0 = 0, fenceAcked0 = 0, fenceFailed0 = 0;''')])

# ---------------------------------------------------------------- the control thread
edit('viewer_control_client.cpp', [
('''#include "viewer_control_client.hpp"''', '''#include "viewer_control_client.hpp"
#include "viewer_owner_fence.hpp"'''),
('''#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
    // TEST BUILD ONLY (GNLinkViewerKaTest, ka-pc-impl r4, Codex R3): REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS=N
    // holds every outbound control action for N ms after the owner fence -- a sender that is slow at exactly the
    // wrong moment -- so a test can show the fence waits for its releases instead of closing over them.
    {
      static const uint64_t holdMs = [] {
        const char* v = std::getenv("REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS");
        return v ? std::strtoull(v, nullptr, 10) : 0ull;
      }();
      const uint64_t fencedAt = ctx.session.ownerFenceAtMs.load();
      if (holdMs && fencedAt && GetTickCount64() - fencedAt < holdMs) {
        Sleep(5);
        continue;
      }
    }
#endif
    // r4 (Codex R3): raised before the dequeue (see ViewerControlState::inputInFlight), lowered after the exchange.
    if (!ctx.control.inputQueue.Empty()) ctx.control.inputInFlight.store(true);''',
'''    // r4 (Codex R3): raised before the dequeue (see ViewerControlState::inputInFlight), lowered after the exchange.
    if (!ctx.control.inputQueue.Empty()) ctx.control.inputInFlight.store(true);'''),
('''    bool didWork = false;
    const uint64_t nowUs = qpc_now_us();''', '''    bool didWork = false;
    const uint64_t nowUs = qpc_now_us();
    // ka-pc-impl r5 (Codex R3-b): after the owner fence this thread sends RELEASES ONLY -- taken straight off the
    // input queue (the fence dropped every other queued input and cancelled the pending clipboard / credential). No
    // ping-scheduled work, no pick, no stream state, no clipboard, no credential, no IME align from here on. Each
    // release's exchange is counted (taken / acked / failed) for the fence's verdict; a failed one is not resumed.
    if (ctx.session.ownerFenceAtMs.load() != 0) {
#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
      // TEST BUILD ONLY (GNLinkViewerKaTest): REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS=N holds this sender for N ms
      // after the fence -- a sender slow at exactly the wrong moment; REMOTE60_VIEWER_TEST_FAIL_RELEASE_AFTER_FENCE=1
      // makes the first release exchange after the fence fail (as a write the link refused) -- so a test can show the
      // fence waits for its releases, and that a failed one is NOT reported as acknowledged.
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
      if (!ctx.control.inputQueue.Empty()) ctx.control.inputInFlight.store(true);
      struct FencedInFlightDone {
        std::atomic<bool>* f;
        ~FencedInFlightDone() { f->store(false); }
      } fencedInFlightDone{&ctx.control.inputInFlight};
      ControlOutboundAction release{};
      if (!take_release_action(ctx.control.inputQueue, nowUs, &release)) {
        ctx.control.inputInFlight.store(false);
        ctx.control.inputQueue.WaitForInput(5);
        continue;
      }
      ctx.control.inputExchanges.taken.fetch_add(1);
      TcpControlResponse releaseResponse{};
      bool releaseOk = execute_control_action(*controlLink, release, &releaseResponse);
#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
      {
        static std::atomic<bool> failOnce{[] {
          const char* v = std::getenv("REMOTE60_VIEWER_TEST_FAIL_RELEASE_AFTER_FENCE");
          return v && *v == '1';
        }()};
        if (failOnce.exchange(false)) releaseOk = false;
      }
#endif
      if (releaseOk && releaseResponse.kind == TcpControlResponseKind::InputAck) {
        ctx.control.inputExchanges.acked.fetch_add(1);
      } else {
        ctx.control.inputExchanges.failed.fetch_add(1);
        std::cout << "[native-video-client][control] a release after the owner fence was not acknowledged; "
                     "no resume (the session is ending)''' + NL + '''";
        break;
      }
      continue;
    }'''),
('''      TcpControlResponse response{};
      const uint64_t actionStartUs = qpc_now_us();
      const bool actionOk = execute_control_action(*controlLink, action, &response);''', '''      TcpControlResponse response{};
      const uint64_t actionStartUs = qpc_now_us();
      const bool countedInput = action.kind == ControlOutboundActionKind::InputEvent ||
                                action.kind == ControlOutboundActionKind::InputText ||
                                action.kind == ControlOutboundActionKind::PhysicalKey;
      if (countedInput) ctx.control.inputExchanges.taken.fetch_add(1);
      const bool actionOk = execute_control_action(*controlLink, action, &response);
      if (countedInput) {
        // r5 (Codex R3-a): acknowledged only by the host's InputAck
        if (actionOk && response.kind == TcpControlResponseKind::InputAck) ctx.control.inputExchanges.acked.fetch_add(1);
        else ctx.control.inputExchanges.failed.fetch_add(1);
      }'''),
])

# ---------------------------------------------------------------- the fence (UI thread)
edit('viewer_window_proc.cpp', [
('''        // 1. Whatever is held is released FIRST, while input is still on -- the same set the control channel's''',
'''        // 0. r5 (Codex R3-b): what has not left yet and is not a release is cancelled at once -- every queued down /
        //    text / move / wheel, the pending clipboard text (and the clipboard sync: no send, no poll), a typed
        //    password waiting to be sealed (zeroed). Image / file offers stop. The counters are taken now, before
        //    the releases, for the fence's verdict (R3-a).
        const uint32_t droppedInputs = owner_fence_cancel_pending(
            ctx.control.inputQueue, ctx.control.clipboard, ctx.session.preloginCredMu, ctx.session.preloginCredPassword,
            ctx.session.preloginCredRequested);
        ctx.control.clipImage.Stop();
        ctx.control.fileCopy.Stop();
        {
          const FenceCounterSnapshot at = snapshot(ctx.control.inputExchanges);
          ctx.session.fenceTaken0 = at.taken;
          ctx.session.fenceAcked0 = at.acked;
          ctx.session.fenceFailed0 = at.failed;
        }
        // 1. Whatever is held is released FIRST, while input is still on -- the same set the control channel's'''),
('''        std::cout << "[native-video-client] owner fence: held input released (modifierUps=" << modifierUps
                  << " queued), input and clipboard cut; waiting for the releases to reach the host''' + NL + '''";''',
'''        std::cout << "[native-video-client] owner fence: held input released (modifierUps=" << modifierUps
                  << " queued), " << droppedInputs << " queued non-release input(s), the pending clipboard and any "
                  << "waiting password dropped; input and clipboard cut; waiting for the releases to reach the host''' + NL + '''";'''),
('''        const uint64_t waited = GetTickCount64() - ctx.session.ownerFenceAtMs.load();
        // every queued input taken AND acknowledged (input actions are request / response: the host acks each one)
        const bool delivered = ctx.control.inputQueue.Empty() && !ctx.control.inputInFlight.load() &&
                               !ctx.control.udpControl.TxPending();
        if (delivered || waited >= kOwnerFenceDrainBudgetMs) {
          KillTimer(hwnd, kOwnerFenceDrainTimerId);
          std::cout << "[native-video-client] owner fence: releases "
                    << (delivered ? "acknowledged by the host" : "NOT confirmed within the budget") << " after "
                    << waited << " ms; closing the session''' + NL + '''";
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;''', '''        const uint64_t waited = GetTickCount64() - ctx.session.ownerFenceAtMs.load();
        // r5 (Codex R3-a): confirmed only by the host's InputAck for every release exchange since the fence -- an empty
        // queue or an idle channel is not an acknowledgement (a failed exchange leaves both empty)
        const FenceVerdict v = fence_release_verdict(
            FenceCounterSnapshot{ctx.session.fenceTaken0, ctx.session.fenceAcked0, ctx.session.fenceFailed0},
            snapshot(ctx.control.inputExchanges), ctx.control.inputQueue.Empty(), ctx.control.inputInFlight.load());
        const bool confirmed = v == FenceVerdict::Confirmed && !ctx.control.udpControl.TxPending();
        if (confirmed || v == FenceVerdict::Failed || waited >= kOwnerFenceDrainBudgetMs) {
          KillTimer(hwnd, kOwnerFenceDrainTimerId);
          std::cout << "[native-video-client] owner fence: releases "
                    << (confirmed ? "acknowledged by the host"
                                  : v == FenceVerdict::Failed ? "NOT confirmed (an exchange failed)"
                                                              : "NOT confirmed within the budget")
                    << " after " << waited << " ms; closing the session''' + NL + '''";
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;'''),
])
print('ok')
