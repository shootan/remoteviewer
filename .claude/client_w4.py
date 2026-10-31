import io

BS = chr(92)
p = 'apps/native_poc/src/client_shell_main.cpp'
d = io.open(p, encoding='utf-8').read()

d = d.replace('#include "update_check.hpp"', '#include "update_check.hpp"\n#include "update_handoff.hpp"', 1)

old = '''void start_update_check() {'''
new = '''/**
 * Asks for consent once, and starts the updater with it.
 *
 * The client is not an administrator program, so it cannot replace anything in %ProgramFiles%
 * itself. It asks the shell to run the updater elevated, which puts exactly one consent prompt in
 * front of the user.
 *
 * Declining is an answer, not a failure. Nothing is shown, nothing is retried, and the client
 * carries on exactly as it was -- the user still has a working program, which is the whole reason
 * the prompt could be declined in the first place. Reporting a decline as an error would be
 * telling someone that the thing they just chose did not work.
 */
void start_client_update(const std::string& availableVersion) {
  namespace upd = remote60::native_poc::update;

  const std::wstring installDir = executable_dir();
  // A sibling of the install directory, never a child: a child would be inside the directory the
  // updater is about to replace, and it refuses to run from there.
  const std::wstring workDir = installDir + L".update";

  upd::UpdaterLaunchSpec spec;
  spec.installDir = installDir;
  spec.stagingDir = workDir + L"@@staging";
  spec.workDir = workDir;
  spec.manifestUrl = remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL");
  spec.platform = "windows";
  spec.installedVersion = narrow(kProductVersion);
  spec.healthLogPath = log_file_path();
  spec.logPath = workDir + L"@@updater.log";
  spec.serviceName = L"GNLinkSecureInput";
  spec.registryRoot = L"HKLM@@SOFTWARE@@Microsoft@@Windows@@CurrentVersion@@Uninstall@@GNLink";
  spec.parentPid = GetCurrentProcessId();
  // No ready event: this process is not elevated and does not need to wait for permission to
  // leave. The updater stops the client the same way it stops any other product process.
  spec.readyEventName.clear();

  std::wstring parameters;
  for (const std::wstring& arg : upd::updater_arguments(spec)) {
    if (!parameters.empty()) parameters += L" ";
    parameters += L"@Q";
    parameters += arg;
    parameters += L"@Q";
  }
  const std::wstring exePath = installDir + L"@@GNLinkUpdater.exe";

  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
  info.lpVerb = L"runas";  // one consent prompt, and only one
  info.lpFile = exePath.c_str();
  info.lpParameters = parameters.c_str();
  info.lpDirectory = installDir.c_str();
  info.nShow = SW_SHOWNORMAL;

  const BOOL launched = ShellExecuteExW(&info);
  const upd::ElevationOutcome outcome =
      upd::elevation_outcome(launched != FALSE, static_cast<uint32_t>(GetLastError()));
  if (info.hProcess) CloseHandle(info.hProcess);

  switch (outcome) {
    case upd::ElevationOutcome::Launched:
      log_line("update: started the updater for " + availableVersion);
      post_status("idle", "업데이트를 시작했습니다. 잠시 후 프로그램이 다시 시작됩니다.");
      return;
    case upd::ElevationOutcome::Cancelled:
      // Recorded, not shown. The user declined and still has a working program.
      log_line("update: the user declined the elevation prompt");
      return;
    case upd::ElevationOutcome::Failed:
      log_line("update: could not start the updater");
      post_status("error", "업데이트를 시작하지 못했습니다. 설치된 버전은 그대로입니다.");
      return;
  }
}

void start_update_check() {'''
assert old in d, 'client insertion anchor'
d = d.replace(old, new.replace('@@', BS * 2).replace('@Q', BS + '"'), 1)

# --- remember what the check found, so the page's request has something to act on
old = '''std::mutex gUpdateNoticeMu;
std::string gPendingUpdateNotice;
bool gPageReady = false;'''
new = '''std::mutex gUpdateNoticeMu;
std::string gPendingUpdateNotice;
bool gPageReady = false;
/** The version the last check found, empty when there is nothing to install. */
std::string gAvailableVersion;'''
assert old in d, 'globals anchor'
d = d.replace(old, new, 1)

old = '''        const ShellUpdateNotice notice = shell_update_notice(
            upd::check_outcome_name(result.outcome), result.availableVersion, result.detail);
        log_line(notice.logLine);
        if (notice.show) deliver_update_notice(shell_status_json("idle", notice.text));'''
new = '''        const ShellUpdateNotice notice = shell_update_notice(
            upd::check_outcome_name(result.outcome), result.availableVersion, result.detail);
        log_line(notice.logLine);
        if (notice.show) {
          {
            std::lock_guard<std::mutex> lock(gUpdateNoticeMu);
            gAvailableVersion = result.availableVersion;
          }
          deliver_update_notice(shell_status_json("idle", notice.text));
        }'''
assert old in d, 'callback anchor'
d = d.replace(old, new, 1)

# --- the page can ask for it
old = '''  if (type == "settings") {'''
new = '''  if (type == "update") {
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
  }
  if (type == "settings") {'''
assert old in d, 'settings handler anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('client elevated launch wired')
