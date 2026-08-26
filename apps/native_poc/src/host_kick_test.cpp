// Pins the trailing-edge kick and static-refresh decisions of KickState (host_kick.hpp): arming and the
// 150ms deadline, the raw-mode no-op, one kick per held input, the barrier override, and the refresh
// cadence anchored on both the last emitted AU and the last attempt. Time is an argument.
//
// Host split refactor Phase 2-T3 (2026-08-26).

#include "host_kick.hpp"

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

int main() {
  TestArmDueCancel();
  TestOneKickPerHeldInput();
  TestBarrierOverridesTheGuard();
  TestStaticRefreshCadence();
  if (gFailures == 0) {
    std::printf("host_kick_test: PASS\n");
    return 0;
  }
  std::printf("host_kick_test: FAIL (%d)\n", gFailures);
  return 1;
}
