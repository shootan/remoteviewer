// Live Win32/DPAPI implementation of the prelogin machine credential store (prelogin_machine_store.hpp).

#include "prelogin_machine_store.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>   // DPAPI CryptProtectData / CryptUnprotectData
#include <aclapi.h>     // SetNamedSecurityInfoW / GetNamedSecurityInfoW
#include <sddl.h>       // ConvertStringSecurityDescriptorToSecurityDescriptorW / ...ToString...

#include <vector>

namespace remote60::native_poc::prelogin {

namespace {

// SYSTEM + Administrators, full access, protected (no inherited ACEs). The machine blob is readable by
// exactly those two principals.
constexpr wchar_t kProtectedSddl[] = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";

bool write_all_atomic(const std::wstring& path, const uint8_t* data, size_t len, uint32_t* win32) {
  const std::wstring tmp = path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) { if (win32) *win32 = GetLastError(); return false; }
  const uint8_t* p = data;
  size_t remaining = len;
  bool ok = true;
  while (remaining > 0) {
    DWORD chunk = static_cast<DWORD>(remaining > 0x10000000 ? 0x10000000 : remaining);
    DWORD wrote = 0;
    if (!WriteFile(h, p, chunk, &wrote, nullptr) || wrote != chunk) { ok = false; break; }
    p += wrote;
    remaining -= wrote;
  }
  if (ok && !FlushFileBuffers(h)) ok = false;
  if (!ok && win32) *win32 = GetLastError();
  CloseHandle(h);
  if (!ok) { DeleteFileW(tmp.c_str()); return false; }
  // Atomic replace (single-file rename is atomic; MOVEFILE_WRITE_THROUGH flushes the directory entry).
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    if (win32) *win32 = GetLastError();
    DeleteFileW(tmp.c_str());
    return false;
  }
  return true;
}

bool apply_protected_dacl(const std::wstring& path, uint32_t* win32) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kProtectedSddl, SDDL_REVISION_1, &sd, nullptr)) {
    if (win32) *win32 = GetLastError();
    return false;
  }
  BOOL present = FALSE, defaulted = FALSE;
  PACL dacl = nullptr;
  bool ok = false;
  if (GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) && present) {
    const DWORD rc = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
                                           DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                           nullptr, nullptr, dacl, nullptr);
    if (rc == ERROR_SUCCESS) {
      ok = true;
    } else if (win32) {
      *win32 = rc;
    }
  } else if (win32) {
    *win32 = GetLastError();
  }
  LocalFree(sd);
  return ok;
}

bool read_all(const std::wstring& path, std::vector<uint8_t>* out, uint32_t* win32) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) { if (win32) *win32 = GetLastError(); return false; }
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(h, &size) || size.QuadPart > (64 * 1024 * 1024)) {
    if (win32) *win32 = GetLastError();
    CloseHandle(h);
    return false;
  }
  out->resize(static_cast<size_t>(size.QuadPart));
  size_t off = 0;
  bool ok = true;
  while (off < out->size()) {
    DWORD got = 0;
    DWORD want = static_cast<DWORD>(out->size() - off > 0x10000000 ? 0x10000000 : out->size() - off);
    if (!ReadFile(h, out->data() + off, want, &got, nullptr) || got == 0) { ok = false; break; }
    off += got;
  }
  if (!ok && win32) *win32 = GetLastError();
  CloseHandle(h);
  return ok;
}

}  // namespace

MachineStoreStatus SealAndWriteMachineRecord(const std::wstring& path, const MachineRecord& r,
                                             bool applyProtectedDacl, uint32_t* win32) {
  if (win32) *win32 = 0;
  std::vector<uint8_t> plain = SerializeMachineRecord(r);
  DATA_BLOB in{static_cast<DWORD>(plain.size()), plain.data()};
  DATA_BLOB out{0, nullptr};
  if (!CryptProtectData(&in, L"GNLink prelogin machine credential", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_LOCAL_MACHINE, &out)) {
    if (win32) *win32 = GetLastError();
    SecureZeroMemory(plain.data(), plain.size());
    return MachineStoreStatus::Protect;
  }
  SecureZeroMemory(plain.data(), plain.size());
  const bool wrote = write_all_atomic(path, out.pbData, out.cbData, win32);
  LocalFree(out.pbData);
  if (!wrote) return MachineStoreStatus::Write;
  if (applyProtectedDacl && !apply_protected_dacl(path, win32)) return MachineStoreStatus::Dacl;
  return MachineStoreStatus::Ok;
}

MachineStoreStatus ReadAndOpenMachineRecord(const std::wstring& path, MachineRecord* out, uint32_t* win32) {
  if (win32) *win32 = 0;
  std::vector<uint8_t> sealed;
  if (!read_all(path, &sealed, win32)) return MachineStoreStatus::Read;
  DATA_BLOB in{static_cast<DWORD>(sealed.size()), sealed.data()};
  DATA_BLOB plain{0, nullptr};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &plain)) {
    if (win32) *win32 = GetLastError();
    return MachineStoreStatus::Unprotect;
  }
  const bool parsed = ParseMachineRecord(plain.pbData, plain.cbData, out);
  SecureZeroMemory(plain.pbData, plain.cbData);
  LocalFree(plain.pbData);
  return parsed ? MachineStoreStatus::Ok : MachineStoreStatus::Parse;
}

bool ReadDaclSddl(const std::wstring& path, std::wstring* outSddl) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  const DWORD rc = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                         nullptr, nullptr, nullptr, nullptr, &sd);
  if (rc != ERROR_SUCCESS) return false;
  LPWSTR str = nullptr;
  ULONG len = 0;
  const bool ok = ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1,
                                                                       DACL_SECURITY_INFORMATION, &str, &len);
  if (ok && str) *outSddl = str;
  if (str) LocalFree(str);
  LocalFree(sd);
  return ok;
}

}  // namespace remote60::native_poc::prelogin
