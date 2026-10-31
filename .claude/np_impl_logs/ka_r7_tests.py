p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_fence_test.cpp'
s = open(p, encoding='utf-8').read()

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('''#include "native_video_client_tcp_control.hpp"
#include "viewer_owner_fence.hpp"''', '''#include "native_video_client_tcp_control.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_owner_fence.hpp"
#include "viewer_state.hpp"''')

block = r'''
  // ================================================================ N1 (r7): the X pair's ups are releases
  {
    ClientInputQueue q;
    const QueuedControlInputMessage x1 = make_control_input_event(q, 3, 0, 0, 0, 0, VK_XBUTTON1, 1);
    const QueuedControlInputMessage x2 = make_control_input_event(q, 3, 0, 0, 0, 0, VK_XBUTTON2, 1);
    const QueuedControlInputMessage left = make_control_input_event(q, 3, 0, 0, 0, 0, VK_LBUTTON, 1);
    const QueuedControlInputMessage x1down = make_control_input_event(q, 2, 0, 0, 0, 0, VK_XBUTTON1, 1);
    check("N1: the shared release rule counts the X1 and X2 ups (and the left up), not an X down",
          control_input_release_identity(x1) != 0 && control_input_release_identity(x2) != 0 &&
              control_input_release_identity(left) != 0 && control_input_release_identity(x1down) == 0);
    // an X up already queued when the fence comes survives the fence's cancellation and is taken by the drain
    OwnerFenceGate g;
    ClipboardSyncState clip;
    std::mutex credMu;
    std::wstring cred;
    std::atomic<bool> credRequested{false};
    q.Enqueue(x1down);
    q.Enqueue(x1);
    fence_raise(g);
    (void)owner_fence_cancel_pending(q, clip, credMu, cred, credRequested);
    ControlOutboundAction a{};
    const bool tookPending = gate_take_release(g, q, 2, &a) && a.inputEvent.keyCode == VK_XBUTTON1 && a.inputEvent.kind == 3;
    if (tookPending) gate_end_input(g, InputOutcome::Acked);
    check("N1: an X1 up queued before the fence is kept and taken by the release drain (the down is dropped)",
          tookPending && q.Empty());
  }
  {
    // fresh ups the fence makes (enqueue_release_for_pressed_mouse_buttons, the product function) for X1, X2 and
    // left held -- on a host that takes the X pair, and on one that does not (no X edge may go to it)
    for (const bool hostTakesX : {true, false}) {
      auto st = std::make_unique<ViewerState>();
      st->session.inputEnabled.store(true);
      st->session.hostMouseXButtons.store(hostTakesX);
      st->input.mouseButtons.store(static_cast<uint16_t>(kMouseWireX1 | kMouseWireX2 | kMouseWireLeft));
      OwnerFenceGate g;
      fence_raise(g);
      enqueue_release_for_pressed_mouse_buttons(*st);
      int xUps = 0, otherUps = 0;
      ControlOutboundAction a{};
      while (gate_take_release(g, st->control.inputQueue, 3, &a)) {
        if (a.inputEvent.keyCode == VK_XBUTTON1 || a.inputEvent.keyCode == VK_XBUTTON2) ++xUps;
        else ++otherUps;
        gate_end_input(g, InputOutcome::Acked);
      }
      if (hostTakesX) {
        check("N1: fresh ups at the fence on an X-capable host: X1 and X2 ups are taken (and left's)",
              xUps == 2 && otherUps == 1, "x=" + std::to_string(xUps) + " other=" + std::to_string(otherUps));
      } else {
        check("N1: on a host that does not take the X pair: no X edge at all (left's up still goes)",
              xUps == 0 && otherUps == 1, "x=" + std::to_string(xUps) + " other=" + std::to_string(otherUps));
      }
      check(hostTakesX ? "N1: ...and the verdict is Confirmed only after those ups were acknowledged"
                       : "N1: ...and the verdict is Confirmed after left's up",
            fence_verdict(g, st->control.inputQueue) == FenceVerdict::Confirmed);
    }
  }

  // ================================================================ N2 (r7): a cancelled down is not a failure
  {
    // a key down admitted BEFORE the fence (not yet written); the fence comes and queues a held Ctrl's up; the down's
    // write is then refused by the gate before any byte -- a cancellation. The Ctrl up must still go.
    auto run = [](bool r6Mapping, FenceVerdict* afterDown, FenceVerdict* end, FenceTally* tally, size_t* wireWrites) {
      OwnerFenceGate g;
      ClientInputQueue q;
      std::unique_ptr<ControlLink> inner = std::make_unique<RecordingLink>();
      auto* rec = static_cast<RecordingLink*>(inner.get());
      FenceGatedLink gated(inner, g);
      ControlOutboundAction down{};
      (void)gate_begin_action(g, &down, [&](ControlOutboundAction* out) {
        out->kind = ControlOutboundActionKind::InputEvent;
        out->inputEvent = key(q, true, 0x41).inputEvent;
        return true;
      });
      fence_raise(g);
      q.Enqueue(key(q, false, VK_CONTROL));
      const bool wrote = gated.Write("d", 1);  // the down's write, after the fence
      // r6 recorded every !actionOk as Failed; r7 asks why
      const InputOutcome o = r6Mapping ? InputOutcome::Failed : outcome_of_failure(gate_take_failure(g));
      (void)wrote;
      gate_end_input(g, o);
      *afterDown = fence_verdict(g, q);
      ControlOutboundAction up{};
      if (*afterDown == FenceVerdict::Pending && gate_take_release(g, q, 4, &up) && gated.Write("u", 1)) {
        gate_end_input(g, InputOutcome::Acked);  // the host's InputAck
      }
      *end = fence_verdict(g, q, tally);
      *wireWrites = rec->writes;
    };
    FenceVerdict a6{}, e6{}, a7{}, e7{};
    FenceTally t6{}, t7{};
    size_t w6 = 0, w7 = 0;
    run(true, &a6, &e6, &t6, &w6);
    run(false, &a7, &e7, &t7, &w7);
    check("N2: r6's mapping -- the refused down counts as Failed and the fence closes with the Ctrl up still queued (the defect)",
          a6 == FenceVerdict::Failed && w6 == 0);
    check("N2: r7 -- the refused down is a cancellation (0 bytes): the verdict waits for the Ctrl up",
          a7 == FenceVerdict::Pending);
    check("N2: ...the up is written and acknowledged: Confirmed, 1 ack + 1 cancelled (said apart)",
          e7 == FenceVerdict::Confirmed && w7 == 1 && t7.acked == 1 && t7.cancelled == 1 && t7.failed == 0,
          "acked=" + std::to_string(t7.acked) + " cancelled=" + std::to_string(t7.cancelled) + " writes=" + std::to_string(w7));
  }
  {
    // the other causes stay failures: a cut message, the test hook's refused release, a transport failure
    OwnerFenceGate g;
    g.lastRefusal = OwnerFenceGate::Refusal::FenceMidMessage;
    const bool cut = gate_take_failure(g) == ExchangeFailure::Broken;
    g.lastRefusal = OwnerFenceGate::Refusal::TestHook;
    const bool hook = gate_take_failure(g) == ExchangeFailure::HookRefused;
    const bool transport = gate_take_failure(g) == ExchangeFailure::Transport;  // no refusal recorded
    check("N2: a cut message / a hook-refused release / a transport failure are failures, not cancellations",
          cut && hook && transport && outcome_of_failure(ExchangeFailure::Broken) == InputOutcome::Failed &&
              outcome_of_failure(ExchangeFailure::HookRefused) == InputOutcome::Failed &&
              outcome_of_failure(ExchangeFailure::Transport) == InputOutcome::Failed);
    // and the link records mid-message: a message begun before the fence, its second write refused after it
    ClientInputQueue q;
    std::unique_ptr<ControlLink> inner = std::make_unique<RecordingLink>();
    FenceGatedLink gated(inner, g);
    OwnerFenceGate& gg = g;
    (void)gated.Write("a", 1);  // first part, before the fence
    fence_raise(gg);
    const bool second = gated.Write("b", 1);
    check("N2: ...a write refused in the middle of a message is recorded as a cut message",
          !second && gate_take_failure(gg) == ExchangeFailure::Broken);
    // a release whose exchange gets no ack (no refusal at all): Failed, not cancelled
    q.Enqueue(key(q, false, 0x10));
    ControlOutboundAction rel{};
    (void)gated.EndMessage();
    const bool took = gate_take_release(gg, q, 5, &rel);
    gate_end_input(gg, outcome_of_failure(gate_take_failure(gg)));
    check("N2: a release with no InputAck (no refusal): Failed", took && fence_verdict(gg, q) == FenceVerdict::Failed);
  }
'''
rep('''
  std::printf("\\nka_pc_fence_test: %s\\n", gFailures ? "FAILED" : "ALL PASS");''', block + '''
  std::printf("\\nka_pc_fence_test: %s\\n", gFailures ? "FAILED" : "ALL PASS");''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
