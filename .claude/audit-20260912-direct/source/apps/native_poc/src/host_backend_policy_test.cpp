// Pins the desktop-backend promotion gate of DesktopBackendState (host_backend_policy.hpp): the secure-
// desktop settle clock and probe cadence, the retry deadline with its exponential backoff and ceiling,
// the once-per-deadline deferral latch, and the resets on success / attempt / idle. Time and the probe
// results are arguments; nothing here opens a desktop or restarts capture.
//
// Host split refactor Phase 2-T4 (2026-08-26).

#include "host_backend_policy.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using remote60::native_poc::DesktopBackendState;
using remote60::native_poc::kDesktopBackendRetryMaxUs;
using remote60::native_poc::kDesktopBackendRetryMinUs;
using remote60::native_poc::kDesktopDefaultProbeIntervalUs;
using remote60::native_poc::kDesktopDefaultStableUs;

namespace {

int gFailures = 0;

void expect(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++gFailures;
  }
}

constexpr uint64_t kSec = 1000000ULL;
constexpr uint64_t kT0 = 100 * kSec;

// The state holds atomics (not copyable), so each test declares its own and primes it as startup does.
void Prime(DesktopBackendState& b) {
  b.retryDelayUs = kDesktopBackendRetryMinUs;
}

void TestSettleClockAndProbeCadence() {
  DesktopBackendState b;
  Prime(b);
  expect(b.DefaultProbeDue(kT0), "gate: the first probe is due at once");
  b.NoteDefaultProbe(kT0, true);
  expect(!b.DefaultProbeDue(kT0 + kDesktopDefaultProbeIntervalUs - 1), "gate: probes are paced");
  expect(b.DefaultProbeDue(kT0 + kDesktopDefaultProbeIntervalUs), "gate: next probe due one interval later");
  expect(!b.DefaultStable(kT0 + kDesktopDefaultStableUs - 1), "gate: not stable before the settle window");
  expect(b.DefaultStable(kT0 + kDesktopDefaultStableUs), "gate: stable once the default desktop was up for the window");
  b.NoteDefaultProbe(kT0 + 2 * kSec, true);
  expect(b.defaultStableSinceUs == kT0, "gate: a repeated default probe keeps the original anchor");
  b.NoteDefaultProbe(kT0 + 3 * kSec, false);
  expect(b.defaultStableSinceUs == 0 && !b.DefaultStable(kT0 + 10 * kSec), "gate: a secure probe restarts the clock");
  expect(b.secureProbeFalseTotal.load() == 1, "gate: secure probes are counted");
  b.NoteDefaultProbe(kT0 + 4 * kSec, true);
  expect(b.defaultStableSinceUs == kT0 + 4 * kSec, "gate: the clock restarts from the next default probe");
  b.NoteSecureAtDeadline();
  expect(b.defaultStableSinceUs == 0 && b.secureProbeFalseTotal.load() == 2, "gate: the final check at the deadline also resets and counts");
}

void TestRetryDeadlineAndBackoff() {
  DesktopBackendState b;
  Prime(b);
  expect(!b.RetryDue(kT0) && b.retryAtUs == kT0 + kDesktopBackendRetryMinUs, "retry: the first sight of a demotion arms the deadline");
  expect(!b.RetryDue(kT0 + kDesktopBackendRetryMinUs - 1), "retry: not due before the deadline");
  expect(b.RetryDue(kT0 + kDesktopBackendRetryMinUs), "retry: due at the deadline");
  // Failures double the delay up to the ceiling.
  uint64_t now = kT0 + kDesktopBackendRetryMinUs;
  const uint64_t expectedDelays[] = {6 * kSec, 12 * kSec, 24 * kSec, 30 * kSec, 30 * kSec};
  for (uint64_t expected : expectedDelays) {
    b.NotePromotionFailure(now);
    expect(b.retryDelayUs == expected && b.retryAtUs == now + expected,
           "retry: backoff step -> " + std::to_string(expected / kSec) + "s");
    now = b.retryAtUs;
  }
  expect(b.promotionFail.load() == 5, "retry: failures counted");
  expect(b.retryDelayUs == kDesktopBackendRetryMaxUs, "retry: ceiling holds");
}

void TestSuccessResetsEpisode() {
  DesktopBackendState b;
  Prime(b);
  b.NoteDemotionEpisode(kT0);
  b.NoteDemotionEpisode(kT0 + kSec);
  expect(b.demotionSinceUs == kT0, "episode: the demotion start is recorded once");
  (void)b.RetryDue(kT0);
  b.NotePromotionFailure(kT0 + 3 * kSec);
  b.NotePromotionSuccess(kT0 + 9 * kSec);
  expect(b.promotionSuccess.load() == 1, "success: counted");
  expect(b.lastPromotionWaitUs.load() == 9 * kSec, "success: demotion -> promotion wait recorded");
  expect(b.retryAtUs == 0 && b.retryDelayUs == kDesktopBackendRetryMinUs && b.demotionSinceUs == 0,
         "success: retry clock and episode reset");
}

void TestDeferralLatchOncePerDeadline() {
  DesktopBackendState b;
  Prime(b);
  expect(b.NoteDeferredForSecure(), "defer: first deferral of a deadline is reported (logged once)");
  expect(!b.NoteDeferredForSecure() && !b.NoteDeferredForSecure(), "defer: later loop iterations stay quiet");
  expect(b.promotionDeferredSecureTotal.load() == 1, "defer: counted once per deadline episode");
  b.NotePromotionAttempt();
  expect(b.promotionAttempts.load() == 1 && !b.promotionDeferredForCurrentDeadline, "attempt: clears the latch");
  expect(b.NoteDeferredForSecure(), "defer: a new deadline episode reports again");
}

void TestEvidenceConsumedAndIdleReset() {
  DesktopBackendState b;
  Prime(b);
  b.NoteDefaultProbe(kT0, true);
  b.ConsumeStabilityEvidence();
  expect(b.defaultStableSinceUs == 0 && b.defaultProbeAtUs == 0, "attempt: stability evidence consumed, next probe due at once");
  b.NoteDemotionEpisode(kT0);
  (void)b.RetryDue(kT0);
  b.NotePromotionFailure(kT0 + 3 * kSec);
  b.NoteDefaultProbe(kT0 + 4 * kSec, true);
  (void)b.NoteDeferredForSecure();
  b.ResetPromotionGate();
  expect(b.retryAtUs == 0 && b.retryDelayUs == kDesktopBackendRetryMinUs && b.defaultStableSinceUs == 0 &&
             b.defaultProbeAtUs == 0 && b.demotionSinceUs == 0 && !b.promotionDeferredForCurrentDeadline,
         "idle: no demotion in progress resets the whole gate");
}

}  // namespace

int main() {
  TestSettleClockAndProbeCadence();
  TestRetryDeadlineAndBackoff();
  TestSuccessResetsEpisode();
  TestDeferralLatchOncePerDeadline();
  TestEvidenceConsumedAndIdleReset();
  if (gFailures == 0) {
    std::printf("host_backend_policy_test: PASS\n");
    return 0;
  }
  std::printf("host_backend_policy_test: FAIL (%d)\n", gFailures);
  return 1;
}
