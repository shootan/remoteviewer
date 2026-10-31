import io

BS = chr(92)
p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()

# --- include
old = '#include "update_check.hpp"'
assert old in d, 'update_check include'
d = d.replace(old, old + '\n#include "update_handoff.hpp"', 1)

# --- a message for "the handoff did not happen"
old = 'constexpr UINT kUpdateCheckDoneMessage = WM_APP + 3;'
new = '''constexpr UINT kUpdateCheckDoneMessage = WM_APP + 3;
// The waiter thread's verdict. Posted rather than acted on directly because deciding to exit is
// a UI-thread decision, and because the wait can outlive the dialog the user answered.
constexpr UINT kUpdateHandoffDoneMessage = WM_APP + 4;'''
assert old in d, 'message anchor'
d = d.replace(old, new, 1)

# --- the launch itself, placed next to start_update_check
old = '''/** Signing in and listing hosts both talk to the network, so they never run on the UI thread. */'''
if old not in d:
    old = 'void start_streaming() {'
assert old in d, 'insertion anchor'

new = '''/**
 * Hands the update over to GNLinkUpdater.exe and waits for permission to exit.
 *
 * The host is one of the files being replaced, so it has to leave for the swap to happen. When it
 * leaves is the whole question. If it exits as soon as the updater starts and the update then
 * fails -- unreachable server, hash mismatch, a file that will not move -- there is nothing left
 * running to put the product back, and what the user sees is that their program is gone. So the
 * updater signals only after it holds the lock and has a verified download, and only then does
 * this window close. Everything before that point is undone by doing nothing.
 *
 * Every way of not getting that signal is treated the same, on purpose: the host keeps running
 * and the update does not happen this time.
 */
void start_update_handoff(HWND window) {
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
  spec.readyEventName =
      upd::make_ready_event_name(GetCurrentProcessId(), GetTickCount64());

  // Created before the updater starts, so there is no window in which it signals an event nobody
  // is listening on. Manual-reset: the waiter must not consume it before it has been observed.
  HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, spec.readyEventName.c_str());
  if (!readyEvent) {
    append_host_app_log("[host-app] update: could not create the ready event");
    MessageBoxW(window, L"업데이트를 시작하지 못했습니다.", kProductName, MB_OK | MB_ICONWARNING);
    return;
  }

  std::wstring commandLine = L"@\\"" + installDir + L"@@GNLinkUpdater.exe@\\"";
  for (const std::wstring& arg : upd::updater_arguments(spec)) {
    commandLine += L" @\\"";
    commandLine += arg;
    commandLine += L"@\\"";
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  // The host is already elevated, so the child inherits the token and no prompt appears.
  // CREATE_BREAKAWAY_FROM_JOB because an updater inside this process's job object would die when
  // this process is asked to stop -- which is the next thing that happens (design 3.2 (2)).
  BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                CREATE_BREAKAWAY_FROM_JOB, nullptr, installDir.c_str(), &si, &pi);
  if (!started) {
    started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                             installDir.c_str(), &si, &pi);
    if (started) append_host_app_log("[host-app] update: the updater could not break away from a job object");
  }
  if (!started) {
    append_host_app_log("[host-app] update: could not start the updater (error " +
                        std::to_string(GetLastError()) + ")");
    CloseHandle(readyEvent);
    MessageBoxW(window, L"업데이터를 시작하지 못했습니다.", kProductName, MB_OK | MB_ICONWARNING);
    return;
  }
  CloseHandle(pi.hThread);
  append_host_app_log("[host-app] update: handed over to the updater, waiting for its signal");

  // Waits off the UI thread: this window has to keep responding while the download runs, and the
  // user may still be using the product right up to the moment it is replaced.
  std::thread([window, readyEvent, process = pi.hProcess]() {
    HANDLE handles[2] = {readyEvent, process};
    // Long enough for a download on a slow link, short enough that a wedged updater does not keep
    // the host waiting forever. Running out is not an error -- it just means no update today.
    const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, 10 * 60 * 1000);
    const bool signalled = (waited == WAIT_OBJECT_0);
    const bool exited = (waited == WAIT_OBJECT_0 + 1);
    const bool timedOut = (waited == WAIT_TIMEOUT);

    std::string why;
    const upd::HandoffVerdict verdict =
        upd::handoff_verdict(signalled, exited, timedOut, &why);
    CloseHandle(readyEvent);
    CloseHandle(process);

    auto* detail = new std::string(std::string(upd::handoff_verdict_name(verdict)) + ": " + why);
    PostMessageW(window, kUpdateHandoffDoneMessage,
                 static_cast<WPARAM>(verdict == upd::HandoffVerdict::ExitNow),
                 reinterpret_cast<LPARAM>(detail));
  }).detach();
}

''' + old
d = d.replace(old, new.replace('@@', BS * 2).replace('@' + BS + '"', BS + '"'), 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('host handoff launcher added')
