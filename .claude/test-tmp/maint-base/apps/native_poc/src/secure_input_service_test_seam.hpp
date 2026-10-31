#pragma once

// TEST BUILD ONLY (REMOTE60_SIS_TEST_SEAM, GNLinkInputServiceLifecycleTest.exe; prelogin r21 E1). The product
// service's own lifecycle -- service_main, its STOP handler, the secure-input pipe server, the unlock / prelogin /
// admin workers and their joins, report_service_status -- run in a process a non-elevated test starts, with only
// its OS-effect boundaries replaced:
//   - SCM: no dispatcher and no SetServiceStatus. Each status the product reports is printed (state + error);
//     an external STOP is the product's own service_control(SERVICE_CONTROL_STOP) called when the test signals
//     an event.
//   - names and DACLs: the three pipes get per-run names and a DACL that lets this user in (the product's are
//     SYSTEM/Administrators only, and the installed service owns the real names).
//   - the secure-input pipe's create can be made to fail on chosen calls (REMOTE60_SIS_TEST_CREATE_FAIL=2,3,4
//     with error REMOTE60_SIS_TEST_CREATE_ERROR) -- the one fault the test is about.
//   - process effects: the prelogin feature reads as off, no owed revoke is carried out, no pre-login worker or
//     user Host is started, and the diag log goes to REMOTE60_SIS_TEST_DIAG instead of ProgramData.
// What this cannot show: the real SCM (registration, rights, QueryServiceStatusEx), SYSTEM, the agent.
// The shipped GNLinkInputService.exe is built without it (build gate: no "sis-test-seam" string).

#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace remote60::native_poc::sis_test {

inline bool& Active() {
  static bool active = false;
  return active;
}

inline std::wstring Env(const wchar_t* name) {
  wchar_t buf[512] = {};
  const DWORD n = GetEnvironmentVariableW(name, buf, 512);
  return (n > 0 && n < 512) ? std::wstring(buf, n) : std::wstring();
}

inline void Note(const char* format, ...) {
  char line[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  std::printf("[sis-test-seam] %s\n", line);
  std::fflush(stdout);
}

// "\\.\pipe\GNLinkSisTest-<suffix>-<which>" (function-local statics: thread-safe first use)
inline std::wstring MakePipeName(const wchar_t* which) {
  return L"\\\\.\\pipe\\GNLinkSisTest-" + Env(L"REMOTE60_SIS_TEST_SUFFIX") + L"-" + which;
}
inline const wchar_t* SecureInputPipe() {
  static const std::wstring n = MakePipeName(L"input");
  return n.c_str();
}
inline const wchar_t* UnlockPipe() {
  static const std::wstring n = MakePipeName(L"unlock");
  return n.c_str();
}
inline const wchar_t* AdminPipe() {
  static const std::wstring n = MakePipeName(L"admin");
  return n.c_str();
}

inline const wchar_t* Sddl() {
  static const std::wstring sddl = [] {
    HANDLE tok = nullptr;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
    DWORD need = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
    std::string buf(need, '\0');
    GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
    CloseHandle(tok);
    LPWSTR sid = nullptr;
    ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid);
    std::wstring s = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    return s;
  }();
  return sddl.c_str();
}

// True when this call of the secure-input pipe's create is to fail (1-based count of creates); *error is the code.
inline bool FailThisCreate(DWORD* error) {
  static std::atomic<int> calls{0};
  const int call = ++calls;
  const std::wstring plan = Env(L"REMOTE60_SIS_TEST_CREATE_FAIL");
  bool fail = false;
  size_t pos = 0;
  while (pos < plan.size()) {
    const size_t comma = plan.find(L',', pos);
    const std::wstring item = plan.substr(pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos);
    if (!item.empty() && _wtoi(item.c_str()) == call) fail = true;
    if (comma == std::wstring::npos) break;
    pos = comma + 1;
  }
  const std::wstring e = Env(L"REMOTE60_SIS_TEST_CREATE_ERROR");
  *error = e.empty() ? ERROR_NO_SYSTEM_RESOURCES : static_cast<DWORD>(_wtoi(e.c_str()));
  Note("secure pipe create #%d -> %s", call, fail ? "INJECTED FAILURE" : "real");
  return fail;
}

inline void Status(DWORD state, DWORD error) { Note("status state=%lu error=%lu", state, error); }

// The test's STOP: when the named event is signalled, `stop` (the product's STOP handler) is called once.
inline void StartStopWatcher(const std::function<void()>& stop) {
  const std::wstring name = Env(L"REMOTE60_SIS_TEST_STOP_EVENT");
  if (name.empty()) return;
  HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
  if (!ev) return;
  std::thread([ev, stop] {
    if (WaitForSingleObject(ev, INFINITE) == WAIT_OBJECT_0) {
      Note("external STOP requested");
      stop();
    }
  }).detach();
}

}  // namespace remote60::native_poc::sis_test
