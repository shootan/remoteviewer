import io

p = 'apps/native_poc/src/client_shell_bridge.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''/** The "type" field, or empty when the message is not something we recognise. */
std::string shell_message_type(const std::string& json);'''
new = '''/** The "type" field, or empty when the message is not something we recognise. */
std::string shell_message_type(const std::string& json);

/** What a start-up update check should say to the user, if anything. */
struct ShellUpdateNotice {
  /**
   * False for every outcome except "there is a newer version".
   *
   * The client checks at start-up, and a start-up check that cannot reach the server is the
   * ordinary case for a laptop opened on a train. Telling the user about it teaches them to
   * dismiss a dialog, and the next time it says something that matters they will dismiss that
   * too. Failures go to the log, where someone diagnosing a problem will look for them.
   *
   * "Up to date" is silent for the same reason: it is the answer the user already assumed.
   */
  bool show = false;
  std::string text;
  /** Always present, for the log, including the outcomes that say nothing to the user. */
  std::string logLine;
};

/**
 * Decides what a check outcome means for the user.
 *
 * Takes the outcome as a string (from update_check's check_outcome_name) rather than the enum, so
 * the UI boundary does not drag the whole update stack into everything that includes this header.
 * An outcome this build does not recognise is logged and shown to nobody -- a name that is not
 * one of the five is not a reason to interrupt someone.
 */
ShellUpdateNotice shell_update_notice(const std::string& outcome,
                                      const std::string& availableVersion,
                                      const std::string& detail);'''
assert old in d, 'bridge header anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/client_shell_bridge.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''std::string shell_restore_json(const std::string& server, const std::string& accountId,'''
new = '''ShellUpdateNotice shell_update_notice(const std::string& outcome,
                                      const std::string& availableVersion,
                                      const std::string& detail) {
  ShellUpdateNotice notice;
  if (outcome == "UpdateAvailable") {
    notice.show = true;
    notice.text = availableVersion.empty()
                      ? std::string("새 버전이 있습니다.")
                      : "새 버전 " + availableVersion + " 이 있습니다.";
    notice.logLine = "update check: newer version available " + availableVersion;
    return notice;
  }
  if (outcome == "UpToDate") {
    notice.logLine = "update check: up to date";
    return notice;
  }
  if (outcome == "NotConfigured") {
    // Not a failure. This build simply cannot check, and saying so to the user would be
    // reporting a fault that does not exist.
    notice.logLine = "update check: not configured -- " + detail;
    return notice;
  }
  if (outcome == "Unreachable") {
    // Explicitly NOT "up to date". The distinction is kept in the log even though the user is
    // not interrupted with it.
    notice.logLine = "update check: server unreachable, so it is not known whether a newer "
                     "version exists -- " + detail;
    return notice;
  }
  if (outcome == "Rejected") {
    notice.logLine = "update check: the answer did not verify and was ignored -- " + detail;
    return notice;
  }
  notice.logLine = "update check: unrecognised outcome " + outcome;
  return notice;
}

std::string shell_restore_json(const std::string& server, const std::string& accountId,'''
assert old in d, 'bridge impl anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('bridge notice added')
