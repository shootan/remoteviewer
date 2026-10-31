import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()

old='''  stop_everything_under(root);
  Sleep(2000);
  stop_everything_under(root);
  // Only this run's own directory, and only after checking that is what it is. A path that does
  // not sit under the configured root is left alone, however it came to be passed here.
  if (inside_run_root(root)) {'''
new='''  stop_everything_under(root);
  Sleep(2000);
  stop_everything_under(root);

  // A shell-routed launch is performed by explorer, later, and there is no way to ask whether it
  // has happened yet. If one was fired and the thing it starts has not recorded itself, the
  // request may still be in flight -- and removing the directory now is what puts "Windows cannot
  // find ...\ScnClient.exe" on the user's desktop. That happened.
  //
  // So the directory is KEPT when that is in doubt. Litter in the repository is a small price; a
  // modal dialog on somebody's screen, from a test, is not something to trade it against. The
  // file staying where it is means a late request finds a real executable, which records itself
  // and exits.
  bool pending = false;
  if (shellFired) {
    for (int waited = 0; waited < 30000 && !witness_has(kClientName); waited += 250) Sleep(250);
    pending = !witness_has(kClientName);
    if (pending) {
      std::cout << "NOTE  a shell-routed launch was fired and never recorded itself; keeping "
                << narrow(root) << " so a late request finds a real file rather than a dialog\n";
    }
  }

  // Only this run's own directory, and only after checking that is what it is. A path that does
  // not sit under the configured root is left alone, however it came to be passed here.
  if (!pending && inside_run_root(root)) {'''
assert old in d,'teardown'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('teardown waits for the launch to land')
