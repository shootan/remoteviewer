// prelogin-ui r7 (H1): the service's launch of the user's GNLinkHost, made the way the service makes it.
//
// The service builds the user's environment with the launch nonce (BuildUserHostEnvironment) and starts
// "<exe> --tray" with it (StartUserHostSuspended). The call it used to make passed the block in the
// working-directory slot: the Host got neither its environment nor its nonce. This test calls the same
// two functions -- with this process's own token instead of the logged-on user's (the one difference) --
// on a copy of itself staged as GNLinkHost.exe, and the child writes down what it received: the nonce,
// a variable of the user's own environment, and its working directory.
//
// Negative control in the same run: the old argument order (the block as the working directory) does not
// start a child that has the nonce.

#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "e2e_isolation.hpp"
#include "prelogin_user_host_launch.hpp"

namespace pl = remote60::native_poc::prelogin;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
std::string narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }
std::wstring self_path() {
  wchar_t p[MAX_PATH * 2] = {};
  GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
  return p;
}
std::wstring folder_of(const std::wstring& p) { return p.substr(0, p.find_last_of(L"\\/")); }

// The child: what the launch gave it, written beside itself.
int child_report() {
  wchar_t nonce[128] = {}, profile[MAX_PATH] = {}, cwd[MAX_PATH] = {};
  GetEnvironmentVariableW(L"REMOTE60_PRELOGIN_READY_NONCE", nonce, 128);
  GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
  GetCurrentDirectoryW(MAX_PATH, cwd);
  std::wofstream out(folder_of(self_path()) + L"\\child_report.txt");
  out << L"nonce=" << nonce << L"\n" << L"userprofile=" << profile << L"\n" << L"cwd=" << cwd << L"\n";
  return 0;
}

std::wstring read_report(const std::wstring& path) {
  std::wifstream in(path);
  std::wstringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

HANDLE own_primary_token() {
  HANDLE self = nullptr, primary = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &self)) return nullptr;
  DuplicateTokenEx(self, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &primary);
  CloseHandle(self);
  return primary;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--tray") return child_report();  // started as "GNLinkHost.exe --tray"
  }
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the service's user-Host launch (prelogin-ui r7, H1)\n\n");
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"user_host_launch")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring dir = staging.path();
  const std::wstring hostExe = dir + L"GNLinkHost.exe";
  check("a copy of this test is staged as GNLinkHost.exe", CopyFileW(self_path().c_str(), hostExe.c_str(), FALSE) != FALSE);
  HANDLE token = own_primary_token();
  check("a primary token (this process's own -- the service uses the logged-on user's)", token != nullptr);

  std::vector<wchar_t> env;
  DWORD err = 0;
  const bool built = token && pl::BuildUserHostEnvironment(token, L"nonce-7f3a", &env, &err);
  check("the user's environment is built, with the nonce", built, std::to_string(err));

  PROCESS_INFORMATION pi{};
  const bool started = built && pl::StartUserHostSuspended(token, hostExe, env, nullptr, &pi, &err);
  check("H1: the Host starts (suspended), the way the service starts it", started, std::to_string(err));
  if (started) {
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  const std::wstring report = read_report(dir + L"child_report.txt");
  check("H1: the Host received the launch nonce (its ready names this launch)",
        report.find(L"nonce=nonce-7f3a\n") != std::wstring::npos, narrow(report));
  wchar_t myProfile[MAX_PATH] = {};
  GetEnvironmentVariableW(L"USERPROFILE", myProfile, MAX_PATH);
  check("H1: ...the user's own environment (USERPROFILE from the user's block)",
        myProfile[0] && report.find(std::wstring(L"userprofile=") + myProfile + L"\n") != std::wstring::npos);
  std::wstring folder = folder_of(hostExe);
  check("H1: ...and its own folder as the working directory",
        report.find(L"cwd=" + folder + L"\n") != std::wstring::npos || report.find(L"cwd=" + folder + L"\\\n") != std::wstring::npos,
        narrow(folder));
  DeleteFileW((dir + L"child_report.txt").c_str());

  // Negative control: the call as it was (lpEnvironment = nullptr, the block as lpCurrentDirectory).
  {
    std::wstring command = L"\"" + hostExe + L"\" --tray";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION old{};
    const BOOL oldStarted = CreateProcessAsUserW(token, nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                                                 CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE, nullptr,
                                                 reinterpret_cast<wchar_t*>(env.data()), &si, &old);
    if (oldStarted) {
      WaitForSingleObject(old.hProcess, 15000);
      CloseHandle(old.hThread);
      CloseHandle(old.hProcess);
    }
    const std::wstring oldReport = read_report(dir + L"child_report.txt");
    check("NEGATIVE: the old argument order starts no Host that has the nonce",
          oldReport.find(L"nonce=nonce-7f3a") == std::wstring::npos,
          oldStarted ? "started, without the nonce" : "did not start at all");
  }
  if (token) CloseHandle(token);
  check("the scratch directory is cleaned up", staging.Remove(), staging.why());
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
