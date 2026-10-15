// keepalive r1: the park's two pure pieces (client_park_state.hpp), against the product input queue.
//
// [1] held input: a key / mouse button / physical key whose down was sent is released, once its up was
//     sent it is not; a make-only pulse owes nothing; the releases come out of the product queue as
//     release edges (DropAllButReleases keeps them).
// [2] the fence: Releasing -> Pausing only when the queue drained, Fencing only once the pause was SENT,
//     Parked only on the Pong of the Ping sent after it. Negative controls that must fail to confirm:
//     a Pong of an earlier Ping, a Pong before any Ping, a resume (StreamState true) instead of a pause.
// [3] budget and limit: not confirmed within 2 s = Failed; parked past 5 min (300 s) = Expired; a second
//     Begin while running is refused; End clears it. r2 K3: a fence Pong after the budget is not success.
// [4] r2 K1: a down that is IN FLIGHT when the park begins (dequeued and being sent, its InputAck and so
//     its record still to come). The d4b3068 order -- releases computed at BeginPark -- is run as the
//     negative control and must leave it held at the pause; the pass-boundary collection releases it.
// [5] r2 K1: what a park may still send.

#include <cstdio>
#include <string>

#include "client_park_state.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

ControlOutboundAction input(uint16_t kind, uint32_t keyCode, int32_t x = 0, int32_t y = 0) {
  ControlOutboundAction a{};
  a.kind = ControlOutboundActionKind::InputEvent;
  a.inputEvent.kind = kind;
  a.inputEvent.keyCode = keyCode;
  a.inputEvent.x = x;
  a.inputEvent.y = y;
  return a;
}
ControlOutboundAction physical(bool down, uint16_t scan, uint16_t flags = 0) {
  ControlOutboundAction a{};
  a.kind = ControlOutboundActionKind::PhysicalKey;
  a.physicalKey.down = down ? 1 : 0;
  a.physicalKey.vk = 0x41;
  a.physicalKey.scanCode = scan;
  a.physicalKey.flags = flags;
  return a;
}

}  // namespace

int main() {
  std::printf("[1] held input\n");
  {
    ClientHeldInput held;
    held.OnSent(input(5, 0x11));         // Ctrl down
    held.OnSent(input(5, 0x41));         // A down
    held.OnSent(input(6, 0x41));         // A up
    held.OnSent(input(2, 1, 300, 200));  // left button down at 300,200
    held.OnSent(input(1, 0, 310, 205));  // a move with it
    held.OnSent(physical(true, 0x1D));   // physical Ctrl down
    held.OnSent(physical(true, 0x72, 0x4));  // a make-only Hangul pulse
    check("[1] what is still down: Ctrl, the left button, the physical Ctrl (not A, not the pulse)", held.Count() == 3,
          std::to_string(held.Count()));
    ClientInputQueue q;
    const auto rel = held.Releases(q, 1000);
    bool ctrlUp = false, buttonUp = false, physUp = false;
    for (const auto& m : rel) {
      if (m.type == MessageType::ControlInputEvent && m.inputEvent.kind == 6 && m.inputEvent.keyCode == 0x11) ctrlUp = true;
      if (m.type == MessageType::ControlInputEvent && m.inputEvent.kind == 3 && m.inputEvent.keyCode == 1 &&
          m.inputEvent.x == 310 && m.inputEvent.y == 205) {
        buttonUp = true;
      }
      if (m.type == MessageType::ControlPhysicalKey && m.physicalKey.down == 0 && m.physicalKey.scanCode == 0x1D) physUp = true;
    }
    check("[1] the releases: Ctrl up, left up where the pointer was, physical Ctrl up", rel.size() == 3 && ctrlUp &&
          buttonUp && physUp);
    for (const auto& m : rel) q.Enqueue(m);
    q.Enqueue(make_control_input_event(q, 5, 0, 0, 0, 0, 0x42, 1000));  // a down queued with them
    uint32_t dropped = 0, kept = 0;
    q.DropAllButReleases(&dropped, &kept);
    check("[1] the product queue keeps the releases and drops the down (what BeginPark does)", kept == 3 && dropped == 1,
          "kept " + std::to_string(kept) + " dropped " + std::to_string(dropped));
    ControlOutboundAction sent{};
    QueuedControlInputMessage m{};
    while (q.TryDequeue(&m)) {
      sent = ControlOutboundAction{};
      if (m.type == MessageType::ControlInputEvent) {
        sent.kind = ControlOutboundActionKind::InputEvent;
        sent.inputEvent = m.inputEvent;
      } else {
        sent.kind = ControlOutboundActionKind::PhysicalKey;
        sent.physicalKey = m.physicalKey;
      }
      held.OnSent(sent);
    }
    check("[1] once the releases are sent nothing is held", held.Empty() && q.Empty());
    held.OnSent(input(5, 0x10));
    held.Reset();
    check("[1] a new connection holds nothing", held.Empty());
  }

  std::printf("[2] the fence\n");
  {
    ClientParkFence f;
    check("[2] begins", f.Begin(0) && f.phase() == ClientParkPhase::Releasing && f.Inhibiting());
    f.OnStreamStateSent(false);
    check("[2] a pause sent while still releasing does not move it (the pause comes after the releases)",
          f.phase() == ClientParkPhase::Releasing);
    check("[2] the first pass boundary collects (no pause yet)", f.OnPassBoundary(1, true, true).collect &&
          f.phase() == ClientParkPhase::Releasing);
    check("[2] the queue not drained yet: no pause", !f.OnPassBoundary(1, false, true).pauseNow);
    check("[2] drained and nothing held: request the pause now", f.OnPassBoundary(1, true, true).pauseNow &&
          f.phase() == ClientParkPhase::Pausing);
    check("[2] ...once", !f.OnPassBoundary(1, true, true).pauseNow);
    f.OnPingSent(7);
    f.OnPong(7, 1);
    check("NEGATIVE: a Ping sent BEFORE the pause, and its Pong, do not confirm it", f.phase() == ClientParkPhase::Pausing);
    f.OnStreamStateSent(true);
    check("NEGATIVE: a resume sent instead of the pause does not count", f.phase() == ClientParkPhase::Pausing);
    f.OnStreamStateSent(false);
    check("[2] the pause sent: fencing, a Ping wanted (once)", f.phase() == ClientParkPhase::Fencing && f.TakePingWanted() &&
          !f.TakePingWanted());
    f.OnPong(7, 2);
    check("NEGATIVE: a Pong before the fence Ping was sent does not confirm", f.phase() == ClientParkPhase::Fencing);
    f.OnPingSent(8);
    f.OnPingSent(9);
    f.OnPong(9, 3);
    check("NEGATIVE: a Pong of a later Ping than the fence's does not confirm", f.phase() == ClientParkPhase::Fencing &&
          f.fence_ping_seq() == 8);
    f.OnPong(8, 4);
    check("[2] the fence Ping's Pong: parked", f.phase() == ClientParkPhase::Parked && f.parked_us() == 4);
    check("[2] a second Begin while parked is refused", !f.Begin(5));
    f.End();
    check("[2] End: nothing parked, nothing inhibited", f.phase() == ClientParkPhase::None && !f.Inhibiting());
  }

  std::printf("[3] budget and limit\n");
  {
    ClientParkFence f;
    f.Begin(1'000'000);
    f.OnPassBoundary(1'000'001, true, true);
    f.OnPassBoundary(1'000'002, true, true);
    check("[3] confirming within 2 s stays", f.Tick(2'999'999) == ClientParkPhase::Pausing);
    check("[3] NOT CONFIRMED WITHIN 2 s: failed (the control loop ends the session)",
          f.Tick(3'000'001) == ClientParkPhase::Failed && f.OnPassBoundary(3'000'002, true, true).phase == ClientParkPhase::Failed);
    {
      ClientParkFence late;
      late.Begin(0);
      late.OnPassBoundary(1, true, true);
      late.OnPassBoundary(2, true, true);
      late.OnStreamStateSent(false);
      late.OnPingSent(5);
      late.OnPong(5, 2'000'001);  // its Tick never ran (the loop was waiting on the answer)
      check("[3] r2 K3: A FENCE PONG AFTER THE 2 s BUDGET DOES NOT CONFIRM (failed, not parked)",
            late.phase() == ClientParkPhase::Failed, client_park_phase_name(late.phase()));
    }
    {
      ClientParkFence held;
      held.Begin(0);
      held.OnPassBoundary(1, true, false);  // collect
      check("[3] queue drained but something still held: one more round of releases",
            held.OnPassBoundary(2, true, false).reRelease && held.phase() == ClientParkPhase::Releasing);
      check("[3] ...and still held after it: failed (the disconnect teardown releases)",
            held.OnPassBoundary(3, true, false).phase == ClientParkPhase::Failed);
    }
    check("[3] a failed park may begin again", f.Begin(4'000'000));
    f.OnPassBoundary(4'000'001, true, true);
    f.OnPassBoundary(4'000'002, true, true);
    f.OnStreamStateSent(false);
    f.OnPingSent(1);
    f.OnPong(1, 4'100'000);
    check("[3] parked within the budget", f.Tick(4'200'000) == ClientParkPhase::Parked);
    check("[3] r2 K3: the limit is the contract's 300 s", ClientParkFence::kParkedLimitUs == 300'000'000ULL);
    check("[3] parked just inside the limit stays", f.Tick(4'000'000 + ClientParkFence::kParkedLimitUs) == ClientParkPhase::Parked);
    check("[3] PAST THE LIMIT: expired (the session ends itself)",
          f.Tick(4'000'001 + ClientParkFence::kParkedLimitUs) == ClientParkPhase::Expired);
  }

  std::printf("[4] r2 K1: a down in flight when the park begins\n");
  {
    // The control loop's order of events, both ways. The down is dequeued and being sent when the UI
    // calls BeginPark; its InputAck (and so heldInput.OnSent) comes after.
    const ControlOutboundAction down = input(5, 0x11);  // Ctrl down
    auto drive = [&](bool d4b3068Order) {
      ClientHeldInput held;
      ClientInputQueue q;
      ClientParkFence f;
      f.Begin(0);  // BeginPark (UI thread) while the down is in flight
      if (d4b3068Order) {
        // d4b3068, transcribed: the releases were computed inside BeginPark -- the down was not recorded yet.
        for (const auto& r : held.Releases(q, 0)) q.Enqueue(r);
      }
      held.OnSent(down);  // the in-flight down's InputAck arrives, it is recorded
      bool pausedWithHeld = false;
      for (uint64_t t = 1; t < 50 && f.phase() == ClientParkPhase::Releasing; ++t) {
        if (d4b3068Order) {
          // d4b3068: pause as soon as the queue is empty.
          if (q.Empty()) {
            pausedWithHeld = !held.Empty();
            break;
          }
        } else {
          const auto st = f.OnPassBoundary(t, q.Empty(), held.Empty());
          if (st.collect || st.reRelease) {
            uint32_t dropped = 0, kept = 0;
            if (st.collect) q.DropAllButReleases(&dropped, &kept);
            for (const auto& r : held.Releases(q, t)) q.Enqueue(r);
          }
          if (st.pauseNow) {
            pausedWithHeld = !held.Empty();
            break;
          }
        }
        // the loop sends what is queued; each is answered and recorded
        QueuedControlInputMessage m{};
        if (q.TryDequeue(&m)) {
          ControlOutboundAction sent{};
          sent.kind = ControlOutboundActionKind::InputEvent;
          sent.inputEvent = m.inputEvent;
          held.OnSent(sent);
        }
      }
      return pausedWithHeld;
    };
    check("NEGATIVE (d4b3068 order): the in-flight Ctrl down is STILL HELD at the pause", drive(true));
    check("[4] r2 K1: COLLECTED AT THE PASS BOUNDARY, THE IN-FLIGHT DOWN IS RELEASED BEFORE THE PAUSE", !drive(false));
  }

  std::printf("[5] r2 K1: what a park may still send\n");
  {
    using K = ControlOutboundActionKind;
    bool releaseAndFence = client_park_allows(K::Ping) && client_park_allows(K::StreamState) &&
                           client_park_allows(K::InputEvent) && client_park_allows(K::PhysicalKey) &&
                           client_park_allows(K::Metrics);
    bool noOthers = !client_park_allows(K::WindowListRequest) && !client_park_allows(K::WindowSelect) &&
                    !client_park_allows(K::MonitorSelect) && !client_park_allows(K::MonitorListRequest) &&
                    !client_park_allows(K::CaptureMode) && !client_park_allows(K::KeyframeRequest) &&
                    !client_park_allows(K::RuntimeTune) && !client_park_allows(K::DesktopBackend);
    check("[5] the fence, releases and the paused report go out", releaseAndFence);
    check("[5] a list, selection, monitor, capture mode, key request, tune or backend does not", noOthers);
  }

  std::printf("\nclient_park_state_test: %d passed, %d failed\nRESULT: %s\n", gChecks - gFailures, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
