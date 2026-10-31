import io

# ================================================================== the interface
p = 'apps/native_poc/src/update_state_machine.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  virtual RelaunchVerdict Relaunch() = 0;'''
new = '''  /**
   * Brings back the images the machine NEEDS: the host and the service.
   *
   * Separate from the optional ones because of when each may run. This one happens while a
   * rollback is still possible, so anything it starts can still be stopped and undone.
   */
  virtual RelaunchVerdict RelaunchRequired() = 0;

  /**
   * Brings back the images a person wants but the machine does not need -- the client.
   *
   * Runs only AFTER the update is committed, and that is the whole point of it being its own
   * step. The client is started in the user's context, and when the route that hands back a
   * handle is unavailable the shell starts it instead and nothing comes back: no handle, no way
   * to say which process is ours, and therefore no way to stop it. A rollback has to move the
   * files such a process is holding, so starting one before the commit means discovering, too
   * late, that the way back has been closed by the act of restoring the product.
   *
   * After the commit there is nothing left to undo, so an unownable process costs nothing.
   */
  virtual RelaunchVerdict RelaunchOptional() = 0;'''
assert old in d, 'interface'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== the ordering
p = 'apps/native_poc/src/update_state_machine.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''    out.result = (effects.Relaunch() == RelaunchVerdict::AllBack)
                     ? UpdateResult::AbandonedBeforeSwap
                     : UpdateResult::AbandonedNotRelaunched;
    return out;'''
new = '''    // Both kinds, and both count. Nothing on disk was changed, so there is no rollback to
    // protect and no reason to hold the optional ones back -- the only thing this path can get
    // wrong is leaving something down that it took down.
    const RelaunchVerdict required = effects.RelaunchRequired();
    const RelaunchVerdict optional = effects.RelaunchOptional();
    const bool everything =
        required == RelaunchVerdict::AllBack && optional == RelaunchVerdict::AllBack;
    out.result = everything ? UpdateResult::AbandonedBeforeSwap
                            : UpdateResult::AbandonedNotRelaunched;
    return out;'''
assert old in d, 'abandon'
d = d.replace(old, new, 1)

old = '''      enter(UpdateState::Relaunch);
      if (effects.Relaunch() != RelaunchVerdict::AllBack) {
        out.result = UpdateResult::RolledBackNotRelaunched;
      } else {
        // And the restored build is checked the same way a new one would be. What is on disk now
        // is the previous version, so this is a question about that.
        //
        // The answer is USED. It was discarded, which meant a restore that came back broken was
        // reported as a successful rollback -- "we put it back" and "it works" are different
        // claims and only the first one had been established.
        enter(UpdateState::Health);
        out.result = effects.HealthCheck() ? UpdateResult::RolledBack
                                           : UpdateResult::RestoredButUnhealthy;
      }'''
new = '''      enter(UpdateState::Relaunch);
      // The same order as a successful update, for the same reasons: what the machine needs
      // first, then its health, and only then the rest. A client that will not come back must
      // not stop this path from establishing whether the restored host is up.
      if (effects.RelaunchRequired() != RelaunchVerdict::AllBack) {
        out.result = UpdateResult::RolledBackNotRelaunched;
      } else {
        // And the restored build is checked the same way a new one would be. What is on disk now
        // is the previous version, so this is a question about that.
        //
        // The answer is USED. It was discarded, which meant a restore that came back broken was
        // reported as a successful rollback -- "we put it back" and "it works" are different
        // claims and only the first one had been established.
        enter(UpdateState::Health);
        const bool healthy = effects.HealthCheck();
        // The old client goes back last, and whether it makes it does not change the verdict
        // above. Reported, because the user will notice, but a window that did not reopen is not
        // the same as a machine nobody can reach -- and the old outcome name said it was.
        const RelaunchVerdict optional = effects.RelaunchOptional();
        out.result = healthy ? UpdateResult::RolledBack : UpdateResult::RestoredButUnhealthy;
        if (healthy && optional != RelaunchVerdict::AllBack) {
          out.detail = std::string(why) + " -- restored, but something optional did not come back";
        }
      }'''
assert old in d, 'rollback relaunch'
d = d.replace(old, new, 1)

old = '''  enter(UpdateState::Relaunch);
  const RelaunchVerdict relaunched = effects.Relaunch();

  if (relaunched == RelaunchVerdict::RequiredMissing) {
    // The files are the new version and they are fine. The machine is not reachable, and a
    // machine nobody can reach is worth less than an older one somebody can -- so this goes back,
    // which is only possible because nothing has been committed yet and the backups are still
    // there. Folding this into the same branch as a missing client dropped those backups.
    return rollback("something the machine needs did not come back");
  }

  // Health FIRST, whatever the relaunch reported.
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

  enter(UpdateState::Done);
  // Only here. Everything before this point could still have ended in a rollback.
  effects.Commit();
  effects.DiscardDownload();
  out.result = UpdateResult::Updated;
  out.detail = fields.version;
  return out;'''
new = '''  // ---- required first, health, commit, and only then the rest.
  //
  // The order is the safety property. While a rollback is still possible, the only processes
  // started are ones this attempt can stop: the host and the service, both launched with a handle
  // in hand. The client is not, because the route that starts it as the logged-on user may fall
  // through to the shell, and a shell launch hands nothing back -- no handle, no proof of which
  // process is ours, no way to stop it. A rollback has to move the files it would be holding.
  //
  // Starting it first and reclassifying afterwards was the obvious repair and it is too late by
  // construction: by the time the fall-through is discovered the process exists, and the way back
  // has been closed by the act of putting the product back on its feet. So the optional images
  // wait until there is nothing left to undo.
  enter(UpdateState::Relaunch);
  if (effects.RelaunchRequired() != RelaunchVerdict::AllBack) {
    // The files are the new version and they are fine. The machine is not reachable, and a
    // machine nobody can reach is worth less than an older one somebody can -- so this goes back,
    // which is only possible because nothing has been committed yet and the backups are still
    // there.
    return rollback("something the machine needs did not come back");
  }

  // "Required started" means CreateProcess returned. It does not mean the host came up, and the
  // difference is the whole reason this question is asked before anything is committed.
  enter(UpdateState::Health);
  if (!effects.HealthCheck()) return rollback("new build did not come up healthy");

  enter(UpdateState::Done);
  // The point of no return, and it is deliberate that it comes before the optional relaunch
  // rather than after. Everything that could have sent this back has now been answered.
  effects.Commit();
  effects.DiscardDownload();

  // Past the commit. Nothing started here can force a rollback, because there is no longer one to
  // force -- which is exactly the condition under which starting something unownable is safe.
  const RelaunchVerdict optional = effects.RelaunchOptional();
  if (optional != RelaunchVerdict::AllBack) {
    // Not a failure of the update and not hidden as a success either. The install is correct and
    // healthy; something a person expects to see did not reappear, and they need to be told so
    // they start it themselves instead of assuming the update broke it.
    out.result = UpdateResult::UpdatedButNotRelaunched;
    out.detail = "installed and healthy, but something optional did not come back up";
    return out;
  }

  out.result = UpdateResult::Updated;
  out.detail = fields.version;
  return out;'''
assert old in d, 'main tail'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('required -> health -> commit -> optional')
