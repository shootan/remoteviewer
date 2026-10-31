# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/client_shell_main.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- one snapshot, and the gate
old = '''void start_update_check() {
  namespace upd = remote60::native_poc::update;

  // One snapshot, built where the session and the server address are known. A shell that has
  // not signed in yet has no credential to send, which is normal: the server says so, and that
  // answer is an update-check failure rather than a reason to sign out.
  std::string session;
  std::string owner;
  uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    session = gSessionToken;
    owner = gAccountId;
    epoch = gOwnerEpoch;
  }
  const upd::UpdateEndpoint endpoint =
      remote60::native_poc::directory::update_endpoint_for(
          remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
          configured_directory_url(), "windows",
          session.empty() ? std::string() : "Authorization: Bearer " + session, owner, epoch);'''
new = '''/** The gate's memory. Guarded by gStateMu, like everything else it is compared against. */
remote60::native_poc::client::UpdateGateState gUpdateGate;

void start_update_check() {
  namespace upd = remote60::native_poc::update;
  namespace cli = remote60::native_poc::client;

  // ONE snapshot, and the url is part of it.
  //
  // It used to read the session under the lock and then call configured_directory_url(), which
  // takes the same lock again -- so the two halves could describe different moments. They are
  // read together now; the settings-file fallback happens after, outside the lock, and only when
  // nothing is signed in (in which case there is no session to be inconsistent with).
  std::string session;
  std::string owner;
  uint64_t epoch = 0;
  std::string url;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    session = gSessionToken;
    owner = gAccountId;
    epoch = gOwnerEpoch;
    url = gServerUrl;
  }
  if (url.empty()) {
    std::string server;
    std::string accountId;
    ShellRuntimeSettings settings;
    load_settings(&server, &accountId, &settings);
    url = server;
  }

  {
    // Invited from more than one place now -- start-up and every sign-in -- so the same session
    // must not be asked twice. Signing in again moves the epoch, which is a new question.
    std::lock_guard<std::mutex> lock(gStateMu);
    if (!cli::should_check(gUpdateGate, owner, epoch)) {
      log_line("update check: already asked for this session");
      return;
    }
    cli::note_checked(&gUpdateGate, owner, epoch);
  }

  const upd::UpdateEndpoint endpoint =
      remote60::native_poc::directory::update_endpoint_for(
          remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
          url, "windows",
          session.empty() ? std::string() : "Authorization: Bearer " + session, owner, epoch);'''
assert d.count(old) == 1, 'snapshot'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the callback validates its own snapshot
old2 = '''      [](upd::CheckResult result) {
        const ShellUpdateNotice notice = shell_update_notice(
            upd::check_outcome_name(result.outcome), result.availableVersion, result.detail);
        log_line(notice.logLine);
        if (notice.show) {
          {
            std::lock_guard<std::mutex> lock(gUpdateNoticeMu);
            gAvailableVersion = result.availableVersion;
          }
          deliver_update_notice(shell_status_json("idle", notice.text));
        }
      });'''
new2 = '''      [owner, epoch](upd::CheckResult result) {
        const ShellUpdateNotice notice = shell_update_notice(
            upd::check_outcome_name(result.outcome), result.availableVersion, result.detail);
        log_line(notice.logLine);
        if (!notice.show) return;
        // The answer describes the session that asked. A sign-out or a sign-in as somebody else
        // while this was in flight makes it an answer about a session that no longer exists, and
        // publishing it would put one account's update notice on another's screen.
        std::string currentOwner;
        uint64_t currentEpoch = 0;
        {
          std::lock_guard<std::mutex> lock(gStateMu);
          currentOwner = gAccountId;
          currentEpoch = gOwnerEpoch;
        }
        if (!remote60::native_poc::client::may_publish(owner, epoch, currentOwner, currentEpoch)) {
          log_line("update check: answer arrived for a session that has ended; not shown");
          return;
        }
        {
          std::lock_guard<std::mutex> lock(gUpdateNoticeMu);
          gAvailableVersion = result.availableVersion;
        }
        deliver_update_notice(shell_status_json("idle", notice.text));
      });'''
assert d.count(old2) == 1, 'callback'
d = d.replace(old2, new2, 1)

# ---------------------------------------------------------------- ask again once there is a session
old3 = '''    {
      std::lock_guard<std::mutex> lock(gStateMu);
      save_settings(server, accountId, gSettings);
    }
'''
new3 = '''    {
      std::lock_guard<std::mutex> lock(gStateMu);
      save_settings(server, accountId, gSettings);
    }

    // The check that ran at start-up had neither a token nor an address and reported "not
    // configured" -- correctly, and then nothing asked again, so signing in never produced an
    // update check at all. Signing in is exactly the moment both appear, so it is asked here.
    // Asynchronous, not waited on: a server that never answers must not hold up the host list.
    start_update_check();
'''
assert d.count(old3) == 1, 'login'
d = d.replace(old3, new3, 1)

if '#include "client_update_gate.hpp"' not in d:
    anchor = '#include "client_shell_bridge.hpp"'
    assert d.count(anchor) == 1, 'include'
    d = d.replace(anchor, anchor + '\n#include "client_update_gate.hpp"', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
