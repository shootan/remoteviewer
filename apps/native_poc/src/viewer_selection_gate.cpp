// The PC-side target-selection gate's transitions (viewer split refactor Phase 2-5). Each member is the
// atomic sequence the former free function / inline block performed on the gSelection* globals, in the
// same order with the same memory orders; the callers keep everything that touches other state (the
// panel model, the stream request, the picker, the toolbar, PostMessage).

#include "viewer_selection_gate.hpp"

namespace remote60::native_poc::viewer {

void SelectionGateState::Begin() {
  std::lock_guard<std::mutex> lk(mu);
  localAckless.store(false, std::memory_order_release);
  expectedGeneration.store(0, std::memory_order_release);
  awaitingAck.store(true, std::memory_order_release);
  pending.store(true, std::memory_order_release);
  // Bumped so the receive loop resets the decoder and holds for the new generation's keyframe.
  epoch.fetch_add(1, std::memory_order_acq_rel);
}

void SelectionGateState::Clear() {
  std::lock_guard<std::mutex> lk(mu);
  pending.store(false, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  expectedGeneration.store(0, std::memory_order_release);
  localAckless.store(false, std::memory_order_release);
}
// rel-0163 r3: the same transaction as Begin -- pending, a new epoch (the receive loop resets the decoder and
// waits for a keyframe) -- without an ack to wait for. Its generation is bound by its first decoded keyframe.
void SelectionGateState::BeginLocal() {
  std::lock_guard<std::mutex> lk(mu);
  expectedGeneration.store(0, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  localAckless.store(true, std::memory_order_release);
  pending.store(true, std::memory_order_release);
  epoch.fetch_add(1, std::memory_order_acq_rel);
}
// A transaction begun the ordinary way whose WindowSelect(0) turned out unsendable (the host's role became
// known as the logon screen before it went out): the SAME op -- epoch unchanged, its decoder reset already done --
// continues as a local one. Only that op (its own tag), only while it still waits for its ack.
bool SelectionGateState::ConvertToLocal(uint64_t opEpoch) {
  std::lock_guard<std::mutex> lk(mu);
  if (!pending.load(std::memory_order_acquire) || !awaitingAck.load(std::memory_order_acquire) ||
      localAckless.load(std::memory_order_acquire) || epoch.load(std::memory_order_acquire) != opEpoch) {
    return false;
  }
  expectedGeneration.store(0, std::memory_order_release);
  localAckless.store(true, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  return true;
}
bool SelectionGateState::LocalUnbound(uint64_t opEpoch) const {
  std::lock_guard<std::mutex> lk(mu);
  return pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire) &&
         epoch.load(std::memory_order_acquire) == opEpoch && expectedGeneration.load(std::memory_order_acquire) == 0;
}
bool SelectionGateState::IsLiveLocal(uint64_t opEpoch) const {
  std::lock_guard<std::mutex> lk(mu);
  return pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire) &&
         epoch.load(std::memory_order_acquire) == opEpoch;
}
bool SelectionGateState::BindLocalGeneration(uint64_t opEpoch, uint64_t streamGeneration) {
  std::lock_guard<std::mutex> lk(mu);
  if (streamGeneration == 0 || !pending.load(std::memory_order_acquire) || !localAckless.load(std::memory_order_acquire) ||
      epoch.load(std::memory_order_acquire) != opEpoch || expectedGeneration.load(std::memory_order_acquire) != 0) {
    return false;
  }
  expectedGeneration.store(streamGeneration, std::memory_order_release);
  return true;
}

bool SelectionGateState::CommitFrame(uint64_t opEpoch, bool frameLocal, uint64_t candidateGen, uint64_t inputGeneration,
                                     uint64_t* revealGen) {
  std::lock_guard<std::mutex> lk(mu);
  if (revealGen) *revealGen = inputGeneration;
  if (!frameLocal) return true;  // an ordinary frame: as before (the generation gate decided it)
  if (!pending.load(std::memory_order_acquire) || !localAckless.load(std::memory_order_acquire) ||
      epoch.load(std::memory_order_acquire) != opEpoch) {
    // Its op was cancelled / replaced / revealed-and-closed meanwhile. A revealed local op's later pictures are
    // ordinary from then on (pending is over) -- but this frame was taken for the op, and only the op it was
    // taken for may own it while that op is pending; after the reveal the active-generation filter applies.
    if (!pending.load(std::memory_order_acquire) && activeStreamGeneration.load(std::memory_order_acquire) != 0 &&
        epoch.load(std::memory_order_acquire) == opEpoch) {
      if (revealGen) *revealGen = activeStreamGeneration.load(std::memory_order_acquire);
      return true;  // the same op, already revealed: its stream goes on
    }
    localFramesRefused.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  if (expectedGeneration.load(std::memory_order_acquire) == 0) {
    if (candidateGen == 0) return false;  // no decoded keyframe of this op yet: nothing to stand for
    expectedGeneration.store(candidateGen, std::memory_order_release);
  }
  if (revealGen) *revealGen = expectedGeneration.load(std::memory_order_acquire);
  return true;
}

SelectionAck SelectionGateState::ApplyHostAck(const std::function<std::pair<bool, uint64_t>()>& applyPanel) {
  std::lock_guard<std::mutex> lk(mu);
  if (pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire)) {
    return SelectionAck::IgnoredLocal;  // the panel is not touched either
  }
  const std::pair<bool, uint64_t> r = applyPanel();
  if (!pending.load(std::memory_order_acquire)) return SelectionAck::NotPending;
  if (r.first) {
    expectedGeneration.store(r.second, std::memory_order_release);
    awaitingAck.store(false, std::memory_order_release);
    return SelectionAck::Acked;
  }
  return SelectionAck::Failed;
}

SelectionAck SelectionGateState::ApplyAck(bool ok, uint64_t streamGeneration) {
  std::lock_guard<std::mutex> lk(mu);
  if (!pending.load(std::memory_order_acquire)) return SelectionAck::NotPending;
  // rel-0163 r4 (Codex R4): a local op asked for no ack; an ack arriving now is an older op's -- success or
  // failure, bound or not, it changes nothing here.
  if (localAckless.load(std::memory_order_acquire)) return SelectionAck::IgnoredLocal;
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
  std::lock_guard<std::mutex> lk(mu);
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
  std::lock_guard<std::mutex> lk(mu);
  // rel-0163 r3: a local transaction counts once its generation is bound (its first keyframe), not before.
  return pending.load(std::memory_order_acquire) && !awaitingAck.load(std::memory_order_acquire) &&
         !(localAckless.load(std::memory_order_acquire) && expectedGeneration.load(std::memory_order_acquire) == 0);
}

bool SelectionGateState::RecordReveal(uint64_t readyGen, uint64_t readyEp) {
  readyGeneration.store(readyGen, std::memory_order_release);
  readyEpoch.store(readyEp, std::memory_order_release);
  bool expected = false;
  return revealPosted.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
}

bool SelectionGateState::CommitReveal() {
  std::lock_guard<std::mutex> lk(mu);
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
