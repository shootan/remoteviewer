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

// r9 (Codex r9 s2): accepted is not delivered. Acceptance closes the submission debt and opens a
// separate output-progress state bound to codec instance / epochs / generation / ordinal; at most ONE
// extra kick 150 ms after acceptance, merged away when another input moved the encoder, exhausted
// (not success) when the kick's call still brings no AU, bounded by a deadline in every case, and the
// kick never re-opens it. The stage (host_stage_encode_send_h264.cpp settle_await) drives exactly
// these transitions; the sequences below are the ones it produces.
void TestCatchupAwait() {
  const uint64_t t0 = 50 * kSec;
  // (a) accepted, no AU in its own call -> await; the one kick is due at +150 ms, not before
  {
    KickState k;
    k.OpenAwait(/*instance*/ 4, /*session*/ 7, /*inputEpoch*/ 3, /*gen*/ 2, /*ordinal*/ 105, /*version*/ 349, t0);
    expect(k.await.active && k.awaitOpened == 1, "await: an accepted catch-up without its AU opens the await");
    expect(!k.CatchupDue(t0 + 10 * kSec), "await: acceptance leaves no submission debt (no per-tick re-submit)");
    expect(!k.AwaitKickDue(t0 + KickState::kAwaitKickDelayUs - 1) && k.AwaitKickDue(t0 + KickState::kAwaitKickDelayUs),
           "await: the extra kick is due 150 ms after acceptance, not before");
    k.await.kickSubmitted = true;  // stage: the kick went to the encode stage
    expect(!k.AwaitKickDue(t0 + 2 * KickState::kAwaitKickDelayUs), "await: one kick only -- not due again once submitted");
    k.NoteAwaitInputAccepted(true);
    expect(k.await.kickAccepted && k.awaitKickAccepted == 1 && k.awaitOpened == 1,
           "await: the kick's acceptance is counted and does NOT open another await");
    expect(k.AwaitSatisfiedBy(4, 3, 105), "await: the catch-up's own ordinal from the same instance/epoch satisfies it");
    k.EndAwait(KickState::AwaitEnd::Observed);
    expect(!k.await.active && k.awaitExhausted == 0 && k.awaitDeferred == 0, "await: observed closes it, nothing else counted");
  }
  // (b) provenance: only the same instance + input epoch, ordinal >= the catch-up's, counts
  {
    KickState k;
    k.OpenAwait(4, 7, 3, 2, 105, 349, t0);
    expect(!k.AwaitSatisfiedBy(3, 3, 200), "await: a late output of an OLDER codec instance never counts");
    expect(!k.AwaitSatisfiedBy(4, 2, 200), "await: an AU of another input epoch never counts");
    expect(!k.AwaitSatisfiedBy(4, 3, 104), "await: an AU of an EARLIER input does not count");
    expect(!k.AwaitSatisfiedBy(4, 3, 0), "await: an AU without provenance (ordinal 0) does not count");
    expect(k.AwaitSatisfiedBy(4, 3, 106), "await: a LATER input's AU (outputs follow accepted order) counts");
    expect(k.AwaitIdentityMatches(4, 7, 3, 2) && !k.AwaitIdentityMatches(5, 7, 3, 2) &&
               !k.AwaitIdentityMatches(4, 8, 3, 2) && !k.AwaitIdentityMatches(4, 7, 4, 2) &&
               !k.AwaitIdentityMatches(4, 7, 3, 3),
           "await: identity = codec instance + session epoch + input epoch + stream generation");
    k.EndAwait(KickState::AwaitEnd::Cancelled);
    expect(!k.await.active && k.awaitCancelled == 1 && !k.AwaitKickDue(t0 + kSec),
           "await: cancelled (identity / instance / pause) -> no kick");
  }
  // (c) a new real input accepted before the kick: merged, no extra kick; its call without the AU defers
  {
    KickState k;
    k.OpenAwait(4, 7, 3, 2, 105, 349, t0);
    k.NoteAwaitInputAccepted(false);
    expect(k.await.advancedByOther && k.awaitKickMerged == 1 && !k.AwaitKickDue(t0 + kSec),
           "await: another accepted input moves the encoder -> the extra kick is dropped (merged)");
    k.NoteAwaitCallEnded(false, true);
    expect(!k.await.active && k.awaitDeferred == 1 && k.awaitExhausted == 0,
           "await: that call without the AU defers to the input's own trailing edge / refresh");
  }
  // (d) the kick was taken but still no AU: exhausted -- not delivered, not re-kicked, not re-opened
  {
    KickState k;
    k.OpenAwait(4, 7, 3, 2, 105, 349, t0);
    k.await.kickSubmitted = true;
    k.NoteAwaitInputAccepted(true);
    k.NoteAwaitCallEnded(true, true);
    expect(!k.await.active && k.awaitExhausted == 1 && !k.AwaitKickDue(t0 + 5 * kSec) && k.awaitOpened == 1,
           "await: kick taken, no AU -> exhausted; no second kick, nothing reopened");
  }
  // (e) a refused / not-accepted kick waits like any delta, but the whole state is bounded
  {
    KickState k;
    k.OpenAwait(4, 7, 3, 2, 105, 349, t0);
    k.await.kickSubmitted = true;
    k.NoteAwaitCallEnded(true, /*inputAccepted*/ false);
    expect(k.await.active, "await: a kick the encoder did not take does not end the await");
    k.await.kickSubmitted = false;  // stage: refused -> retry later
    k.await.kickDueUs = t0 + 300000;
    expect(!k.AwaitKickDue(t0 + 299999) && k.AwaitKickDue(t0 + 300000), "await: a refused kick is tried again later");
    const uint64_t deadline = t0 + KickState::kAwaitKickDelayUs + KickState::kAwaitKickDeadlineUs;
    expect(!k.AwaitKickExpired(deadline - 1) && k.AwaitKickExpired(deadline), "await: bounded -- expires at its deadline");
    k.await.kickSubmitted = true;  // even with a kick in hand, the deadline still bounds it
    expect(k.AwaitKickExpired(deadline), "await: the deadline holds even after a submitted kick");
    k.EndAwait(KickState::AwaitEnd::Deferred);
    expect(!k.await.active && k.awaitDeferred == 1, "await: expired -> deferred to the refresh");
  }
  // (f) a newer catch-up accepted while one is awaited: the old await is advanced-by-other
  {
    KickState k;
    k.OpenAwait(4, 7, 3, 2, 105, 349, t0);
    k.NoteAwaitInputAccepted(false);  // the newer catch-up's own acceptance
    k.NoteAwaitCallEnded(false, true);
    expect(!k.await.active && k.awaitDeferred == 1, "await: a newer accepted input supersedes the old await");
    k.OpenAwait(4, 7, 3, 2, 120, 400, t0 + kSec);
    expect(k.await.active && k.await.ordinal == 120 && k.await.version == 400 && k.awaitOpened == 2,
           "await: ... and the newer catch-up opens its own");
  }
  // nothing awaited: every transition is a no-op
  {
    KickState k;
    k.NoteAwaitInputAccepted(false);
    k.NoteAwaitInputAccepted(true);
    k.NoteAwaitCallEnded(true, true);
    k.EndAwait(KickState::AwaitEnd::Exhausted);
    expect(!k.await.active && k.awaitKickMerged == 0 && k.awaitKickAccepted == 0 && k.awaitExhausted == 0 &&
               !k.AwaitKickDue(t0) && !k.AwaitKickExpired(t0 + 10 * kSec) && !k.AwaitSatisfiedBy(4, 3, 105),
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
