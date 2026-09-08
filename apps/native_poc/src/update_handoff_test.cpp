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

  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
