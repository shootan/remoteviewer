#include "secure_input_protocol.hpp"
#include "update_process_targets.hpp"

#include <windows.h>
#include <tlhelp32.h>

namespace remote60::native_poc::update {
namespace {

struct WindowCloser {
  DWORD pid = 0;
  bool posted = false;
  // Look without touching. The same walk answers two different questions -- "close this" and
  // "could this be closed" -- and the second one has to be asked BEFORE deciding whom to ask,
  // which is the whole point of routing a windowless process to its supervisor.
  bool dryRun = false;
};

BOOL CALLBACK close_top_level(HWND hwnd, LPARAM param) {
  auto* closer = reinterpret_cast<WindowCloser*>(param);
  DWORD owner = 0;
  GetWindowThreadProcessId(hwnd, &owner);
  if (owner == closer->pid) {
    if (!closer->dryRun) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    closer->posted = true;
  }
  return TRUE;
}

/** Case-insensitive comparison of a full path's file name against one name. */
bool image_leaf_is(const std::wstring& imagePath, const wchar_t* name) {
  const size_t slash = imagePath.find_last_of(L"\\/");
  const std::wstring leaf = slash == std::wstring::npos ? imagePath : imagePath.substr(slash + 1);
  return _wcsicmp(leaf.c_str(), name) == 0;
}

/**
 * Asks the SCM to stop a service, and says nothing it cannot support.
 *
 * Three answers collapsed into two would be the same mistake as everywhere else here: a service
 * that is already stopped is a success, a stop that was accepted is a success, and being unable
 * to open or control it is NOT -- it is the case where we do not know, and an update that treats
 * it as success proceeds over a running service.
 */
bool request_service_stop(const wchar_t* serviceName) {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) return false;
  SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (!service) {
    const DWORD err = GetLastError();
    CloseServiceHandle(manager);
    // Not installed at all: there is nothing to stop, which is not a failure to ask.
    return err == ERROR_SERVICE_DOES_NOT_EXIST;
  }

  SERVICE_STATUS status{};
  bool ok = ControlService(service, SERVICE_CONTROL_STOP, &status) != FALSE;
  if (!ok && GetLastError() == ERROR_SERVICE_NOT_ACTIVE) ok = true;  // already where we want it
  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return ok;
}

/** Whether this process owns a top-level window, i.e. whether WM_CLOSE is a possible request. */
bool process_has_top_level_window(uint32_t pid) {
  WindowCloser probe{static_cast<DWORD>(pid), false, true};
  EnumWindows(close_top_level, reinterpret_cast<LPARAM>(&probe));
  return probe.posted;
}

}  // namespace

const std::vector<std::wstring>& product_image_names() {
  // The same six executables the installer's payload carries. GNLinkClient and GNLinkViewer are
  // in this list on purpose: the installer's own stop list omits them while its payload includes
  // them, which is exactly the mismatch that leaves a mixed-version directory behind
  // (ledger I01).
  static const std::vector<std::wstring> names = {
      L"GNLinkHost.exe",   L"GNLinkStream.exe", L"GNLinkCapture.exe",
      L"GNLinkInputService.exe", L"GNLinkClient.exe", L"GNLinkViewer.exe",
      // The installer counts too, now that it is a member of the update package. If one is
      // running, an uninstall may be in progress -- replacing its binary underneath that is worse
      // than not updating, and the identity check means a stale PID cannot be mistaken for it.
      L"GNLinkSetup.exe",
  };
  return names;
}

std::vector<std::wstring> product_payload_names() {
  std::vector<std::wstring> names = product_image_names();
  // Replaced but never stopped -- see the header. The destination is installDir\\GNLinkUpdater.exe
  // while the process runs from the working copy outside it, which is what makes this safe;
  // UpdateEffectsConfig::validate() checks that as full destination paths, not names.
  names.push_back(L"GNLinkUpdater.exe");
  names.push_back(L"ui\\shell.html");
  names.push_back(L"ui\\macro.html");
  return names;
}

bool product_payload_list_contract() {
  const std::vector<std::wstring> payload = product_payload_names();
  for (const std::wstring& stopped : product_image_names()) {
    bool found = false;
    for (const std::wstring& name : payload) {
      if (_wcsicmp(stopped.c_str(), name.c_str()) == 0) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

std::vector<ProcessTarget> enumerate_product_processes(const std::vector<std::wstring>& imageNames) {
  std::vector<ProcessTarget> targets;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return targets;

  const DWORD self = GetCurrentProcessId();
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      // The updater never stops itself, whatever it happens to be called.
      if (entry.th32ProcessID == self) continue;
      for (const std::wstring& name : imageNames) {
        if (_wcsicmp(entry.szExeFile, name.c_str()) != 0) continue;
        // The name is only how a candidate is found. What is carried forward is a full identity,
        // so that a PID reused between here and Quiesce cannot be mistaken for this process.
        ProcessTarget target;
        IdentityFailure why = IdentityFailure::None;
        if (capture_process_identity(static_cast<uint32_t>(entry.th32ProcessID), &target, &why)) {
          target.parentPid = static_cast<uint32_t>(entry.th32ParentProcessID);
          target.hasWindow = process_has_top_level_window(target.pid);
          targets.push_back(std::move(target));
        } else if (why == IdentityFailure::Unknowable) {
          // Running, and this updater cannot identify it. This used to be dropped here, silently,
          // which removed it from the list before anything downstream could object: nothing asked
          // it to stop, nothing waited for it, and the swap went ahead over a process still
          // holding its own files. Both places that would have refused -- request_process_stop and
          // Quiesce, which each treat access-denied as "not knowing" rather than "gone" -- never
          // saw it at all.
          //
          // So it is carried forward, marked, and the swap refuses with something it can name.
          ProcessTarget unknown;
          unknown.pid = static_cast<uint32_t>(entry.th32ProcessID);
          unknown.imagePath = entry.szExeFile;  // the name is all that could be read
          unknown.creationTime = 0;
          unknown.parentPid = static_cast<uint32_t>(entry.th32ParentProcessID);
          unknown.hasWindow = false;
          unknown.identityKnown = false;
          targets.push_back(std::move(unknown));
        }
        // IdentityFailure::Gone falls through deliberately: the pid stopped being a process
        // between the snapshot and the open, which is exactly what stopping it would achieve.
        // Treating that as a blocker would let an ordinary exit cancel an update.
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return targets;
}

bool request_process_stop(const ProcessTarget& target) {
  // Open, verify, and then KEEP THE HANDLE OPEN for the rest of this function.
  //
  // Verifying and then closing would leave a window: between the check and the WM_CLOSE the
  // process could exit and its PID be reused, and the message would go to a stranger. An open
  // handle is what closes that window -- Windows will not recycle a PID while a handle to it
  // exists, so holding one makes the identity checked above stay true for as long as it is held.
  SetLastError(0);
  HANDLE held = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
  if (!held) {
    // "Could not open" was being read as "already gone", and the two are not the same answer.
    // ERROR_INVALID_PARAMETER is a pid that is no longer a process -- nothing to ask, so the ask
    // succeeded vacuously. ERROR_ACCESS_DENIED is a process we cannot even look at, and calling
    // that success means the swap proceeds over something that may still be holding the files it
    // is about to replace. Access denied is not death; it is not knowing.
    if (GetLastError() == ERROR_ACCESS_DENIED) return false;
    return true;
  }
  if (!process_identity_matches(held, target)) {
    CloseHandle(held);
    return true;  // the process we meant has exited
  }
  struct HandleGuard {
    HANDLE h;
    ~HandleGuard() { if (h) CloseHandle(h); }
  } guard{held};

  const uint32_t pid = target.pid;

  // A service is not asked the way a program is. It has no window and no console, so both
  // mechanisms below are inapplicable by construction -- and it does have a supervisor: the SCM.
  // Asking the SCM is the polite request for a service, exactly as WM_CLOSE is for a window.
  if (image_leaf_is(target.imagePath, L"GNLinkInputService.exe")) {
    return request_service_stop(kSecureInputServiceName);
  }

  // A GUI process gets WM_CLOSE on its top-level windows.
  WindowCloser closer{static_cast<DWORD>(pid), false};
  EnumWindows(close_top_level, reinterpret_cast<LPARAM>(&closer));
  if (closer.posted) return true;

  // A windowless process, and nobody we asked owns it.
  //
  // This almost never succeeds, and it is worth being exact about why: the second argument is a
  // process GROUP id, not a pid. It works only if this pid happens to be a group leader AND
  // shares our console -- and the product's supervisor starts its children with neither. For
  // years this was the only path for GNLinkStream and GNLinkCapture, which is why an update could
  // not proceed while either was running.
  //
  // It is kept for the case it was always meant for: a windowless process that is NOT a child of
  // anything we are already asking, where there is nothing else to try. PrepareForSwap no longer
  // routes owned children here.
  if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(pid))) return true;

  // Nothing worked. Reported as "could not ask", which abandons the update before the disk is
  // touched. Deliberately NOT escalated to TerminateProcess: killing a streaming host mid-session
  // to install an update is worse than not installing it.
  return false;
}

}  // namespace remote60::native_poc::update
