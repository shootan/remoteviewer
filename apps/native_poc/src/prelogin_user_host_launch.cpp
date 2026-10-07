// See prelogin_user_host_launch.hpp (prelogin-ui r7, H1).

#include "prelogin_user_host_launch.hpp"

#include <userenv.h>

#include <cwchar>

#pragma comment(lib, "userenv.lib")

namespace remote60::native_poc::prelogin {

bool BuildUserHostEnvironment(HANDLE token, const std::wstring& nonce, std::vector<wchar_t>* out, DWORD* win32) {
  out->clear();
  if (win32) *win32 = 0;
  LPVOID block = nullptr;
  if (!CreateEnvironmentBlock(&block, token, FALSE) || !block) {
    if (win32) *win32 = GetLastError();
    return false;
  }
  constexpr wchar_t kNonce[] = L"REMOTE60_PRELOGIN_READY_NONCE=";
  const size_t nonceLen = wcslen(kNonce);
  for (const wchar_t* p = static_cast<const wchar_t*>(block); *p; p += wcslen(p) + 1) {
    if (_wcsnicmp(p, kNonce, nonceLen) == 0) continue;
    out->insert(out->end(), p, p + wcslen(p) + 1);
  }
  DestroyEnvironmentBlock(block);
  const std::wstring mine = kNonce + nonce;
  out->insert(out->end(), mine.begin(), mine.end());
  out->push_back(L'\0');
  out->push_back(L'\0');
  return true;
}

bool StartUserHostSuspended(HANDLE token, const std::wstring& exe, const std::vector<wchar_t>& environment,
                            const wchar_t* desktop, PROCESS_INFORMATION* pi, DWORD* win32) {
  if (win32) *win32 = 0;
  if (environment.size() < 2) {
    if (win32) *win32 = ERROR_INVALID_PARAMETER;
    return false;
  }
  const size_t slash = exe.find_last_of(L"\\/");
  const std::wstring folder = slash == std::wstring::npos ? std::wstring() : exe.substr(0, slash);
  std::wstring command = L"\"" + exe + L"\" --tray";
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  std::wstring desk = desktop ? desktop : L"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.lpDesktop = desk.empty() ? nullptr : desk.data();
  *pi = PROCESS_INFORMATION{};
  // lpEnvironment = the block, lpCurrentDirectory = the Host's own folder (r7 H1: these two were swapped).
  const BOOL created = CreateProcessAsUserW(token, nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                                            CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE | CREATE_SUSPENDED,
                                            const_cast<wchar_t*>(environment.data()),
                                            folder.empty() ? nullptr : folder.c_str(), &startup, pi);
  if (!created && win32) *win32 = GetLastError();
  return created != FALSE;
}

}  // namespace remote60::native_poc::prelogin
