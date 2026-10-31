import io

# ---------------------------------------------------------------- three-state liveness
p = 'apps/native_poc/src/update_relaunch.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  /**
   * Whether a captured target is still running under the same identity.
   *
   * Injected so a test can answer without real processes, and needed because capturing an
   * identity is not the same as that process having left. Abandoning after a waiting caller was
   * released is exactly the case where some of the product has gone and some has not -- and
   * relaunching something that never went produces a second instance.
   *
   * Empty means "assume everything left", which is right for the paths that follow a completed
   * Quiesce.
   */
  std::function<bool(const ProcessTarget&)> isStillRunning;'''
new = '''  /**
   * Whether a captured target is still running under the same identity.
   *
   * Three answers, not two, and the third is the point: a check that could not be completed must
   * not be read as "it exited". Assuming departure relaunches something that is still there and
   * leaves two of it; assuming presence leaves a machine down while reporting success. Neither is
   * safe to guess, so Unknown is carried through and refused.
   *
   * Injected so a test can answer without real processes. When it is empty the real check is
   * used -- it used to default to "everything left", which meant PRODUCTION never performed this
   * check at all while the tests that exercised it passed.
   */
  enum class Liveness { Running, Exited, Unknown };
  std::function<Liveness(const ProcessTarget&)> isStillRunning;'''
assert old in d, 'liveness field'
d = d.replace(old, new, 1)

old = '''  /**
   * True when it never left, so there was nothing to bring back.'''
new = '''  /**
   * True when the check could not answer.
   *
   * Not started, and not silently counted as fine: something that might be down is not something
   * to report as back.
   */
  bool livenessUnknown = false;
  /**
   * True when it never left, so there was nothing to bring back.'''
assert old in d, 'outcome'
d = d.replace(old, new, 1)

old = '''  bool failed() const { return !started && !skipped && !alreadyRunning; }'''
new = '''  bool failed() const { return !started && !skipped && !alreadyRunning; }'''
assert old in d
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the real check, used by default
p = 'apps/native_poc/src/update_relaunch.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''bool wait_for_service_state(SC_HANDLE service, DWORD wanted, uint32_t timeoutMs,'''
new = '''/**
 * Whether `target` is still the process it was when captured.
 *
 * The real answer, used whenever a caller does not supply one -- which in production is always.
 * It used to default to "everything left", so this check existed only in tests: the code that
 * shipped skipped it entirely, and the suites that covered it were covering a configuration
 * nobody ran.
 *
 * A process that cannot be opened is NOT assumed gone. Access can be denied for reasons that have
 * nothing to do with whether it is alive, and reading that as "exited" is what leads to starting
 * a second copy of something that is still running.
 */
RelaunchConfig::Liveness real_liveness(const ProcessTarget& target) {
  HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
  if (!h) {
    // ERROR_INVALID_PARAMETER is what a pid that no longer exists gives; anything else means the
    // question could not be asked, which is a different answer.
    const DWORD err = GetLastError();
    return (err == ERROR_INVALID_PARAMETER) ? RelaunchConfig::Liveness::Exited
                                            : RelaunchConfig::Liveness::Unknown;
  }
  const bool signalled = WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
  if (signalled) {
    CloseHandle(h);
    return RelaunchConfig::Liveness::Exited;
  }
  // Alive -- but is it the one that was captured? A pid can be reused, and stopping or skipping a
  // stranger because it inherited a number is exactly what identity checking is for.
  const bool same = process_identity_matches(h, target);
  CloseHandle(h);
  return same ? RelaunchConfig::Liveness::Running : RelaunchConfig::Liveness::Exited;
}

bool wait_for_service_state(SC_HANDLE service, DWORD wanted, uint32_t timeoutMs,'''
assert old in d, 'real_liveness anchor'
d = d.replace(old, new, 1)

old = '''      if (shared->config.isStillRunning) {
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
      }'''
new = '''      {
        // The supplied check, or the real one. There is no "skip this" branch any more: that
        // branch was what production took, so the check shipped switched off.
        const auto ask = shared->config.isStillRunning
                             ? shared->config.isStillRunning
                             : std::function<RelaunchConfig::Liveness(const ProcessTarget&)>(
                                   real_liveness);
        bool alive = false;
        bool unknown = false;
        for (const ProcessTarget& target : shared->stopped) {
          if (image_leaf_lower(target.imagePath) != image_leaf_lower(entry.imageName)) continue;
          const RelaunchConfig::Liveness answer = ask(target);
          if (answer == RelaunchConfig::Liveness::Running) alive = true;
          if (answer == RelaunchConfig::Liveness::Unknown) unknown = true;
        }
        if (alive) {
          outcome.alreadyRunning = true;
          outcome.detail = "still running -- nothing to bring back";
          shared->outcomes.push_back(outcome);
          continue;
        }
        if (unknown) {
          // Neither started nor excused. Starting it might make two; not starting it might leave
          // the machine down -- so it is reported as a failure and the caller decides, which for
          // a required image means rolling back rather than declaring success.
          outcome.livenessUnknown = true;
          outcome.detail = "could not determine whether it is still running, so it was not started";
          shared->outcomes.push_back(outcome);
          continue;
        }
      }'''
assert old in d, 'liveness use'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('liveness: three answers, real check by default')
