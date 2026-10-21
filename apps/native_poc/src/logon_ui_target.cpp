// See logon_ui_target.hpp. The product's OS questions (moved from prelogin_cred_host.cpp, unchanged).

#include "logon_ui_target.hpp"

#include <windows.h>

#include <UIAutomation.h>

#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace remote60::native_poc::logon_ui {
namespace {

bool process_is_local_system(HANDLE proc) {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(proc, TOKEN_QUERY, &tok)) return false;
  BYTE buf[SECURITY_MAX_SID_SIZE + sizeof(TOKEN_USER)] = {};
  DWORD n = 0;
  bool system = false;
  if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n)) {
    BYTE sys[SECURITY_MAX_SID_SIZE];
    DWORD sysSize = sizeof(sys);
    system = CreateWellKnownSid(WinLocalSystemSid, nullptr, sys, &sysSize) &&
             EqualSid(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, sys);
  }
  CloseHandle(tok);
  return system;
}

// r7 H5: the element that has the keyboard focus is a password field, in process `pid`.
bool focused_password_field_in(DWORD pid) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool ok = false;
  IUIAutomation* uia = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) && uia) {
    IUIAutomationElement* focused = nullptr;
    if (SUCCEEDED(uia->GetFocusedElement(&focused)) && focused) {
      BOOL isPassword = FALSE;
      int owner = 0;
      ok = SUCCEEDED(focused->get_CurrentIsPassword(&isPassword)) && isPassword &&
           SUCCEEDED(focused->get_CurrentProcessId(&owner)) && static_cast<DWORD>(owner) == pid;
      focused->Release();
    }
    uia->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return ok;
}

}  // namespace

std::wstring InputDesktopName() {
  std::wstring name;
  if (HDESK d = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS)) {
    wchar_t buf[128] = {};
    DWORD need = 0;
    if (GetUserObjectInformationW(d, UOI_NAME, buf, sizeof(buf), &need)) name = buf;
    CloseDesktop(d);
  }
  return name;
}

uint64_t FocusedPasswordFieldIdentity(void* window, uint32_t pid) {
  if (!window || pid == 0 || !focused_password_field_in(pid)) return 0;
  return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(window)) & 0xFFFFFFFFull) | (static_cast<uint64_t>(pid) << 32) | 1ull;
}

uint64_t PasswordFieldIdentity() {
  wchar_t sysDir[MAX_PATH] = {};
  const UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return 0;
  const std::wstring logonUi = std::wstring(sysDir, len) + L"\\LogonUI.exe";
  HWND fg = GetForegroundWindow();
  DWORD pid = 0;
  if (!fg || !GetWindowThreadProcessId(fg, &pid) || pid == 0) return 0;
  DWORD mine = 0, theirs = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &mine) || !ProcessIdToSessionId(pid, &theirs) || mine != theirs) return 0;
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return 0;
  wchar_t path[MAX_PATH * 2] = {};
  DWORD plen = MAX_PATH * 2;
  const bool image = QueryFullProcessImageNameW(p, 0, path, &plen) != FALSE &&
                     CompareStringOrdinal(path, static_cast<int>(plen), logonUi.c_str(), static_cast<int>(logonUi.size()),
                                          TRUE) == CSTR_EQUAL;
  const bool system = image && process_is_local_system(p);
  CloseHandle(p);
  // r7 H5: and the focus is in a password field of it -- not the user-name box of the same window.
  return system ? FocusedPasswordFieldIdentity(fg, pid) : 0;
}

bool TypeUnicode(const uint16_t* units, uint16_t count, uint32_t* err) {
  // One SendInput for all of it: the system does not interleave other input into one call's events.
  std::vector<INPUT> in(static_cast<size_t>(count) * 2);
  for (uint16_t i = 0; i < count; ++i) {
    for (int k = 0; k < 2; ++k) {
      INPUT& e = in[static_cast<size_t>(i) * 2 + k];
      e.type = INPUT_KEYBOARD;
      e.ki.wScan = units[i];
      e.ki.dwFlags = KEYEVENTF_UNICODE | (k == 1 ? KEYEVENTF_KEYUP : 0);
    }
  }
  const UINT sent = in.empty() ? 0 : SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
  const DWORD e = GetLastError();
  if (!in.empty()) SecureZeroMemory(in.data(), in.size() * sizeof(INPUT));
  if (sent != in.size()) {
    *err = e ? e : ERROR_GEN_FAILURE;
    return false;
  }
  return true;
}

}  // namespace remote60::native_poc::logon_ui
