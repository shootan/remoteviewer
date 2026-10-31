// When the host may get out of the way, and what it hands over.
//
// The host is one of the files the update replaces, so it has to exit for the swap to happen --
// and exiting at the wrong moment is the failure this whole handshake exists to prevent. If it
// leaves before the updater has the lock and a verified download, and the update then fails,
// nothing is left running to put the product back. The user's program is just gone.
//
// So most of what is asserted here is that the host STAYS. Only one combination lets it leave.

#include "update_handoff.hpp"

#include <iostream>
#include <string>
#include <vector>

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

std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }

UpdaterLaunchSpec complete_spec() {
  UpdaterLaunchSpec spec;
  spec.installDir = L"C:\\Program Files\\GNLink";
  spec.stagingDir = L"C:\\Program Files\\GNLink.update\\staging";
  spec.workDir = L"C:\\Program Files\\GNLink.update";
  spec.manifestUrl = "https://updates.example/api/update/manifest";
  spec.platform = "windows";
  spec.installedVersion = "0.2.104";
  spec.healthLogPath = L"C:\\Users\\u\\AppData\\Local\\GNLink\\host_app.log";
  spec.logPath = L"C:\\Program Files\\GNLink.update\\updater.log";
  spec.serviceName = L"GNLinkSecureInput";
  spec.registryRoot = L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GNLink";
  spec.readyEventName = L"Local\\GNLinkUpdateReady-5156-99";
  spec.parentPid = 5156;
  return spec;
}

/** The value following `flag`, or empty when it is not there. */
std::wstring value_of(const std::vector<std::wstring>& args, const std::wstring& flag) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == flag) return args[i + 1];
  }
  return {};
}

bool has_flag(const std::vector<std::wstring>& args, const std::wstring& flag) {
  for (const std::wstring& a : args) {
    if (a == flag) return true;
  }
  return false;
}

}  // namespace

int main() {
  // ---------------------------------------------------------------- what gets handed over

  // ---------------------------------------------- what the argument list may and may not carry
  //
  // The pipe name is in here. The credential is not, and cannot be: there is no field on the spec
  // that could hold one. Asserted rather than assumed, because "we would never put it there" is
  // the kind of thing that stops being true in a hurry once somebody needs it quickly.
  {
    UpdaterLaunchSpec spec = complete_spec();
    spec.credentialPipeName = L"\\\\.\\pipe\\GNLinkUpdateCred-1-2";
    spec.derivedEndpoint = true;
    const auto args = updater_arguments(spec);

    bool named = false;
    bool envelope = false;
    for (size_t i = 0; i < args.size(); ++i) {
      if (args[i] == L"--credential-pipe" && i + 1 < args.size() &&
          args[i + 1] == spec.credentialPipeName) {
        named = true;
      }
      if (args[i] == L"--manifest-envelope") envelope = true;
    }
    check("the pipe is named in the arguments", named);
    check("and so is the wire shape", envelope);

    // The token a real run would hold. It must not be reachable from anything the launch writes.
    const std::wstring fixtureToken = L"fixture-token-not-a-real-credential";
    bool leaked = false;
    for (const std::wstring& arg : args) {
      if (arg.find(fixtureToken) != std::wstring::npos) leaked = true;
    }
    check("no credential is anywhere in the argument list", !leaked);

    UpdaterLaunchSpec plain = complete_spec();
    const auto plainArgs = updater_arguments(plain);
    bool mentionsChannel = false;
    for (const std::wstring& arg : plainArgs) {
      if (arg == L"--credential-pipe" || arg == L"--manifest-envelope") mentionsChannel = true;
    }
    check("a launch with no credential names no channel", !mentionsChannel);
  }

  {
    const auto args = updater_arguments(complete_spec());
    check("the install directory is passed",
          value_of(args, L"--install-dir") == L"C:\\Program Files\\GNLink");
    check("and the staging directory",
          value_of(args, L"--staging-dir") == L"C:\\Program Files\\GNLink.update\\staging");
    check("and the working directory",
          value_of(args, L"--work-dir") == L"C:\\Program Files\\GNLink.update");
    check("and the manifest url",
          value_of(args, L"--manifest-url") ==
              L"https://updates.example/api/update/manifest");
    check("and the installed version, so the updater does not guess it",
          value_of(args, L"--installed-version") == L"0.2.104");
    check("and the ready event, which is how the host learns it may leave",
          value_of(args, L"--ready-event") == L"Local\\GNLinkUpdateReady-5156-99");
    check("and the parent pid", value_of(args, L"--parent-pid") == L"5156");
    check("every flag has a value", args.size() % 2 == 0, std::to_string(args.size()));
  }

  {
    // An empty optional is left out rather than passed as "". The updater reports which required
    // argument is missing; `--log ""` would turn that clear message into a confusing one.
    UpdaterLaunchSpec spec = complete_spec();
    spec.readyEventName.clear();
    spec.parentPid = 0;
    const auto args = updater_arguments(spec);
    check("no ready event means the flag is absent, not empty", !has_flag(args, L"--ready-event"));
    check("and the same for the parent pid", !has_flag(args, L"--parent-pid"));
    check("the required ones are still there",
          !value_of(args, L"--install-dir").empty() && !value_of(args, L"--log").empty());
  }

  // ---------------------------------------------------------------- the event name

  {
    const std::wstring a = make_ready_event_name(5156, 100);
    const std::wstring b = make_ready_event_name(5156, 101);
    const std::wstring c = make_ready_event_name(9999, 100);
    // A leftover event from an earlier attempt, already signalled, would tell the host to leave
    // before this updater had done anything -- the exact failure the handshake prevents, reached
    // by reusing a name.
    check("two launches from one process get different names", a != b, narrow(a) + " vs " + narrow(b));
    check("and two processes get different names", a != c, narrow(a) + " vs " + narrow(c));
    // Local, not Global: the host and the updater it starts share a session, and a global name
    // would be visible to every session on the machine.
    check("the name is session-local", a.rfind(L"Local\\", 0) == 0, narrow(a));
    check("and it says what it is", a.find(L"GNLinkUpdateReady") != std::wstring::npos, narrow(a));
  }

  // ---------------------------------------------------------------- when it is safe to leave

  // ---------------------------------------------------- the clock, at the values that broke it
  {
    check("plenty of time left", remaining_ms(1000, 2000, 10000) == 9000,
          std::to_string(remaining_ms(1000, 2000, 10000)));
    check("exactly out of time", remaining_ms(1000, 11000, 10000) == 0);
    check("past the deadline stays zero, it does not wrap",
          remaining_ms(1000, 999999, 10000) == 0);

    // THE case. A 32-bit tick count near its ceiling: the old form computed
    // `deadline = now + timeout` in 32 bits, which wrapped past zero, and every wait then ended
    // immediately reporting that the updater had run out of time. On a machine up for 49.7 days
    // no update could ever hand over, and it would have looked like a flaky server.
    const uint64_t nearWrap = 0xFFFFFF00ull;  // ~4.29e9 ms, a little under 2^32
    check("near the 32-bit ceiling: the full timeout is still available",
          remaining_ms(nearWrap, nearWrap, 600000) == 600000,
          std::to_string(remaining_ms(nearWrap, nearWrap, 600000)));
    // And ACROSS it -- the point where a 32-bit sum would have gone backwards.
    check("across the 32-bit ceiling: time still counts down normally",
          remaining_ms(nearWrap, nearWrap + 1000, 600000) == 599000,
          std::to_string(remaining_ms(nearWrap, nearWrap + 1000, 600000)));

    // A clock that appears to move backwards is not evidence that time has passed. Treating it as
    // elapsed would end the wait early for a reason that has nothing to do with the update.
    check("a clock that went backwards does not shorten the wait",
          remaining_ms(5000, 4000, 10000) == 10000,
          std::to_string(remaining_ms(5000, 4000, 10000)));
  }

  // ---------------------------------------------------- the step, which the verdict could not say
  {
    std::string why;
    // THE case, and the one the old two-answer verdict could not express.
    //
    // The process the host starts is a bootstrap; it copies the updater aside, starts that copy
    // and exits at once, because it cannot hold open a file the update replaces. Its exit is
    // success. Asked the old question -- "did the updater exit?" -- the honest answer was yes,
    // and the honest conclusion was that the update had failed. Every ordinary update took that
    // path.
    check("the bootstrap handed over and exited: keep waiting",
          handoff_step(false, true, 0, false, &why) == HandoffStep::KeepWaiting, why);
    check("...and the reason says that is what it is meant to do",
          why.find("meant to do") != std::string::npos, why);

    // The counter-control for the fix: a bootstrap that really failed must NOT be waited on.
    // Without this, "keep waiting on exit" would be right for the wrong reason -- it would be
    // waiting out the full timeout on a handover that never happened.
    check("the bootstrap failed: stop waiting",
          handoff_step(false, true, 3, false, &why) == HandoffStep::KeepRunning, why);
    check("...and the reason distinguishes it from a handover",
          why.find("failed before") != std::string::npos, why);

    check("ready wins over everything",
          handoff_step(true, true, 0, true, &why) == HandoffStep::ExitNow, why);
    check("out of time: keep running",
          handoff_step(false, false, 0, true, &why) == HandoffStep::KeepRunning, why);
  }

  // ---------------------------------------------------- and the updater's half of the handshake
  {
    std::string why;
    check("delivered and answered: the updater may stop the product",
          may_stop_the_product(true, true, &why), why);
    // Delivered to nobody. The event could not be opened, so nothing is waiting -- and stopping
    // the product would stop something with nobody to bring it back.
    check("not delivered: it may not", !may_stop_the_product(false, false, &why), why);
    check("...and the reason names delivery",
          why.find("could not be delivered") != std::string::npos, why);
    // Delivered, and never answered. The waiting caller gave up -- in the product it has already
    // told the user the installed version is unchanged. Stopping it now is the worst outcome on
    // this path: the user was promised nothing would happen, and then the machine goes.
    check("delivered but unanswered: it may not", !may_stop_the_product(true, false, &why), why);
    check("...and the reason names the missing answer",
          why.find("never answered") != std::string::npos, why);
  }

  {
    std::string why;
    // The one combination that permits it. Everything before the signal is recoverable by doing
    // nothing, because nothing on disk has changed.
    check("signalled: the host may exit",
          handoff_verdict(true, false, false, &why) == HandoffVerdict::ExitNow, why);
    check("and the reason says the download is verified",
          why.find("verified") != std::string::npos, why);
  }

  {
    std::string why;
    check("nothing yet: the host stays",
          handoff_verdict(false, false, false, &why) == HandoffVerdict::KeepRunning, why);
    check("the updater died first: the host stays",
          handoff_verdict(false, true, false, &why) == HandoffVerdict::KeepRunning, why);
    check("and the reason says nothing was changed",
          why.find("nothing was changed") != std::string::npos, why);
    check("it ran out of time: the host stays",
          handoff_verdict(false, false, true, &why) == HandoffVerdict::KeepRunning, why);
    check("died AND timed out: the host stays",
          handoff_verdict(false, true, true, &why) == HandoffVerdict::KeepRunning, why);
  }

  {
    std::string why;
    // Ordering that matters. An updater which signalled and then exited did what the signal
    // promised -- the case where the manifest said there was nothing to do. Reading the exit as a
    // failure would keep the host running after it had already agreed to leave, and the swap
    // would then find it still holding its files.
    check("signalled and then exited: still safe to leave",
          handoff_verdict(true, true, false, &why) == HandoffVerdict::ExitNow, why);
    check("signalled, exited, and the wait ran out: still safe to leave",
          handoff_verdict(true, true, true, &why) == HandoffVerdict::ExitNow, why);
  }

  {
    // Stated as its own claim because it is the property, not a case: there is exactly one way to
    // reach ExitNow, and it goes through the signal.
    int exitNowCount = 0;
    for (int bits = 0; bits < 8; ++bits) {
      const bool signalled = (bits & 1) != 0;
      const bool exited = (bits & 2) != 0;
      const bool timedOut = (bits & 4) != 0;
      const HandoffVerdict verdict = handoff_verdict(signalled, exited, timedOut, nullptr);
      if (verdict == HandoffVerdict::ExitNow) {
        ++exitNowCount;
        check("every ExitNow was signalled", signalled,
              "signalled=0 exited=" + std::to_string(exited) + " timedOut=" +
                  std::to_string(timedOut));
      }
    }
    // Four of the eight combinations have the signal set, and all four permit leaving.
    check("only the signal permits leaving, and it always does", exitNowCount == 4,
          std::to_string(exitNowCount));
  }

  // ---------------------------------------------------------------- declining is an answer
  //
  // The client is not an administrator program, so replacing files in %ProgramFiles% means asking
  // for consent once. What the user says to that prompt is a decision, not a fault.

  {
    check("a successful elevated launch", elevation_outcome(true, 0) == ElevationOutcome::Launched);
    // 1223 is ERROR_CANCELLED -- what the shell reports when the consent prompt is dismissed.
    check("declining the prompt is Cancelled, not Failed",
          elevation_outcome(false, 1223) == ElevationOutcome::Cancelled,
          elevation_outcome_name(elevation_outcome(false, 1223)));
    // Kept apart because the responses differ: one carries on silently, the other is worth
    // saying out loud. Reporting a decline as an error tells someone that the thing they just
    // declined did not work.
    check("anything else is Failed",
          elevation_outcome(false, 5) == ElevationOutcome::Failed &&
              elevation_outcome(false, 2) == ElevationOutcome::Failed,
          elevation_outcome_name(elevation_outcome(false, 5)));
    check("and a success is never mistaken for a decline",
          elevation_outcome(true, 1223) == ElevationOutcome::Launched);
  }

  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
