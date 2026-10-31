import io

# ============================================================ (2) ownership of what we stopped
p = 'apps/native_poc/src/update_relaunch.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  /**
   * Stops the processes THIS attempt started, and returns how many.
   *
   * A rollback has to move files those processes are holding open, so they have to go first.
   * Restricted to what this attempt started and checked against the image name before anything
   * happens to it: a pid is not an identity, and nothing else on the machine is this function's
   * business.
   */
  std::function<int()> stopStarted;'''
new = '''  /** What stopping the processes this attempt started achieved. */
  struct StopReport {
    int stopped = 0;
    /**
     * Things this attempt started that it could not prove it owns, or could not stop.
     *
     * Non-empty means a rollback must not begin. Restoring files while something may still be
     * holding them produces a half-restored installation, and the reason will look like the
     * rollback's fault rather than like this.
     */
    std::vector<std::wstring> unstoppable;
    bool complete() const { return unstoppable.empty(); }
  };

  /**
   * Stops the processes THIS attempt started.
   *
   * Ownership is proved by the handle received when the process was created, not by matching a
   * pid to an image name. A name match is not ownership: a pid can be reused, and another process
   * of the same name may be someone else's. Anything started through the shell has no such handle
   * -- the shell created it, not us -- so it is reported as unstoppable rather than killed on a
   * guess.
   */
  std::function<StopReport()> stopStarted;'''
assert old in d, 'stopStarted decl'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/update_relaunch.cpp'
d = io.open(p, encoding='utf-8').read()

# the shared state keeps the handles we were given
d = d.replace('''    std::vector<RelaunchOutcome> outcomes;
    std::vector<ProcessTarget> stopped;''',
'''    std::vector<RelaunchOutcome> outcomes;
    std::vector<ProcessTarget> stopped;
    /**
     * Handles for processes this attempt created, by image name.
     *
     * A handle IS ownership: it names one process for as long as it is held, which is precisely
     * what a pid does not do. Kept so a rollback can stop what this attempt started without
     * having to guess which pid is still the right one.
     */
    std::vector<std::pair<std::wstring, HANDLE>> ownedHandles;''', 1)

# CreateProcessW: keep the handle instead of closing it
d = d.replace('''          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          outcome.started = true;
          // Remembered so a rollback can stop what this attempt started before it tries to move
          // the files those processes are holding open.
          outcome.startedPid = pi.dwProcessId;''',
'''          CloseHandle(pi.hThread);
          // The handle is KEPT, not closed. It is the only proof of ownership there is -- while it
          // is held the pid cannot be reused, so "this handle" and "the process we started" stay
          // the same thing. Matching a pid to an image name later is not the same claim.
          shared->ownedHandles.push_back({entry.imageName, pi.hProcess});
          outcome.started = true;
          outcome.startedPid = pi.dwProcessId;''', 1)

# stopStarted: only what we own
old = d[d.find('  effects.stopStarted = [shared]() {'):d.find('  effects.lastOutcomes = [shared]() { return shared->outcomes; };')]
new = '''  effects.stopStarted = [shared]() {
    RelaunchEffects::StopReport report;
    // Only processes this attempt created, identified by the handle it was given at creation.
    for (auto& owned : shared->ownedHandles) {
      if (!owned.second) continue;
      if (WaitForSingleObject(owned.second, 0) == WAIT_OBJECT_0) {
        // Already exited on its own.
        CloseHandle(owned.second);
        owned.second = nullptr;
        continue;
      }
      if (TerminateProcess(owned.second, 0) &&
          WaitForSingleObject(owned.second, 5000) == WAIT_OBJECT_0) {
        ++report.stopped;
        CloseHandle(owned.second);
        owned.second = nullptr;
        continue;
      }
      // Asked and it did not go. Reported, not assumed away -- a rollback that moved files now
      // would be moving files something is still holding.
      report.unstoppable.push_back(owned.first);
    }

    // Anything started through the shell. We have no handle for it, because the shell created it;
    // a pid found by comparing snapshots is a good guess and a guess is not ownership. Killing on
    // that basis can kill a stranger, so it is reported instead.
    for (const RelaunchOutcome& outcome : shared->outcomes) {
      if (outcome.kind != RelaunchKind::UserProcess || !outcome.started) continue;
      bool owned = false;
      for (const auto& pair : shared->ownedHandles) {
        if (pair.first == outcome.imageName) owned = true;
      }
      if (!owned) report.unstoppable.push_back(outcome.imageName);
    }
    return report;
  };

'''
assert old, 'stopStarted body'
d = d.replace(old, new, 1)

# the handles are released when the effects go
d = d.replace('''  effects.lastActions = [shared]() { return shared->actions; };''', '', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ============================================================ (4) Commit must not lose a failure
p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''void WindowsUpdateEffects::Commit() {
  // The update is not going to be rolled back, so the way back is no longer needed.
  for (const std::wstring& name : movedAside_) {
    DeleteFileW(backup_path(name).c_str());
  }
  movedAside_.clear();
}'''
new = '''void WindowsUpdateEffects::Commit() {
  // The update is not going to be rolled back, so the way back is no longer needed.
  //
  // The result of each delete is READ. It was not, and the list was cleared regardless, so a
  // backup that could not be removed was forgotten twice over: the file stayed beside the
  // installation and nothing recorded that it had. That is the mechanism behind a stale
  // .gnlink-old nobody could explain.
  std::vector<std::wstring> stuck;
  for (const std::wstring& name : movedAside_) {
    const std::wstring backup = backup_path(name);
    if (DeleteFileW(backup.c_str())) continue;
    if (GetLastError() == ERROR_FILE_NOT_FOUND) continue;  // already gone is the desired state
    stuck.push_back(name);
  }
  movedAside_.clear();
  orphanedBackups_ = stuck;
  if (!stuck.empty()) {
    std::string names;
    for (const std::wstring& name : stuck) {
      if (!names.empty()) names += ", ";
      names += to_utf8(name);
    }
    // Not a failure of the update -- the files on disk are correct -- but litter that a person
    // should be able to find out about, and that a later attempt will trip over when it tries to
    // move a file aside onto a name that is already taken.
    lastError_ = "backups left behind after commit: " + names;
  }
}'''
assert old in d, 'Commit'
d = d.replace(old, new, 1)

old = '''  // First, and before a single file moves.'''
new = '''  // First, and before a single file moves.'''
assert old in d
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/update_effects.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  std::vector<std::wstring> placed_;'''
new = '''  std::vector<std::wstring> placed_;
  /**
   * Backups that could not be deleted at commit time.
   *
   * Recorded because they used to vanish: the delete result was ignored and the list cleared, so
   * a file that stayed beside the installation left no trace of itself anywhere.
   */
  std::vector<std::wstring> orphanedBackups_;'''
assert old in d, 'orphaned field'
d = d.replace(old, new, 1)
old = '''  /** Last failure reason, for the log and for test messages. */
  const std::string& last_error() const { return lastError_; }'''
new = '''  /** Last failure reason, for the log and for test messages. */
  const std::string& last_error() const { return lastError_; }
  /** Backups that survived a commit, if any. Empty is the normal case. */
  const std::vector<std::wstring>& orphaned_backups() const { return orphanedBackups_; }'''
assert old in d, 'accessor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ownership by handle, and commit records what it could not delete')
