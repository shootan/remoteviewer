import io

p = 'apps/native_poc/src/update_state_machine_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- the case whose meaning changed
old = '''    FakeEffects f;
    f.registerOk = false;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("rolled back but not relaunched is its own outcome",
          o.result == UpdateResult::RolledBackNotRelaunched, result_name(o.result));
    check("...and it is not reported as an ordinary rollback",
          o.result != UpdateResult::RolledBack);'''
new = '''    // "Rolled back and the machine is unreachable" now means the REQUIRED images did not come
    // back after the restore. It used to mean any relaunch failure, which lumped a client window
    // that did not reopen in with a host nobody can reach -- and those are not the same event for
    // the person on the other end.
    FakeEffects f;
    f.registerOk = false;
    const RelaunchVerdict stillMissing = RelaunchVerdict::RequiredMissing;
    f.requiredAfterRollback = &stillMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("rolled back but not relaunched is its own outcome",
          o.result == UpdateResult::RolledBackNotRelaunched, result_name(o.result));
    check("...and it is not reported as an ordinary rollback",
          o.result != UpdateResult::RolledBack);'''
assert old in d, 'rolled back not relaunched'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the seven
old = '''  // ---------------------------------------------------------------- relaunch is not a rollback'''
new = '''  // ------------------------------------------- the order: required, health, commit, then optional
  //
  // The optional images start AFTER the commit, and the reason is not tidiness. The client is
  // started as the logged-on user, and when the route that hands back a handle is unavailable the
  // shell starts it and returns nothing -- no handle, no way to know which process is ours, no way
  // to stop it. A rollback has to move the files such a process holds. Starting it before the
  // commit therefore risks closing the way back at the exact moment the way back is needed.
  //
  // Reclassifying afterwards does not help: by the time the fall-through is discovered the process
  // exists. The order is the fix, so the order is what these assert.

  {
    // 1. The token launch fails and the shell would take over -- and the new host is unhealthy.
    // The rollback must succeed, and NOTHING optional may have been started before it.
    FakeEffects f;
    f.healthOk = false;
    f.healthAfterRollback = 1;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;  // the shell route, unownable
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("unhealthy host -> the rollback runs and succeeds", o.result == UpdateResult::RolledBack,
          result_name(o.result));
    // The assertion this whole reordering exists for.
    check("...and no optional image was started before the rollback",
          !ran_before(f, "RelaunchOptional", "Rollback"), joined(f.calls));
    check("...and none was committed either", f.commitCount == 0, std::to_string(f.commitCount));
  }
  {
    // 2. Required missing beats optional missing. Both fail; the severe one decides.
    FakeEffects f;
    f.requiredVerdict = RelaunchVerdict::RequiredMissing;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("required missing decides, whatever the optional one says", f.ran("Rollback"),
          joined(f.calls));
    check("...and it is never a partial success",
          o.result != UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    // And the optional phase never ran on the way there -- there was nothing to run it for.
    check("...and the optional phase was not reached before the rollback",
          !ran_before(f, "RelaunchOptional", "Rollback"), joined(f.calls));
  }
  {
    // 3. The ordinary success: zero optional launches before the commit, exactly one after.
    FakeEffects f;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("happy path -> Updated", o.result == UpdateResult::Updated, result_name(o.result));
    check("nothing optional starts before the commit", ran_before(f, "Commit", "RelaunchOptional"),
          joined(f.calls));
    check("...and exactly one optional phase runs after it",
          f.count("RelaunchOptional") == 1, joined(f.calls));
    check("...while the required one ran before health", ran_before(f, "RelaunchRequired", "HealthCheck"),
          joined(f.calls));
  }
  {
    // 4. After a rollback the OLD optional comes back too. Restoring the files and leaving the
    // user without their window is half a restore.
    FakeEffects f;
    f.registerOk = false;  // forces a rollback
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("a rollback brings the old optional images back as well",
          ran_before(f, "Rollback", "RelaunchOptional"), joined(f.calls));
    check("...after the required ones and their health check",
          ran_before(f, "RelaunchRequired", "RelaunchOptional") &&
              ran_before(f, "HealthCheck", "RelaunchOptional"),
          joined(f.calls));
    check("...and the result is a clean rollback", o.result == UpdateResult::RolledBack,
          result_name(o.result));
  }
  {
    // 5. Client-only: the host was never running, so there is no required plan. Nothing waits on
    // a health report that nobody was ever going to write.
    FakeEffects f;
    f.requiredVerdict = RelaunchVerdict::AllBack;  // an empty required plan reports AllBack
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("client-only -> the update completes", o.result == UpdateResult::Updated,
          result_name(o.result));
    check("client-only -> the client still comes back, after the commit",
          ran_before(f, "Commit", "RelaunchOptional"), joined(f.calls));
    check("client-only -> health is still asked once, not skipped and not repeated",
          f.count("HealthCheck") == 1, joined(f.calls));
  }
  {
    // 6. Optional failing after the commit: the backups are already gone and that is correct,
    // and the outcome is NOT reported as a plain success.
    FakeEffects f;
    f.optionalVerdict = RelaunchVerdict::OptionalMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("optional failing after the commit -> UpdatedButNotRelaunched",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...not hidden as Updated", o.result != UpdateResult::Updated, result_name(o.result));
    check("...the commit already happened and is not undone",
          f.commitCount == 1 && !f.ran("Rollback"), joined(f.calls));
    check("...and the detail says what happened",
          o.detail.find("optional") != std::string::npos, o.detail);
  }
  {
    // 7. A partial quiesce: something never left. It must not be started a second time, and the
    // abandon path brings back both kinds because nothing on disk was touched.
    FakeEffects f;
    f.quiesceOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("partial quiesce -> abandoned without touching the disk", disk_untouched(f),
          joined(f.calls));
    check("...and both phases are attempted exactly once",
          f.count("RelaunchRequired") == 1 && f.count("RelaunchOptional") == 1, joined(f.calls));
    check("...and it is an abandon, not an update", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
  }

  // ---------------------------------------------------------------- relaunch is not a rollback'''
assert old in d, 'seven anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the seven regressions')
