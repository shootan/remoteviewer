import io

p = 'apps/native_poc/src/update_relaunch.cpp'
d = io.open(p, encoding='utf-8').read()

# --- the test seam for "there is no shell"
old = '''bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments) {'''
new = '''namespace {
bool gShellLaunchDisabled = false;
}  // namespace

void set_shell_launch_disabled_for_test(bool disabled) { gShellLaunchDisabled = disabled; }

bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments) {
  // The one branch that cannot be arranged by any other means: a machine with no route to the
  // user's context. What must NOT happen there is a fallback to starting it as a child.
  if (gShellLaunchDisabled) return false;
'''
assert old in d, 'shell impl anchor'
d = d.replace(old, new, 1)

# --- the notice
old = '''RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped) {'''
new = '''std::string relaunch_user_notice(const std::vector<RelaunchOutcome>& outcomes) {
  std::vector<std::string> missing;
  for (const RelaunchOutcome& outcome : outcomes) {
    if (outcome.failed()) missing.push_back(to_utf8(outcome.imageName));
  }
  if (missing.empty()) return {};

  std::string names;
  for (size_t i = 0; i < missing.size(); ++i) {
    if (i) names += ", ";
    names += missing[i];
  }
  // Says three things, in this order, because that is the order the user needs them: the update
  // worked, this did not come back, here is what to do. Leaving out the first turns a partial
  // success into what looks like a broken installation.
  return "업데이트는 완료되었습니다. 다만 " + names +
         " 을(를) 자동으로 다시 시작하지 못했습니다. 시작 메뉴에서 직접 실행해 주세요.";
}

RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped) {
  return make_relaunch_effects(std::move(config), stopped, product_images());
}

RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped,
                                      const std::vector<KnownImage>& table) {'''
assert old in d, 'make anchor'
d = d.replace(old, new, 1)

d = d.replace('''    std::vector<std::string> actions;
    std::string healthDetail;''', '''    std::vector<RelaunchOutcome> outcomes;
    std::string healthDetail;''', 1)
d = d.replace('  shared->plan = relaunch_plan(stopped);',
              '  shared->plan = relaunch_plan(stopped, table);', 1)

# --- the relaunch body records outcomes instead of strings
old = d[d.find('  effects.relaunch = [shared]() {'):d.find('  effects.healthCheck = [shared]() {')]
new = '''  effects.relaunch = [shared]() {
    shared->outcomes.clear();
    // Before anything is started, so nothing written by a previous run can be read as evidence.
    shared->logMark = file_size_or_zero(shared->config.healthLogPath);
    shared->marked = true;

    bool allStarted = true;
    for (const RelaunchEntry& entry : shared->plan) {
      RelaunchOutcome outcome;
      outcome.imageName = entry.imageName;
      outcome.kind = entry.kind;

      if (entry.kind == RelaunchKind::SupervisedByAnother) {
        // Not started, and not a failure. Something else owns its lifetime.
        outcome.skipped = true;
        outcome.detail = entry.reason;
        shared->outcomes.push_back(outcome);
        continue;
      }

      const std::wstring exePath = shared->config.installDir + L"\\\\" + entry.imageName;
      if (entry.kind == RelaunchKind::Service) {
        std::string detail;
        outcome.started = start_service_and_wait(shared->config.serviceName, 30000, &detail);
        outcome.detail = outcome.started ? "service running" : ("service did not start -- " + detail);
      } else if (entry.kind == RelaunchKind::UserProcess) {
        outcome.started = launch_via_shell(exePath, L"");
        // Not started, and deliberately not retried as a child of this process. Starting it here
        // would give it an administrator token it never had. That is a safe failure, not a
        // success, so it is recorded as a failure and the user is told.
        outcome.detail = outcome.started
                             ? "started in the user context"
                             : "could not be started in the user context, and was NOT started "
                               "elevated instead";
      } else {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring commandLine = L"\\"" + exePath + L"\\"";
        if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           shared->config.installDir.c_str(), &si, &pi)) {
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          outcome.started = true;
          outcome.detail = "started";
        } else {
          outcome.detail = "could not be started (error " + std::to_string(GetLastError()) + ")";
        }
      }

      if (outcome.failed()) allStarted = false;
      shared->outcomes.push_back(outcome);
    }
    return allStarted;
  };

'''
assert old, 'relaunch body'
d = d.replace(old, new, 1)

d = d.replace('  effects.lastActions = [shared]() { return shared->actions; };',
              '  effects.lastOutcomes = [shared]() { return shared->outcomes; };\n'
              '  effects.userNotice = [shared]() { return relaunch_user_notice(shared->outcomes); };', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('R2 done: per-entry outcomes and the user notice')
