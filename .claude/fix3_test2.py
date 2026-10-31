import io
p='apps/native_poc/src/update_state_machine_test.cpp'
d=io.open(p,encoding='utf-8').read()

# the restored build can come back fully even though the new one did not
old='''  RelaunchVerdict relaunchVerdict = RelaunchVerdict::AllBack;'''
new='''  RelaunchVerdict relaunchVerdict = RelaunchVerdict::AllBack;
  /**
   * What the relaunch of the RESTORED build reports, when it differs. Absent means the same
   * verdict -- fine for most cases, but a test that wants to see a clean RolledBack after a new
   * build failed needs the second attempt to succeed where the first did not.
   */
  const RelaunchVerdict* relaunchAfterRollback = nullptr;'''
assert old in d,'verdict field'
d=d.replace(old,new,1)

old='''  RelaunchVerdict Relaunch() override {
    calls.push_back("Relaunch");
    return relaunchVerdict;
  }'''
new='''  RelaunchVerdict Relaunch() override {
    const bool restored = ran("Rollback");
    calls.push_back("Relaunch");
    if (restored && relaunchAfterRollback) return *relaunchAfterRollback;
    return relaunchVerdict;
  }'''
assert old in d,'relaunch impl'
d=d.replace(old,new,1)

old='''    f.healthOk = false;             // the host came up and is not answering
    f.healthAfterRollback = 1;      // the version it goes back to is fine
    const UpdateOutcome o = run_update(f, accepting(), "windows");'''
new='''    f.healthOk = false;             // the host came up and is not answering
    f.healthAfterRollback = 1;      // the version it goes back to is fine
    const RelaunchVerdict backUp = RelaunchVerdict::AllBack;
    f.relaunchAfterRollback = &backUp;  // and it comes back complete
    const UpdateOutcome o = run_update(f, accepting(), "windows");'''
assert old in d,'regression setup'
d=d.replace(old,new,1)

# the check that encoded the old order
old='''    check("relaunch fails -> health is not claimed", !o.entered(UpdateState::Health));'''
new='''    // This required the OPPOSITE, and it was protecting the defect above: it said that when
    // something optional did not come back, health was never asked. That was true, and it was the
    // bug -- the machine's own health went unexamined and the backups were dropped anyway.
    check("relaunch fails -> health IS asked, because the install is being kept",
          o.entered(UpdateState::Health), joined(f.calls));'''
assert old in d,'old order check'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('restored-build verdict + corrected order check')
