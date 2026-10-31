import io
p='apps/native_poc/src/update_state_machine_test.cpp'
d=io.open(p,encoding='utf-8').read()

# --- fake: health can answer differently after a rollback
old='''  bool healthOk = true;
  bool rollbackOk = true;'''
new='''  bool healthOk = true;
  /**
   * What health says about the RESTORED build, when that differs from what it said about the new
   * one. -1 means "the same answer", which is what every case wanted until a case needed the new
   * build to be unhealthy and the old one fine -- the two are different questions asked of
   * different installations, and one bool could not tell them apart.
   */
  int healthAfterRollback = -1;
  bool rollbackOk = true;'''
assert old in d,'health field'
d=d.replace(old,new,1)

old='''  bool HealthCheck() override { calls.push_back("HealthCheck"); return healthOk; }'''
new='''  bool HealthCheck() override {
    const bool restored = ran("Rollback");
    calls.push_back("HealthCheck");
    if (restored && healthAfterRollback >= 0) return healthAfterRollback != 0;
    return healthOk;
  }'''
assert old in d,'health impl'
d=d.replace(old,new,1)

# --- an ordering helper
old='''/** Shorthand for "the destructive steps never ran". */'''
new='''/** Whether `first` ran before `second`. Both must have run. */
bool ran_before(const FakeEffects& f, const std::string& first, const std::string& second) {
  int a = -1, b = -1;
  for (size_t i = 0; i < f.calls.size(); ++i) {
    if (a < 0 && f.calls[i] == first) a = static_cast<int>(i);
    if (b < 0 && f.calls[i] == second) b = static_cast<int>(i);
  }
  return a >= 0 && b >= 0 && a < b;
}

/** Shorthand for "the destructive steps never ran". */'''
assert old in d,'helper anchor'
d=d.replace(old,new,1)

# --- the regression Codex named, plus the ordering assertion on the case beside it
old='''    check("only an optional image missing -> the update stands",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...and it is committed, because it is not going back", f.commitCount == 1,
          std::to_string(f.commitCount));
    check("...and no rollback happened", !f.ran("Rollback"));
  }'''
new='''    check("only an optional image missing -> the update stands",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...and it is committed, because it is not going back", f.commitCount == 1,
          std::to_string(f.commitCount));
    check("...and no rollback happened", !f.ran("Rollback"));
    // The order, not just the outcome. This branch used to commit without health having been
    // asked at all, so "committed" was true here for the wrong reason and the case below could
    // not exist.
    check("...and health was asked BEFORE the backups were dropped",
          ran_before(f, "HealthCheck", "Commit"), joined(f.calls));
  }
  {
    // Host launched-but-unhealthy + client launch fails.
    //
    // The two halves are individually harmless and together were a trap. The relaunch reports
    // OptionalMissing because the client did not come back -- which on its own is not worth
    // undoing an install for. But the host DID launch, so the verdict says nothing is missing
    // that the machine needs, and the host then failed to come up. The old order committed on the
    // optional verdict alone, deleted the backups, and reported a partial success for a machine
    // that was not reachable and had nothing left to go back to.
    //
    // "Required started" is not "required healthy": CreateProcess returning tells you a process
    // exists, and health is the only thing that asks whether the product is answering.
    FakeEffects f;
    f.relaunchVerdict = RelaunchVerdict::OptionalMissing;
    f.healthOk = false;             // the host came up and is not answering
    f.healthAfterRollback = 1;      // the version it goes back to is fine
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("unhealthy host + missing client -> rolled back, not a partial success",
          o.result == UpdateResult::RolledBack, result_name(o.result));
    check("...and NOT reported as UpdatedButNotRelaunched",
          o.result != UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...and the backups were never dropped", f.commitCount == 0,
          std::to_string(f.commitCount));
    check("...and the rollback actually ran", f.ran("Rollback"), joined(f.calls));
    check("...and health was asked before anything was decided",
          ran_before(f, "HealthCheck", "Rollback"), joined(f.calls));
  }
  {
    // The counter-control for the case above: same missing client, host healthy. If the new order
    // simply rolled back whenever something was missing, this would fail -- so the two together
    // say health is what decides, not the verdict.
    FakeEffects f;
    f.relaunchVerdict = RelaunchVerdict::OptionalMissing;
    f.healthOk = true;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("healthy host + missing client -> the install still stands",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...and it did not roll back", !f.ran("Rollback"), joined(f.calls));
  }'''
assert old in d,'optional case'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('regression + counter-control')
