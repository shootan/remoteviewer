import io

# ================================================================== header
p = 'apps/native_poc/src/update_relaunch.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  std::function<RelaunchVerdict()> relaunch;
  std::function<bool()> healthCheck;'''
new = '''  /**
   * Starts the images the machine needs -- the host and the service -- and nothing else.
   *
   * Split from the optional ones because of WHEN each may run, not because they are started
   * differently. These run while a rollback is still possible, so every one of them is launched
   * by a route that hands back a handle and can therefore be stopped again.
   */
  std::function<RelaunchVerdict()> relaunchRequired;

  /**
   * Starts the images a person wants back -- the client -- and nothing else.
   *
   * Runs after the update is committed. The client is started as the logged-on user, and when the
   * route that returns a handle is unavailable the shell starts it and returns nothing; such a
   * process cannot be stopped, and a rollback would have to move the files it holds. After the
   * commit there is no rollback left to block, which is what makes that acceptable here and not
   * one step earlier.
   */
  std::function<RelaunchVerdict()> relaunchOptional;
  std::function<bool()> healthCheck;'''
assert old in d, 'effects decl'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== implementation
p = 'apps/native_poc/src/update_relaunch.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  effects.relaunch = [shared]() {
    shared->outcomes.clear();
    // Before anything is started, so nothing written by a previous run can be read as evidence.
    shared->logMark = file_size_or_zero(shared->config.healthLogPath);
    shared->marked = true;

    for (const RelaunchEntry& entry : shared->plan) {'''
new = '''  // One body, two entry points. `wantRequired` selects which half of the plan runs, and the two
  // halves never overlap: an entry is required or it is not.
  //
  // Written once rather than twice because everything except the filter -- the liveness check, the
  // ownership rules, the log mark -- must be identical for both. Two copies of this would drift,
  // and the half that drifted would be the one that runs after the commit, where nothing is
  // watching any more.
  const auto run_phase = [shared](bool wantRequired) {
    if (!shared->marked) {
      // Taken once per attempt, before anything is started, so nothing written by a previous run
      // can be read as evidence. The optional phase runs later and must NOT move the mark -- the
      // health check has already been made against this one.
      shared->logMark = file_size_or_zero(shared->config.healthLogPath);
      shared->marked = true;
    }
    // Only this phase's outcomes. `lastOutcomes` reports the phase that just ran, which is what
    // the caller is logging about.
    shared->outcomes.clear();

    for (const RelaunchEntry& entry : shared->plan) {
      if (required_kind(entry.kind) != wantRequired) continue;'''
assert old in d, 'relaunch head'
d = d.replace(old, new, 1)

old = '''    RelaunchVerdict verdict = RelaunchVerdict::AllBack;
    for (const RelaunchOutcome& outcome : shared->outcomes) {
      if (!outcome.failed()) continue;
      if (outcome.required()) return RelaunchVerdict::RequiredMissing;  // decides on its own
      verdict = RelaunchVerdict::OptionalMissing;
    }
    return verdict;
  };'''
new = '''    RelaunchVerdict verdict = RelaunchVerdict::AllBack;
    for (const RelaunchOutcome& outcome : shared->outcomes) {
      if (!outcome.failed()) continue;
      if (outcome.required()) return RelaunchVerdict::RequiredMissing;  // decides on its own
      verdict = RelaunchVerdict::OptionalMissing;
    }
    return verdict;
  };

  effects.relaunchRequired = [run_phase]() { return run_phase(true); };
  effects.relaunchOptional = [run_phase]() { return run_phase(false); };'''
assert old in d, 'verdict tail'
d = d.replace(old, new, 1)

# the kind predicate, next to the one on the outcome so the two cannot disagree
old = '''RelaunchConfig::Liveness real_liveness(const ProcessTarget& target) {'''
new = '''/**
 * Whether the machine needs this image, as opposed to a person wanting it.
 *
 * The same rule RelaunchOutcome::required() applies, stated once for the plan so the phase filter
 * and the verdict cannot disagree about what "required" means. If they ever did, an image could
 * be started in the optional phase and then judged by the required rule, or the reverse.
 */
bool required_kind(RelaunchKind kind) {
  return kind == RelaunchKind::ElevatedProcess || kind == RelaunchKind::Service;
}

RelaunchConfig::Liveness real_liveness(const ProcessTarget& target) {'''
assert old in d, 'real_liveness'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the plan runs in two phases from one body')
