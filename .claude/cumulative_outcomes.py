import io
p='apps/native_poc/src/update_relaunch.cpp'
d=io.open(p,encoding='utf-8').read()
old='''    // Only this phase's outcomes. `lastOutcomes` reports the phase that just ran, which is what
    // the caller is logging about.
    shared->outcomes.clear();
'''
new='''    // Cleared at the START of an attempt, which is the required phase, and appended to by the
    // optional one. So `lastOutcomes` means "everything this attempt tried", not "whatever ran
    // last" -- and a caller that reads it after both phases still sees the host.
    //
    // Clearing per phase looked tidier and quietly dropped half the record: the required outcomes
    // vanished the moment the optional phase ran, so anything reading afterwards saw an empty
    // list and indexed into it. A rollback calls the required phase again, and that is a new
    // attempt against the restored build, so clearing there is right.
    if (wantRequired) shared->outcomes.clear();
'''
assert old in d,'clear'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)

p='apps/native_poc/src/updater_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  const auto report = [deps, relaunchEffects, notice](const char* phase) {
    if (relaunchEffects->lastOutcomes) {
      for (const RelaunchOutcome& outcome : relaunchEffects->lastOutcomes()) {
        deps.log(std::string("relaunch (") + phase + ") " + to_utf8(outcome.imageName) + ": " +
                 outcome.detail);
      }
    }
    if (relaunchEffects->userNotice) *notice = relaunchEffects->userNotice();
  };'''
new='''  // Outcomes accumulate across the two phases, so this logs only what the phase just added --
  // otherwise the optional phase would repeat every line the required one already wrote.
  auto reported = std::make_shared<size_t>(0);
  const auto report = [deps, relaunchEffects, notice, reported](const char* phase) {
    if (relaunchEffects->lastOutcomes) {
      const std::vector<RelaunchOutcome> all = relaunchEffects->lastOutcomes();
      if (*reported > all.size()) *reported = 0;  // the list was reset for a new attempt
      for (size_t i = *reported; i < all.size(); ++i) {
        deps.log(std::string("relaunch (") + phase + ") " + to_utf8(all[i].imageName) + ": " +
                 all[i].detail);
      }
      *reported = all.size();
    }
    // Read after both phases have contributed, so a client that did not come back is still named
    // even though the phase that failed was not the last one to run.
    if (relaunchEffects->userNotice) {
      const std::string current = relaunchEffects->userNotice();
      if (!current.empty()) *notice = current;
    }
  };'''
assert old in d,'report'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('outcomes accumulate; the log reports only what is new')
