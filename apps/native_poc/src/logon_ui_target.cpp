// See logon_ui_target.hpp. The product's OS questions (moved from prelogin_cred_host.cpp; r2 (Codex L4): the
// field's identity includes the focused element itself, and the replacing typing checks it around the clear).

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

// COM for one call on this thread, released however it ends.
struct ComScope {
  HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IUIAutomation* uia = nullptr;
  ComScope() {
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)))) uia = nullptr;
  }
  ~ComScope() {
    if (uia) uia->Release();
    if (SUCCEEDED(init)) CoUninitialize();
  }
};

// FNV-1a over the element's UIA RuntimeId: which element it is, not which window (r2 L4). 0 when unreadable.
uint32_t runtime_id_hash(IUIAutomationElement* e) {
  SAFEARRAY* ids = nullptr;
  if (FAILED(e->GetRuntimeId(&ids)) || !ids) return 0;
  uint32_t h = 2166136261u;
  LONG lo = 0, hi = -1;
  SafeArrayGetLBound(ids, 1, &lo);
  SafeArrayGetUBound(ids, 1, &hi);
  for (LONG i = lo; i <= hi; ++i) {
    int v = 0;
    if (FAILED(SafeArrayGetElement(ids, &i, &v))) {
      SafeArrayDestroy(ids);
      return 0;
    }
    for (int b = 0; b < 4; ++b) {
      h ^= static_cast<uint8_t>(v >> (8 * b));
      h *= 16777619u;
    }
  }
  SafeArrayDestroy(ids);
  return h ? h : 1u;
}

// The identity of the focused element if it is a password field of `pid`, else 0. The process id in the high
// half (the agent reads it back); the window and the element's RuntimeId hashed into the low half.
uint64_t identity_of_focused(IUIAutomation* uia, HWND window, DWORD pid, IUIAutomationElement** keep = nullptr) {
  if (!uia) return 0;
  IUIAutomationElement* focused = nullptr;
  if (FAILED(uia->GetFocusedElement(&focused)) || !focused) return 0;
  BOOL isPassword = FALSE;
  int owner = 0;
  uint64_t id = 0;
  if (SUCCEEDED(focused->get_CurrentIsPassword(&isPassword)) && isPassword &&
      SUCCEEDED(focused->get_CurrentProcessId(&owner)) && static_cast<DWORD>(owner) == pid) {
    const uint32_t element = runtime_id_hash(focused);
    if (element != 0) {
      uint32_t low = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window)) * 2654435761u ^ element;
      low |= 1u;
      id = (static_cast<uint64_t>(pid) << 32) | low;
    }
  }
  if (id != 0 && keep) {
    *keep = focused;
  } else {
    focused->Release();
  }
  return id;
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
  if (!window || pid == 0) return 0;
  ComScope com;
  return identity_of_focused(com.uia, static_cast<HWND>(window), pid);
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

std::vector<ReleaseStep> PendingReleases(bool selectAllFirst, const uint16_t* units, uint16_t count, size_t sent) {
  // The layout send_units uses: [Ctrl down, A down, A up, Ctrl up] when selecting all, then down/up per unit.
  std::vector<ReleaseStep> out;
  const size_t lead = selectAllFirst ? 4 : 0;
  if (selectAllFirst) {
    const bool ctrlDown = sent >= 1 && sent < 4;  // Ctrl went down, its up (index 3) did not
    const bool aDown = sent == 2;                  // A went down, its up (index 2) did not
    if (aDown) out.push_back(ReleaseStep{false, 'A'});
    if (ctrlDown) out.push_back(ReleaseStep{false, VK_CONTROL});
  }
  if (sent > lead) {
    const size_t past = sent - lead;
    if (past % 2 == 1) {  // unit (past / 2) went down, its up did not
      const size_t i = past / 2;
      if (i < count && units) out.insert(out.begin(), ReleaseStep{true, units[i]});
    }
  }
  return out;
}

namespace {
// The typing, optionally preceded by Ctrl+A in the same call. A partial send releases exactly what IT pressed
// and did not release (r2 L4) -- never anything else the user may hold.
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
    std::vector<ReleaseStep> ups = PendingReleases(selectAllFirst, units, count, sent);
    for (const ReleaseStep& r : ups) {
      INPUT up{};
      up.type = INPUT_KEYBOARD;
      if (r.unicode) {
        up.ki.wScan = r.code;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
      } else {
        up.ki.wVk = r.code;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
      }
      SendInput(1, &up, sizeof(INPUT));
      SecureZeroMemory(&up, sizeof(up));
    }
    for (ReleaseStep& r : ups) r.code = 0;
    *err = e ? e : ERROR_GEN_FAILURE;
    return false;
  }
  return true;
}

bool any_modifier_held() {
  for (int vk : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN}) {
    if (GetAsyncKeyState(vk) & 0x8000) return true;
  }
  return false;
}
}  // namespace

bool TypeUnicode(const uint16_t* units, uint16_t count, uint32_t* err) { return send_units(units, count, false, err); }

bool ReplaceWithUnicode(uint64_t expectedIdentity, const uint16_t* units, uint16_t count, uint32_t* err) {
  *err = 0;
  const DWORD pid = static_cast<DWORD>(expectedIdentity >> 32);
  ComScope com;
  if (!com.uia) {
    *err = kNotTypedError;  // UIA itself failed: nothing typed (never "no pattern")
    return false;
  }
  HWND fg = GetForegroundWindow();
  IUIAutomationElement* field = nullptr;
  if (identity_of_focused(com.uia, fg, pid, &field) != expectedIdentity || !field) {
    if (field) field->Release();
    *err = kNotTypedError;  // not the field that was verified
    return false;
  }
  IUIAutomationValuePattern* value = nullptr;
  const HRESULT got = field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value));
  bool selectAll = false;
  if (FAILED(got)) {
    field->Release();
    *err = kNotTypedError;  // the question failed: not the same as "no pattern"
    return false;
  }
  if (value) {
    BOOL readOnly = TRUE;
    const bool cleared = SUCCEEDED(value->get_CurrentIsReadOnly(&readOnly)) && !readOnly && [&] {
      BSTR empty = SysAllocString(L"");
      const bool ok = empty && SUCCEEDED(value->SetValue(empty));
      if (empty) SysFreeString(empty);
      return ok;
    }();
    value->Release();
    if (!cleared) {
      field->Release();
      *err = kNotTypedError;  // read-only, or the clear refused: nothing typed
      return false;
    }
  } else {
    // The field has no ValuePattern at all: select all in the same call as the typing -- but only if no
    // modifier is held now (it would turn Ctrl+A into another chord).
    if (any_modifier_held()) {
      field->Release();
      *err = kNotTypedError;
      return false;
    }
    selectAll = true;
  }
  field->Release();
  // The same field still has the focus after the clear -- right before the typing.
  if (identity_of_focused(com.uia, GetForegroundWindow(), pid) != expectedIdentity) {
    *err = kNotTypedError;
    return false;
  }
  return send_units(units, count, selectAll, err);
}

}  // namespace remote60::native_poc::logon_ui
