// Unit test for the viewer's PC-side target-selection gate (viewer split refactor Phase 2 / T2):
// the transaction from a pick through the host's ack to the first presented frame, the frame
// admission rules on both sides of the ack, the persistent active-generation filter after a reveal,
// and the reveal race (a cancel / new pick / disconnect between the recv thread's post and the UI
// thread's commit must not close the picker). Atomics only -- no window, no sockets.
//
// Build: remote60_viewer_selection_gate_test (CMake). Run: prints "viewer_selection_gate_test: PASS".

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

}  // namespace

int main() {
  test_pick_ack_first_frame_reveal();
  test_ack_failure_and_not_pending();
  test_reveal_races();
  if (gFailures != 0) {
    std::printf("viewer_selection_gate_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("viewer_selection_gate_test: PASS\n");
  return 0;
}
