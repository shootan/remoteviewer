// What the tray menu and the client's start-up check actually ask, and what they get back.
//
// The distinctions being pinned here are the ones a user notices. "We could not reach the server"
// must never come back as "you are up to date" -- a machine that has failed to check for a month
// telling its user it is current is the failure mode worth designing against. And a build with no
// trusted key must say so plainly rather than producing a signature error that reads like the
// server is broken.
//
// No sockets: the fetcher is injected.

#include "update_check.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

using namespace remote60::native_poc::update;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

const char* kDocument =
    "schema=2\n"
    "releaseId=r-0.2.105\n"
    "platform=windows\n"
    "arch=x64\n"
    "version=0.2.105\n"
    "artifact=GNLinkSetup.exe|3475968|0000000000000000000000000000000000000000000000000000000000000000|https://u.example/s.exe\n";

const char* kSignatureHex =
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000";

SignatureVerifier accepting() {
  return [](const std::string&, const std::vector<uint8_t>&) { return true; };
}
SignatureVerifier rejecting() {
  return [](const std::string&, const std::vector<uint8_t>&) { return false; };
}

ManifestFetcher serving(const std::string& document) {
  return [document](const std::string&, size_t, std::string* doc, std::string* sig,
                    std::string*) {
    *doc = document;
    *sig = kSignatureHex;
    return true;
  };
}
ManifestFetcher unreachable(const char* why) {
  return [why](const std::string&, size_t, std::string*, std::string*, std::string* error) {
    if (error) *error = why;
    return false;
  };
}

CheckConfig base_config(const std::string& installed) {
  CheckConfig c;
  c.manifestUrl = "https://updates.example/manifest?platform=windows";
  c.trustedPublicKeyHex = std::string(128, 'a');  // shape only; the verifier is injected
  c.platform = "windows";
  c.installedVersion = installed;
  return c;
}

}  // namespace

int main() {
  // ---------------------------------------------------------------- not configured

  {
    CheckConfig c = base_config("0.2.104");
    c.trustedPublicKeyHex.clear();
    bool fetched = false;
    const ManifestFetcher watcher = [&fetched](const std::string&, size_t, std::string*,
                                               std::string*, std::string*) {
      fetched = true;
      return true;
    };
    const CheckResult r = check_for_update(c, watcher, accepting());
    check("no trusted key -> NotConfigured", r.outcome == CheckOutcome::NotConfigured,
          check_outcome_name(r.outcome));
    check("and the network was never touched", !fetched);
    check("it is not reported as an update", !r.actionable());
    check("the reason names the missing key",
          r.detail.find("trusted key") != std::string::npos, r.detail);
  }
  {
    CheckConfig c = base_config("0.2.104");
    c.manifestUrl.clear();
    const CheckResult r = check_for_update(c, serving(kDocument), accepting());
    check("no URL -> NotConfigured", r.outcome == CheckOutcome::NotConfigured,
          check_outcome_name(r.outcome));
  }
  {
    const CheckResult r = check_for_update(base_config("0.2.104"), nullptr, accepting());
    check("no fetcher -> NotConfigured", r.outcome == CheckOutcome::NotConfigured,
          check_outcome_name(r.outcome));
  }

  // ---------------------------------------------------------------- unreachable is not up to date

  {
    const CheckResult r =
        check_for_update(base_config("0.2.104"), unreachable("TLS certificate validation failed"),
                         accepting());
    check("an unreachable server -> Unreachable", r.outcome == CheckOutcome::Unreachable,
          check_outcome_name(r.outcome));
    // The distinction this whole enum exists for.
    check("NOT reported as up to date", r.outcome != CheckOutcome::UpToDate);
    check("NOT reported as an update", !r.actionable());
    check("the reason is carried through", r.detail.find("certificate") != std::string::npos,
          r.detail);
    check("no version is claimed", r.availableVersion.empty(), r.availableVersion);
  }

  // ---------------------------------------------------------------- rejected

  {
    const CheckResult r = check_for_update(base_config("0.2.104"), serving(kDocument), rejecting());
    check("a manifest that does not verify -> Rejected", r.outcome == CheckOutcome::Rejected,
          check_outcome_name(r.outcome));
    check("not actionable", !r.actionable());
    check("and no version is taken from it", r.availableVersion.empty(), r.availableVersion);
  }
  {
    const CheckResult r =
        check_for_update(base_config("0.2.104"), serving("this is not a manifest\n"), accepting());
    check("an unparseable manifest -> Rejected", r.outcome == CheckOutcome::Rejected,
          check_outcome_name(r.outcome));
  }
  {
    CheckConfig c = base_config("0.2.104");
    c.platform = "android";
    const CheckResult r = check_for_update(c, serving(kDocument), accepting());
    check("a manifest for another platform -> Rejected", r.outcome == CheckOutcome::Rejected,
          check_outcome_name(r.outcome));
  }

  // ---------------------------------------------------------------- the ordinary answers

  {
    const CheckResult r = check_for_update(base_config("0.2.104"), serving(kDocument), accepting());
    check("a newer version -> UpdateAvailable", r.outcome == CheckOutcome::UpdateAvailable,
          check_outcome_name(r.outcome));
    check("actionable", r.actionable());
    check("the version is reported", r.availableVersion == "0.2.105", r.availableVersion);
  }
  {
    const CheckResult r = check_for_update(base_config("0.2.105"), serving(kDocument), accepting());
    check("the same version -> UpToDate", r.outcome == CheckOutcome::UpToDate,
          check_outcome_name(r.outcome));
    check("not actionable", !r.actionable());
  }
  {
    const CheckResult r = check_for_update(base_config("0.2.106"), serving(kDocument), accepting());
    check("a newer install -> UpToDate", r.outcome == CheckOutcome::UpToDate,
          check_outcome_name(r.outcome));
  }
  {
    // Numeric, not lexicographic -- reached through the shared contract.
    const CheckResult r = check_for_update(base_config("0.2.99"), serving(kDocument), accepting());
    check("0.2.105 over 0.2.99 -> UpdateAvailable", r.outcome == CheckOutcome::UpdateAvailable,
          check_outcome_name(r.outcome));
  }

  // ---------------------------------------------------------------- the caller is never blocked

  {
    // The fetcher sleeps. The point is that check_for_update_async returns long before it
    // finishes -- a tray menu must not sit on a socket timeout.
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    CheckResult received;

    const auto slow = [](const std::string&, size_t, std::string* doc, std::string* sig,
                         std::string*) {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      *doc = kDocument;
      *sig = kSignatureHex;
      return true;
    };

    const auto start = std::chrono::steady_clock::now();
    check_for_update_async(base_config("0.2.104"), slow, accepting(), [&](CheckResult r) {
      std::lock_guard<std::mutex> lock(mu);
      received = r;
      done = true;
      cv.notify_one();
    });
    const auto returnedAfter = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
    check("the async form returns immediately", returnedAfter < 100,
          std::to_string(returnedAfter) + "ms");

    std::unique_lock<std::mutex> lock(mu);
    const bool arrived = cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; });
    check("and the result arrives afterwards", arrived);
    check("with the right answer", received.outcome == CheckOutcome::UpdateAvailable,
          check_outcome_name(received.outcome));
  }

  {
    // A failing check must still call back. A UI that only hears about successes would sit on
    // "checking..." forever whenever the server was down -- which is exactly when a user looks.
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    CheckResult received;
    check_for_update_async(base_config("0.2.104"), unreachable("connection refused"), accepting(),
                           [&](CheckResult r) {
                             std::lock_guard<std::mutex> lock(mu);
                             received = r;
                             done = true;
                             cv.notify_one();
                           });
    std::unique_lock<std::mutex> lock(mu);
    const bool arrived = cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; });
    check("a failed check still calls back", arrived);
    check("with Unreachable", received.outcome == CheckOutcome::Unreachable,
          check_outcome_name(received.outcome));
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
