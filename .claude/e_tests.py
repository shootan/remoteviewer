import io

BS = chr(92)
p = 'apps/native_poc/src/update_relaunch_test.cpp'
d = io.open(p, encoding='utf-8').read()

d = d.replace('''// The two decisions behind Relaunch and HealthCheck, tested without making either happen.
//
// Nothing here starts a process, touches the SCM, or reads a real log. The production code that
// does those things lives in update_relaunch.cpp, which this binary does NOT link -- the same
// arrangement as update_process_targets.cpp, and for the same reason: on the machine this was
// written on, GNLinkHost, GNLinkStream and GNLinkInputService were running, and a test that could
// call StartServiceW or CreateProcessW on the real install directory is a test that can disturb
// someone's session.
//
// Design: docs/업데이트_기능_설계.md 3.5-3.6.''',
'''// Relaunch and HealthCheck: the decisions, and the execution.
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
// Design: docs/업데이트_기능_설계.md 3.5-3.6, docs/업데이트_배선_계획.md §2.''', 1)

d = d.replace('''#include "update_health.hpp"
#include "update_health_log.hpp"
#include "update_relaunch_plan.hpp"''', '''#include "update_health.hpp"
#include "update_health_log.hpp"
#include "update_relaunch.hpp"
#include "update_relaunch_plan.hpp"''', 1)

# helpers for the execution cases
d = d.replace('''}  // namespace

int main() {''', '''/**
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
  out << "@echo off\\r\\n";
  out << ">>\\"" << std::string(witness.begin(), witness.end()) << "\\" echo "
      << std::string(marker.begin(), marker.end()) << "\\r\\n";
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
  HANDLE h = FindFirstFileW((dir + L"@@*").c_str(), &find);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = find.cFileName;
      if (name == L"." || name == L"..") continue;
      DeleteFileW((dir + L"@@" + name).c_str());
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

int main() {''', 1)

anchor = '''  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks'''
block = '''  // ================================================================ the execution layer, for real
  //
  // Same code the product will run. Different table, different directory, different names.

  {
    const std::wstring root = make_temp_dir_named(L"root");
    const std::wstring witness = root + L"@@witness.txt";
    const std::wstring log = root + L"@@dummy.log";

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
      write_dummy(root + L"@@" + image.name, image.name, witness);
    }

    const auto stopped_dummy = [&](const std::wstring& name, uint32_t pid) {
      return running(root + L"@@" + name, pid);
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
      check("E1: the elevated-process entry reports started", all, "relaunch() said false");
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
      write_dummy(root + L"@@Stranger.cmd", L"Stranger.cmd", witness);
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
      DeleteFileW((root + L"@@DummyHost.cmd").c_str());  // make the host unstartable
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
      write_dummy(root + L"@@DummyHost.cmd", L"DummyHost.cmd", witness);  // put it back
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
      check("E8: the old report does not satisfy the check", !e.healthCheck(),
            e.lastHealthDetail());
      append_line(log, "09-08 12:00:00 [host-app] health version=0.2.105 directory=ok");
      check("E8: a report written after the mark does", e.healthCheck(), e.lastHealthDetail());
    }

    DeleteFileW(witness.c_str());
    remove_tree_flat(root);
  }

''' + anchor
block = block.replace('@@', BS * 2)
assert anchor in d, 'tail anchor'
d = d.replace(anchor, block, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('E1-E8 written')
