import io

p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()

# 1) include
old = 'constexpr UINT kTrayMessage = WM_APP + 1;'
new = ('constexpr UINT kTrayMessage = WM_APP + 1;\n'
       '// An update check finishes on a worker thread, and a worker thread must not touch this\n'
       '// window. The answer comes back as a posted message carrying a heap-allocated string the\n'
       '// UI thread takes ownership of.\n'
       'constexpr UINT kUpdateCheckDoneMessage = WM_APP + 3;')
assert old in d, 'tray message anchor'
d = d.replace(old, new, 1)

old = '#include "product_version.hpp"'
if old not in d:
    # find whatever include block exists and append after the last project include
    old = '#include "log_upload.hpp"'
assert old in d, 'include anchor: ' + old
d = d.replace(old, old + '\n#include "update_check.hpp"', 1)

# 2) menu id
old = """enum MenuId : int {
  IdMenuOpen = 2001,
  IdMenuChangeAccount,
  IdMenuSignOut,
  IdMenuExit,
};"""
new = """enum MenuId : int {
  IdMenuOpen = 2001,
  IdMenuChangeAccount,
  IdMenuSignOut,
  IdMenuCheckUpdate,
  IdMenuExit,
};"""
assert old in d, 'menu id anchor'
d = d.replace(old, new, 1)

# 3) menu item
old = """  AppendMenuW(menu, MF_STRING | (g.signedIn ? 0 : MF_GRAYED), IdMenuSignOut, L"Sign out");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IdMenuExit, L"Exit");"""
new = """  AppendMenuW(menu, MF_STRING | (g.signedIn ? 0 : MF_GRAYED), IdMenuSignOut, L"Sign out");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  // Always enabled, including when signed out: "is there a newer build" does not depend on being
  // signed in, and greying it out would make an unconfigured build look like a broken one.
  AppendMenuW(menu, MF_STRING, IdMenuCheckUpdate, L"Check for updates");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IdMenuExit, L"Exit");"""
assert old in d, 'menu item anchor'
d = d.replace(old, new, 1)

# 4) handler + the message that delivers the result
old = """      if (id == IdMenuExit) {
        DestroyWindow(window);
        return 0;
      }
      break;
    }"""
new = """      if (id == IdMenuCheckUpdate) {
        start_update_check(window);
        return 0;
      }
      if (id == IdMenuExit) {
        DestroyWindow(window);
        return 0;
      }
      break;
    }

    case kUpdateCheckDoneMessage: {
      // Ownership of the string transfers here. Shown from the UI thread, which is the reason
      // the worker posts instead of calling MessageBox itself.
      std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
      if (text) {
        MessageBoxW(window, text->c_str(), kProductName, MB_OK | MB_ICONINFORMATION);
      }
      return 0;
    }"""
assert old in d, 'handler anchor'
d = d.replace(old, new, 1)

# 5) the function itself, placed just above show_tray_menu
old = 'void show_tray_menu(HWND window) {'
new = '''/**
 * Asks whether there is a newer build, without blocking the tray.
 *
 * The check runs on a detached thread and posts its answer back; the menu closes immediately
 * either way. That matters more than it looks: the failure this guards against is a socket
 * timeout freezing the tray icon of a product whose whole job is to be running quietly.
 *
 * With no trusted key compiled in, the honest answer is that this build cannot check -- said
 * plainly rather than dressed up as a server error.
 */
void start_update_check(HWND window) {
  namespace upd = remote60::native_poc::update;

  upd::CheckConfig config;
  config.manifestUrl = env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL");
  config.trustedPublicKeyHex = upd::trusted_public_key_hex();
  config.platform = "windows";
  config.installedVersion = narrow(kProductVersion);

  upd::check_for_update_async(
      config, upd::https_manifest_fetcher(), upd::default_verifier(),
      [window](upd::CheckResult result) {
        std::wstring text;
        switch (result.outcome) {
          case upd::CheckOutcome::UpdateAvailable:
            text = L"A newer version is available: " + widen(result.availableVersion) +
                   L"\\n\\nInstalling it from here is not implemented yet.";
            break;
          case upd::CheckOutcome::UpToDate:
            text = L"This is the latest version.";
            break;
          case upd::CheckOutcome::NotConfigured:
            // Not an error, and not "up to date" either. Saying which is the point.
            text = L"This build cannot check for updates.\\n\\n" + widen(result.detail);
            break;
          case upd::CheckOutcome::Unreachable:
            text = L"Could not reach the update server, so it is not known whether a newer "
                   L"version exists.\\n\\n" + widen(result.detail);
            break;
          case upd::CheckOutcome::Rejected:
            text = L"The update information could not be trusted and was ignored.\\n\\n" +
                   widen(result.detail);
            break;
        }
        AppendLogLineOnce(std::string("[host-app] update check: ") +
                          upd::check_outcome_name(result.outcome) + " " + result.detail);
        // Posted, not called: this runs on the worker thread.
        PostMessageW(window, kUpdateCheckDoneMessage, 0,
                     reinterpret_cast<LPARAM>(new std::wstring(std::move(text))));
      });
}

void show_tray_menu(HWND window) {'''
assert old in d, 'show_tray_menu anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('tray wired')
