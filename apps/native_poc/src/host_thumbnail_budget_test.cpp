// The retry policy for window previews, exercised without a window.
//
// Everything here is the part of the fix that does not need GDI: how many attempts a window gets,
// when it stops being asked, when it starts being asked again, and what happens to the state when
// a window goes away. The capture mechanism is deliberately not involved -- that is what makes
// these cases deterministic, and it is why this file can be written before the mechanism is
// settled.
//
// Time is a parameter, so "60 seconds later" is a number here, not a sleep.

#include "host_thumbnail_budget.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using remote60::native_poc::ThumbnailBudget;
using remote60::native_poc::ThumbnailBudgetConfig;
using remote60::native_poc::ThumbnailOutcome;
using remote60::native_poc::ThumbnailTargetKey;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

constexpr uint64_t kSec = 1000ull * 1000;

ThumbnailTargetKey key(uint64_t windowId, uint32_t pid = 4242, uint64_t createdUs = 1000) {
  ThumbnailTargetKey k;
  k.windowId = windowId;
  k.ownerPid = pid;
  k.processCreatedUs = createdUs;
  return k;
}

/**
 * Burn the whole attempt budget, spacing attempts so the retry interval never blocks one.
 *
 * Leaves *nowUs on the LAST attempt rather than past it. The cooldown is measured from that
 * moment, so a caller asking "is it still skipped a second before the end" has to share the
 * origin -- advancing one interval further made the first version of this overshoot by two
 * seconds and call a correct skip a failure.
 */
void fail_until_cooldown(ThumbnailBudget& budget, const ThumbnailTargetKey& k, uint64_t* nowUs,
                         ThumbnailOutcome how = ThumbnailOutcome::TimedOut) {
  for (uint32_t i = 0; i < budget.config().maxAttempts; ++i) {
    if (i > 0) *nowUs += budget.config().retryIntervalUs;
    budget.Allow(k, *nowUs);
    budget.Record(k, how, *nowUs);
  }
}

void test_first_attempt_is_allowed() {
  ThumbnailBudget budget;
  check("a window nobody has asked about is allowed", budget.Allow(key(1), 0));
  check("...and asking did not create a grudge", budget.ConsecutiveFailures(key(1)) == 0);
}

void test_success_never_throttles() {
  ThumbnailBudget budget;
  uint64_t now = 0;
  for (int i = 0; i < 10; ++i) {
    const bool allowed = budget.Allow(key(1), now);
    budget.Record(key(1), ThumbnailOutcome::Ok, now);
    if (!allowed) {
      check("a window that answers is never throttled", false, "blocked on iteration " + std::to_string(i));
      return;
    }
    now += 1;  // back to back, deliberately faster than any interval
  }
  check("a window that answers is never throttled", true, "10 consecutive successes");
}

void test_retry_interval_spaces_a_burst() {
  ThumbnailBudget budget;
  const ThumbnailTargetKey k = key(1);
  budget.Allow(k, 0);
  budget.Record(k, ThumbnailOutcome::TimedOut, 0);

  check("a second try immediately after a timeout is refused",
        !budget.Allow(k, budget.config().retryIntervalUs - 1));
  check("...and allowed once the interval has passed",
        budget.Allow(k, budget.config().retryIntervalUs));
}

void test_three_failures_buy_a_cooldown() {
  ThumbnailBudget budget;
  const ThumbnailTargetKey k = key(1);
  uint64_t now = 0;
  fail_until_cooldown(budget, k, &now);

  check("three timeouts put the window in cooldown", budget.InCooldown(k, now));
  check("...so it is skipped", !budget.Allow(k, now));
  check("...still skipped one second before the cooldown ends",
        !budget.Allow(k, now + budget.config().cooldownUs - kSec));
  check("...and asked again once it ends", budget.Allow(k, now + budget.config().cooldownUs));
}

void test_cooldown_grants_a_fresh_burst() {
  // The failure that matters here: if the counter were not reset with the cooldown, the window
  // would come back one failure away from another minute of silence, forever.
  ThumbnailBudget budget;
  const ThumbnailTargetKey k = key(1);
  uint64_t now = 0;
  fail_until_cooldown(budget, k, &now);

  uint64_t after = now + budget.config().cooldownUs;
  bool allowedEveryTime = true;
  for (uint32_t i = 0; i < budget.config().maxAttempts; ++i) {
    if (!budget.Allow(k, after)) allowedEveryTime = false;
    budget.Record(k, ThumbnailOutcome::TimedOut, after);
    after += budget.config().retryIntervalUs;
  }
  check("after a cooldown the window gets the full burst again", allowedEveryTime,
        std::to_string(budget.config().maxAttempts) + " attempts");
  check("...and a second cooldown follows the same way", budget.InCooldown(k, after));
}

void test_one_success_clears_everything() {
  ThumbnailBudget budget;
  const ThumbnailTargetKey k = key(1);
  uint64_t now = 0;
  fail_until_cooldown(budget, k, &now);
  check("precondition: in cooldown", budget.InCooldown(k, now));

  budget.Record(k, ThumbnailOutcome::Ok, now);
  check("a success ends a running cooldown", !budget.InCooldown(k, now));
  check("...and the window is immediately allowed again", budget.Allow(k, now));

  // Mid-burst recovery, which is the common case: one flaky frame, then the window answers.
  ThumbnailBudget second;
  second.Allow(k, 0);
  second.Record(k, ThumbnailOutcome::TimedOut, 0);
  second.Record(k, ThumbnailOutcome::Ok, kSec);
  check("a success mid-burst clears the failure count", second.ConsecutiveFailures(k) == 0);
  check("...and does not leave the retry interval in the way", second.Allow(k, kSec));
}

void test_cancel_costs_nothing() {
  ThumbnailBudget budget;
  const ThumbnailTargetKey k = key(1);
  uint64_t now = 0;
  for (int i = 0; i < 20; ++i) {
    budget.Allow(k, now);
    budget.Record(k, ThumbnailOutcome::Canceled, now);
    now += 1;
  }
  check("cancelling never spends the budget", !budget.InCooldown(k, now));
  check("...and leaves no failure count", budget.ConsecutiveFailures(k) == 0);
  check("...and the window is still allowed", budget.Allow(k, now));
}

void test_failed_and_timed_out_both_count() {
  // Failed is the helper dying or the capture refusing; TimedOut is the deadline. Both mean no
  // pixels, and a window that alternates between them must not retry forever.
  ThumbnailBudget budget;
  const ThumbnailTargetKey k = key(1);
  uint64_t now = 0;
  budget.Record(k, ThumbnailOutcome::Failed, now);
  now += budget.config().retryIntervalUs;
  budget.Record(k, ThumbnailOutcome::TimedOut, now);
  now += budget.config().retryIntervalUs;
  budget.Record(k, ThumbnailOutcome::Failed, now);
  check("Failed and TimedOut count toward the same budget", budget.InCooldown(k, now));
}

void test_identity_separates_processes() {
  ThumbnailBudget budget;
  uint64_t now = 0;
  const ThumbnailTargetKey original = key(0x1234, 100, 5000);
  fail_until_cooldown(budget, original, &now);
  check("precondition: the original window is in cooldown", budget.InCooldown(original, now));

  // Same HWND value, different process: a window handle recycled after the owner exited.
  const ThumbnailTargetKey reusedByOtherPid = key(0x1234, 101, 5000);
  check("the same HWND under a different pid is a different window",
        budget.Allow(reusedByOtherPid, now));

  // Same HWND value, same pid number, but the pid was itself reused -- the creation time is what
  // catches this one.
  const ThumbnailTargetKey reusedPid = key(0x1234, 100, 9000);
  check("the same pid with a later creation time is a different window",
        budget.Allow(reusedPid, now));

  check("and the original is still in cooldown throughout", budget.InCooldown(original, now));
}

void test_identity_cannot_separate_same_process_reuse() {
  // Stated as a test because it is a documented limit, not an oversight: within one live process
  // the three fields are identical for a closed window and its replacement. RetainOnly is the
  // defence, and the next case checks it.
  ThumbnailBudget budget;
  uint64_t now = 0;
  const ThumbnailTargetKey k = key(0x1234, 100, 5000);
  fail_until_cooldown(budget, k, &now);
  check("a same-process HWND reuse inherits the cooldown (known limit)", !budget.Allow(k, now));
}

void test_leaving_the_list_drops_the_state() {
  ThumbnailBudget budget;
  uint64_t now = 0;
  const ThumbnailTargetKey gone = key(0x1234, 100, 5000);
  const ThumbnailTargetKey stays = key(0x5678, 100, 5000);
  fail_until_cooldown(budget, gone, &now);
  fail_until_cooldown(budget, stays, &now);
  check("precondition: both are in cooldown",
        budget.InCooldown(gone, now) && budget.InCooldown(stays, now));

  budget.RetainOnly({0x5678}, now);
  check("a window that left the list is forgotten", budget.Allow(gone, now));
  check("...and one that is still listed is not", !budget.Allow(stays, now));
  check("...and the map shrank", budget.TrackedCount() == 1,
        std::to_string(budget.TrackedCount()) + " tracked");

  // This is the recovery path for the limit above: close the window, and its replacement starts
  // clean even though the key is identical.
  check("so a same-process reuse after a close is allowed", budget.Allow(gone, now));
}

void test_entries_expire() {
  ThumbnailBudgetConfig config;
  config.entryTtlUs = 10 * kSec;
  ThumbnailBudget budget(config);
  const ThumbnailTargetKey k = key(1);
  uint64_t now = 0;
  fail_until_cooldown(budget, k, &now);
  check("precondition: tracked", budget.TrackedCount() == 1);

  // Long enough that the TTL has passed, and past the cooldown too, so what is being observed is
  // the entry going away rather than the cooldown ending.
  const uint64_t muchLater = now + config.cooldownUs + config.entryTtlUs + kSec;
  budget.Allow(key(2), muchLater);  // any call sweeps
  check("an entry nobody touched for the TTL is dropped", budget.TrackedCount() <= 1,
        std::to_string(budget.TrackedCount()) + " tracked");
  check("...and the window it described starts clean", budget.ConsecutiveFailures(k) == 0);
}

void test_map_stays_bounded() {
  ThumbnailBudgetConfig config;
  config.maxEntries = 8;
  ThumbnailBudget budget(config);
  for (uint64_t i = 0; i < 100; ++i) {
    budget.Record(key(i), ThumbnailOutcome::TimedOut, i * kSec);
  }
  check("the map never grows past its cap", budget.TrackedCount() <= config.maxEntries,
        std::to_string(budget.TrackedCount()) + " of " + std::to_string(config.maxEntries));

  // The most recent should have survived; the oldest should not. What proves survival is the
  // failure the entry remembers -- one timeout is not a cooldown, so asking InCooldown here would
  // be testing the wrong thing and would pass only by accident.
  check("the newest entry survived eviction", budget.ConsecutiveFailures(key(99)) == 1,
        std::to_string(budget.ConsecutiveFailures(key(99))) + " failures remembered");
  check("the oldest was the one evicted",
        budget.ConsecutiveFailures(key(0)) == 0 && budget.Allow(key(0), 99 * kSec));
}

void test_config_guards_degenerate_values() {
  ThumbnailBudgetConfig config;
  config.maxAttempts = 0;
  config.maxEntries = 0;
  ThumbnailBudget budget(config);
  check("zero attempts is clamped to one", budget.config().maxAttempts == 1);
  check("zero entries is clamped to one", budget.config().maxEntries == 1);

  // With one attempt the first failure should already buy the cooldown.
  const ThumbnailTargetKey k = key(1);
  budget.Record(k, ThumbnailOutcome::TimedOut, 0);
  check("...and one attempt means one failure is enough", budget.InCooldown(k, 0));
}

}  // namespace

int main() {
  test_first_attempt_is_allowed();
  test_success_never_throttles();
  test_retry_interval_spaces_a_burst();
  test_three_failures_buy_a_cooldown();
  test_cooldown_grants_a_fresh_burst();
  test_one_success_clears_everything();
  test_cancel_costs_nothing();
  test_failed_and_timed_out_both_count();
  test_identity_separates_processes();
  test_identity_cannot_separate_same_process_reuse();
  test_leaving_the_list_drops_the_state();
  test_entries_expire();
  test_map_stays_bounded();
  test_config_guards_degenerate_values();

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  ("
            << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
