# -*- coding: utf-8 -*-
"""Wires the client's update offer to something the user can press.

The notice was wired and the action was not: the page was told a new version existed and had no
button, while the native side sat on a {"type":"update"} handler nobody ever sent.
"""
import io

p = 'apps/native_poc/src/client_shell_main.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- a flag, so one click is one updater
old = '''/** The version the last check found, empty when there is nothing to install. */
std::string gAvailableVersion;'''
new = '''/** The version the last check found, empty when there is nothing to install. */
std::string gAvailableVersion;
/**
 * Set from the moment an install is asked for until it has been started or has failed.
 *
 * The page disables its button while this is true, but the page is not where this can be
 * enforced: a second message can already be in flight when the first one is being handled. The
 * decision lives on this side, where there is one of it.
 */
bool gUpdateStarting = false;'''
assert d.count(old) == 1, 'flag anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- send the version, not a sentence
old = '''        deliver_update_notice(shell_status_json("idle", notice.text));'''
new = '''        // A message with the version in a field of its own. The status line still carries the
        // sentence; what the page needs in order to put up a button is the version, and reading
        // it back out of Korean prose would break the first time the wording changed.
        deliver_update_notice(shell_update_available_json(result.availableVersion, notice.text));'''
assert d.count(old) == 1, 'notice anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the handler: guard, busy, release
old = '''  if (type == "update") {
    std::string version;
    {
      std::lock_guard<std::mutex> lock(gUpdateNoticeMu);
      version = gAvailableVersion;
    }
    // Only when a check actually found something. A page that asked otherwise would be asking to
    // elevate for no reason, and one consent prompt with nothing behind it is worse than none.
    if (version.empty()) {
      post_status("idle", "설치할 새 버전이 없습니다.");
      return;
    }
    start_client_update(version);
    return;
  }'''
new = '''  if (type == "update") {
    std::string version;
    {
      std::lock_guard<std::mutex> lock(gUpdateNoticeMu);
      version = gAvailableVersion;
      // Two clicks arriving together must not become two updaters, each asking for consent and
      // each replacing the same files. The page disables its button, but a message can already
      // be in flight when it does; this is the check that actually decides.
      if (gUpdateStarting) {
        log_line("update: already starting; the second request was ignored");
        return;
      }
      if (!version.empty()) gUpdateStarting = true;
    }
    // Only when a check actually found something. A page that asked otherwise would be asking to
    // elevate for no reason, and one consent prompt with nothing behind it is worse than none.
    if (version.empty()) {
      post_status("idle", "설치할 새 버전이 없습니다.");
      post_to_page(shell_update_cleared_json());
      return;
    }
    post_to_page(shell_update_busy_json(true));
    start_client_update(version);
    // start_client_update reports its own outcome. Whatever it was, the request is over: leaving
    // the flag set would mean a failed attempt could never be retried.
    {
      std::lock_guard<std::mutex> lock(gUpdateNoticeMu);
      gUpdateStarting = false;
    }
    post_to_page(shell_update_busy_json(false));
    return;
  }'''
assert d.count(old) == 1, 'handler anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- signing out withdraws the offer
old = '''    remote60::native_poc::log_upload_clear_credentials("logged out");
    post_to_page("{\\"type\\":\\"signedOut\\"}");'''
new = '''    remote60::native_poc::log_upload_clear_credentials("logged out");
    {
      // The answer described the session that asked for it. Leaving the button up would offer
      // the next person to sign in an install that was found for somebody else.
      std::lock_guard<std::mutex> lock(gUpdateNoticeMu);
      gAvailableVersion.clear();
      gPendingUpdateNotice.clear();
    }
    post_to_page(shell_update_cleared_json());
    post_to_page("{\\"type\\":\\"signedOut\\"}");'''
assert d.count(old) == 1, 'signout anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('client_shell_main.cpp ok')
