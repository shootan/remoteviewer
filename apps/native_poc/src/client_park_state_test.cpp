// keepalive r1: the park's two pure pieces (client_park_state.hpp), against the product input queue.
//
// [1] held input: a key / mouse button / physical key whose down was sent is released, once its up was
//     sent it is not; a make-only pulse owes nothing; the releases come out of the product queue as
//     release edges (DropAllButReleases keeps them).
// [2] the fence: Releasing -> Pausing only when the queue drained, Fencing only once the pause was SENT,
//     Parked only on the Pong of the Ping sent after it. Negative controls that must fail to confirm:
//     a Pong of an earlier Ping, a Pong before any Ping, a resume (StreamState true) instead of a pause.
// [3] budget and limit: not confirmed within 2 s = Failed; parked past the limit = Expired; a second
//     Begin while running is refused; End clears it.

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
    check("[2] drained: request the pause now", f.OnInputDrained() && f.phase() == ClientParkPhase::Pausing);
    check("[2] ...once", !f.OnInputDrained());
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
    f.OnInputDrained();
    check("[3] confirming within 2 s stays", f.Tick(2'999'999) == ClientParkPhase::Pausing);
    check("[3] NOT CONFIRMED WITHIN 2 s: failed (the app disconnects)", f.Tick(3'000'001) == ClientParkPhase::Failed);
    check("[3] a failed park may begin again", f.Begin(4'000'000));
    f.OnInputDrained();
    f.OnStreamStateSent(false);
    f.OnPingSent(1);
    f.OnPong(1, 4'100'000);
    check("[3] parked within the budget", f.Tick(4'200'000) == ClientParkPhase::Parked);
    check("[3] parked just inside the limit stays", f.Tick(4'000'000 + ClientParkFence::kParkedLimitUs) == ClientParkPhase::Parked);
    check("[3] PAST THE LIMIT: expired (the session ends itself)",
          f.Tick(4'000'001 + ClientParkFence::kParkedLimitUs) == ClientParkPhase::Expired);
  }

  std::printf("\nclient_park_state_test: %d passed, %d failed\nRESULT: %s\n", gChecks - gFailures, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
