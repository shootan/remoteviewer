import io

p = 'apps/native_poc/src/update_handoff_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  {
    std::string why;
    // The one combination that permits it. Everything before the signal is recoverable by doing
    // nothing, because nothing on disk has changed.
    check("signalled: the host may exit",'''
new = '''  // ---------------------------------------------------- the step, which the verdict could not say
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
    check("signalled: the host may exit",'''
assert old in d, 'verdict block'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the step and the acknowledgement have unit cover')
