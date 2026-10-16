// Pins the trailing-edge kick and static-refresh decisions of KickState (host_kick.hpp): arming and the
// 150ms deadline, the raw-mode no-op, one kick per held input, the barrier override, and the refresh
// cadence anchored on both the last emitted AU and the last attempt. Time is an argument.
//
// Host split refactor Phase 2-T3 (2026-08-26).

#include "host_kick.hpp"

#include <cstdint>
#include <memory>
#include <vector>
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

// catch-up (Codex r8 s3): a debt per skipped picture, closed only by an ACCEPTED input, bound to the
// session / input epoch / generation, kept across a re-rejection, never reopened without a new skip.
void TestCatchupDebt() {
  KickState k;
  auto pix = std::make_shared<std::vector<uint8_t>>(16, 1);
  expect(!k.CatchupDue(10 * kSec), "catchup: nothing owed at start");
  k.NoteInputAccepted(false);
  expect(k.catchupClosedByOther == 0 && k.catchupAccepted == 0, "catchup: an accepted input with no debt counts nothing");
  k.NoteAdmissionSkip(10 * kSec, 30, false, 7, 3, 2, 100, pix);  // 2/30 s = 66.666 ms
  expect(k.catchup.owed && k.catchupOwed == 1 && k.catchup.version == 100 && k.catchup.payload == pix,
         "catchup: a skipped real capture opens a debt bound to its picture");
  expect(!k.CatchupDue(10 * kSec + 66000) && k.CatchupDue(10 * kSec + 66667), "catchup: due two frame periods after the skip");
  auto pix2 = std::make_shared<std::vector<uint8_t>>(16, 2);
  k.NoteAdmissionSkip(10 * kSec + 50000, 30, false, 7, 3, 2, 101, pix2);
  expect(k.catchupOwed == 1 && k.catchup.version == 101 && k.catchup.payload == pix2 &&
             !k.CatchupDue(10 * kSec + 100000),
         "catchup: a newer skipped capture re-targets the same debt and pushes it out");
  expect(k.CatchupIdentityMatches(7, 3, 2) && !k.CatchupIdentityMatches(8, 3, 2) &&
             !k.CatchupIdentityMatches(7, 4, 2) && !k.CatchupIdentityMatches(7, 3, 3),
         "catchup: identity = session epoch + input epoch + stream generation");
  k.NoteCatchupServed(11 * kSec, 30);
  expect(k.catchup.attempts == 1 && k.catchupServed == 1 && !k.CatchupDue(11 * kSec + 1000),
         "catchup: a served attempt is counted and the next one waits");
  k.NoteAdmissionSkip(11 * kSec + 5000, 30, true, 7, 3, 2, 999, nullptr);
  expect(k.catchup.owed && k.catchup.version == 101 && k.catchupReRejected == 1,
         "catchup: a re-rejected catch-up keeps the SAME debt (its picture), not a new one");
  // not accepted: the stage counts it; the debt is untouched
  expect(k.catchup.owed, "catchup: an attempt the encoder did not accept leaves the debt open");
  k.NoteInputAccepted(true);
  expect(!k.catchup.owed && !k.catchup.payload && k.catchupAccepted == 1,
         "catchup: the catch-up's own accepted input pays the debt and releases the picture");
  k.NoteAdmissionSkip(20 * kSec, 30, true, 7, 3, 2, 5, nullptr);
  expect(!k.catchup.owed && k.catchupOwed == 1, "catchup: a refused catch-up never reopens a closed debt (no periodic catch-up)");
  k.NoteAdmissionSkip(30 * kSec, 30, false, 7, 3, 2, 200, pix);
  k.NoteInputAccepted(false);
  expect(!k.catchup.owed && k.catchupClosedByOther == 1, "catchup: a newer real input the encoder accepted pays it too");
  k.NoteAdmissionSkip(40 * kSec, 30, false, 7, 3, 2, 300, pix);
  k.CancelCatchup(&k.catchupCancelledIdentity);
  expect(!k.catchup.owed && k.catchupCancelledIdentity == 1 && !k.CatchupDue(50 * kSec), "catchup: a cancelled debt is gone");
  KickState off;
  off.catchupEnabled = false;
  off.NoteAdmissionSkip(10 * kSec, 30, false, 1, 1, 1, 1, pix);
  expect(!off.catchup.owed && !off.CatchupDue(20 * kSec) && off.catchupOwed == 0, "catchup: off never owes");
  KickState slow;
  slow.NoteAdmissionSkip(10 * kSec, 0, false, 1, 1, 1, 1, nullptr);
  expect(!slow.CatchupDue(10 * kSec + KickState::kTrailingKickDelayUs - 1) &&
             slow.CatchupDue(10 * kSec + KickState::kTrailingKickDelayUs),
         "catchup: unknown fps uses the 150 ms trailing delay");
}

// r11 (Codex r11 s1): the await state, collect-only. Acceptance closes the submission debt and opens a
// separate output-progress state bound to codec instance / epochs / generation / ordinal; it is observed
// only by an AU of the same instance + input epoch with that ordinal or later, deferred at the deadline
// (late watch on), cancelled on identity / pause; another accepted input does NOT end it. The tick that
// drives these transitions on the product path is catchup_await_tick (host_catchup_await.hpp), tested
// against the real H264Encoder in host_catchup_collect_test.cpp.
void TestCatchupAwait() {
  const uint64_t t0 = 50 * kSec;
  {
    KickState k;
    k.OpenAwait(/*instance*/ 4, /*session*/ 7, /*inputEpoch*/ 3, /*gen*/ 2, /*ordinal*/ 105, /*version*/ 349, t0);
    expect(k.await.active && k.awaitOpened == 1 && k.await.deadlineUs == t0 + KickState::kAwaitDeadlineUs,
           "await: an accepted catch-up without its AU opens the await, deadline 1.15 s");
    expect(!k.CatchupDue(t0 + 10 * kSec), "await: acceptance leaves no submission debt (no per-tick re-submit)");
    expect(!k.AwaitExpired(t0 + KickState::kAwaitDeadlineUs - 1) && k.AwaitExpired(t0 + KickState::kAwaitDeadlineUs),
           "await: bounded by its deadline");
    expect(k.AwaitSatisfiedBy(4, 3, 105), "await: the catch-up's own ordinal from the same instance/epoch satisfies it");
    expect(!k.AwaitSatisfiedBy(3, 3, 200), "await: a late output of an OLDER codec instance never counts");
    expect(!k.AwaitSatisfiedBy(4, 2, 200), "await: an AU of another input epoch never counts");
    expect(!k.AwaitSatisfiedBy(4, 3, 104), "await: an AU of an EARLIER input does not count");
    expect(!k.AwaitSatisfiedBy(4, 3, 0), "await: an AU without provenance (ordinal 0) does not count");
    expect(k.AwaitSatisfiedBy(4, 3, 106), "await: a LATER input's AU (outputs follow accepted order) counts");
    expect(k.AwaitIdentityMatches(4, 7, 3, 2) && !k.AwaitIdentityMatches(5, 7, 3, 2) &&
               !k.AwaitIdentityMatches(4, 8, 3, 2) && !k.AwaitIdentityMatches(4, 7, 4, 2) &&
               !k.AwaitIdentityMatches(4, 7, 3, 3),
           "await: identity = codec instance + session epoch + input epoch + stream generation");
    k.NoteAwaitOtherInputAccepted();
    k.NoteAwaitOtherInputAccepted();
    expect(k.await.active && k.await.advancedByOther && k.awaitOtherInputs == 1,
           "await: another accepted input does not end it (counted once)");
    k.EndAwait(KickState::AwaitEnd::Observed, t0 + 40000);
    expect(!k.await.active && k.awaitDeferred == 0 && k.awaitCancelled == 0 && !k.lateWatch.active,
           "await: observed closes it, nothing else counted, nothing watched");
  }
  {
    KickState k;
    k.OpenAwait(4, 7, 3, 2, 105, 349, t0);
    k.EndAwait(KickState::AwaitEnd::Deferred, t0 + KickState::kAwaitDeadlineUs);
    expect(!k.await.active && k.awaitDeferred == 1 && k.lateWatch.active && k.lateWatch.ordinal == 105 &&
               k.lateWatch.acceptedUs == t0 && k.lateWatch.endedUs == t0 + KickState::kAwaitDeadlineUs,
           "await: deferred -> its ordinal is still watched with accept / end times");
    expect(!k.LateWatchSatisfiedBy(3, 3, 200) && !k.LateWatchSatisfiedBy(4, 2, 200) &&
               !k.LateWatchSatisfiedBy(4, 3, 104) && !k.LateWatchSatisfiedBy(4, 3, 0) && k.LateWatchSatisfiedBy(4, 3, 105),
           "late watch: same provenance rules as the await");
    const uint64_t ended = t0 + KickState::kAwaitDeadlineUs;
    expect(!k.LateWatchExpired(ended + KickState::kAwaitLateWatchUs - 1) && k.LateWatchExpired(ended + KickState::kAwaitLateWatchUs),
           "late watch: given up 5 s after the await ended");
    KickState c;
    c.OpenAwait(4, 7, 3, 2, 105, 349, t0);
    c.EndAwait(KickState::AwaitEnd::Cancelled, t0 + 1000);
    expect(!c.await.active && c.awaitCancelled == 1 && !c.lateWatch.active, "await: cancelled (identity / pause) is not watched");
  }
  {
    KickState k;  // nothing awaited: every transition is a no-op
    k.NoteAwaitOtherInputAccepted();
    k.EndAwait(KickState::AwaitEnd::Deferred, t0);
    expect(!k.await.active && k.awaitOtherInputs == 0 && k.awaitDeferred == 0 && !k.lateWatch.active &&
               !k.AwaitExpired(t0 + 10 * kSec) && !k.AwaitSatisfiedBy(4, 3, 105),
           "await: without an open await nothing is counted or due");
  }
}

int main() {
  TestCatchupDebt();
  TestCatchupAwait();
  TestArmDueCancel();
  TestOneKickPerHeldInput();
  TestBarrierOverridesTheGuard();
  TestStaticRefreshCadence();
  TestRealStampClampedBehindSyntheticStamp();
  if (gFailures == 0) {
    std::printf("host_kick_test: PASS\n");
    return 0;
  }
  std::printf("host_kick_test: FAIL (%d)\n", gFailures);
  return 1;
}
