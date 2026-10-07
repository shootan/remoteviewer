// prelogin-ui r9 (R4/W1): when the service starts the logged-on user's GNLinkHost.
//
// The counter-example is the one the verifier traced in the c196833 service: the user switched the feature
// on from the GNLinkHost they were running (A); after logon the service started B ("--tray"), B found A's
// single-instance mutex and left (exit 0), no ready came, and the next tick started B again -- every
// second, for as long as A ran; and when the user closed their GNLinkHost it came straight back.
// The ticks below are what the service's reconcile feeds the product policy (prelogin_user_host_policy.hpp).

#include <cstdio>
#include <string>

#include "prelogin_user_host_policy.hpp"

namespace pl = remote60::native_poc::prelogin;
using D = pl::UserHostDecision;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
pl::UserHostObservation obs(uint64_t logon, uint64_t now) {
  pl::UserHostObservation o;
  o.logon = logon;
  o.nowMs = now;
  return o;
}
const char* name(D d) { return d == D::Launch ? "Launch" : d == D::Adopt ? "Adopt" : "None"; }
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the user's GNLinkHost after logon (prelogin-ui r9, R4/W1)\n\n");
  {
    std::printf("--- the user already runs GNLinkHost (the one the feature was switched on from) ---\n");
    pl::UserHostLaunchPolicy p;
    auto o = obs(1, 1000);
    o.otherRunning = true;
    check("R4: a GNLinkHost already running in the session is adopted, none is started", p.Decide(o) == D::Adopt);
    int launches = 0;
    for (int t = 0; t < 60; ++t) {  // a minute of ticks with the adopted one alive
      auto a = obs(1, 2000 + t * 1000);
      a.watchedAlive = true;
      if (p.Decide(a) == D::Launch) ++launches;
    }
    check("R4: ...and nothing is started on the following ticks (it was one per second)", launches == 0,
          std::to_string(launches) + " launches");
    auto closed = obs(1, 70000);
    closed.watchedExited = true;
    closed.watchedExitCode = 0;
    check("R4: the user closes it (exit 0): it is NOT started again", p.Decide(closed) == D::None);
    int after = 0;
    for (int t = 0; t < 120; ++t) {
      if (p.Decide(obs(1, 71000 + t * 1000)) == D::Launch) ++after;
    }
    check("...not for the rest of this logon", after == 0, std::to_string(after));
    check("a new logon starts over: then it is started", p.Decide(obs(2, 300000)) == D::Launch);
  }
  {
    std::printf("\n--- the service starts it; a second instance yields ---\n");
    pl::UserHostLaunchPolicy p;
    check("no GNLinkHost in the session: one is started", p.Decide(obs(1, 1000)) == D::Launch);
    auto yielded = obs(1, 1500);
    yielded.watchedExited = true;
    yielded.watchedExitCode = 0;  // found another instance's mutex: exit 0
    yielded.otherRunning = true;
    check("R4: the started one yielded to a GNLinkHost that appeared meanwhile: that one is adopted",
          p.Decide(yielded) == D::Adopt);
    auto gone = obs(1, 5000);
    gone.watchedExited = true;
    gone.watchedExitCode = 0;
    check("...and when the user closes that one, nothing comes back", p.Decide(gone) == D::None &&
                                                                          p.Decide(obs(1, 9000)) == D::None);
  }
  {
    std::printf("\n--- a crash is restarted, bounded ---\n");
    pl::UserHostLaunchPolicy p;
    check("started", p.Decide(obs(1, 0)) == D::Launch);
    auto crash = obs(1, 500);
    crash.watchedExited = true;
    crash.watchedExitCode = 0xC0000005u;
    const D first = p.Decide(crash);
    check("a crash within seconds: not restarted at once (backoff)", first == D::None, name(first));
    check("...but after the wait (2 s)", p.Decide(obs(1, 2400)) == D::None && p.Decide(obs(1, 2500)) == D::Launch);
    auto again = obs(1, 2600);
    again.watchedExited = true;
    again.watchedExitCode = 0xC0000409u;
    (void)p.Decide(again);
    check("...dying again doubles the wait (4 s)", p.Decide(obs(1, 6500)) == D::None && p.Decide(obs(1, 6600)) == D::Launch);
    check("a crash is not taken as the user closing it", !p.userClosed());
    pl::UserHostLaunchPolicy tm;
    (void)tm.Decide(obs(1, 0));
    auto ended = obs(1, 5000);
    ended.watchedExited = true;
    ended.watchedExitCode = 1;  // Task Manager "End task"
    check("r10: ended from Task Manager (exit 1) is the user's choice: not started again",
          tm.Decide(ended) == D::None && tm.Decide(obs(1, 120000)) == D::None && tm.userClosed());
    pl::UserHostLaunchPolicy f;
    (void)f.Decide(obs(1, 0));
    f.OnLaunchFailed(0);
    check("a start that could not be made counts as a quick death (waits)", f.Decide(obs(1, 100)) == D::None);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
