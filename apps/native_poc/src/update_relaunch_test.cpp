// The two decisions behind Relaunch and HealthCheck, tested without making either happen.
//
// Nothing here starts a process, touches the SCM, or reads a real log. The production code that
// does those things lives in update_relaunch.cpp, which this binary does NOT link -- the same
// arrangement as update_process_targets.cpp, and for the same reason: on the machine this was
// written on, GNLinkHost, GNLinkStream and GNLinkInputService were running, and a test that could
// call StartServiceW or CreateProcessW on the real install directory is a test that can disturb
// someone's session.
//
// Design: docs/업데이트_기능_설계.md 3.5-3.6.

#include "update_health.hpp"
#include "update_health_log.hpp"
#include "update_relaunch_plan.hpp"

#include <windows.h>

#include <fstream>
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

ProcessTarget running(const std::wstring& imagePath, uint32_t pid) {
  ProcessTarget t;
  t.pid = pid;
  t.imagePath = imagePath;
  t.creationTime = 1000 + pid;
  return t;
}

const RelaunchEntry* entry_for(const std::vector<RelaunchEntry>& plan, const std::wstring& name) {
  for (const RelaunchEntry& e : plan) {
    if (e.imageName == name) return &e;
  }
  return nullptr;
}

std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }

std::wstring temp_log_path() {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  wchar_t path[MAX_PATH]{};
  swprintf(path, MAX_PATH, L"%sgnlink-healthtest-%lu.log", base, GetCurrentProcessId());
  return path;
}

void append_line(const std::wstring& path, const std::string& line) {
  std::ofstream out(path, std::ios::binary | std::ios::app);
  out << line << "\n";
}

}  // namespace

int main() {
  const std::wstring dir = L"C:\\Program Files\\GNLink\\";

  // ---------------------------------------------------------------- what comes back

  {
    // Only what was running. The plan is "the same configuration as before", not "everything the
    // product has" -- starting something the user had closed is as wrong as not restarting
    // something they had open.
    const auto plan = relaunch_plan({running(dir + L"GNLinkHost.exe", 100)});
    check("only what was running is in the plan", plan.size() == 1,
          std::to_string(plan.size()));
    check("and it is the host", !plan.empty() && plan[0].imageName == L"GNLinkHost.exe");
    check("nothing else was invented",
          entry_for(plan, L"GNLinkClient.exe") == nullptr &&
              entry_for(plan, L"GNLinkInputService.exe") == nullptr);
  }

  {
    const auto plan = relaunch_plan({});
    check("nothing running means nothing to start", plan.empty(),
          std::to_string(plan.size()));
    check("and no entries to start either", entries_to_start(plan).empty());
  }

  {
    // Two instances of one image collapse to one entry. The product runs one of each, and coming
    // back with two would be a worse outcome than coming back with none.
    const auto plan = relaunch_plan({running(dir + L"GNLinkHost.exe", 100),
                                     running(dir + L"GNLinkHost.exe", 101)});
    check("duplicate instances collapse to one entry", plan.size() == 1,
          std::to_string(plan.size()));
  }

  {
    // Case is not identity on Windows, and the enumerator reports whatever the OS gives it.
    const auto plan = relaunch_plan({running(L"C:\\PROGRA~1\\GNLINK\\GNLINKHOST.EXE", 100)});
    check("an image path in another case is still the host", plan.size() == 1 &&
                                                                 plan[0].imageName ==
                                                                     L"GNLinkHost.exe",
          plan.empty() ? "" : narrow(plan[0].imageName));
    // And the entry carries the canonical spelling, because a launcher builds a path from it.
    check("the entry carries the canonical spelling, not the observed one",
          !plan.empty() && plan[0].imageName == L"GNLinkHost.exe");
  }

  {
    // The point of the whole file. These four are not peers, and treating them as peers is how a
    // relaunch produces an unsupervised orphan, a duplicate, a service that is not a service, and
    // a client running as administrator.
    const auto plan = relaunch_plan({running(dir + L"GNLinkHost.exe", 100),
                                     running(dir + L"GNLinkStream.exe", 101),
                                     running(dir + L"GNLinkInputService.exe", 102),
                                     running(dir + L"GNLinkClient.exe", 103),
                                     running(dir + L"GNLinkViewer.exe", 104)});
    check("every running product image is accounted for", plan.size() == 5,
          std::to_string(plan.size()));

    const RelaunchEntry* host = entry_for(plan, L"GNLinkHost.exe");
    const RelaunchEntry* stream = entry_for(plan, L"GNLinkStream.exe");
    const RelaunchEntry* service = entry_for(plan, L"GNLinkInputService.exe");
    const RelaunchEntry* client = entry_for(plan, L"GNLinkClient.exe");
    const RelaunchEntry* viewer = entry_for(plan, L"GNLinkViewer.exe");

    check("the host comes back as a child of the updater",
          host && host->kind == RelaunchKind::ElevatedProcess);
    check("the streaming child is NOT started here -- the host supervises it",
          stream && stream->kind == RelaunchKind::SupervisedByAnother);
    check("the viewer is NOT started here -- the client shell starts it on connect",
          viewer && viewer->kind == RelaunchKind::SupervisedByAnother);
    check("the service comes back through the SCM",
          service && service->kind == RelaunchKind::Service);
    // The one that matters most: an elevated updater must not hand the client its token.
    check("the client comes back in the ordinary user context",
          client && client->kind == RelaunchKind::UserProcess);
    check("and every decision carries a reason",
          host && !host->reason.empty() && stream && !stream->reason.empty() && client &&
              !client->reason.empty() && service && !service->reason.empty());

    const auto starting = entries_to_start(plan);
    check("only three of the five are actually started", starting.size() == 3,
          std::to_string(starting.size()));
    check("and none of them is a supervised child",
          entry_for(starting, L"GNLinkStream.exe") == nullptr &&
              entry_for(starting, L"GNLinkCapture.exe") == nullptr &&
              entry_for(starting, L"GNLinkViewer.exe") == nullptr);
    // "We decided not to start this" and "there was nothing to start" stay distinguishable.
    check("but the plan still records them", plan.size() > starting.size());
  }

  {
    // Order follows first appearance, so a caller starting entries in order starts the supervisor
    // before anything that might want it.
    const auto plan = relaunch_plan({running(dir + L"GNLinkInputService.exe", 100),
                                     running(dir + L"GNLinkHost.exe", 101)});
    check("order follows what was observed",
          plan.size() == 2 && plan[0].imageName == L"GNLinkInputService.exe" &&
              plan[1].imageName == L"GNLinkHost.exe");
  }

  {
    // A name that is not one of ours never becomes something this process runs. The list comes
    // from an enumerator today, but the shape is the same as a name arriving from a manifest, and
    // "data does not get to nominate an executable" has to hold in both.
    const auto plan = relaunch_plan({running(L"C:\\Windows\\System32\\cmd.exe", 100),
                                     running(L"C:\\evil\\GNLinkHost.exe.bak", 101),
                                     running(dir + L"GNLinkHost.exe", 102)});
    check("an unknown image is dropped, not launched", plan.size() == 1,
          std::to_string(plan.size()));
    check("and the one kept is ours", !plan.empty() && plan[0].imageName == L"GNLinkHost.exe");
  }

  {
    // An empty image path carries no identity, so it cannot select anything.
    const auto plan = relaunch_plan({running(L"", 100)});
    check("an empty image path selects nothing", plan.empty());
  }

  // ---------------------------------------------------------------- what counts as healthy

  {
    const HealthSignals none = scan_health_report("");
    check("an empty log reports nothing", !none.reported);
    std::string detail;
    check("and judging it says to keep waiting",
          judge_health(none, "0.2.105", &detail) == HealthVerdict::Waiting, detail);
  }

  {
    // Lines that are not health reports, including ones that mention a version.
    const std::string log =
        "09-08 12:00:01 [host-app] starting the streaming host (tune=low_latency)\n"
        "09-08 12:00:02 [host-app] update check: UpToDate 0.2.105\n"
        "09-08 12:00:03 [gnlink-host] log upload on\n";
    const HealthSignals signals = scan_health_report(log);
    check("ordinary log lines are not health reports", !signals.reported);
  }

  {
    const std::string log =
        "09-08 12:00:01 [host-app] health version=0.2.105 directory=ok\n";
    const HealthSignals signals = scan_health_report(log);
    check("a report is read", signals.reported && signals.reportedVersion == "0.2.105",
          signals.reportedVersion);
    check("with its directory state", signals.directory == DirectoryHealth::Ok,
          directory_health_name(signals.directory));
    std::string detail;
    check("and the expected version is healthy",
          judge_health(signals, "0.2.105", &detail) == HealthVerdict::Healthy, detail);
  }

  {
    // The state that must NOT be a failure. A machine sitting at the sign-in screen has no
    // directory to reach, and rolling an update back for that would invent a fault.
    const HealthSignals signals =
        scan_health_report("09-08 12:00:01 [host-app] health version=0.2.105 "
                           "directory=not-configured\n");
    std::string detail;
    check("no account signed in is healthy",
          judge_health(signals, "0.2.105", &detail) == HealthVerdict::Healthy, detail);
    check("and the reason says why", detail.find("signed in") != std::string::npos, detail);
  }

  {
    // Pending is not failure. A slower answer is still an answer.
    const HealthSignals signals =
        scan_health_report("09-08 12:00:01 [host-app] health version=0.2.105 directory=pending\n");
    std::string detail;
    check("a pending directory means keep waiting",
          judge_health(signals, "0.2.105", &detail) == HealthVerdict::Waiting, detail);
  }

  {
    // Last wins: the product reports again when it learns something.
    const std::string log =
        "09-08 12:00:01 [host-app] health version=0.2.105 directory=pending\n"
        "09-08 12:00:04 [host-app] health version=0.2.105 directory=ok\n";
    const HealthSignals signals = scan_health_report(log);
    check("a later report supersedes an earlier one", signals.directory == DirectoryHealth::Ok,
          directory_health_name(signals.directory));
    std::string detail;
    check("so the run is healthy",
          judge_health(signals, "0.2.105", &detail) == HealthVerdict::Healthy, detail);
  }

  {
    // The failure the check exists for: something is running the files the update replaced.
    // Distinguished from Waiting because waiting longer cannot fix it.
    const HealthSignals signals =
        scan_health_report("09-08 12:00:01 [host-app] health version=0.2.104 directory=ok\n");
    std::string detail;
    check("the old version reporting is a failure, not a delay",
          judge_health(signals, "0.2.105", &detail) == HealthVerdict::WrongVersion, detail);
    check("and the detail names both versions",
          detail.find("0.2.104") != std::string::npos &&
              detail.find("0.2.105") != std::string::npos,
          detail);
  }

  {
    // Half a report is not evidence.
    check("a report with no directory field is ignored",
          !scan_health_report("09-08 [host-app] health version=0.2.105\n").reported);
    check("a report with no version is ignored",
          !scan_health_report("09-08 [host-app] health directory=ok\n").reported);
    check("a report with an empty version is ignored",
          !scan_health_report("09-08 [host-app] health version= directory=ok\n").reported);
  }

  {
    // A state a newer product might write. Ignored rather than guessed at -- an unrecognised
    // value must not be able to satisfy an older updater by accident.
    const HealthSignals signals =
        scan_health_report("09-08 [host-app] health version=0.2.105 directory=degraded\n");
    check("an unknown directory state is not evidence", !signals.reported,
          directory_health_name(signals.directory));
  }

  {
    // A key that merely ends in the one being looked for must not match it.
    const HealthSignals signals = scan_health_report(
        "09-08 [host-app] health otherversion=9.9.9 version=0.2.105 directory=ok\n");
    check("a field boundary is required", signals.reportedVersion == "0.2.105",
          signals.reportedVersion);
  }

  {
    // Logs travel as CRLF often enough that a checker which does not strip the CR reports a
    // version with an invisible character on the end and then says it does not match.
    const HealthSignals signals =
        scan_health_report("09-08 [host-app] health version=0.2.105 directory=ok\r\n");
    std::string detail;
    check("a CRLF log still parses", signals.reportedVersion == "0.2.105",
          signals.reportedVersion);
    check("and is judged healthy",
          judge_health(signals, "0.2.105", &detail) == HealthVerdict::Healthy, detail);
  }

  {
    // No trailing newline: the last line is still a line.
    const HealthSignals signals =
        scan_health_report("09-08 [host-app] health version=0.2.105 directory=ok");
    check("a final line without a newline is read", signals.reported);
  }

  {
    // An empty expected version disables the comparison, which is what a caller that does not
    // know the version yet would pass. It must not therefore accept anything at all -- the
    // directory state still has to be acceptable.
    const HealthSignals pending =
        scan_health_report("09-08 [host-app] health version=0.2.105 directory=pending\n");
    std::string detail;
    check("no expected version still requires the directory to answer",
          judge_health(pending, "", &detail) == HealthVerdict::Waiting, detail);
  }

  // ---------------------------------------------------------------- evidence has to be new
  //
  // The failure this whole arrangement exists to prevent: a log already contains a health report
  // from every previous run, including the version the update just replaced. A check that reads
  // the file reports success in exactly the case it was written to catch.

  {
    const std::wstring log = temp_log_path();
    DeleteFileW(log.c_str());

    // A log that does not exist yet has nothing in it, and everything from now on is new.
    check("a missing log marks at zero", file_size_or_zero(log) == 0);
    check("and reading it yields nothing", read_from_offset(log, 0).empty());

    // The previous version ran, and ran fine. This is the trap.
    append_line(log, "09-08 11:00:00 [host-app] starting the streaming host (tune=low_latency)");
    append_line(log, "09-08 11:00:01 [host-app] health version=0.2.104 directory=ok");

    // Reading the whole file finds it, which is exactly why nothing reads the whole file.
    {
      const HealthSignals whole = scan_health_report(read_from_offset(log, 0));
      std::string detail;
      check("read from the start, the OLD run looks healthy for the OLD version",
            judge_health(whole, "0.2.104", &detail) == HealthVerdict::Healthy, detail);
    }

    // The mark is taken here -- before anything is relaunched.
    const uint64_t mark = file_size_or_zero(log);
    check("the mark is where the log ended", mark > 0, std::to_string(mark));

    {
      // Nothing new yet. THIS is the assertion that matters: the old report must not be able to
      // satisfy the check, not even for the version it names.
      const std::string fresh = read_from_offset(log, mark);
      check("nothing appended reads as nothing", fresh.empty(), fresh);
      const HealthSignals signals = scan_health_report(fresh);
      std::string detail;
      check("so the previous run's report is not evidence for this one",
            judge_health(signals, "0.2.105", &detail) == HealthVerdict::Waiting, detail);
      check("and not for its own version either",
            judge_health(signals, "0.2.104", &detail) == HealthVerdict::Waiting, detail);
    }

    {
      // The new build comes up, but reports the version it replaced -- something is still running
      // the old files. Distinguished from "not yet", because waiting cannot fix it.
      append_line(log, "09-08 12:00:00 [host-app] health version=0.2.104 directory=ok");
      const HealthSignals signals = scan_health_report(read_from_offset(log, mark));
      std::string detail;
      check("a fresh report of the old version is a failure, not a delay",
            judge_health(signals, "0.2.105", &detail) == HealthVerdict::WrongVersion, detail);
    }

    {
      // And now the real thing.
      append_line(log, "09-08 12:00:02 [host-app] health version=0.2.105 directory=pending");
      const HealthSignals pending = scan_health_report(read_from_offset(log, mark));
      std::string detail;
      check("the new version reporting pending means keep waiting",
            judge_health(pending, "0.2.105", &detail) == HealthVerdict::Waiting, detail);

      append_line(log, "09-08 12:00:05 [host-app] health version=0.2.105 directory=ok");
      const HealthSignals ok = scan_health_report(read_from_offset(log, mark));
      check("and once the directory answers, it is healthy",
            judge_health(ok, "0.2.105", &detail) == HealthVerdict::Healthy, detail);
    }

    {
      // A log that was rotated away under us is smaller than the mark. Its remaining bytes belong
      // to a different file, so they are not this run's evidence.
      const uint64_t beyond = file_size_or_zero(log) + 1000;
      check("a mark past the end reads as nothing", read_from_offset(log, beyond).empty());
    }

    {
      // The product writes to this file while the check reads it. A check that opened it
      // exclusively would be preventing the evidence it is waiting for.
      HANDLE writer = CreateFileW(log.c_str(), FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
      check("the log can be held open for writing", writer != INVALID_HANDLE_VALUE);
      const HealthSignals signals = scan_health_report(read_from_offset(log, mark));
      std::string detail;
      check("and it is still readable while held",
            judge_health(signals, "0.2.105", &detail) == HealthVerdict::Healthy, detail);
      if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
    }

    DeleteFileW(log.c_str());
  }

  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
