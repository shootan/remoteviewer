// The service's side of "로그인 전에도 원격 접속 허용" (prelogin-ui r1, item 4): the start-type and
// machine-store primitives the admin pipe's decision runs on. Service-only (GNLinkInputService), so the
// GNLinkHost client does not carry the store or SCM code.

#include <windows.h>

#include <vector>

#include "prelogin_admin_pipe.hpp"
#include "prelogin_machine_store.hpp"

namespace remote60::native_poc::prelogin {

namespace {
constexpr wchar_t kRememberKey[] = L"SOFTWARE\\GNLink";
constexpr wchar_t kRememberValue[] = L"PreloginOriginalStartType";
}  // namespace

void BindProductStartType(AdminPrimitives* p, const wchar_t* serviceName) {
  const std::wstring name = serviceName;
  p->queryStartType = [name](uint32_t* out) {
    SC_HANDLE m = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!m) return false;
    SC_HANDLE s = OpenServiceW(m, name.c_str(), SERVICE_QUERY_CONFIG);
    bool ok = false;
    if (s) {
      DWORD need = 0;
      QueryServiceConfigW(s, nullptr, 0, &need);
      std::vector<uint8_t> buf(need ? need : 1);
      if (QueryServiceConfigW(s, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data()), need, &need)) {
        *out = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data())->dwStartType;
        ok = true;
      }
      CloseServiceHandle(s);
    }
    CloseServiceHandle(m);
    return ok;
  };
  p->setStartType = [name](uint32_t type) -> uint32_t {
    SC_HANDLE m = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!m) return GetLastError();
    SC_HANDLE s = OpenServiceW(m, name.c_str(), SERVICE_CHANGE_CONFIG);
    if (!s) {
      const DWORD e = GetLastError();
      CloseServiceHandle(m);
      return e;
    }
    const BOOL ok = ChangeServiceConfigW(s, SERVICE_NO_CHANGE, type, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr,
                                         nullptr, nullptr, nullptr);
    const DWORD e = ok ? 0 : GetLastError();
    CloseServiceHandle(s);
    CloseServiceHandle(m);
    return e;
  };
  p->loadRememberedStartType = [](uint32_t* out) {
    DWORD v = 0, cb = sizeof(v);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kRememberKey, kRememberValue, RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &v,
                     &cb) != ERROR_SUCCESS) {
      return false;
    }
    *out = v;
    return true;
  };
  p->rememberStartType = [](uint32_t type) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kRememberKey, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
      return false;
    }
    const DWORD v = type;
    const bool ok = RegSetValueExW(key, kRememberValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v)) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
  };
  p->forgetRememberedStartType = [] {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRememberKey, 0, KEY_WRITE | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
      RegDeleteValueW(key, kRememberValue);
      RegCloseKey(key);
    }
  };
}

void BindProductMachineStore(AdminPrimitives* p) {
  p->readRecord = [](const std::wstring& path, MachineRecord* out) {
    uint32_t w = 0;
    return ReadAndOpenMachineRecord(path, out, &w) == MachineStoreStatus::Ok;
  };
  p->writeRecord = [](const std::wstring& path, const MachineRecord& r) -> uint32_t {
    uint32_t w = 0;
    if (SealAndWriteMachineRecord(path, r, &w) == MachineStoreStatus::Ok) return 0;
    return w != 0 ? w : ERROR_WRITE_FAULT;
  };
  p->probeRecord = [](const std::wstring& path) -> int {  // r10 T1: there or not, not readable or not
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return 1;
    const DWORD e = GetLastError();
    return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? 0 : -1;
  };
  p->removeRecord = [](const std::wstring& path) -> uint32_t {
    if (DeleteFileW(path.c_str())) return 0;
    const DWORD e = GetLastError();
    return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? 0 : e;
  };
}


void BindProductReboot(AdminPrimitives* p) {
  p->requestOsReboot = []() -> uint32_t {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return GetLastError();
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool looked = LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &tp.Privileges[0].Luid) != FALSE;
    const bool adjusted = looked && AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) &&
                          GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    if (!adjusted) return ERROR_PRIVILEGE_NOT_HELD;
    wchar_t msg[] = L"GNLink 원격 요청으로 다시 시작합니다.";
    const BOOL ok = InitiateSystemShutdownExW(nullptr, msg, /*dwTimeout=*/5, /*bForceAppsClosed=*/FALSE,
                                              /*bRebootAfterShutdown=*/TRUE,
                                              SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_RECONFIG |
                                                  SHTDN_REASON_FLAG_PLANNED);
    return ok ? 0u : GetLastError();
  };
}

}  // namespace remote60::native_poc::prelogin
