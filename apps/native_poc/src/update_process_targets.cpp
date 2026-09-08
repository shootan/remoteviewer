#include "update_process_targets.hpp"

#include <windows.h>
#include <tlhelp32.h>

namespace remote60::native_poc::update {
namespace {

struct WindowCloser {
  DWORD pid = 0;
  bool posted = false;
};

BOOL CALLBACK close_top_level(HWND hwnd, LPARAM param) {
  auto* closer = reinterpret_cast<WindowCloser*>(param);
  DWORD owner = 0;
  GetWindowThreadProcessId(hwnd, &owner);
  if (owner == closer->pid) {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    closer->posted = true;
  }
  return TRUE;
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
  };
  return names;
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
        if (capture_process_identity(static_cast<uint32_t>(entry.th32ProcessID), &target)) {
          targets.push_back(std::move(target));
        }
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return targets;
}

bool request_process_stop(const ProcessTarget& target) {
  // Confirm the PID still belongs to the process that was enumerated before asking it to do
  // anything. Without this, a reused PID would receive a close request meant for something else.
  {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
    if (!h) return true;  // already gone; nothing to ask
    const bool same = process_identity_matches(h, target);
    CloseHandle(h);
    if (!same) return true;  // the process we meant has exited
  }

  const uint32_t pid = target.pid;
  // A GUI process gets WM_CLOSE on its top-level windows.
  WindowCloser closer{static_cast<DWORD>(pid), false};
  EnumWindows(close_top_level, reinterpret_cast<LPARAM>(&closer));
  if (closer.posted) return true;

  // The console-subsystem children (GNLinkStream, GNLinkCapture, GNLinkViewer) have no window.
  // A CTRL_BREAK to their group is the closest thing to a polite request; it only works when
  // they share a console with us, which the product's own supervisor does not guarantee.
  if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(pid))) return true;

  // Nothing worked. Reported as "could not ask", which abandons the update before the disk is
  // touched. Deliberately NOT escalated to TerminateProcess: killing a streaming host mid-session
  // to install an update is worse than not installing it.
  return false;
}

}  // namespace remote60::native_poc::update
