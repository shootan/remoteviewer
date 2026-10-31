import io

p = 'apps/native_poc/src/client_shell_main.cpp'
d = io.open(p, encoding='utf-8').read()

# 1) includes
old = '#include "client_shell_bridge.hpp"'
if old not in d:
    raise SystemExit('bridge include not found')
d = d.replace(old, old + '\n#include "update_check.hpp"', 1)

# 2) the check itself, placed next to post_status which it uses
old = '''/** Signing in and listing hosts both talk to the network, so they never run on the UI thread. */'''
new = '''/**
 * Whether the page has said it is ready to receive, and a notice waiting for that to happen.
 *
 * The start-up check and the WebView both start at once and neither waits for the other, so the
 * answer can arrive before there is anywhere to put it. It is held rather than dropped, and sent
 * when the page reports ready.
 */
std::mutex gUpdateNoticeMu;
std::string gPendingUpdateNotice;
bool gPageReady = false;

void deliver_update_notice(const std::string& json) {
  {
    std::lock_guard<std::mutex> lock(gUpdateNoticeMu);
    if (!gPageReady) {
      gPendingUpdateNotice = json;
      return;
    }
  }
  post_to_page(json);
}

void flush_pending_update_notice() {
  std::string json;
  {
    std::lock_guard<std::mutex> lock(gUpdateNoticeMu);
    gPageReady = true;
    json.swap(gPendingUpdateNotice);
  }
  if (!json.empty()) post_to_page(json);
}

/**
 * Asks whether there is a newer version, without anything waiting for the answer.
 *
 * Started before the WebView exists and never joined. That is the requirement rather than an
 * optimisation: a client that will not show its sign-in window until an update server answers is
 * a client that cannot be used on a machine with no route to that server, and "cannot sign in
 * because the update check is still going" is a worse failure than never checking at all.
 *
 * Only "there is a newer version" reaches the user. Everything else goes to the log -- see
 * shell_update_notice for why.
 *
 * Design: docs/업데이트_기능_설계.md 3.5.
 */
void start_update_check() {
  namespace upd = remote60::native_poc::update;

  upd::CheckConfig config;
  config.manifestUrl = remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL");
  config.trustedPublicKeyHex = upd::trusted_public_key_hex();
  config.platform = "windows";
  config.installedVersion = narrow(kProductVersion);

  upd::check_for_update_async(
      config, upd::https_manifest_fetcher(), upd::default_verifier(),
      [](upd::CheckResult result) {
        const ShellUpdateNotice notice = shell_update_notice(
            upd::check_outcome_name(result.outcome), result.availableVersion, result.detail);
        log_line(notice.logLine);
        if (notice.show) deliver_update_notice(shell_status_json("idle", notice.text));
      });
}

/** Signing in and listing hosts both talk to the network, so they never run on the UI thread. */'''
assert old in d, 'insertion anchor'
d = d.replace(old, new, 1)

# 3) the page reports ready -> flush
old = '''    post_to_page(shell_restore_json(server, accountId, settings));
    return;'''
new = '''    post_to_page(shell_restore_json(server, accountId, settings));
    // Anything the start-up check found while the page was still loading goes out now.
    flush_pending_update_notice();
    return;'''
assert old in d, 'ready anchor'
d = d.replace(old, new, 1)

# 4) start it as early as there is a window, in parallel with the WebView
old = '''  if (!gWindow) return 2;
  ShowWindow(gWindow, SW_SHOW);'''
new = '''  if (!gWindow) return 2;
  ShowWindow(gWindow, SW_SHOW);

  // Before the WebView is created, and not waited on. The window is already up, so a server that
  // never answers costs nothing here.
  start_update_check();'''
assert old in d, 'wWinMain anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('client shell start-up check wired')
