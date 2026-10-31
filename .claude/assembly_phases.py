import io
p='apps/native_poc/src/updater_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  config.relaunch = [deps, relaunchConfig, stopped, relaunchEffects, expectedVersion, notice]() {
    RelaunchConfig live = relaunchConfig;
    live.expectedVersion = *expectedVersion;
    *relaunchEffects = deps.makeRelaunch(live, *stopped);
    // No relaunch to run means nothing came back. Reported as the severe verdict rather than the
    // mild one: an assembly that produced no relaunch has not established that the machine is
    // reachable, and guessing in the reassuring direction is how this class of defect starts.
    if (!relaunchEffects->relaunch) return RelaunchVerdict::RequiredMissing;
    const RelaunchVerdict verdict = relaunchEffects->relaunch();
    if (relaunchEffects->lastOutcomes) {
      for (const RelaunchOutcome& outcome : relaunchEffects->lastOutcomes()) {
        deps.log("relaunch " + to_utf8(outcome.imageName) + ": " + outcome.detail);
      }
    }
    if (relaunchEffects->userNotice) *notice = relaunchEffects->userNotice();
    return verdict;
  };'''
new='''  // Built ONCE, on whichever phase runs first, and reused by the other.
  //
  // The two phases share one set of relaunch effects on purpose: the handles the required phase
  // takes are the ones a rollback stops, and the log mark the health check reads is taken before
  // anything starts. Building a second set for the optional phase would take a second mark, after
  // the host had already written its banner, and the health check would then be reading against a
  // mark that came after the evidence it is looking for.
  auto ensure = [deps, relaunchConfig, stopped, relaunchEffects, expectedVersion]() {
    if (relaunchEffects->relaunchRequired || relaunchEffects->relaunchOptional) return;
    RelaunchConfig live = relaunchConfig;
    live.expectedVersion = *expectedVersion;
    *relaunchEffects = deps.makeRelaunch(live, *stopped);
  };
  const auto report = [deps, relaunchEffects, notice](const char* phase) {
    if (relaunchEffects->lastOutcomes) {
      for (const RelaunchOutcome& outcome : relaunchEffects->lastOutcomes()) {
        deps.log(std::string("relaunch (") + phase + ") " + to_utf8(outcome.imageName) + ": " +
                 outcome.detail);
      }
    }
    if (relaunchEffects->userNotice) *notice = relaunchEffects->userNotice();
  };
  config.relaunchRequired = [ensure, report, relaunchEffects]() {
    ensure();
    // Nothing to run means nothing came back. Reported as the severe verdict rather than the mild
    // one: an assembly that produced no relaunch has not established that the machine is
    // reachable, and guessing in the reassuring direction is how this class of defect starts.
    if (!relaunchEffects->relaunchRequired) return RelaunchVerdict::RequiredMissing;
    const RelaunchVerdict verdict = relaunchEffects->relaunchRequired();
    report("required");
    return verdict;
  };
  config.relaunchOptional = [ensure, report, relaunchEffects]() {
    ensure();
    // The mild verdict when there is nothing to run. This phase happens after the commit, so
    // there is no longer a decision it could change -- reporting it as severe would turn a
    // finished, healthy update into an alarm.
    if (!relaunchEffects->relaunchOptional) return RelaunchVerdict::OptionalMissing;
    const RelaunchVerdict verdict = relaunchEffects->relaunchOptional();
    report("optional");
    return verdict;
  };'''
assert old in d,'assembly relaunch'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('assembly wires both phases to one set of effects')
