# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/updater_effects.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  config.registerInstall = [ensureRegistration, registration, versionToInstall]() {
    if (versionToInstall->empty()) return false;  // nothing verified: nothing to register as
    ensureRegistration();
    return registration->apply();
  };'''
new = '''  auto logFn = deps_.log;
  config.registerInstall = [ensureRegistration, registration, versionToInstall, logFn]() {
    if (versionToInstall->empty()) return false;  // nothing verified: nothing to register as
    ensureRegistration();
    const bool ok = registration->apply();
    // The reason, which used to be thrown away here.
    //
    // register_install() stops at the first failure and records WHERE in a RegistrationResult --
    // the steps that completed, the one that failed, and a detail carrying the Win32 or HRESULT.
    // This lambda returned only the bool, so the log said "registration failed" and nothing else,
    // and a field failure could not be told from any other. The result was being produced and
    // discarded one line from where it was needed.
    if (registration->lastResult && logFn) {
      const install::RegistrationResult r = registration->lastResult();
      std::string line = "registration ";
      line += ok ? "ok" : "FAILED";
      if (!r.completed.empty()) {
        line += " completed=";
        for (size_t i = 0; i < r.completed.size(); ++i) {
          if (i) line += ",";
          line += install::step_name(r.completed[i]);
        }
      }
      if (r.failedAt) {
        line += std::string(" failedAt=") + install::step_name(*r.failedAt);
      }
      if (!r.detail.empty()) line += " detail=" + r.detail;
      logFn(line);
    }
    return ok;
  };'''
assert d.count(old) == 1, 'registerInstall'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
