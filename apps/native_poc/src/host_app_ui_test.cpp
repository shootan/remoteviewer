// GNLinkHost's own window, in a build a test can start without touching the installed host.
//
// The whole of host_app_main.cpp is compiled in -- the form, the layout, the sign-in worker, the
// HTTP, the cache, the supervisor -- and only the process entry is renamed. What differs from
// GNLinkHost.exe is exactly what REMOTE60_HOST_TEST_SEAM replaces, and the manifest:
//
//   * the directory is the fixture's, named by the runner, instead of kFixedDirectoryUrl,
//     and so is the list of its former names (kMigratableDirectoryOrigins)
//   * host.json is the fixture's, not %LOCALAPPDATA%\remote60\host.json
//   * the streaming child is this executable in stand-in mode, not GNLinkStream.exe
//   * asInvoker, so a runner without elevation can start it and UI Automation can reach it
//
// So this does not show requireAdministrator, UAC, UIPI, the real streaming host, or the real
// server. It shows what the window asks for and what signing in through it does.
//
// Nothing is driven from in here. The runner (apps/directory/test/host_login_ui_runner.js)
// starts this process and automation/gnlink_host_login_uia.ps1 works the window from outside,
// through UI Automation, the way a person's hands would reach it.

#define wWinMain host_product_entry
#include "host_app_main.cpp"
#undef wWinMain

#include <filesystem>
#include <fstream>

namespace {

std::wstring test_env(const wchar_t* name) {
  wchar_t value[1024] = {};
  const DWORD n = GetEnvironmentVariableW(name, value, 1024);
  return (n > 0 && n < 1024) ? std::wstring(value) : std::wstring();
}

bool inside(const std::filesystem::path& root, const std::filesystem::path& candidate) {
  const std::wstring rel =
      std::filesystem::weakly_canonical(candidate).lexically_relative(root).wstring();
  return !rel.empty() && rel.rfind(L"..", 0) != 0;
}

/**
 * Stand-in streaming host: writes down what it was started with and waits to be stopped.
 *
 * It reports nothing on stdout, so the window never reads "online" from it -- the status card
 * this build shows is signed in and STARTING / NOT REACHABLE, never REACHABLE.
 */
int run_stand_in_stream(const std::filesystem::path& root, int argc, LPWSTR* argv) {
  std::ofstream seen(root / "stream-launches.txt", std::ios::app);
  for (int i = 1; i < argc; ++i) seen << (i > 1 ? "\t" : "") << narrow(argv[i]);
  seen << "\n";
  seen.close();
  Sleep(INFINITE);  // ended by the supervisor, or by its job when the window's process goes
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int show) {
  // Refused before a window exists unless everything this process can write is the runner's.
  const std::wstring rootText = test_env(L"GNLINK_HOST_TEST_ROOT");
  if (rootText.empty() || !std::filesystem::exists(std::filesystem::path(rootText) / ".fixture")) {
    return 2;
  }
  const std::filesystem::path root = std::filesystem::weakly_canonical(rootText);

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--transport") return run_stand_in_stream(root, argc, argv);
  }

  const std::wstring appData = test_env(L"LOCALAPPDATA");
  const std::string directoryUrl = narrow(test_env(L"GNLINK_HOST_TEST_DIRECTORY"));
  if (appData.empty() || !inside(root, appData) ||
      directoryUrl.rfind("http://127.0.0.1:", 0) != 0) {
    return 2;
  }
  gHostTest.directoryUrl = directoryUrl;
  // Optional: a second loopback address standing in for a former name of the server.
  const std::string formerName = narrow(test_env(L"GNLINK_HOST_TEST_FORMER_NAME"));
  if (!formerName.empty()) {
    if (formerName.rfind("http://127.0.0.1:", 0) != 0) return 2;
    gHostTest.migratableOrigins.push_back(formerName);
  }
  gHostTest.cachePath = narrow((std::filesystem::path(appData) / "remote60" / "host.json").wstring());
  gHostTest.streamExe = own_executable_path();
  return host_product_entry(instance, previous, commandLine, show);
}
