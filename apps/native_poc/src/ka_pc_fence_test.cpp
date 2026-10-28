// ka-pc-impl r5 (Codex codex_ka_pc_src_r4.md R3-a / R3-b): the owner fence's verdict and cancellation, run on the
// viewer's own code (viewer_owner_fence.hpp, ClientInputQueue, ClipboardSyncState) -- no window, no host, no network,
// no clipboard API, no real password (a made-up string, zeroed by the code under test).
//
// Each case also runs the r4 rule the fix replaces, copied verbatim from d81148e (viewer_window_proc.cpp, the drain
// timer): the same fixture makes that rule FAIL -- it claims what did not happen -- and the r5 code PASS.
#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <string>

#include "viewer_owner_fence.hpp"

namespace {
int gFailures = 0;
void check(const char* name, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
  if (!ok) ++gFailures;
}
using namespace remote60::native_poc;
using namespace remote60::native_poc::viewer;

// d81148e viewer_window_proc.cpp, the drain timer's whole rule (verbatim but for the names of its inputs):
//   const bool delivered = ctx.control.inputQueue.Empty() && !ctx.control.inputInFlight.load() &&
//                          !ctx.control.udpControl.TxPending();
bool r4_delivered(bool queueEmpty, bool inFlight, bool txPending) { return queueEmpty && !inFlight && !txPending; }

QueuedControlInputMessage key(ClientInputQueue& q, bool down, uint32_t vk) {
  return make_control_input_event(q, down ? 5 : 6, 0, 0, 0, 0, vk, 1);
}
}  // namespace

int main() {
  // ------------------------------------------------------------ R3-a: an empty queue is not an acknowledgement
  {
    // the fence: counters before its releases, then two releases queued
    InputExchangeCounters c;
    ClientInputQueue q;
    const FenceCounterSnapshot at = snapshot(c);
    q.Enqueue(key(q, false, 0x10));
    q.Enqueue(key(q, false, 0x11));
    // the control thread runs both; the LAST exchange fails (a write the link refused: no InputAck). Then the queue
    // is empty, nothing is in flight, and on TCP the reliable-UDP layer has nothing pending.
    ControlOutboundAction a{};
    c.taken.fetch_add(1); take_release_action(q, 2, &a); c.acked.fetch_add(1);
    c.taken.fetch_add(1); take_release_action(q, 3, &a); c.failed.fetch_add(1);
    const bool r4 = r4_delivered(q.Empty(), false, false);
    check("R3-a: the r4 rule on a failed last release claims 'acknowledged by the host' (the defect)", r4,
          r4 ? "r4 delivered=1" : "r4 delivered=0");
    const FenceVerdict v = fence_release_verdict(at, snapshot(c), q.Empty(), false);
    check("R3-a: r5 -- the same fixture is NOT confirmed (Failed)", v == FenceVerdict::Failed);
    check("R3-a: the r4 rule would therefore FAIL this case and r5 passes it", r4 && v != FenceVerdict::Confirmed);
  }
  {
    InputExchangeCounters c;
    ClientInputQueue q;
    const FenceCounterSnapshot at = snapshot(c);
    for (uint32_t vk : {0x10u, 0x11u, 0x12u}) q.Enqueue(key(q, false, vk));
    ControlOutboundAction a{};
    check("R3-a: while releases are queued: pending", fence_release_verdict(at, snapshot(c), q.Empty(), false) == FenceVerdict::Pending);
    while (take_release_action(q, 5, &a)) {
      c.taken.fetch_add(1);
      check("R3-a: ...each one is taken as an input exchange that expects an InputAck",
            a.expectedResponseType && *a.expectedResponseType == MessageType::ControlInputAck);
      check("R3-a: ...an exchange in flight is pending", fence_release_verdict(at, snapshot(c), q.Empty(), true) == FenceVerdict::Pending);
      c.acked.fetch_add(1);
    }
    check("R3-a: every release acknowledged by the host: confirmed",
          fence_release_verdict(at, snapshot(c), q.Empty(), false) == FenceVerdict::Confirmed);
  }
  {
    // an exchange already in flight at the fence, acknowledged after it: still confirmed (not double-counted)
    InputExchangeCounters c;
    c.taken.store(4); c.acked.store(3);
    const FenceCounterSnapshot at = snapshot(c);
    c.acked.fetch_add(1);
    check("R3-a: an exchange in flight at the fence, acked after it: confirmed",
          fence_release_verdict(at, snapshot(c), true, false) == FenceVerdict::Confirmed);
  }

  // ------------------------------------------------------------ R3-b: nothing but releases after the fence
  {
    ClientInputQueue q;
    ClipboardSyncState clip;
    clip.hostSupports.store(true);
    std::mutex credMu;
    std::wstring cred = L"made-up-test-password";
    std::atomic<bool> credRequested{true};
    {
      std::lock_guard<std::mutex> lock(clip.mu);
      clip.hasPending = true;
      clip.pendingText = u"made-up clipboard text";
    }
    // queued before the fence: a key down, text, a key up
    q.Enqueue(key(q, true, 0x41));
    const uint16_t text[] = {u'x', u'y'};
    enqueue_control_input_text(q, text, 2, 1);
    q.Enqueue(key(q, false, 0x41));

    // r4: the fence removed the clipboard listener and cancelled pastes -- none of this state. What the control
    // thread's pump_clipboard_sync checks before sending (d81148e viewer_control_client.cpp :258-262):
    //   if (!clip.enabled || !clip.hostSupports) return 0;  ... then the pending text is sent
    const bool r4WouldSend = clip.enabled.load() && clip.hostSupports.load() && clip.hasPending;
    check("R3-b: after the r4 fence the pending clipboard would still be sent (the defect)", r4WouldSend);

    const uint32_t dropped = owner_fence_cancel_pending(q, clip, credMu, cred, credRequested);
    const bool clipGateClosed = !(clip.enabled.load() && clip.hostSupports.load());
    bool pendingGone = false;
    {
      std::lock_guard<std::mutex> lock(clip.mu);
      pendingGone = !clip.hasPending && clip.pendingText.empty();
    }
    check("R3-b: r5 -- the clipboard sync is off and the pending text is gone (pump_clipboard_sync sends nothing)",
          clipGateClosed && pendingGone);
    check("R3-b: ...a password waiting to be sealed is zeroed, never sent", cred.empty() && !credRequested.load());
    ControlOutboundAction a{};
    int releases = 0, others = 0;
    while (take_release_action(q, 9, &a)) {
      if (a.kind == ControlOutboundActionKind::InputEvent && a.inputEvent.kind == 6) ++releases;
      else ++others;
    }
    check("R3-b: ...the queued down and text are dropped; only the release goes",
          dropped == 2 && releases == 1 && others == 0,
          "dropped=" + std::to_string(dropped) + " releases=" + std::to_string(releases) + " others=" + std::to_string(others));
  }

  std::printf("\nka_pc_fence_test: %s\n", gFailures ? "FAILED" : "ALL PASS");
  return gFailures ? 1 : 0;
}
