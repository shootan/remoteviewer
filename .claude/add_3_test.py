# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/update_relaunch_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''      check("E10: and the user is told nothing", e.userNotice().empty(), e.userNotice());
    }'''
new = '''      check("E10: and the user is told nothing", e.userNotice().empty(), e.userNotice());
    }

    // -------------------------------------- E10b: closing is not the same as refusing
    {
      // The field failure, as a unit. At 18:56:20.266 the host acknowledged the update and was
      // standing down; nine milliseconds later this check read "still running", concluded there
      // was nothing to bring back, and skipped the relaunch. The host then finished closing on
      // its own and the machine was left with nothing running.
      //
      // A process that was ASKED to stop and is still there may simply be closing. Asked once,
      // immediately, "closing" and "refused" are indistinguishable -- so the answer is re-asked
      // until it settles.
      DeleteFileW(witness.c_str());
      RelaunchConfig c = base();
      c.closingGraceMs = 5000;
      auto calls = std::make_shared<int>(0);
      c.isStillRunning = [calls](const ProcessTarget&) {
        // Running for the first few reads, then gone: a process on its way out.
        return (++*calls <= 3) ? RelaunchConfig::Liveness::Running
                               : RelaunchConfig::Liveness::Exited;
      };
      RelaunchEffects e = make_relaunch_effects(c, {stopped_dummy(L"DummyHost.cmd", 100)},
                                                dummies);
      const RelaunchVerdict verdict = run_all(e);
      Sleep(300);
      check("E10b: a process that was closing IS brought back",
            count_occurrences(read_witness(witness), "DummyHost.cmd") == 1, read_witness(witness));
      check("E10b: and the attempt reports it started", verdict == RelaunchVerdict::AllBack,
            relaunch_verdict_name(verdict));
      const auto outcomes = e.lastOutcomes();
      check("E10b: recorded as started, not as already running",
            outcomes.size() == 1 && outcomes[0].started && !outcomes[0].alreadyRunning);
      check("E10b: ...and it took more than one look to find out", *calls > 1,
            std::to_string(*calls) + " liveness reads");
    }
    {
      // The other side of the grace: running it out is NOT an exit. A process that is still there
      // when the time is up is still there, and starting a second copy of it would be worse than
      // leaving it alone. Without this, the wait above could be read as "wait and then assume".
      DeleteFileW(witness.c_str());
      RelaunchConfig c = base();
      c.closingGraceMs = 300;  // short: the point is what happens when it expires
      c.isStillRunning = [](const ProcessTarget&) { return RelaunchConfig::Liveness::Running; };
      RelaunchEffects e = make_relaunch_effects(c, {stopped_dummy(L"DummyHost.cmd", 100)},
                                                dummies);
      const RelaunchVerdict verdict = run_all(e);
      Sleep(300);
      check("E10c: a grace that runs out does not mean it exited",
            count_occurrences(read_witness(witness), "DummyHost.cmd") == 0, read_witness(witness));
      check("E10c: and it is still reported as already running",
            e.lastOutcomes().size() == 1 && e.lastOutcomes()[0].alreadyRunning &&
                !e.lastOutcomes()[0].started);
      check("E10c: which is not a failure", verdict == RelaunchVerdict::AllBack,
            relaunch_verdict_name(verdict));
    }'''

assert d.count(old) == 1, 'anchor'
d = d.replace(old, new, 1)
if '#include <memory>' not in d:
    d = d.replace('#include <string>', '#include <memory>\n#include <string>', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
