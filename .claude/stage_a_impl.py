import io

BS = chr(92)
p = 'apps/native_poc/src/update_relaunch.cpp'
d = io.open(p, encoding='utf-8').read()

d = d.replace('''    bool allStarted = true;
    for (const RelaunchEntry& entry : shared->plan) {
      RelaunchOutcome outcome;
      outcome.imageName = entry.imageName;
      outcome.kind = entry.kind;

      if (entry.kind == RelaunchKind::SupervisedByAnother) {''',
'''    for (const RelaunchEntry& entry : shared->plan) {
      RelaunchOutcome outcome;
      outcome.imageName = entry.imageName;
      outcome.kind = entry.kind;

      // Still there? Then it never left, and starting it would produce a second instance. This is
      // the ordinary case when an attempt is abandoned after a waiting caller was released: some
      // of the product went and some did not, and having captured an identity says nothing about
      // whether that process has since exited.
      if (shared->config.isStillRunning) {
        bool alive = false;
        for (const ProcessTarget& target : shared->stopped) {
          if (image_leaf_lower(target.imagePath) != image_leaf_lower(entry.imageName)) continue;
          if (shared->config.isStillRunning(target)) alive = true;
        }
        if (alive) {
          outcome.alreadyRunning = true;
          outcome.detail = "still running -- nothing to bring back";
          shared->outcomes.push_back(outcome);
          continue;
        }
      }

      if (entry.kind == RelaunchKind::SupervisedByAnother) {''', 1)

d = d.replace('''        if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           shared->config.installDir.c_str(), &si, &pi)) {
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          outcome.started = true;
          outcome.detail = "started";''',
'''        if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           shared->config.installDir.c_str(), &si, &pi)) {
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          outcome.started = true;
          // Remembered so a rollback can stop what this attempt started before it tries to move
          // the files those processes are holding open.
          outcome.startedPid = pi.dwProcessId;
          outcome.detail = "started";''', 1)

d = d.replace('''      if (outcome.failed()) allStarted = false;
      shared->outcomes.push_back(outcome);
    }
    return allStarted;
  };''',
'''      shared->outcomes.push_back(outcome);
    }

    // Folded into a verdict rather than a bool, because a missing client and a missing host call
    // for different responses: one is an inconvenience, the other leaves a remote user with no
    // way into the machine.
    RelaunchVerdict verdict = RelaunchVerdict::AllBack;
    for (const RelaunchOutcome& outcome : shared->outcomes) {
      if (!outcome.failed()) continue;
      if (outcome.required()) return RelaunchVerdict::RequiredMissing;  // decides on its own
      verdict = RelaunchVerdict::OptionalMissing;
    }
    return verdict;
  };''', 1)

# the shared state keeps the stopped list for the liveness comparison
d = d.replace('''    std::vector<RelaunchOutcome> outcomes;
    std::string healthDetail;''',
'''    std::vector<RelaunchOutcome> outcomes;
    std::vector<ProcessTarget> stopped;
    std::string healthDetail;''', 1)
d = d.replace('  shared->plan = relaunch_plan(stopped, table);',
              '  shared->plan = relaunch_plan(stopped, table);\n  shared->stopped = stopped;', 1)

# stopping what this attempt started, for a rollback that has to move those files
d = d.replace('''  effects.lastOutcomes = [shared]() { return shared->outcomes; };''',
'''  effects.stopStarted = [shared]() {
    // Only what THIS attempt started, identified by the pid it was handed at creation and
    // checked against the image before anything is done to it. Nothing else on the machine is
    // this function's business, and a pid alone is not an identity.
    int stopped = 0;
    for (const RelaunchOutcome& outcome : shared->outcomes) {
      if (!outcome.started || outcome.startedPid == 0) continue;
      HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                             outcome.startedPid);
      if (!h) continue;  // already gone
      wchar_t image[MAX_PATH]{};
      DWORD size = MAX_PATH;
      const bool named = QueryFullProcessImageNameW(h, 0, image, &size) != FALSE;
      if (named && image_leaf_lower(image) == image_leaf_lower(outcome.imageName)) {
        TerminateProcess(h, 0);
        WaitForSingleObject(h, 5000);
        ++stopped;
      }
      CloseHandle(h);
    }
    return stopped;
  };
  effects.lastOutcomes = [shared]() { return shared->outcomes; };''', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- header for stopStarted
p = 'apps/native_poc/src/update_relaunch.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  /** What the last relaunch did, per entry. Empty until relaunch() has run. */'''
new = '''  /**
   * Stops the processes THIS attempt started, and returns how many.
   *
   * A rollback has to move files those processes are holding open, so they have to go first.
   * Restricted to what this attempt started and checked against the image name before anything
   * happens to it: a pid is not an identity, and nothing else on the machine is this function's
   * business.
   */
  std::function<int()> stopStarted;

  /** What the last relaunch did, per entry. Empty until relaunch() has run. */'''
assert old in d, 'stopStarted decl'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('stage A implementation')
