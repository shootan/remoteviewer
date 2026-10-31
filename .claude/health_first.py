import io

p = 'apps/native_poc/src/update_state_machine.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  if (relaunched == RelaunchVerdict::OptionalMissing) {
    // NOT a rollback. The files are the new version and they are consistent; undoing a good
    // install because it did not restart itself would be the worse outcome, and the user can
    // start it from the Start menu. Reported distinctly so it is visible rather than silent.
    // Committed too: this outcome does not roll back, so the way back is no longer needed and
    // leaving stale backups beside the install would be litter that a later rollback might trust.
    effects.Commit();
    effects.DiscardDownload();
    out.result = UpdateResult::UpdatedButNotRelaunched;
    out.detail = "installed, but the product did not come back up";
    return out;
  }

  enter(UpdateState::Health);
  if (!effects.HealthCheck()) return rollback("new build did not come up healthy");
'''
new = '''  // Health FIRST, whatever the relaunch reported.
  //
  // An optional image missing used to commit right here, before anything had asked whether the
  // parts the machine needs were actually working -- and "required started" only means
  // CreateProcess returned, not that the host came up. So a client that did not return, plus a
  // host that started and then died, dropped the backups and called it a partial success, with
  // nothing left to go back to.
  enter(UpdateState::Health);
  if (!effects.HealthCheck()) return rollback("new build did not come up healthy");

  if (relaunched == RelaunchVerdict::OptionalMissing) {
    // Healthy, and something optional did not come back. NOT a rollback: the files are the new
    // version, they are consistent, and what the machine needs is up -- undoing that because a
    // window did not reopen would be the worse outcome. Committing is safe HERE, which it was not
    // before, because the question the backups exist for has now been answered.
    effects.Commit();
    effects.DiscardDownload();
    out.result = UpdateResult::UpdatedButNotRelaunched;
    out.detail = "installed and healthy, but something optional did not come back up";
    return out;
  }
'''
assert old in d, 'optional block'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('health asked before any commit')
