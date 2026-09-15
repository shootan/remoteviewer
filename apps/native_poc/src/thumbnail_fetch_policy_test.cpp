// The client-side half of the hung-window fix: when to stop asking.
//
// The case this exists for is the one both clients got wrong. A window whose preview never
// arrives is absent from the preview cache, and both of them throttled on that cache -- so the
// window fell past the throttle and was re-queued on every panel refresh, forever, while the host
// was deliberately skipping it. The first check below is that one.

#include "thumbnail_fetch_policy.hpp"

#include <iostream>
#include <string>

namespace {

using remote60::native_poc::ThumbnailFetchPolicy;
using remote60::native_poc::ThumbnailFetchState;
using remote60::native_poc::thumbnail_fetch_due;

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

}  // namespace

int main() {
  const ThumbnailFetchPolicy policy;

  // A window nobody has asked about yet.
  {
    ThumbnailFetchState fresh;
    check("a window with no preview and no attempt is due", thumbnail_fetch_due(fresh, policy, 0));
  }

  // The regression this file is named after.
  {
    ThumbnailFetchState failed;
    failed.attempted = true;
    failed.lastAttemptFailed = true;
    failed.lastAttemptUs = 0;
    check("a failed preview is NOT re-queued on the next panel refresh",
          !thumbnail_fetch_due(failed, policy, 1 * kSec));
    check("...nor ten refreshes later", !thumbnail_fetch_due(failed, policy, 10 * kSec));
    check("...nor one second before the host's cooldown ends",
          !thumbnail_fetch_due(failed, policy, policy.retryAfterFailureUs - kSec));
    check("...and is asked again once the cooldown has passed",
          thumbnail_fetch_due(failed, policy, policy.retryAfterFailureUs));
  }

  // The existing behaviour for previews that work has to survive the change.
  {
    ThumbnailFetchState good;
    good.havePreview = true;
    good.previewFetchedUs = 0;
    good.attempted = true;
    good.lastAttemptFailed = false;
    good.lastAttemptUs = 0;
    check("a fresh preview is left alone", !thumbnail_fetch_due(good, policy, kSec));
    check("...and refreshed once it is stale", thumbnail_fetch_due(good, policy, policy.refreshUs));
    check("...on the refresh interval, not the failure one",
          policy.refreshUs < policy.retryAfterFailureUs);
  }

  // A window that worked and then stopped. The failure is the newer fact, and checking the cache
  // first would refresh it straight back into the host's cooldown.
  {
    ThumbnailFetchState wasGoodNowFailing;
    wasGoodNowFailing.havePreview = true;
    wasGoodNowFailing.previewFetchedUs = 0;
    wasGoodNowFailing.attempted = true;
    wasGoodNowFailing.lastAttemptFailed = true;
    wasGoodNowFailing.lastAttemptUs = 30 * kSec;
    check("a stale preview whose last attempt failed is not refreshed",
          !thumbnail_fetch_due(wasGoodNowFailing, policy, 40 * kSec),
          "preview is 40s old, failure is 10s old");
    check("...until the cooldown from that failure has passed",
          thumbnail_fetch_due(wasGoodNowFailing, policy, 30 * kSec + policy.retryAfterFailureUs));
  }

  // Recovery: the window answers again and goes back to the ordinary refresh rhythm.
  {
    ThumbnailFetchState recovered;
    recovered.havePreview = true;
    recovered.previewFetchedUs = 100 * kSec;
    recovered.attempted = true;
    recovered.lastAttemptFailed = false;
    recovered.lastAttemptUs = 100 * kSec;
    check("a recovered window is on the refresh interval again",
          !thumbnail_fetch_due(recovered, policy, 100 * kSec + kSec));
    check("...and refreshes normally after it",
          thumbnail_fetch_due(recovered, policy, 100 * kSec + policy.refreshUs));
  }

  // An attempt that succeeded but returned nothing to cache cannot look like "never asked", or the
  // throttle has nothing to bite on.
  {
    ThumbnailFetchState askedNoPixels;
    askedNoPixels.havePreview = false;
    askedNoPixels.attempted = true;
    askedNoPixels.lastAttemptFailed = true;
    askedNoPixels.lastAttemptUs = 5 * kSec;
    check("no preview plus a recent failure is still throttled",
          !thumbnail_fetch_due(askedNoPixels, policy, 6 * kSec));
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  ("
            << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
