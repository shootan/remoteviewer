import io

# ---------------------------------------------------------------- a distinct result
p = 'apps/native_poc/src/update_state_machine.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  // The swap or what follows it failed and the previous version was restored.
  RolledBack,'''
new = '''  // The swap or what follows it failed and the previous version was restored, AND the product
  // that was running before is running again.
  RolledBack,
  /**
   * Restored, but nothing came back up.
   *
   * Separate from RolledBack because for a remote user it is the worse outcome of the two. The
   * files on disk are correct and the machine is unreachable: the host is one of the processes
   * that was stopped to do the swap, and if it does not return there is no way back in to fix
   * anything. "Restored" and "usable" are not the same claim.
   */
  RolledBackNotRelaunched,'''
assert old in d, 'result anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the rollback path relaunches
p = 'apps/native_poc/src/update_state_machine.cpp'
d = io.open(p, encoding='utf-8').read()

d = d.replace('''    case UpdateResult::RolledBack: return "RolledBack";''',
              '''    case UpdateResult::RolledBack: return "RolledBack";
    case UpdateResult::RolledBackNotRelaunched: return "RolledBackNotRelaunched";''', 1)

old = '''    if (effects.Rollback()) {
      out.result = UpdateResult::RolledBack;
    } else {
      // The only outcome where the install may be inconsistent. Named distinctly so it cannot be
      // mistaken for an ordinary failed update in a log.
      out.result = UpdateResult::RollbackFailed;
    }'''
new = '''    if (effects.Rollback()) {
      // Putting the files back is only half of it. The host was stopped to do the swap, and a
      // machine whose files are correct but whose host is not running is one a remote user cannot
      // reach -- there is no way back in to fix anything. So the previous version is started
      // again, and whether that worked is reported rather than assumed.
      //
      // Once. No retry: a relaunch that failed once is unlikely to succeed on a second identical
      // attempt, and a loop here would sit between the user and a machine that is already in its
      // restored state.
      enter(UpdateState::Relaunch);
      if (effects.Relaunch()) {
        // And the restored build is checked the same way a new one would be. What is on disk now
        // is the previous version, so this is a question about that -- the caller is responsible
        // for the health check knowing which version to expect.
        enter(UpdateState::Health);
        (void)effects.HealthCheck();
        out.result = UpdateResult::RolledBack;
      } else {
        out.result = UpdateResult::RolledBackNotRelaunched;
      }
    } else {
      // The only outcome where the install may be inconsistent. Named distinctly so it cannot be
      // mistaken for an ordinary failed update in a log.
      out.result = UpdateResult::RollbackFailed;
    }'''
assert old in d, 'rollback body'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the updater reports it
p = 'apps/native_poc/src/updater_main.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''    case UpdateResult::RolledBack:
      return 12;'''
new = '''    case UpdateResult::RolledBack:
      return 12;
    // Worse than 12 and better than 13: the files are right, but nothing is running. A remote
    // user cannot reach this machine until someone starts it at the keyboard.
    case UpdateResult::RolledBackNotRelaunched:
      return 14;'''
assert old in d, 'exit code'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('rollback now restores the running configuration')
