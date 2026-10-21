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

namespace {
// The typing, optionally preceded by Ctrl+A in the same call.
bool send_units(const uint16_t* units, uint16_t count, bool selectAllFirst, uint32_t* err) {
  // One SendInput for all of it: the system does not interleave other input into one call's events.
  const size_t lead = selectAllFirst ? 4 : 0;
  std::vector<INPUT> in(lead + static_cast<size_t>(count) * 2);
  if (selectAllFirst) {
    const WORD vks[4] = {VK_CONTROL, 'A', 'A', VK_CONTROL};
    for (size_t i = 0; i < 4; ++i) {
      in[i].type = INPUT_KEYBOARD;
      in[i].ki.wVk = vks[i];
      in[i].ki.dwFlags = (i >= 2) ? KEYEVENTF_KEYUP : 0;
    }
  }
  for (uint16_t i = 0; i < count; ++i) {
    for (int k = 0; k < 2; ++k) {
      INPUT& e = in[lead + static_cast<size_t>(i) * 2 + k];
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

// The focused element, if it is a password field of `pid` with a writable ValuePattern: emptied. 1 emptied,
// 0 no such pattern (the caller selects all instead), -1 the pattern refused (nothing is typed then).
int empty_by_value_pattern(DWORD pid) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  int result = 0;
  IUIAutomation* uia = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) && uia) {
    IUIAutomationElement* focused = nullptr;
    if (SUCCEEDED(uia->GetFocusedElement(&focused)) && focused) {
      BOOL isPassword = FALSE;
      int owner = 0;
      if (SUCCEEDED(focused->get_CurrentIsPassword(&isPassword)) && isPassword &&
          SUCCEEDED(focused->get_CurrentProcessId(&owner)) && static_cast<DWORD>(owner) == pid) {
        IUIAutomationValuePattern* value = nullptr;
        if (SUCCEEDED(focused->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value) {
          BOOL readOnly = TRUE;
          if (SUCCEEDED(value->get_CurrentIsReadOnly(&readOnly)) && !readOnly) {
            BSTR empty = SysAllocString(L"");
            result = (empty && SUCCEEDED(value->SetValue(empty))) ? 1 : -1;
            if (empty) SysFreeString(empty);
          }
          value->Release();
        }
      } else {
        result = -1;  // the focus moved off the verified field: nothing is typed
      }
      focused->Release();
    }
    uia->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return result;
}

}  // namespace

bool TypeUnicode(const uint16_t* units, uint16_t count, uint32_t* err) { return send_units(units, count, false, err); }

bool ReplaceWithUnicode(uint32_t pid, const uint16_t* units, uint16_t count, uint32_t* err) {
  const int emptied = empty_by_value_pattern(static_cast<DWORD>(pid));
  if (emptied < 0) {
    *err = ERROR_INVALID_DATA;
    return false;
  }
  return send_units(units, count, /*selectAllFirst=*/emptied == 0, err);
}

}  // namespace remote60::native_poc::logon_ui
