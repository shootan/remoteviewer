import io

# ---------------------------------------------------------------- a verdict, not a bool
p = 'apps/native_poc/src/update_state_machine.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  /** Brings the product back in whatever configuration it was running in before. */'''
assert old in d, 'relaunch doc'
new = '''/**
 * What a relaunch achieved, in the only terms the decision needs.
 *
 * A bool cannot express this. A client that did not come back is an inconvenience the user fixes
 * from the Start menu; a HOST that did not come back leaves a remote user with no way into the
 * machine at all, and the right response to those is not the same one. Folding both into false
 * meant every relaunch failure committed the update and dropped the backups -- including the one
 * case where the backups were the only way back.
 */
enum class RelaunchVerdict {
  /** Everything that was supposed to come back did. */
  AllBack,
  /**
   * Something came back but something optional did not -- in practice the client.
   *
   * Not a rollback. The files are the new version and they are consistent, and undoing a good
   * install because a window did not reopen would be the worse outcome.
   */
  OptionalMissing,
  /**
   * Something the machine needs did not come back: the host, or the input service.
   *
   * This is a rollback. Not because the files are wrong -- they are fine -- but because a machine
   * nobody can reach is worth less than an older version somebody can.
   */
  RequiredMissing,
};

const char* relaunch_verdict_name(RelaunchVerdict verdict);

'''
d = d.replace(old, new + old, 1)
d = d.replace('''  virtual bool Relaunch() = 0;''',
              '''  virtual RelaunchVerdict Relaunch() = 0;''', 1)
old2 = '''  // The swap or what follows it failed and the previous version was restored, AND the product
  // that was running before is running again.
  RolledBack,'''
new2 = '''  // The swap or what follows it failed and the previous version was restored, AND the product
  // that was running before is running again and reported itself healthy.
  RolledBack,
  /**
   * Restored and running, and it does not look right.
   *
   * The previous version is back on disk and started, but it did not report itself healthy --
   * which may mean the restore was incomplete, or that whatever broke the update also affects the
   * version being restored to. Not reported as a successful rollback, because "we put it back"
   * and "it works" are different claims and only the first one has been established.
   */
  RestoredButUnhealthy,'''
assert old2 in d, 'result'
d = d.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the relaunch reports it
p = 'apps/native_poc/src/update_relaunch.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''struct RelaunchEffects {
  std::function<bool()> relaunch;'''
new = '''struct RelaunchEffects {
  /**
   * Brings back what was stopped, and says what it managed.
   *
   * Returns a verdict rather than a bool because the caller has to treat a missing host
   * differently from a missing client -- see RelaunchVerdict.
   */
  std::function<RelaunchVerdict()> relaunch;'''
assert old in d, 'relaunch field'
d = d.replace(old, new, 1)

old = '''  /** True only when the thing is actually running now. Skipped entries are not "started". */
  bool started = false;'''
new = '''  /** True only when the thing is actually running now. Skipped entries are not "started". */
  bool started = false;
  /**
   * True when it never left, so there was nothing to bring back.
   *
   * Distinct from `started`: relaunching something that is still running produces a second
   * instance. Abandoning after a caller was released is exactly the case -- some of the product
   * may have gone and some may not, and capturing an identity is not the same as that process
   * having exited.
   */
  bool alreadyRunning = false;
  /** The process this attempt started, when it started one. 0 otherwise. */
  uint32_t startedPid = 0;'''
assert old in d, 'outcome fields'
d = d.replace(old, new, 1)

old = '''  /** A failure that matters: something that should have come back did not. */
  bool failed() const { return !started && !skipped; }'''
new = '''  /** A failure that matters: something that should have come back did not. */
  bool failed() const { return !started && !skipped && !alreadyRunning; }
  /** Whether the machine needs this one. A host or a service, not a client. */
  bool required() const {
    return kind == RelaunchKind::ElevatedProcess || kind == RelaunchKind::Service;
  }'''
assert old in d, 'failed()'
d = d.replace(old, new, 1)

old = '''  /** How long Quiesce waits for the targets to go away before giving up. Never forces. */'''
new = '''  /**
   * Whether a captured target is still running under the same identity.
   *
   * Injected so a test can answer without real processes, and required because capturing an
   * identity before a caller is released is not the same as that caller having left. Relaunching
   * something that never went produces a duplicate.
   */
  std::function<bool(const ProcessTarget&)> isStillRunning;

  /** How long Quiesce waits for the targets to go away before giving up. Never forces. */'''
if old in d:
    d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('stage A headers')
