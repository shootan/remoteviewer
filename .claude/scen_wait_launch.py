import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()

# per-scenario: wait for the launch this scenario fired, before the next seed() clears the witness
old='''    // Noted before anything is cleaned up: a launch that went through the shell has not
    // necessarily happened yet, and teardown has to know that one is outstanding.
    if (outcome.detail.find("shell") != std::string::npos) shellFired = true;
    for (const std::string& line : *logs) {
      if (line.find("started by the shell") != std::string::npos) shellFired = true;
    }
'''
new='''    // A launch this scenario routed through the shell is waited for HERE, while the fixture and
    // its witness still belong to this scenario. The next scenario's seed() clears the witness,
    // so asking later asks about the wrong run -- and the answer would be "it never happened",
    // which is the answer that makes teardown keep a directory it did not need to keep.
    bool firedHere = false;
    for (const std::string& line : *logs) {
      if (line.find("started by the shell") != std::string::npos) firedHere = true;
    }
    if (firedHere) {
      shellFired = true;
      // The request is with explorer now. Waiting for the fixture's own line is the only honest
      // way to know it arrived; there is nothing to poll on the request itself.
      if (!wait_for_witness(kClientName, 30000)) shellPending = true;
    }
'''
assert old in d,'shellFired block'
d=d.replace(old,new,1)

old='''  bool shellFired = false;'''
new='''  bool shellFired = false;
  /** True when a shell-routed launch was fired and never recorded itself. Teardown reads it. */
  bool shellPending = false;'''
assert old in d,'shellFired decl'
d=d.replace(old,new,1)

old='''  bool pending = false;
  if (shellFired) {
    for (int waited = 0; waited < 30000 && witness_count(kClientName) == 0; waited += 250) Sleep(250);
    pending = witness_count(kClientName) == 0;
    if (pending) {
      std::cout << "NOTE  a shell-routed launch was fired and never recorded itself; keeping "
                << narrow(root) << " so a late request finds a real file rather than a dialog\n";
    }
  }

  // Only this run's own directory, and only after checking that is what it is. A path that does
  // not sit under the configured root is left alone, however it came to be passed here.
  if (!pending && inside_run_root(root)) {
    remove_tree(root);
  } else {
    std::cout << "NOTE  refusing to remove " << narrow(root)
              << " -- it is not under this suite's root\n";
  }'''
new='''  // Two separate reasons not to remove this, and they are reported separately -- a message that
  // gives the wrong reason is worse than none, because it sends the reader somewhere else.
  if (shellPending) {
    std::cout << "NOTE  a shell-routed launch never recorded itself; keeping " << narrow(root)
              << " so a late request finds a real file rather than a dialog\n";
  } else if (!inside_run_root(root)) {
    std::cout << "NOTE  refusing to remove " << narrow(root)
              << " -- it is not under this suite's root\n";
  } else {
    remove_tree(root);
  }
  (void)shellFired;'''
assert old in d,'teardown block'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('the wait happens where the witness still means something')
