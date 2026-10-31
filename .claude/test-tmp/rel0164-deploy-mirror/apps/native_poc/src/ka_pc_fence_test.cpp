// ka-pc-impl r5/r6 (Codex codex_ka_pc_src_r4.md R3-a/R3-b, codex_ka_pc_src_r5.md F1/F2): the owner fence on the
// viewer's own code (viewer_owner_fence.hpp: the gate, its verdict, the gated link, the cancellation; ClientInputQueue;
// ClipboardSyncState; send_clipboard_update) -- no window, no host, no network (the "link" is a recorder in this
// process), no clipboard API, no real password (made-up strings, zeroed by the code under test).
//
// F1 and F2 each run the r5 code -- copied verbatim from 1c321d1 below -- and the r6 code on the SAME schedule / fixture:
// r5 claims what did not happen (FAIL as a fence), r6 does not (PASS). F1's schedule is made with a real second thread
// that stops between the dequeue and the count.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "native_video_client_tcp_control.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_owner_fence.hpp"
#include "viewer_state.hpp"

namespace {
int gFailures = 0;
void check(const char* name, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!ok) ++gFailures;
}
using namespace remote60::native_poc;
using namespace remote60::native_poc::viewer;

QueuedControlInputMessage key(ClientInputQueue& q, bool down, uint32_t vk) {
  return make_control_input_event(q, down ? 5 : 6, 0, 0, 0, 0, vk, 1);
}

// ---------------------------------------------------------------- r5 (1c321d1), verbatim but for the namespace
namespace r5 {
struct InputExchangeCounters {
  std::atomic<uint64_t> taken{0};
  std::atomic<uint64_t> acked{0};
  std::atomic<uint64_t> failed{0};
};
struct FenceCounterSnapshot {
  uint64_t taken = 0, acked = 0, failed = 0;
};
inline FenceCounterSnapshot snapshot(const InputExchangeCounters& c) {
  return FenceCounterSnapshot{c.taken.load(), c.acked.load(), c.failed.load()};
}
enum class FenceVerdict { Pending, Confirmed, Failed };
inline FenceVerdict fence_release_verdict(const FenceCounterSnapshot& atFence, const FenceCounterSnapshot& now,
                                          bool queueEmpty, bool inFlight) {
  if (now.failed > atFence.failed) return FenceVerdict::Failed;
  if (!queueEmpty || inFlight) return FenceVerdict::Pending;
  return (now.acked - atFence.acked) >= (now.taken - atFence.taken) ? FenceVerdict::Confirmed : FenceVerdict::Pending;
}
}  // namespace r5

// A control link in this process: counts what was written, answers nothing.
struct RecordingLink : ControlLink {
  size_t writes = 0, bytes = 0;
  bool Read(void*, size_t) override { return false; }
  bool Write(const void*, size_t len) override { ++writes; bytes += len; return true; }
  bool EndMessage() override { return true; }
  bool Alive() const override { return true; }
};
}  // namespace

int main() {
  using clk = std::chrono::steady_clock;

  // ================================================================ F1: the verdict sees one consistent state
  {
    // r5 on the schedule Codex found: the timer reads inFlight=false; the sender raises inFlight, dequeues the LAST
    // release, and stops before taken++; the timer then reads the queue (empty) and the counters (old taken=acked).
    r5::InputExchangeCounters c;
    std::atomic<bool> inFlight{false};
    ClientInputQueue q;
    const r5::FenceCounterSnapshot at = r5::snapshot(c);
    q.Enqueue(key(q, false, 0x10));
    const bool timerReadInFlight = inFlight.load();  // (1) the timer's first read
    inFlight.store(true);                             // (2) the sender ...
    QueuedControlInputMessage m{};
    q.TryDequeue(&m);                                 //     ... takes the last release, not yet counted
    const r5::FenceVerdict v5 = r5::fence_release_verdict(at, r5::snapshot(c), q.Empty(), timerReadInFlight);  // (3)
    check("F1: r5 on that schedule says Confirmed with no ack and the release still in the sender's hands (the defect)",
          v5 == r5::FenceVerdict::Confirmed);
  }
  {
    // r6: the same schedule through the gate. The sender (a real thread) stops between the dequeue and the count,
    // inside gate_take_release; the timer asks for the verdict while it is stopped there.
    OwnerFenceGate g;
    ClientInputQueue q;
    fence_raise(g);
    q.Enqueue(key(q, false, 0x10));
    std::atomic<bool> dequeued{false};
    std::function<void()> stall = [&] {
      dequeued.store(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    };
    ControlOutboundAction a{};
    std::thread sender([&] { (void)gate_take_release(g, q, 1, &a, &stall); });
    while (!dequeued.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto t0 = clk::now();
    const FenceVerdict v = fence_verdict(g, q);  // the timer, mid-handoff
    const auto waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
    sender.join();
    check("F1: r6 -- the timer's verdict mid-handoff is NOT Confirmed (it waits for the handoff, then sees it in flight)",
          v == FenceVerdict::Pending, "waited " + std::to_string(waitedMs) + " ms");
    // no InputAck arrives: a failed exchange
    gate_end_input(g, InputOutcome::Failed);
    check("F1: ...the exchange then fails (no ack): Failed, never Confirmed", fence_verdict(g, q) == FenceVerdict::Failed);
  }
  {
    // r6, the ordinary end: the release acknowledged -> Confirmed; a baseline taken with an exchange in flight
    OwnerFenceGate g;
    ClientInputQueue q;
    // an input exchange in flight BEFORE the fence (taken, not ended)
    ControlOutboundAction pre{};
    q.Enqueue(key(q, true, 0x41));
    (void)gate_begin_action(g, &pre, [&](ControlOutboundAction* out) {
      QueuedControlInputMessage mm{};
      if (!q.TryDequeue(&mm)) return false;
      out->kind = ControlOutboundActionKind::InputEvent;
      out->inputEvent = mm.inputEvent;
      return true;
    });
    fence_raise(g);
    q.Enqueue(key(q, false, 0x41));
    check("F1: r6 -- a release queued: Pending", fence_verdict(g, q) == FenceVerdict::Pending);
    gate_end_input(g, InputOutcome::Acked);  // the pre-fence exchange ends
    ControlOutboundAction rel{};
    check("F1: ...taken as a counted release", gate_take_release(g, q, 2, &rel) && action_is_release(rel));
    check("F1: ...in flight: Pending", fence_verdict(g, q) == FenceVerdict::Pending);
    gate_end_input(g, InputOutcome::Acked);
    check("F1: ...acknowledged by the host: Confirmed", fence_verdict(g, q) == FenceVerdict::Confirmed);
  }

  // ================================================================ F2: no non-release write starts after the fence
  {
    // r5: the control thread has ALREADY moved the pending text to its local copy (pump_clipboard_sync); then the UI
    // fence runs (shared pending zeroed, sync off); then the control thread sends its copy. r5 had no gate on the
    // write: it goes.
    ClipboardSyncState clip;
    clip.hostSupports.store(true);
    std::mutex credMu;
    std::wstring cred;
    std::atomic<bool> credRequested{false};
    ClientInputQueue q;
    { std::lock_guard<std::mutex> lock(clip.mu); clip.hasPending = true; clip.pendingText = u"made-up text"; }
    std::u16string outText;
    { std::lock_guard<std::mutex> lock(clip.mu); outText = std::move(clip.pendingText); clip.hasPending = false; }
    (void)owner_fence_cancel_pending(q, clip, credMu, cred, credRequested);  // the r5 fence did exactly this
    RecordingLink wire;
    (void)send_clipboard_update(wire, 1, outText, 0, 1);
    check("F2: r5 -- the clipboard text the sender already held is written after the fence (the defect)", wire.writes > 0,
          "writes=" + std::to_string(wire.writes));
  }
  {
    // r6: the same, through the gate: the fence raised first, then the sender's write asks the gate
    OwnerFenceGate g;
    ClipboardSyncState clip;
    clip.hostSupports.store(true);
    std::mutex credMu;
    std::wstring cred;
    std::atomic<bool> credRequested{false};
    ClientInputQueue q;
    { std::lock_guard<std::mutex> lock(clip.mu); clip.hasPending = true; clip.pendingText = u"made-up text"; }
    std::u16string outText;
    { std::lock_guard<std::mutex> lock(clip.mu); outText = std::move(clip.pendingText); clip.hasPending = false; }
    fence_raise(g);
    (void)owner_fence_cancel_pending(q, clip, credMu, cred, credRequested);
    std::unique_ptr<ControlLink> inner = std::make_unique<RecordingLink>();
    auto* rec = static_cast<RecordingLink*>(inner.get());
    FenceGatedLink gated(inner, g);
    const bool sent = send_clipboard_update(gated, 1, outText, 0, 1);
    check("F2: r6 -- that write is refused by the gate: nothing reaches the wire", !sent && rec->writes == 0,
          "writes=" + std::to_string(rec->writes));
    // the ordinary branch after the fence: a pick, a down -- refused in the gate's critical section; a release admitted
    ControlOutboundAction pick{};
    const GateAdmit ap = gate_begin_action(g, &pick, [](ControlOutboundAction* out) {
      out->kind = ControlOutboundActionKind::WindowSelect;
      return true;
    });
    ControlOutboundAction down{};
    const GateAdmit ad = gate_begin_action(g, &down, [&](ControlOutboundAction* out) {
      out->kind = ControlOutboundActionKind::InputEvent;
      out->inputEvent = key(q, true, 0x41).inputEvent;
      return true;
    });
    check("F2: r6 -- after the fence the ordinary branch's pick and key down are refused (never written)",
          ap == GateAdmit::Refused && ad == GateAdmit::Refused);
    ControlOutboundAction up{};
    const GateAdmit au = gate_begin_action(g, &up, [&](ControlOutboundAction* out) {
      out->kind = ControlOutboundActionKind::InputEvent;
      out->inputEvent = key(q, false, 0x41).inputEvent;
      return true;
    });
    const size_t before = rec->writes;
    const bool upWrote = au == GateAdmit::Admitted && gated.Write("x", 1) && rec->writes == before + 1;
    gate_end_input(g, InputOutcome::Acked);
    check("F2: r6 -- a release is admitted and its write goes", upWrote);
    check("F2: r6 -- with no exchange running, any write is refused again", !gated.Write("x", 1));
    check("F2: r6 -- no bulk (image / file) datagram after the fence", !gate_bulk_send_allowed(g));
    // the fenced branch's take: a down still queued is dropped, never taken; the release is
    q.Enqueue(key(q, true, 0x42));
    q.Enqueue(key(q, false, 0x42));
    ControlOutboundAction t{};
    const bool took = gate_take_release(g, q, 3, &t);
    check("F2: r6 -- the release-only take skips a queued down and takes the release",
          took && action_is_release(t) && q.Empty());
    gate_end_input(g, InputOutcome::Acked);
  }
  {
    // r5/r6 R3-b (kept): the fence cancels what is still shared -- queued down / text, clipboard, a waiting password
    ClientInputQueue q;
    ClipboardSyncState clip;
    clip.hostSupports.store(true);
    std::mutex credMu;
    std::wstring cred = L"made-up-test-password";
    std::atomic<bool> credRequested{true};
    { std::lock_guard<std::mutex> lock(clip.mu); clip.hasPending = true; clip.pendingText = u"made-up clipboard text"; }
    q.Enqueue(key(q, true, 0x41));
    const uint16_t text[] = {u'x', u'y'};
    enqueue_control_input_text(q, text, 2, 1);
    q.Enqueue(key(q, false, 0x41));
    const uint32_t dropped = owner_fence_cancel_pending(q, clip, credMu, cred, credRequested);
    bool pendingGone = false;
    { std::lock_guard<std::mutex> lock(clip.mu); pendingGone = !clip.hasPending && clip.pendingText.empty(); }
    check("R3-b: the fence turns the clipboard sync off and drops its pending text", !clip.enabled.load() && pendingGone);
    check("R3-b: ...zeroes a password waiting to be sealed", cred.empty() && !credRequested.load());
    check("R3-b: ...and drops the queued down and text (the release stays)", dropped == 2 && !q.Empty());
  }

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

  std::printf("\nka_pc_fence_test: %s\n", gFailures ? "FAILED" : "ALL PASS");
  return gFailures ? 1 : 0;
}
