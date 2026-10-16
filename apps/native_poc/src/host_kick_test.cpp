// Pins the trailing-edge kick and static-refresh decisions of KickState (host_kick.hpp): arming and the
// 150ms deadline, the raw-mode no-op, one kick per held input, the barrier override, and the refresh
// cadence anchored on both the last emitted AU and the last attempt. Time is an argument.
//
// Host split refactor Phase 2-T3 (2026-08-26).

#include "host_kick.hpp"
#include "host_encode_admission.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using remote60::native_poc::KickState;

namespace {

int gFailures = 0;

void expect(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++gFailures;
  }
}

constexpr uint64_t kSec = 1000000ULL;

void TestArmDueCancel() {
  KickState k;
  expect(!k.Due(5 * kSec), "kick: nothing armed -> not due");
  k.Arm(5 * kSec, true);
  expect(k.pending && k.dueAtUs == 5 * kSec + KickState::kTrailingKickDelayUs, "kick: arm sets the 150ms deadline");
  expect(!k.Due(5 * kSec + KickState::kTrailingKickDelayUs - 1), "kick: not due one microsecond early");
  expect(k.Due(5 * kSec + KickState::kTrailingKickDelayUs), "kick: due at the deadline");
  k.Arm(6 * kSec, true);  // motion keeps pushing the deadline out
  expect(k.dueAtUs == 6 * kSec + KickState::kTrailingKickDelayUs, "kick: re-arm moves the deadline");
  k.Cancel();
  expect(!k.pending && k.dueAtUs == 0 && !k.Due(100 * kSec), "kick: cancel disarms");
  KickState raw;
  raw.Arm(5 * kSec, false);
  KickState typing;
  typing.Arm(5 * kSec, true, 60);
  expect(typing.dueAtUs == 5 * kSec + 33333, "typing: 60fps tail is released after two frames, not 150ms");
  typing.Arm(5 * kSec, true, 30);
  expect(typing.dueAtUs == 5 * kSec + 66666, "typing: 30fps tail leaves room for the next real frame");
  expect(!raw.pending, "kick: raw mode never arms");
}

void TestOneKickPerHeldInput() {
  KickState k;
  expect(!k.NeedKick(false), "kick: no input yet -> nothing to flush");
  k.lastRealInputCaptureUs = 10 * kSec;  // a real frame went into the MFT ...
  k.lastEmittedAuCaptureUs = 9 * kSec;   // ... and the newest AU out is older: the input is held
  expect(k.NeedKick(false), "kick: held input needs a kick");
  k.MarkKickedForCurrentInput();
  expect(!k.NeedKick(false), "kick: the same held input is not kicked twice");
  k.lastRealInputCaptureUs = 11 * kSec;  // a newer input, again held
  expect(k.NeedKick(false), "kick: a newer held input is kicked once more");
  k.lastEmittedAuCaptureUs = 11 * kSec;  // its AU came out
  expect(!k.NeedKick(false), "kick: nothing held once the newest input emerged");
}

void TestBarrierOverridesTheGuard() {
  KickState k;
  k.lastRealInputCaptureUs = 10 * kSec;
  k.lastEmittedAuCaptureUs = 10 * kSec;  // nothing held ...
  expect(k.NeedKick(true), "kick: a closed barrier kicks even with nothing held");
  k.lastEmittedAuCaptureUs = 9 * kSec;
  k.MarkKickedForCurrentInput();          // ... or already kicked
  expect(k.NeedKick(true), "kick: a closed barrier ignores the one-shot guard");
  expect(!k.NeedKick(false), "kick: open barrier + already kicked -> quiet");
}

void TestStaticRefreshCadence() {
  KickState k;
  k.staticRefreshIntervalUs = kSec;
  expect(!k.StaticRefreshDue(50 * kSec), "refresh: never before the first emitted AU");
  k.lastEmittedAuCaptureUs = 5 * kSec;
  expect(!k.StaticRefreshDue(5 * kSec + 500000), "refresh: not inside the interval after the last AU");
  expect(k.StaticRefreshDue(6 * kSec), "refresh: due one interval after the last AU");
  k.lastStaticRefreshAttemptUs = 6 * kSec;  // an attempt (even one the MFT answered with no output)
  expect(!k.StaticRefreshDue(6 * kSec + 500000), "refresh: the attempt clock holds the next try");
  expect(k.StaticRefreshDue(7 * kSec), "refresh: due one interval after the attempt");
  k.Arm(7 * kSec, true);
  expect(!k.StaticRefreshDue(8 * kSec), "refresh: a pending trailing kick blocks the refresh");
  k.Cancel();
  expect(k.StaticRefreshDue(8 * kSec), "refresh: resumes once the kick is gone");
}

}  // namespace

// Field bug (0.2.97, "slow after a UAC"): a kick is stamped "now"; a real frame that a slow readback
// publishes late reaches the encoder after it with an OLDER stamp, and the viewer reads the reversal
// as a stale reference (~1 Hz decoder-reset + IDR storm). The real stamp must be clamped to strictly
// after the last stamp handed to the encoder; a real frame that is already newer keeps its own stamp.
void TestRealStampClampedBehindSyntheticStamp() {
  KickState k;
  expect(KickState::ClampRealStamp(10 * kSec, 0) == 10 * kSec, "stamp: nothing fed yet -> real stamp untouched");
  k.NoteEncoderStamp(10 * kSec, false);           // real frame captured at 10s fed
  k.NoteEncoderStamp(10 * kSec + 150000, true);   // trailing kick stamped "now" = 10.15s
  expect(k.lastEncoderStampUs == 10 * kSec + 150000 && k.lastSyntheticStampUs == 10 * kSec + 150000,
         "stamp: the kick stamp is recorded as the last encoder stamp");
  // a real frame captured at 10.05s but published only now (slow readback) -> behind the kick
  const uint64_t late = KickState::ClampRealStamp(10 * kSec + 50000, k.lastEncoderStampUs);
  expect(late == 10 * kSec + 150001, "stamp: a late real frame moves to just after the kick stamp");
  k.NoteEncoderStamp(late, false);
  expect(k.lastSyntheticStampUs == 10 * kSec + 150000, "stamp: a real note leaves the synthetic record alone");
  // a second late real frame (captured 10.08s, also published late) lands strictly after the first
  const uint64_t late2 = KickState::ClampRealStamp(10 * kSec + 80000, k.lastEncoderStampUs);
  expect(late2 == 10 * kSec + 150002, "stamp: consecutive late real frames keep strictly increasing stamps");
  k.NoteEncoderStamp(late2, false);
  // a real frame captured after the kick keeps its own stamp
  expect(KickState::ClampRealStamp(10 * kSec + 400000, k.lastEncoderStampUs) == 10 * kSec + 400000,
         "stamp: a newer real frame is not touched");
  // an equal stamp is a reversal too (the timeline must be strictly increasing)
  expect(KickState::ClampRealStamp(k.lastEncoderStampUs, k.lastEncoderStampUs) == k.lastEncoderStampUs + 1,
         "stamp: an equal stamp is bumped by one microsecond");
  // an older note never moves the record backwards
  k.NoteEncoderStamp(5 * kSec, false);
  expect(k.lastEncoderStampUs == late2, "stamp: the record only moves forward");
}

// encoder-output-poll r2, experiment C: the trailing-refine decision (KickState::DecideTrailing) driven
// through episodes the way stage_pop_frame applies it (the state updates below mirror that call site;
// the decision itself is the product function). Admission: the refine is gated as a delta.
struct Env {
  bool realWaiting = false, barrierClosed = false, forceKey = false, streamActive = true, epochMatch = true,
       senderBusy = false;
};
using TA = KickState::TrailingAction;
TA DueOnce(KickState& k, const Env& e, bool* rearm) {
  KickState::TrailingInputs ti;
  ti.realWaiting = e.realWaiting;
  ti.needKick = k.NeedKick(e.barrierClosed) || e.forceKey;
  ti.refineEnabled = true;
  ti.refineOwed = k.RefineOwed();
  ti.streamActive = e.streamActive;
  ti.epochMatch = e.epochMatch;
  ti.senderBlocked = e.barrierClosed || e.senderBusy;
  ti.alreadyDeferred = k.refineDeferredForInputCaptureUs == k.lastRealInputCaptureUs;
  const TA a = KickState::DecideTrailing(ti);
  *rearm = false;
  if (a == TA::Kick || a == TA::Refine || a == TA::Cancel) k.MarkKickedForCurrentInput();
  if (a == TA::Defer) { k.refineDeferredForInputCaptureUs = k.lastRealInputCaptureUs; *rearm = true; }
  return a;
}

void TestTrailingRefine() {
  using remote60::native_poc::EncodeAdmission;
  using remote60::native_poc::EncodeAdmissionInputs;
  using remote60::native_poc::decide_encode_admission;
  bool rearm = false;
  Env e;
  {  // [r1] output already out -> one refine, then nothing for the same input
    KickState k;
    k.lastRealInputCaptureUs = 100; k.lastEmittedAuCaptureUs = 100;
    expect(DueOnce(k, e, &rearm) == TA::Refine && !rearm, "refine: emitted input -> one refine");
    expect(DueOnce(k, e, &rearm) == TA::None, "refine: same input -> no second synthetic");
    k.lastEmittedAuCaptureUs = 200;  // the refine's own AU (stamped later) comes out
    expect(DueOnce(k, e, &rearm) == TA::None, "refine: a synthetic's AU does not reopen the episode");
  }
  {  // [r2] held output -> the existing kick, which is also the episode's refine (merged, 1 input)
    KickState k;
    k.lastRealInputCaptureUs = 100; k.lastEmittedAuCaptureUs = 50;
    expect(DueOnce(k, e, &rearm) == TA::Kick, "refine: held input -> the liveness kick");
    k.lastEmittedAuCaptureUs = 100;
    expect(DueOnce(k, e, &rearm) == TA::None, "refine: kick + refine merged (no second input)");
  }
  {  // [r3] a newer real frame waiting wins; the new input opens its own episode, the old one is gone
    KickState k;
    k.lastRealInputCaptureUs = 100; k.lastEmittedAuCaptureUs = 100;
    Env w = e; w.realWaiting = true;
    expect(DueOnce(k, w, &rearm) == TA::None, "refine: a waiting real frame cancels the synthetic");
    k.lastRealInputCaptureUs = 300; k.lastEmittedAuCaptureUs = 300;  // the real frame encoded + emitted
    expect(DueOnce(k, e, &rearm) == TA::Refine, "refine: the newest real input gets the refine");
    expect(DueOnce(k, e, &rearm) == TA::None, "refine: and only once");
  }
  {  // [r4] static refresh / kicks never open an episode: no real input -> nothing owed
    KickState k;
    expect(!k.RefineOwed() && DueOnce(k, e, &rearm) == TA::None, "refine: no real input -> nothing");
    k.lastRealInputCaptureUs = 100; k.lastEmittedAuCaptureUs = 100;
    (void)DueOnce(k, e, &rearm);
    k.staticRefreshCount = 5; k.lastEmittedAuCaptureUs = 900;  // refreshes later, same real input
    expect(!k.RefineOwed() && DueOnce(k, e, &rearm) == TA::None, "refine: a refresh does not reopen");
  }
  {  // [r5] paused stream / new epoch -> cancel, owed cleared
    KickState k;
    k.lastRealInputCaptureUs = 100; k.lastEmittedAuCaptureUs = 100;
    Env p = e; p.streamActive = false;
    expect(DueOnce(k, p, &rearm) == TA::Cancel && !k.RefineOwed(), "refine: paused -> cancel, not owed");
    expect(DueOnce(k, e, &rearm) == TA::None, "refine: resume does not revive the cancelled episode");
    KickState k2;
    k2.lastRealInputCaptureUs = 100; k2.lastEmittedAuCaptureUs = 100;
    Env ep = e; ep.epochMatch = false;
    expect(DueOnce(k2, ep, &rearm) == TA::Cancel && !k2.RefineOwed(), "refine: epoch changed -> cancel");
  }
  {  // [r6] busy sender: one defer (re-armed), then cancel; or defer then clear -> refine
    KickState k;
    k.lastRealInputCaptureUs = 100; k.lastEmittedAuCaptureUs = 100;
    Env b = e; b.senderBusy = true;
    expect(DueOnce(k, b, &rearm) == TA::Defer && rearm, "refine: busy -> one defer, re-armed");
    expect(DueOnce(k, b, &rearm) == TA::Cancel && !rearm && !k.RefineOwed(), "refine: busy again -> cancel");
    KickState k2;
    k2.lastRealInputCaptureUs = 100; k2.lastEmittedAuCaptureUs = 100;
    expect(DueOnce(k2, b, &rearm) == TA::Defer, "refine: busy -> defer");
    expect(DueOnce(k2, e, &rearm) == TA::Refine, "refine: clear after the defer -> refine");
    expect(DueOnce(k2, e, &rearm) == TA::None, "refine: then nothing");
    // a new real input gets its own one defer
    k.lastRealInputCaptureUs = 400; k.lastEmittedAuCaptureUs = 400;
    expect(DueOnce(k, b, &rearm) == TA::Defer, "refine: a new episode has its own defer");
  }
  {  // [r7] a closed barrier / pending forced key stays the kick's job (an IDR), never a refine
    KickState k;
    k.lastRealInputCaptureUs = 100; k.lastEmittedAuCaptureUs = 100;
    Env c = e; c.barrierClosed = true;
    expect(DueOnce(k, c, &rearm) == TA::Kick, "refine: closed barrier -> kick, not refine");
    KickState k2;
    k2.lastRealInputCaptureUs = 100; k2.lastEmittedAuCaptureUs = 100;
    Env f = e; f.forceKey = true;
    expect(DueOnce(k2, f, &rearm) == TA::Kick, "refine: pending forced key -> kick");
  }
  {  // [r8] disabled (shipping) -> the old behaviour: nothing when the output is out
    KickState k;
    k.lastRealInputCaptureUs = 100; k.lastEmittedAuCaptureUs = 100;
    KickState::TrailingInputs ti;
    ti.needKick = k.NeedKick(false);
    ti.refineEnabled = false;
    ti.refineOwed = k.RefineOwed();
    ti.streamActive = ti.epochMatch = true;
    expect(KickState::DecideTrailing(ti) == TA::None, "refine: disabled -> no synthetic");
  }
  {  // [r9] admission: the refine is a delta (wire cap gate skips it on a backlog); a kick is admitted
    EncodeAdmissionInputs a;
    a.wireCapActive = true; a.senderQueueDepth = 2;
    a.servedBootstrap = true && !true;  // servedBootstrap && !servedRefine, as in stage_encode_send
    expect(decide_encode_admission(a) == EncodeAdmission::SkipOverloaded, "refine: backlogged wire skips it");
    a.servedBootstrap = true && !false;
    expect(decide_encode_admission(a) == EncodeAdmission::Admit, "refine: a kick is still admitted");
    a.servedBootstrap = false; a.senderQueueDepth = 0;
    expect(decide_encode_admission(a) == EncodeAdmission::Admit, "refine: an idle wire admits the refine");
  }
}

int main() {
  TestArmDueCancel();
  TestOneKickPerHeldInput();
  TestBarrierOverridesTheGuard();
  TestStaticRefreshCadence();
  TestRealStampClampedBehindSyntheticStamp();
  TestTrailingRefine();
  if (gFailures == 0) {
    std::printf("host_kick_test: PASS\n");
    return 0;
  }
  std::printf("host_kick_test: FAIL (%d)\n", gFailures);
  return 1;
}
