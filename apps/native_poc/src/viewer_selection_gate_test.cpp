// Unit test for the viewer's PC-side target-selection gate (viewer split refactor Phase 2 / T2):
// the transaction from a pick through the host's ack to the first presented frame, the frame
// admission rules on both sides of the ack, the persistent active-generation filter after a reveal,
// and the reveal race (a cancel / new pick / disconnect between the recv thread's post and the UI
// thread's commit must not close the picker). Atomics only -- no window, no sockets.
//
// Build: remote60_viewer_selection_gate_test (CMake). Run: prints "viewer_selection_gate_test: PASS".

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <cstdint>
#include <cstdio>

#include "viewer_selection_gate.hpp"

using namespace remote60::native_poc::viewer;

namespace {

int gFailures = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
      ++gFailures;                                                                   \
    }                                                                                \
  } while (0)

void test_pick_ack_first_frame_reveal() {
  std::printf("[T2] pick -> ack -> first frame of the acked generation -> commit; stragglers filtered afterwards\n");
  SelectionGateState g;
  // before any pick: everything is accepted (legacy stream view / pre-first-pick window)
  CHECK(g.AdmitGeneration(7) == SelectionAdmit::Accept);
  CHECK(!g.AckedSelectionPending());
  uint64_t seen = g.epoch.load();
  CHECK(!g.EpochChanged(seen));
  // the pick
  g.Begin();
  CHECK(g.pending.load() && g.awaitingAck.load() && g.expectedGeneration.load() == 0);
  CHECK(g.epoch.load() == 1);
  CHECK(g.EpochChanged(seen) && seen == 1);
  CHECK(!g.EpochChanged(seen));
  // frames before the ack: dropped whatever their generation
  CHECK(g.AdmitGeneration(3) == SelectionAdmit::DropAwaitingAck);
  CHECK(g.AdmitGeneration(4) == SelectionAdmit::DropAwaitingAck);
  CHECK(!g.AckedSelectionPending());
  // the ack names generation 4
  CHECK(g.ApplyAck(true, 4) == SelectionAck::Acked);
  CHECK(!g.awaitingAck.load() && g.expectedGeneration.load() == 4 && g.pending.load());
  CHECK(g.AckedSelectionPending());
  CHECK(g.AdmitGeneration(3) == SelectionAdmit::DropWrongGeneration);   // old target still draining
  CHECK(g.AdmitGeneration(4) == SelectionAdmit::Accept);
  // the recv thread records the first frame and posts once; a second post is suppressed by the latch
  CHECK(g.RecordReveal(4, g.epoch.load()));
  CHECK(!g.RecordReveal(4, g.epoch.load()));
  CHECK(g.readyGeneration.load() == 4 && g.readyEpoch.load() == 1 && g.revealPosted.load());
  // the UI commits: the active generation becomes the persistent filter
  CHECK(g.CommitReveal());
  CHECK(g.activeStreamGeneration.load() == 4);
  g.Clear();
  g.ReleaseRevealLatch();
  CHECK(!g.pending.load() && !g.revealPosted.load());
  CHECK(g.AdmitGeneration(4) == SelectionAdmit::Accept);
  CHECK(g.AdmitGeneration(3) == SelectionAdmit::DropStraggler);   // late frame of the previous target
  CHECK(g.AdmitGeneration(5) == SelectionAdmit::DropStraggler);   // not ours either
  CHECK(!g.AckedSelectionPending());
  // a later legitimate first frame can post again (latch released)
  g.Begin();
  CHECK(g.ApplyAck(true, 9) == SelectionAck::Acked);
  CHECK(g.RecordReveal(9, g.epoch.load()));
}

void test_ack_failure_and_not_pending() {
  std::printf("[T2] a failed ack leaves the transaction to the caller to clear; no transaction -> NotPending\n");
  SelectionGateState g;
  CHECK(g.ApplyAck(true, 4) == SelectionAck::NotPending);
  CHECK(g.ApplyAck(false, 4) == SelectionAck::NotPending);
  g.Begin();
  CHECK(g.ApplyAck(false, 4) == SelectionAck::Failed);
  // ApplyAck itself touches nothing on failure: the caller stops the stream first, then clears
  CHECK(g.pending.load() && g.awaitingAck.load());
  g.Clear();
  CHECK(!g.pending.load() && !g.awaitingAck.load() && g.expectedGeneration.load() == 0);
  CHECK(g.AdmitGeneration(4) == SelectionAdmit::Accept);   // activeStreamGeneration still 0
  // ack with generation 0: any generation is accepted while pending (expectedGen == 0)
  g.Begin();
  CHECK(g.ApplyAck(true, 0) == SelectionAck::Acked);
  CHECK(g.AdmitGeneration(12) == SelectionAdmit::Accept);
}

void test_reveal_races() {
  std::printf("[T2] reveal race: cancel / new pick / disconnect between the post and the commit -> no commit, latch released\n");
  // 1. cleared (disconnect) before the UI handles the post
  {
    SelectionGateState g;
    g.Begin();
    g.ApplyAck(true, 4);
    CHECK(g.RecordReveal(4, g.epoch.load()));
    g.Clear();
    CHECK(!g.CommitReveal());
    CHECK(g.activeStreamGeneration.load() == 0);
    g.ReleaseRevealLatch();
    CHECK(!g.revealPosted.load());
  }
  // 2. a new pick (epoch bumped) before the commit: the recorded epoch no longer matches
  {
    SelectionGateState g;
    g.Begin();
    g.ApplyAck(true, 4);
    CHECK(g.RecordReveal(4, g.epoch.load()));
    g.Begin();                       // epoch 2, awaiting a new ack
    CHECK(!g.CommitReveal());
    g.ReleaseRevealLatch();
    g.ApplyAck(true, 6);
    CHECK(g.AdmitGeneration(4) == SelectionAdmit::DropWrongGeneration);
    CHECK(g.AdmitGeneration(6) == SelectionAdmit::Accept);
    CHECK(g.RecordReveal(6, g.epoch.load()));
    CHECK(g.CommitReveal());
    CHECK(g.activeStreamGeneration.load() == 6);
  }
  // 3. the ack arrives with a different generation than the recorded candidate
  {
    SelectionGateState g;
    g.Begin();
    g.ApplyAck(true, 4);
    CHECK(g.RecordReveal(5, g.epoch.load()));   // candidate of the wrong generation
    CHECK(!g.CommitReveal());
    g.ReleaseRevealLatch();
  }
  // 4. still awaiting the ack when the post is handled
  {
    SelectionGateState g;
    g.Begin();
    CHECK(g.RecordReveal(4, g.epoch.load()));
    CHECK(!g.CommitReveal());
  }
}

// rel-0163 r3/r4 (Codex codex_rel_0163_e1_decision.md, codex_rel_0163_src_r3.md): the local (logon-screen
// desktop) transaction -- bound by op token, never by a live read.
void test_local_ackless_selection() {
  {
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t op = g.epoch.load();
    CHECK(g.pending.load() && !g.awaitingAck.load() && g.localAckless.load());
    CHECK(g.LocalUnbound(op) && g.IsLiveLocal(op));
    // Before its decoded keyframe binds a generation: frames pass the gate (the decoder waits for a keyframe
    // anyway), but nothing can reveal -- no acked-pending state, a recorded candidate does not commit.
    CHECK(g.AdmitGeneration(7) == SelectionAdmit::Accept);
    CHECK(!g.AckedSelectionPending());
    CHECK(g.RecordReveal(7, op));
    CHECK(!g.CommitReveal());
    g.ReleaseRevealLatch();
    // Bound for THIS op, once.
    CHECK(!g.BindLocalGeneration(op + 1, 7));  // another op's token
    CHECK(g.BindLocalGeneration(op, 7));
    CHECK(!g.LocalUnbound(op));
    CHECK(!g.BindLocalGeneration(op, 9));
    CHECK(g.expectedGeneration.load() == 7);
    CHECK(g.AckedSelectionPending());
    CHECK(g.AdmitGeneration(9) == SelectionAdmit::DropWrongGeneration);
    CHECK(g.AdmitGeneration(7) == SelectionAdmit::Accept);
    // A candidate of another generation, or of an older op, does not commit; the right one does.
    CHECK(g.RecordReveal(9, op));
    CHECK(!g.CommitReveal());
    g.ReleaseRevealLatch();
    CHECK(g.RecordReveal(7, op - 1));
    CHECK(!g.CommitReveal());
    g.ReleaseRevealLatch();
    CHECK(g.RecordReveal(7, op));
    CHECK(g.CommitReveal());
    CHECK(g.activeStreamGeneration.load() == 7);
  }
  {
    // R3 (ABA): op A's frame held -> A cancelled -> B begins (unbound again) -> A's frame comes back: it binds
    // nothing of B's, and is not B's live op; B binds with its own token.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t a = g.epoch.load();
    g.Clear();
    g.BeginLocal();
    const uint64_t b = g.epoch.load();
    CHECK(a != b);
    CHECK(!g.BindLocalGeneration(a, 5));
    CHECK(!g.IsLiveLocal(a) && g.IsLiveLocal(b) && g.LocalUnbound(b));
    CHECK(g.RecordReveal(5, a));
    CHECK(!g.CommitReveal());  // A's candidate never reveals B
    g.ReleaseRevealLatch();
    CHECK(g.BindLocalGeneration(b, 6));
    CHECK(g.RecordReveal(6, b));
    CHECK(g.CommitReveal());
  }
  {
    // Generation 0 is never bound (nothing names it).
    SelectionGateState g;
    g.BeginLocal();
    CHECK(!g.BindLocalGeneration(g.epoch.load(), 0));
    CHECK(g.LocalUnbound(g.epoch.load()));
  }
  {
    // Cancel (Clear) ends it: no bind, no reveal, the local mode is gone.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t op = g.epoch.load();
    g.Clear();
    CHECK(!g.localAckless.load() && !g.LocalUnbound(op) && !g.IsLiveLocal(op));
    CHECK(!g.BindLocalGeneration(op, 7));
    CHECK(g.RecordReveal(7, op));
    CHECK(!g.CommitReveal());
  }
  {
    // A new pick after a local one is an ordinary (acked) transaction again.
    SelectionGateState g;
    g.BeginLocal();
    g.Clear();
    g.Begin();
    CHECK(!g.localAckless.load() && g.awaitingAck.load());
    CHECK(g.AdmitGeneration(7) == SelectionAdmit::DropAwaitingAck);
  }
  {
    // R2: an ordinary op whose WindowSelect(0) was not sendable continues as local -- by ITS tag only; another
    // op's tag changes nothing.
    SelectionGateState g;
    g.Begin();
    const uint64_t op = g.epoch.load();
    CHECK(!g.ConvertToLocal(op + 1));
    CHECK(!g.ConvertToLocal(op - 1));
    CHECK(g.awaitingAck.load() && !g.localAckless.load());
    CHECK(g.ConvertToLocal(op));
    CHECK(!g.awaitingAck.load() && g.localAckless.load() && g.LocalUnbound(op) && g.epoch.load() == op);
    CHECK(!g.ConvertToLocal(op));  // not twice
    CHECK(g.BindLocalGeneration(op, 3));
    CHECK(g.RecordReveal(3, op));
    CHECK(g.CommitReveal());
  }
  {
    // R2: A's unsendable notice after A was cancelled and a local B began: B is untouched.
    SelectionGateState g;
    g.Begin();
    const uint64_t a = g.epoch.load();
    g.Clear();
    g.BeginLocal();
    const uint64_t b = g.epoch.load();
    CHECK(!g.ConvertToLocal(a));
    CHECK(g.pending.load() && g.localAckless.load() && g.LocalUnbound(b));
  }
  {
    // R4: a late host ack -- success or failure, before or after the bind -- never touches a local op.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t op = g.epoch.load();
    CHECK(g.ApplyAck(true, 5) == SelectionAck::IgnoredLocal);
    CHECK(g.ApplyAck(false, 0) == SelectionAck::IgnoredLocal);
    CHECK(g.expectedGeneration.load() == 0 && g.pending.load() && g.localAckless.load() && g.LocalUnbound(op));
    CHECK(g.BindLocalGeneration(op, 8));
    CHECK(g.ApplyAck(true, 5) == SelectionAck::IgnoredLocal);
    CHECK(g.ApplyAck(false, 0) == SelectionAck::IgnoredLocal);
    CHECK(g.expectedGeneration.load() == 8 && g.pending.load());
    // ...and the local op still completes on its own frame.
    CHECK(g.RecordReveal(8, op));
    CHECK(g.CommitReveal());
  }
  {
    // Control: an ordinary op's own ack still works.
    SelectionGateState g;
    g.Begin();
    CHECK(g.ApplyAck(true, 4) == SelectionAck::Acked);
    CHECK(g.RecordReveal(4, g.epoch.load()));
    CHECK(g.CommitReveal());
  }
  {
    // Not pending: nothing to convert or bind.
    SelectionGateState g;
    CHECK(!g.ConvertToLocal(g.epoch.load()));
    CHECK(!g.BindLocalGeneration(g.epoch.load(), 4));
  }
  {
    // r5 (Codex R3): the FINAL adoption. A local op's frame with no decoded keyframe yet (candidate 0) is not
    // adopted and binds nothing; with one, it binds and is adopted; after the op was cancelled and B began, A's
    // frame is refused (and is never re-read as an ordinary frame); B adopts its own.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t a = g.epoch.load();
    uint64_t gen = 0;
    CHECK(!g.CommitFrame(a, true, 0, 4, &gen, nullptr));
    CHECK(g.expectedGeneration.load() == 0);
    g.Clear();
    g.BeginLocal();
    const uint64_t b = g.epoch.load();
    CHECK(!g.CommitFrame(a, true, 4, 4, &gen, nullptr));  // A's picture after B began
    CHECK(g.expectedGeneration.load() == 0 && g.localFramesRefused.load() >= 1);
    g.Clear();
    CHECK(!g.CommitFrame(b, true, 4, 4, &gen, nullptr));  // B's picture after B was cancelled (nothing pending)
    g.BeginLocal();
    const uint64_t c = g.epoch.load();
    CHECK(g.CommitFrame(c, true, 6, 6, &gen, nullptr) && gen == 6 && g.expectedGeneration.load() == 6);
    CHECK(g.CommitFrame(c, true, 6, 6, &gen, nullptr) && gen == 6);  // later frames of the bound op
    CHECK(g.RecordReveal(gen, c));
    CHECK(g.CommitReveal());
    g.Clear();  // what the reveal's UI handler does
    CHECK(g.CommitFrame(c, true, 6, 6, &gen, nullptr));  // the same op, revealed: its stream goes on
    CHECK(g.CommitFrame(0, false, 0, 9, &gen, nullptr) && gen == 9);  // an ordinary frame: as before
  }
  {
    // r6 (Codex R3 2): op R revealed at generation 7; a new local op A begins and is cancelled before its reveal.
    // A's picture is refused (the same epoch as A, but A was never revealed); R's own later picture is refused too
    // (R is no longer the epoch); while R was the epoch, its later pictures went on.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t r = g.epoch.load();
    uint64_t gen = 0;
    CHECK(g.CommitFrame(r, true, 7, 7, &gen, nullptr) && gen == 7);
    CHECK(g.RecordReveal(7, r));
    CHECK(g.CommitReveal());
    g.Clear();
    CHECK(g.CommitFrame(r, true, 7, 7, &gen, nullptr) && gen == 7);  // R revealed: its stream goes on
    g.BeginLocal();
    const uint64_t a = g.epoch.load();
    g.Clear();  // A cancelled before its reveal
    CHECK(!g.CommitFrame(a, true, 9, 9, &gen, nullptr));
    CHECK(!g.CommitFrame(r, true, 7, 7, &gen, nullptr));
  }
  {
    // r6 (R3 1): a permitted picture keeps the gate's lock until the caller lets it go -- a Clear/Begin waits.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t a = g.epoch.load();
    uint64_t gen = 0;
    std::unique_lock<std::mutex> held;
    CHECK(g.CommitFrame(a, true, 5, 5, &gen, &held) && held.owns_lock());
    std::atomic<bool> began{false};
    std::thread other([&] {
      g.Clear();
      g.BeginLocal();
      began = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(!began.load());  // blocked while the write is still being made
    held.unlock();
    other.join();
    CHECK(began.load() && g.epoch.load() == a + 1);
  }
  {
    // r5 (Codex R4): the host ack and the panel are one boundary -- a local op: the panel is never applied.
    SelectionGateState g;
    int panelApplied = 0;
    g.BeginLocal();
    CHECK(g.ApplyHostAck([&] { ++panelApplied; return std::make_pair(true, uint64_t{5}); }) == SelectionAck::IgnoredLocal);
    CHECK(g.ApplyHostAck([&] { ++panelApplied; return std::make_pair(false, uint64_t{0}); }) == SelectionAck::IgnoredLocal);
    CHECK(panelApplied == 0 && g.expectedGeneration.load() == 0 && g.pending.load());
    g.Clear();
    g.Begin();  // an ordinary op: its own ack applies the panel and the gate
    CHECK(g.ApplyHostAck([&] { ++panelApplied; return std::make_pair(true, uint64_t{5}); }) == SelectionAck::Acked);
    CHECK(panelApplied == 1 && g.expectedGeneration.load() == 5 && !g.awaitingAck.load());
  }
}

}  // namespace

int main() {
  test_pick_ack_first_frame_reveal();
  test_ack_failure_and_not_pending();
  test_reveal_races();
  test_local_ackless_selection();
  if (gFailures != 0) {
    std::printf("viewer_selection_gate_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("viewer_selection_gate_test: PASS\n");
  return 0;
}
