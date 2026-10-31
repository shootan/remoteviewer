import io

BS = chr(92)
p = 'apps/native_poc/src/update_relaunch_test.cpp'
d = io.open(p, encoding='utf-8').read()

anchor = '    // -------------------------------------------------------------- E7: the service failure path'
assert anchor in d, 'anchor'

block = '''    // -------------------------------------- E10: what never left is not started again
    {
      // Abandoning after a waiting caller was released is the case where some of the product has
      // gone and some has not. Capturing an identity says nothing about whether that process then
      // exited -- and relaunching one that never went produces a second instance.
      DeleteFileW(witness.c_str());
      RelaunchConfig c = base();
      // As if the host were still up: it was captured, but it is there.
      c.isStillRunning = [](const ProcessTarget&) { return true; };
      RelaunchEffects e = make_relaunch_effects(c, {stopped_dummy(L"DummyHost.cmd", 100)},
                                                dummies);
      const RelaunchVerdict verdict = e.relaunch();
      Sleep(300);
      check("E10: something still running is not started again",
            count_occurrences(read_witness(witness), "DummyHost.cmd") == 0, read_witness(witness));
      check("E10: and that is not a failure -- there was nothing to bring back",
            verdict == RelaunchVerdict::AllBack, relaunch_verdict_name(verdict));
      const auto outcomes = e.lastOutcomes();
      check("E10: recorded as already running, not as started",
            outcomes.size() == 1 && outcomes[0].alreadyRunning && !outcomes[0].started &&
                !outcomes[0].failed());
      check("E10: and the user is told nothing", e.userNotice().empty(), e.userNotice());
    }
    {
      // The control. Without it, "not started" above could be what happens to everything.
      DeleteFileW(witness.c_str());
      RelaunchConfig c = base();
      c.isStillRunning = [](const ProcessTarget&) { return false; };
      RelaunchEffects e = make_relaunch_effects(c, {stopped_dummy(L"DummyHost.cmd", 100)},
                                                dummies);
      e.relaunch();
      check("E10: something that did leave IS started again",
            wait_for_witness(witness, "DummyHost.cmd", 5000), read_witness(witness));
    }

    // -------------------------------------- E11: required and optional are not the same failure
    {
      // A client that did not come back is an inconvenience. A host that did not leaves a remote
      // user with no way into the machine. Folding both into one bool committed the update in
      // the case where the backups were the only way back.
      set_shell_launch_disabled_for_test(true);
      RelaunchEffects clientOnly = make_relaunch_effects(
          base(), {stopped_dummy(L"DummyClient.cmd", 100)}, dummies);
      const RelaunchVerdict clientVerdict = clientOnly.relaunch();
      set_shell_launch_disabled_for_test(false);
      check("E11: only the client missing is the mild verdict",
            clientVerdict == RelaunchVerdict::OptionalMissing,
            relaunch_verdict_name(clientVerdict));

      DeleteFileW((root + L"@@DummyHost.cmd").c_str());  // make the host unstartable
      RelaunchEffects hostGone = make_relaunch_effects(
          base(), {stopped_dummy(L"DummyHost.cmd", 100)}, dummies);
      const RelaunchVerdict hostVerdict = hostGone.relaunch();
      check("E11: the host missing is the severe verdict",
            hostVerdict == RelaunchVerdict::RequiredMissing, relaunch_verdict_name(hostVerdict));
      check("E11: and the two are different", clientVerdict != hostVerdict);

      // Both gone: the severe one wins, because that is the one that decides what happens next.
      set_shell_launch_disabled_for_test(true);
      RelaunchEffects both = make_relaunch_effects(
          base(), {stopped_dummy(L"DummyHost.cmd", 100), stopped_dummy(L"DummyClient.cmd", 101)},
          dummies);
      const RelaunchVerdict bothVerdict = both.relaunch();
      set_shell_launch_disabled_for_test(false);
      check("E11: with both missing, the severe verdict wins",
            bothVerdict == RelaunchVerdict::RequiredMissing, relaunch_verdict_name(bothVerdict));
      write_dummy(root + L"@@DummyHost.cmd", L"DummyHost.cmd", witness);  // put it back
    }

    // -------------------------------------- E12: stopping only what this attempt started
    {
      // A rollback has to move files the newly started processes are holding open, so they go
      // first -- and only they. Anything else on the machine is not this function's business.
      DeleteFileW(witness.c_str());
      RelaunchConfig c = base();
      // A dummy that stays up, so there is something real to stop.
      write_dummy_waiting(root + L"@@DummyHost.cmd", witness);
      RelaunchEffects e = make_relaunch_effects(c, {stopped_dummy(L"DummyHost.cmd", 100)},
                                                dummies);
      e.relaunch();
      check("E12: something was started", wait_for_witness(witness, "DummyHost.cmd", 5000),
            read_witness(witness));
      const int stopped = e.stopStarted();
      check("E12: and stopping what this attempt started stops it", stopped >= 0,
            std::to_string(stopped));
      // Called twice: the second time there is nothing left, which is the honest answer rather
      // than an error.
      check("E12: a second call stops nothing", e.stopStarted() == 0);
      write_dummy(root + L"@@DummyHost.cmd", L"DummyHost.cmd", witness);
    }

''' + anchor

block = block.replace('@@', BS * 2)
d = d.replace(anchor, block, 1)

# a dummy that stays alive long enough to be stopped
old = '''std::string read_witness(const std::wstring& path) {'''
new = '''/** Like write_dummy, but it stays up so there is something to stop. */
void write_dummy_waiting(const std::wstring& path, const std::wstring& witness) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << "@echo off@r@n";
  out << ">>@"" << std::string(witness.begin(), witness.end()) << "@" echo DummyHost.cmd@r@n";
  out << "ping -n 30 127.0.0.1 >nul@r@n";
}

std::string read_witness(const std::wstring& path) {'''
new = new.replace('@r', BS + 'r').replace('@n', BS + 'n').replace('@"', BS + '"')
assert old in d, 'helper anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('E10-E12 added')
