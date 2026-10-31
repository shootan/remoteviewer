import io

p = 'apps/native_poc/src/update_state_machine_test.cpp'
d = io.open(p, encoding='utf-8').read()

anchor = '''  {
    // And the outcome when the restore works but nothing comes back up.'''
assert anchor in d, 'anchor'

block = '''  {
    // Giving up after a caller has already been released on this update's behalf.
    //
    // Nothing on disk was touched -- no file replaced, no registration changed -- and yet the
    // product may be gone, because it was asked to make way and then nobody asked it back. For a
    // remote user that is an unreachable machine reached without a single thing going wrong,
    // which is harder to diagnose than a failed update precisely because there is no damage.
    FakeEffects f;
    f.prepareOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("prepare fails -> what left is started again", f.ran("Relaunch"));
    check("prepare fails -> AbandonedBeforeSwap", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
    check("prepare fails -> nothing was swapped or registered",
          !f.ran("Swap") && !f.ran("RegisterInstall"));
  }
  {
    FakeEffects f;
    f.prepareOk = false;
    f.relaunchOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    // Distinguished, because "we changed nothing" and "the machine is reachable" are separate
    // claims and only one of them is true here.
    check("abandoned and not relaunched is its own outcome",
          o.result == UpdateResult::AbandonedNotRelaunched, result_name(o.result));
    check("...and not reported as an ordinary abandon",
          o.result != UpdateResult::AbandonedBeforeSwap);
    check("relaunch is attempted once", f.count("Relaunch") == 1,
          std::to_string(f.count("Relaunch")));
  }
  {
    FakeEffects f;
    f.quiesceOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("quiesce fails -> what stopped is started again", f.ran("Relaunch"));
    check("quiesce fails -> still an abandon", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
  }
  {
    // Before anyone could have left: the download never completed, so no caller was released and
    // no targets were captured. Relaunch is still called and has nothing to do -- which is why it
    // is safe to call it on every abandon path rather than guessing which ones need it.
    FakeEffects f;
    f.downloadOk = false;
    const UpdateOutcome o = run_update(f, accepting(), "windows");
    check("download fails -> abandoned", o.result == UpdateResult::AbandonedBeforeSwap,
          result_name(o.result));
    check("download fails -> nothing was swapped", !f.ran("Swap"));
  }
''' + anchor

d = d.replace(anchor, block, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('abandon regressions added')
