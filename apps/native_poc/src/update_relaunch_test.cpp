// Relaunch and HealthCheck: the decisions, and the execution.
//
// This file used to say that update_relaunch.cpp was deliberately not linked here, and called
// that isolation. It was not. It meant the code that starts processes had never run, and the
// checklist went as far as recording it as impossible to cover. CreateProcess against a dummy
// executable in a temp directory destroys nothing; what made it untestable was that the relaunch
// code chose the product's image names itself.
//
// So the image table is now an argument, and this binary links the real execution layer and runs
// it. Everything it touches comes from here: a temp install root, dummy executables this test
// builds at start-up, a temp log, and a service name that does not exist. There is no value it
// could be given that points at the real product -- the same discipline UpdateEffectsConfig
// already enforces with validate().
//
// Two things are still not covered here, and they are named rather than implied:
//   * comparing integrity levels needs an elevated parent, which this session may not be;
//   * starting a real service needs administrator. Only the failure path is exercised.
// Both are in docs/수동확인_체크리스트.md as UPD-FIELD items.
//
// Design: docs/업데이트_기능_설계.md 3.5-3.6, docs/업데이트_배선_계획.md §2.

#include "update_health.hpp"
#include "update_health_log.hpp"
#include "update_relaunch.hpp"
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

/**
 * A dummy that reports which one it was.
 *
 * A .cmd rather than a compiled exe so the test builds nothing: what is being checked is that the
 * relaunch code starts the file it was told to, and CreateProcessW starting a batch file goes
 * through the same call. Each writes its own name into a shared file, so "did the right one run"
 * and "did it run twice" are both answerable afterwards.
 */
void write_dummy(const std::wstring& path, const std::wstring& marker,
                 const std::wstring& witness) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << "@echo off\r\n";
  out << ">>\"" << std::string(witness.begin(), witness.end()) << "\" echo "
      << std::string(marker.begin(), marker.end()) << "\r\n";
}

std::string read_witness(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int count_occurrences(const std::string& haystack, const std::string& needle) {
  int n = 0;
  size_t at = haystack.find(needle);
  while (at != std::string::npos) {
    ++n;
    at = haystack.find(needle, at + needle.size());
  }
  return n;
}

std::wstring make_temp_dir_named(const wchar_t* tag) {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  wchar_t unique[MAX_PATH]{};
  swprintf(unique, MAX_PATH, L"%sgnlink-exec-%lu-%s", base, GetCurrentProcessId(), tag);
  CreateDirectoryW(unique, nullptr);
  return unique;
}

void remove_tree_flat(const std::wstring& dir) {
  WIN32_FIND_DATAW find{};
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &find);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = find.cFileName;
      if (name == L"." || name == L"..") continue;
      DeleteFileW((dir + L"\\" + name).c_str());
    } while (FindNextFileW(h, &find));
    FindClose(h);
  }
  RemoveDirectoryW(dir.c_str());
}

/** Waits for the witness file to contain `needle`, or gives up. Dummies exit within moments. */
bool wait_for_witness(const std::wstring& witness, const std::string& needle, int timeoutMs) {
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(timeoutMs);
  for (;;) {
    if (read_witness(witness).find(needle) != std::string::npos) return true;
    if (GetTickCount() >= deadline) return false;
    Sleep(50);
  }
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

  // ================================================================ the execution layer, for real
  //
  // Same code the product will run. Different table, different directory, different names.

  {
    const std::wstring root = make_temp_dir_named(L"root");
    const std::wstring witness = root + L"\\witness.txt";
    const std::wstring log = root + L"\\dummy.log";

    // A table of dummies, mirroring the product's four kinds. The allow-list property is
    // unchanged -- a name not in THIS table still cannot be started.
    const std::vector<KnownImage> dummies = {
        {L"DummyHost.cmd", L"dummyhost.cmd", RelaunchKind::ElevatedProcess, "stands in for the host"},
        {L"DummyClient.cmd", L"dummyclient.cmd", RelaunchKind::UserProcess, "stands in for the client"},
        {L"DummySvc.cmd", L"dummysvc.cmd", RelaunchKind::Service, "stands in for the service"},
        {L"DummyChild.cmd", L"dummychild.cmd", RelaunchKind::SupervisedByAnother,
         "stands in for a supervised child"},
    };
    for (const KnownImage& image : dummies) {
      write_dummy(root + L"\\" + image.name, image.name, witness);
    }

    const auto stopped_dummy = [&](const std::wstring& name, uint32_t pid) {
      return running(root + L"\\" + name, pid);
    };
    const auto base = [&]() {
      RelaunchConfig c;
      c.installDir = root;
      c.serviceName = L"GNLinkNoSuchServiceForTest";  // deliberately absent
      c.healthLogPath = log;
      c.expectedVersion = "0.2.105";
      c.healthTimeoutMs = 1500;
      c.healthPollMs = 100;
      return c;
    };

    // -------------------------------------------------------------- E1: it actually starts
    {
      DeleteFileW(witness.c_str());
      RelaunchEffects e = make_relaunch_effects(base(), {stopped_dummy(L"DummyHost.cmd", 100)},
                                                dummies);
      const bool all = e.relaunch();
      check("E1: the elevated-process entry reports started", all,
            all ? "" : "relaunch() said false");
      check("E1: and the dummy really ran",
            wait_for_witness(witness, "DummyHost.cmd", 5000), read_witness(witness));
      const auto outcomes = e.lastOutcomes();
      check("E1: one outcome, started, not skipped",
            outcomes.size() == 1 && outcomes[0].started && !outcomes[0].skipped,
            std::to_string(outcomes.size()));
      check("E1: nothing to tell the user", e.userNotice().empty(), e.userNotice());
    }

    // -------------------------------------------------------------- E2: no duplicates
    {
      DeleteFileW(witness.c_str());
      RelaunchEffects e = make_relaunch_effects(
          base(), {stopped_dummy(L"DummyHost.cmd", 100), stopped_dummy(L"DummyHost.cmd", 101)},
          dummies);
      e.relaunch();
      check("E2: two instances stopped, one started",
            wait_for_witness(witness, "DummyHost.cmd", 5000));
      Sleep(300);  // give a second launch, if there were one, time to show up
      check("E2: and exactly once", count_occurrences(read_witness(witness), "DummyHost.cmd") == 1,
            read_witness(witness));
    }

    // -------------------------------------------------------------- E3: supervised children
    {
      DeleteFileW(witness.c_str());
      RelaunchEffects e = make_relaunch_effects(
          base(), {stopped_dummy(L"DummyHost.cmd", 100), stopped_dummy(L"DummyChild.cmd", 101)},
          dummies);
      const bool all = e.relaunch();
      check("E3: skipping a supervised child is not a failure", all);
      check("E3: the host ran", wait_for_witness(witness, "DummyHost.cmd", 5000));
      Sleep(300);
      check("E3: the supervised child did NOT run",
            count_occurrences(read_witness(witness), "DummyChild.cmd") == 0, read_witness(witness));
      bool skippedRecorded = false;
      for (const RelaunchOutcome& o : e.lastOutcomes()) {
        if (o.imageName == L"DummyChild.cmd") skippedRecorded = o.skipped && !o.failed();
      }
      check("E3: and it is recorded as skipped, not failed", skippedRecorded);
    }

    // -------------------------------------------------------------- E4: not in the table
    {
      DeleteFileW(witness.c_str());
      write_dummy(root + L"\\Stranger.cmd", L"Stranger.cmd", witness);
      RelaunchEffects e = make_relaunch_effects(base(), {stopped_dummy(L"Stranger.cmd", 100)},
                                                dummies);
      e.relaunch();
      Sleep(300);
      check("E4: a name outside the table is never started",
            count_occurrences(read_witness(witness), "Stranger.cmd") == 0, read_witness(witness));
      check("E4: and there is nothing in the plan to start", e.lastOutcomes().empty(),
            std::to_string(e.lastOutcomes().size()));
    }

    // -------------------------------------------------------------- E5: via the shell
    {
      DeleteFileW(witness.c_str());
      RelaunchEffects e = make_relaunch_effects(base(), {stopped_dummy(L"DummyClient.cmd", 100)},
                                                dummies);
      const bool all = e.relaunch();
      const std::string note = e.userNotice();
      // The shell route needs an interactive desktop with Explorer. When it is there this must
      // succeed; when it is not, the REQUIRED behaviour is a recorded failure -- never a silent
      // success, and never an elevated fallback. Both are asserted, so the case is meaningful
      // either way rather than passing by not running.
      if (all) {
        check("E5: the client started through the shell",
              wait_for_witness(witness, "DummyClient.cmd", 5000), read_witness(witness));
        check("E5: and nothing is reported to the user", note.empty(), note);
      } else {
        Sleep(300);
        check("E5: with no shell route the client is NOT started",
              count_occurrences(read_witness(witness), "DummyClient.cmd") == 0,
              read_witness(witness));
        check("E5: and the user is told", !note.empty(), note);
      }
    }

    // -------------------------------------------------------------- E6: no elevated fallback
    {
      DeleteFileW(witness.c_str());
      set_shell_launch_disabled_for_test(true);
      RelaunchEffects e = make_relaunch_effects(base(), {stopped_dummy(L"DummyClient.cmd", 100)},
                                                dummies);
      const bool all = e.relaunch();
      set_shell_launch_disabled_for_test(false);
      Sleep(300);
      // The absence of a fallback, asserted. Without this, removing the "no fallback" rule would
      // break nothing that anyone would notice.
      check("E6: with no shell route, nothing is started at all",
            count_occurrences(read_witness(witness), "DummyClient.cmd") == 0, read_witness(witness));
      check("E6: and it is NOT reported as success", !all);

      // -------------------------------------------------------- E6b: a safe failure is a failure
      const auto outcomes = e.lastOutcomes();
      check("E6b: the outcome is recorded as failed, not skipped",
            outcomes.size() == 1 && outcomes[0].failed() && !outcomes[0].skipped);
      check("E6b: and the reason says it was not started elevated instead",
            !outcomes.empty() && outcomes[0].detail.find("NOT started") != std::string::npos,
            outcomes.empty() ? "" : outcomes[0].detail);
      const std::string note = e.userNotice();
      check("E6b: the user is told, by name", note.find("DummyClient.cmd") != std::string::npos,
            note);
      check("E6b: and told the update itself worked",
            note.find("업데이트는 완료") != std::string::npos, note);
    }

    // -------------------------------------------------------- E6c: host failure != client failure
    {
      // The distinction that a single bool destroys. A client that did not come back is an
      // inconvenience. A host that did not come back locks a remote user out of the machine.
      DeleteFileW((root + L"\\DummyHost.cmd").c_str());  // make the host unstartable
      RelaunchEffects hostFailed = make_relaunch_effects(
          base(), {stopped_dummy(L"DummyHost.cmd", 100)}, dummies);
      hostFailed.relaunch();
      const auto hostOut = hostFailed.lastOutcomes();

      set_shell_launch_disabled_for_test(true);
      RelaunchEffects clientFailed = make_relaunch_effects(
          base(), {stopped_dummy(L"DummyClient.cmd", 100)}, dummies);
      clientFailed.relaunch();
      set_shell_launch_disabled_for_test(false);
      const auto clientOut = clientFailed.lastOutcomes();

      check("E6c: both are failures", hostOut.size() == 1 && hostOut[0].failed() &&
                                          clientOut.size() == 1 && clientOut[0].failed());
      check("E6c: but they name different images",
            hostOut[0].imageName != clientOut[0].imageName,
            narrow(hostOut[0].imageName) + " vs " + narrow(clientOut[0].imageName));
      check("E6c: and carry different kinds",
            hostOut[0].kind == RelaunchKind::ElevatedProcess &&
                clientOut[0].kind == RelaunchKind::UserProcess);
      check("E6c: the host failure names the host to the user",
            relaunch_user_notice(hostOut).find("DummyHost.cmd") != std::string::npos,
            relaunch_user_notice(hostOut));
      write_dummy(root + L"\\DummyHost.cmd", L"DummyHost.cmd", witness);  // put it back
    }

    // -------------------------------------------------------------- E7: the service failure path
    {
      RelaunchEffects e = make_relaunch_effects(base(), {stopped_dummy(L"DummySvc.cmd", 100)},
                                                dummies);
      const bool all = e.relaunch();
      // Starting a real service needs administrator, so only this half is covered here; the
      // success path is UPD-FIELD-05. What must hold is that a service that cannot be started is
      // a failure with a reason, not a silent pass.
      check("E7: a service that does not exist is a failure", !all);
      const auto outcomes = e.lastOutcomes();
      check("E7: recorded as failed with a reason",
            outcomes.size() == 1 && outcomes[0].failed() && !outcomes[0].detail.empty(),
            outcomes.empty() ? "" : outcomes[0].detail);
      check("E7: and the service is never started as a plain process",
            count_occurrences(read_witness(witness), "DummySvc.cmd") == 0, read_witness(witness));
    }

    // -------------------------------------------------------------- E8: health, end to end
    {
      RelaunchEffects e = make_relaunch_effects(base(), {stopped_dummy(L"DummyHost.cmd", 100)},
                                                dummies);
      // A previous run's report, which must not satisfy anything.
      append_line(log, "09-08 11:00:00 [host-app] health version=0.2.105 directory=ok");
      e.relaunch();  // takes the mark here
      // Called into a variable first: the detail belongs to THIS call, and C++ does not promise
      // that the arguments of check() are evaluated left to right.
      const bool staleAccepted = e.healthCheck();
      check("E8: the old report does not satisfy the check", !staleAccepted, e.lastHealthDetail());
      append_line(log, "09-08 12:00:00 [host-app] health version=0.2.105 directory=ok");
      const bool freshAccepted = e.healthCheck();
      check("E8: a report written after the mark does", freshAccepted, e.lastHealthDetail());
    }

    // The dummies exit on their own, but a .cmd still running holds its file open and the
    // directory will not go. Give them a moment rather than leaving litter in %TEMP%.
    Sleep(500);
    DeleteFileW(witness.c_str());
    remove_tree_flat(root);
  }

  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
