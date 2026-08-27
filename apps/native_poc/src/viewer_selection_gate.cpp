// The PC-side target-selection gate's transitions (viewer split refactor Phase 2-5). Each member is the
// atomic sequence the former free function / inline block performed on the gSelection* globals, in the
// same order with the same memory orders; the callers keep everything that touches other state (the
// panel model, the stream request, the picker, the toolbar, PostMessage).

#include "viewer_selection_gate.hpp"

namespace remote60::native_poc::viewer {

void SelectionGateState::Begin() {
  expectedGeneration.store(0, std::memory_order_release);
  awaitingAck.store(true, std::memory_order_release);
  pending.store(true, std::memory_order_release);
  // Bumped so the receive loop resets the decoder and holds for the new generation's keyframe.
  epoch.fetch_add(1, std::memory_order_acq_rel);
}

void SelectionGateState::Clear() {
  pending.store(false, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  expectedGeneration.store(0, std::memory_order_release);
}

SelectionAck SelectionGateState::ApplyAck(bool ok, uint64_t streamGeneration) {
  if (!pending.load(std::memory_order_acquire)) return SelectionAck::NotPending;
  if (ok) {
    // Ack received: hold the picker up until the first frame of this generation is presented.
    // Do NOT hide the picker here -- that is what the first-frame gate is for.
    expectedGeneration.store(streamGeneration, std::memory_order_release);
    awaitingAck.store(false, std::memory_order_release);
    return SelectionAck::Acked;
  }
  return SelectionAck::Failed;
}

bool SelectionGateState::EpochChanged(uint64_t& seenEpoch) {
  if (epoch.load(std::memory_order_acquire) != seenEpoch) {
    seenEpoch = epoch.load(std::memory_order_acquire);
    return true;
  }
  return false;
}

SelectionAdmit SelectionGateState::AdmitGeneration(uint64_t streamGeneration) const {
  if (pending.load(std::memory_order_acquire)) {
    if (awaitingAck.load(std::memory_order_acquire)) {
      // No ack yet: every frame here is either the old target or an unconfirmed guess.
      return SelectionAdmit::DropAwaitingAck;
    }
    const uint64_t expectedGen = expectedGeneration.load(std::memory_order_acquire);
    if (expectedGen != 0 && streamGeneration != expectedGen) {
      // The previous target's stream still draining after the ack; not what we selected.
      return SelectionAdmit::DropWrongGeneration;
    }
    return SelectionAdmit::Accept;
  }
  // No selection in flight. After a reveal, only the active target's generation is welcome:
  // a late straggler from the previously selected target, still in flight on the wire, would
  // otherwise flash on screen. activeStreamGeneration==0 means no PC-side selection has
  // taken effect (legacy stream-view start, or before the first pick), so accept anything as
  // before. Host auto-resolution changes keep the same generation, so this does not fight
  // them -- only a host-side target selection bumps the generation.
  const uint64_t activeGen = activeStreamGeneration.load(std::memory_order_acquire);
  if (activeGen != 0 && streamGeneration != activeGen) {
    return SelectionAdmit::DropStraggler;
  }
  return SelectionAdmit::Accept;
}

bool SelectionGateState::AckedSelectionPending() const {
  return pending.load(std::memory_order_acquire) && !awaitingAck.load(std::memory_order_acquire);
}

bool SelectionGateState::RecordReveal(uint64_t readyGen, uint64_t readyEp) {
  readyGeneration.store(readyGen, std::memory_order_release);
  readyEpoch.store(readyEp, std::memory_order_release);
  bool expected = false;
  return revealPosted.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
}

bool SelectionGateState::CommitReveal() {
  // The video thread saw the first frame of a selection and posted this once. Revalidate
  // against the live selection state before committing: a cancel / new selection / disconnect
  // may have raced the post, and closing the picker then would be wrong. Require that the same
  // transaction is still pending, its ack is in, and the recorded epoch and generation still
  // match. Always release the latch at the end so a later legitimate first frame can re-post.
  const bool commit =
      pending.load(std::memory_order_acquire) &&
      !awaitingAck.load(std::memory_order_acquire) &&
      epoch.load(std::memory_order_acquire) ==
          readyEpoch.load(std::memory_order_acquire) &&
      expectedGeneration.load(std::memory_order_acquire) ==
          readyGeneration.load(std::memory_order_acquire);
  if (commit) {
    // Persistent filter for late stragglers from the previous target (see the recv gate).
    activeStreamGeneration.store(readyGeneration.load(std::memory_order_acquire),
                                 std::memory_order_release);
  }
  return commit;
}

void SelectionGateState::ReleaseRevealLatch() {
  revealPosted.store(false, std::memory_order_release);
}

}  // namespace remote60::native_poc::viewer
