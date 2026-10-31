import io

BS = chr(92)

# ---------------------------------------------------------------- config gains the reporter
p = 'apps/native_poc/src/update_relaunch.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  /** The log the product writes its health report into. */
  std::wstring healthLogPath;'''
new = '''  /** The log the product writes its health report into. */
  std::wstring healthLogPath;
  /**
   * The image whose report counts as evidence of health.
   *
   * Named rather than assumed, because it decides what happens when that image was not running
   * before the update. An update started from the client on a machine where the host is not
   * running relaunches no host, so no host will ever write a report -- and a health check that
   * waited for one anyway would time out and roll back a perfectly good update. Demanding
   * evidence from a process that was never there is inventing a failure, in the same way that
   * treating "no account signed in" as a directory failure would.
   *
   * Empty means nothing reports, and health is then satisfied by the swap having succeeded.
   */
  std::wstring healthReporterImage;'''
assert old in d, 'config anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the check honours it
p = 'apps/native_poc/src/update_relaunch.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  effects.healthCheck = [shared]() {
    if (!shared->marked) {'''
new = '''  effects.healthCheck = [shared]() {
    // Nothing that reports was brought back, so there is nothing to wait for. This is not a
    // shortcut: waiting anyway would time out and roll back an update whose files are correct,
    // for the sole reason that the process which writes the report was not running beforehand.
    if (!shared->config.healthReporterImage.empty()) {
      bool reporterStarted = false;
      for (const RelaunchOutcome& outcome : shared->outcomes) {
        if (outcome.imageName == shared->config.healthReporterImage && outcome.started) {
          reporterStarted = true;
        }
      }
      if (!reporterStarted) {
        shared->healthDetail = "nothing that reports health was relaunched, so there is no "
                               "report to wait for";
        return true;
      }
    }
    if (!shared->marked) {'''
assert old in d, 'health anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the updater names it
p = 'apps/native_poc/src/updater_main.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''    relaunchConfig.expectedVersion = to_utf8(target.version);'''
new = '''    relaunchConfig.expectedVersion = to_utf8(target.version);
    // Only the host writes a health report. When it was not running before the update -- a
    // client-initiated update on a machine nobody is hosting from -- there is nothing to wait for.
    relaunchConfig.healthReporterImage = L"GNLinkHost.exe";'''
assert old in d, 'updater anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the client points at the host log
p = 'apps/native_poc/src/client_shell_main.cpp'
d = io.open(p, encoding='utf-8').read()
old = '  spec.healthLogPath = log_file_path();'
new = '''  // The HOST's log, not this process's: health evidence is the host reporting its own version,
  // and this program does not write that line.
  spec.healthLogPath = host_health_log_path();'''
assert old in d, 'client health log anchor'
d = d.replace(old, new, 1)

old = '''void start_client_update(const std::string& availableVersion) {'''
new = '''/** Where the host writes its health report. Mirrors host_app_main.cpp's log_file_path(). */
std::wstring host_health_log_path() {
  wchar_t base[MAX_PATH]{};
  const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};
  return std::wstring(base) + L"@@GNLink@@host_app.log";
}

void start_client_update(const std::string& availableVersion) {'''.replace('@@', BS * 2)
assert old in d, 'client helper anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('health reporter wired')
