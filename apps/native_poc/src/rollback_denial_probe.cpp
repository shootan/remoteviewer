#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// Why does the rollback's DeleteFile say ACCESS_DENIED? (updater-health-gate Phase 1, D2)
//
// The 2026-09-21 rollback failed on two files with win32=5:
//
//   rollback-trace remove-live file=GNLinkStream.exe win32=5
//   rollback-trace remove-live file=GNLinkInputService.exe win32=5
//
// Five is what you get from a running executable, and five is also what you get from an ACL that
// refuses you. The updater cannot tell them apart today, and it needs to: one is "wait for it to
// stop", the other is "stop and keep the installation recoverable". Codex asked for exactly this
// distinction before anything is changed.
//
// They separate at the open, but not the way you would guess -- this is what the measurement below
// actually found, after the guess was wrong. A running image does NOT refuse a DELETE-access open:
// it is granted, even with no sharing at all. What refuses is the unlink, which reports five. An
// ACL, by contrast, refuses the open itself, with the same five.
//
// So the discriminator is the open:
//   open for DELETE succeeds, DeleteFile says 5  ->  a running image; it will go when it stops
//   open for DELETE fails with 5                 ->  a permission problem; waiting changes nothing
//
// Non-destructive by construction: it opens handles and closes them. Nothing is deleted, renamed,
// moved or stopped, and no process is touched. It is safe to run against the live installation,
// which is the only place the question can be answered.

#include <windows.h>
#include <tlhelp32.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(n > 0 ? n - 1 : 0, '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
  return out;
}

const char* error_name(DWORD err) {
  switch (err) {
    case 0: return "ok";
    case ERROR_FILE_NOT_FOUND: return "not-found (2)";
    case ERROR_ACCESS_DENIED: return "ACCESS_DENIED (5)";
    case ERROR_SHARING_VIOLATION: return "SHARING_VIOLATION (32)";
    case ERROR_LOCK_VIOLATION: return "LOCK_VIOLATION (33)";
    default: return "other";
  }
}

/** Opens for DELETE access and closes. Never deletes: FILE_FLAG_DELETE_ON_CLOSE is not used. */
DWORD probe_open(const std::wstring& path, DWORD share) {
  SetLastError(0);
  HANDLE h = CreateFileW(path.c_str(), DELETE, share, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  const DWORD err = (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  return err;
}

bool image_is_running(const std::wstring& leaf) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (Process32FirstW(snap, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, leaf.c_str()) == 0) { found = true; break; }
    } while (Process32NextW(snap, &entry));
  }
  CloseHandle(snap);
  return found;
}

}  // namespace

/**
 * The same question where the ACL cannot answer it for us.
 *
 * Run without elevation, every file under %ProgramFiles% returns ACCESS_DENIED whether or not it
 * is running -- the ACL refuses the DELETE access before the sharing state is ever consulted, so
 * the live installation cannot tell us what a running image does. The updater is elevated
 * (CMakeLists /MANIFESTUAC requireAdministrator), so the ACL is not what it meets; the running
 * image is.
 *
 * So the mechanism is measured on a copy in a directory this process owns, started and stopped by
 * this process. The product is not touched.
 */
void probe_owned_copy(const std::wstring& selfPath) {
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"gnlink-denial-" +
                           std::to_wstring(GetCurrentProcessId());
  CreateDirectoryW(dir.c_str(), nullptr);
  const std::wstring copy = dir + L"\\running-image.exe";
  if (!CopyFileW(selfPath.c_str(), copy.c_str(), FALSE)) {
    std::cout << "\n  (could not place a copy to test with; skipping)\n";
    return;
  }

  std::cout << "\n  a copy this process owns, in a directory it owns:\n";
  std::cout << "    before it runs   no-sharing=" << error_name(probe_open(copy, 0))
            << "  full-sharing="
            << error_name(probe_open(copy, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE))
            << "\n";

  std::wstring cmd = L"\"" + copy + L"\" --sleep";
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
    std::cout << "    (could not start it; skipping)\n";
    DeleteFileW(copy.c_str());
    RemoveDirectoryW(dir.c_str());
    return;
  }
  CloseHandle(pi.hThread);
  Sleep(400);

  SetLastError(0);
  const bool deleted = DeleteFileW(copy.c_str()) != FALSE;
  const DWORD deleteErr = deleted ? 0 : GetLastError();
  std::cout << "    while running    no-sharing=" << error_name(probe_open(copy, 0))
            << "  full-sharing="
            << error_name(probe_open(copy, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE))
            << "\n";
  std::cout << "    DeleteFileW on it while running: "
            << (deleted ? "SUCCEEDED" : error_name(deleteErr)) << "\n";

  TerminateProcess(pi.hProcess, 0);
  WaitForSingleObject(pi.hProcess, 3000);
  CloseHandle(pi.hProcess);
  Sleep(200);

  SetLastError(0);
  const bool deletedAfter = DeleteFileW(copy.c_str()) != FALSE;
  std::cout << "    DeleteFileW after it exited:     "
            << (deletedAfter ? "SUCCEEDED" : error_name(GetLastError())) << "\n";
  DeleteFileW(copy.c_str());
  RemoveDirectoryW(dir.c_str());
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--sleep") {
    Sleep(30000);
    return 0;
  }

  std::wstring install = L"C:\\Program Files\\GNLink";
  if (argc > 1) {
    const std::string arg(argv[1]);
    install.assign(arg.begin(), arg.end());
  }

  std::cout << "rollback_denial_probe  install=" << narrow(install) << "\n";
  std::cout << "  Opens each payload file for DELETE and closes it. Deletes nothing.\n\n";
  std::cout << "  file                     running  no-sharing              full-sharing\n";
  std::cout << "  ------------------------ -------  ----------------------  ----------------------\n";

  const wchar_t* names[] = {L"GNLinkHost.exe",   L"GNLinkStream.exe",  L"GNLinkCapture.exe",
                            L"GNLinkInputService.exe", L"GNLinkClient.exe", L"GNLinkViewer.exe",
                            L"GNLinkSetup.exe",  L"GNLinkUpdater.exe"};

  for (const wchar_t* leaf : names) {
    const std::wstring path = install + L"\\" + leaf;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
    const DWORD exclusive = probe_open(path, 0);
    const DWORD shared = probe_open(path, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    std::string line = "  " + narrow(leaf);
    line.resize(27, ' ');
    line += image_is_running(leaf) ? "yes      " : "no       ";
    std::string a = error_name(exclusive);
    a.resize(24, ' ');
    line += a + error_name(shared);
    std::cout << line << "\n";
  }

  wchar_t self[MAX_PATH]{};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  probe_owned_copy(self);

  std::cout << "\n  Reading this: an open for DELETE that SUCCEEDS while DeleteFile reports five is\n"
               "  a running image -- it will go when the process does. An open for DELETE that itself\n"
               "  fails with five is a permission problem, and no amount of waiting changes it. The\n"
               "  rollback calls DeleteFile only, which reports five for both, so it cannot tell a\n"
               "  process it should wait for from a file it will never be allowed to touch.\n";
  return 0;
}
