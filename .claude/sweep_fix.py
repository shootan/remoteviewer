import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  for (int pass = 0; pass < 40; ++pass) {
    const auto found = running_under(dir);
    // Two consecutive empty passes, not one: a shell-routed launch hands off asynchronously, so
    // a single empty look is not evidence that nothing is coming.
    if (found.empty() && pass > 1) return;
    for (const auto& pair : found) {'''
new='''  // EIGHT consecutive empty passes -- 1.6 seconds of seeing nothing -- not two.
  //
  // Two was not enough and the shortfall was cumulative. A shell-routed launch hands off through
  // explorer, and the process can appear more than half a second after the launch returned; the
  // sweep saw two empty passes, stopped looking, and the client turned up immediately after. It
  // then held its own image for the rest of the test process, so the next scenario could not
  // write its starting bytes and failed an assertion about bytes -- and the one after that
  // inherited another one. Four of them were still running when the suite finished.
  int emptyPasses = 0;
  for (int pass = 0; pass < 60; ++pass) {
    const auto found = running_under(dir);
    if (found.empty()) {
      if (++emptyPasses >= 8) return;
      Sleep(200);
      continue;
    }
    emptyPasses = 0;
    for (const auto& pair : found) {'''
assert old in d,'sweep'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('sweep waits long enough for an async handoff')
