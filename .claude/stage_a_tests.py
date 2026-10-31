import io

# ---------------------------------------------------------------- state machine
p = 'apps/native_poc/src/update_state_machine_test.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''    check("health fails -> RolledBack", o.result == UpdateResult::RolledBack, result_name(o.result));'''
new = '''    // The fake's health check answers the same way after the rollback as before it, so the
    // restored build is unhealthy too -- and that is now reported instead of discarded. It used
    // to say RolledBack regardless, which claimed the machine was fine on the strength of a
    // result nobody had looked at.
    check("health fails, and the restore is unhealthy too -> RestoredButUnhealthy",
          o.result == UpdateResult::RestoredButUnhealthy, result_name(o.result));'''
assert old in d, 'health fails'
d = d.replace(old, new, 1)

anchor = '''  {
    // Giving up after a caller has already been released on this update's behalf.'''
block = '''  {
    // A rollback whose restored build DOES come up healthy. The distinction the case above cannot
    // make on its own: without this, "RestoredButUnhealthy" could be what every rollback reports.
    FakeEffects f;
    f.healthOk = false;
    const UpdateOutcome first = run_update(f, accepting(), "windows");
    check("an unhealthy restore is not called a clean rollback",
          first.result == UpdateResult::RestoredButUnhealthy, result_name(first.result));

    FakeEffects g;
    g.registerOk = false;  // forces a rollback with health still answering yes
    const UpdateOutcome second = run_update(g, accepting(), "windows");
    check("a rollback whose restore is healthy is a clean rollback",
          second.result == UpdateResult::RolledBack, result_name(second.result));
  }
  {
    // The contract that a single bool could not express. A client that did not come back is not
    // a reason to undo a good install; a host that did not is, because a machine nobody can reach
    // is worth less than an older one somebody can.
    FakeEffects f;
    f.relaunchVerdict = RelaunchVerdict::OptionalMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("only an optional image missing -> the update stands",
          o.result == UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
    check("...and it is committed, because it is not going back", f.commitCount == 1,
          std::to_string(f.commitCount));
    check("...and no rollback happened", !f.ran("Rollback"));
  }
  {
    FakeEffects f;
    f.relaunchVerdict = RelaunchVerdict::RequiredMissing;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("something the machine needs missing -> rollback", f.ran("Rollback"));
    check("...and NOT committed, because the backups are the way back", f.commitCount == 0,
          std::to_string(f.commitCount));
    check("...and the result is a rollback, not a partial success",
          o.result == UpdateResult::RolledBack ||
              o.result == UpdateResult::RolledBackNotRelaunched ||
              o.result == UpdateResult::RestoredButUnhealthy,
          result_name(o.result));
    check("...and it is never reported as UpdatedButNotRelaunched",
          o.result != UpdateResult::UpdatedButNotRelaunched, result_name(o.result));
  }
''' + anchor
assert anchor in d, 'anchor'
d = d.replace(anchor, block, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- effects test
p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()
d = d.replace('''      // A real launch that really failed. Reported as the severe verdict, which is what a host
      // that will not start is.
      return ok ? RelaunchVerdict::AllBack : RelaunchVerdict::RequiredMissing;''',
'''      // A real launch that really failed, reported as an OPTIONAL image failing -- which is the
      // case this block is about: the install is good and stays. The severe verdict has its own
      // case in the state machine suite, where it rolls back.
      return ok ? RelaunchVerdict::AllBack : RelaunchVerdict::OptionalMissing;''', 1)
d = d.replace('''    check("wiring: a health failure rolls back",''',
              '''    // The restored build is unhealthy too here -- the injected health check answers the same way
    // either side of the rollback -- so this is the honest outcome rather than a clean rollback.
    check("wiring: a health failure rolls back and the restore is not healthy either",''', 1)
d = d.replace('''          out.result == UpdateResult::RolledBack,
          std::string(result_name(out.result)) + " " + e.last_error());''',
              '''          out.result == UpdateResult::RestoredButUnhealthy,
          std::string(result_name(out.result)) + " " + e.last_error());''', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- release test
p = 'apps/native_poc/src/update_release_test.cpp'
d = io.open(p, encoding='utf-8').read()
d = d.replace('''    check("R4b: the attempt rolls back", out.result == UpdateResult::RolledBack,''',
'''    // The same injected health check answers after the rollback as before it, so the restored
    // build is unhealthy too and that is reported. The files are what this case is about, and
    // they are checked below.
    check("R4b: the attempt rolls back", out.result == UpdateResult::RestoredButUnhealthy,''', 1)
d = d.replace('''      check("R5: the abandoned attempt rolls back", out.result == UpdateResult::RolledBack,''',
'''      check("R5: the abandoned attempt rolls back",
            out.result == UpdateResult::RestoredButUnhealthy,''', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('tests updated for the new contract')
