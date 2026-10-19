// Live Win32/DPAPI implementation of the prelogin machine credential store (prelogin_machine_store.hpp).

#include "prelogin_machine_store.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>   // DPAPI CryptProtectData / CryptUnprotectData
#include <aclapi.h>     // SetNamedSecurityInfoW / GetNamedSecurityInfoW
#include <sddl.h>       // ConvertStringSecurityDescriptorToSecurityDescriptorW / ...ToString...

#include <cstdlib>
#include <vector>

#include "fixed_directory.hpp"
#include "prelogin_store_chain.hpp"

namespace remote60::native_poc::prelogin {

namespace {

// SYSTEM + Administrators, full access, protected (no inherited ACEs). The machine blob is readable by
// exactly those two principals.
constexpr wchar_t kProtectedSddl[] = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";

// G5: an owner SID is trusted only if it is Local System or BUILTIN\Administrators. A directory/file owned
// by any other principal (e.g. a non-admin who pre-created the parent) is rejected -- that owner retains
// WRITE_DAC and could re-open the ACL, so re-asserting the DACL is not enough.
bool owner_sid_trusted(PSID owner) {
  if (!owner || !IsValidSid(owner)) return false;
  BYTE buf[SECURITY_MAX_SID_SIZE];
  DWORD n = sizeof(buf);
  if (CreateWellKnownSid(WinLocalSystemSid, nullptr, buf, &n) && EqualSid(owner, buf)) return true;
  n = sizeof(buf);
  if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, buf, &n) && EqualSid(owner, buf)) return true;
  return false;
}

// Validate an ALREADY-OPEN handle: the object must not be a reparse point and must be owned by a trusted
// principal. Validating the opened object (not a re-opened path) closes the TOCTOU between check and use.
bool handle_is_protected_object(HANDLE h) {
  BY_HANDLE_FILE_INFORMATION bi{};
  if (!GetFileInformationByHandle(h, &bi)) return false;
  if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;  // a redirect, not the real object
  PSECURITY_DESCRIPTOR sd = nullptr;
  PSID owner = nullptr;
  if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr,
                      &sd) != ERROR_SUCCESS)
    return false;
  const bool ok = owner_sid_trusted(owner);
  if (sd) LocalFree(sd);
  return ok;
}

// Open a path (file or directory) WITHOUT following reparse points and validate it. `missing` reports a
// plain not-found (distinct from a present-but-hostile object). Returns false for reparse/untrusted/open
// failure.
bool path_is_protected_object(const std::wstring& path, bool* missing) {
  if (missing) *missing = false;
  HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    const DWORD e = GetLastError();
    if (missing && (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)) *missing = true;
    return false;
  }
  const bool ok = handle_is_protected_object(h);
  CloseHandle(h);
  return ok;
}

// Build the protected (SYSTEM+Admins-only) security descriptor. Caller LocalFree()s it.
PSECURITY_DESCRIPTOR make_protected_sd() {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kProtectedSddl, SDDL_REVISION_1, &sd, nullptr)) {
    return nullptr;
  }
  return sd;
}

// Write to a UNIQUE temp created WITH the protected SD (so it is never readable in a window), then
// atomically replace the final path. CREATE_NEW + a unique name defeats a pre-planted temp; the final
// REPLACE_EXISTING rename removes any pre-planted file/symlink at the destination name.
bool write_all_protected(const std::wstring& path, const uint8_t* data, size_t len, uint32_t* win32) {
  PSECURITY_DESCRIPTOR sd = make_protected_sd();
  if (!sd) { if (win32) *win32 = GetLastError(); return false; }
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = sd;
  wchar_t uniq[80] = {};
  _snwprintf_s(uniq, _TRUNCATE, L".%lu.%llu.tmp", GetCurrentProcessId(),
               static_cast<unsigned long long>(GetTickCount64()));
  const std::wstring tmp = path + uniq;
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, &sa, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  LocalFree(sd);
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
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    if (win32) *win32 = GetLastError();
    DeleteFileW(tmp.c_str());
    return false;
  }
  return true;
}

}  // namespace

MachineStoreStatus SealAndWriteMachineRecord(const std::wstring& path, const MachineRecord& r,
                                             uint32_t* win32) {
  if (win32) *win32 = 0;
  // r7 H3: every level of the path is checked (owner AND DACL, not a redirect) and HELD open without delete
  // sharing until the write is done -- the folder the record goes into is the folder that was checked.
  // Missing GNLink levels are created protected; an existing hostile one is refused, not "repaired".
  HeldChain chain;
  const DWORD held = HoldRecordChain(path, /*create=*/true, OwnerIsSystemOrAdministrators, &chain);
  if (held != 0) {
    if (win32) *win32 = held;
    return MachineStoreStatus::Dacl;
  }
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
  // The temp is created WITH the protected DACL and atomically replaces the final path, so the published
  // blob carries the SYSTEM+Admins-only DACL from creation -- no post-hoc SetNamedSecurityInfo window.
  const bool wrote = write_all_protected(path, out.pbData, out.cbData, win32);
  LocalFree(out.pbData);
  return wrote ? MachineStoreStatus::Ok : MachineStoreStatus::Write;
}

MachineStoreStatus ReadAndOpenMachineRecord(const std::wstring& path, MachineRecord* out, uint32_t* win32) {
  if (win32) *win32 = 0;
  // r7 H3: the whole path, from %ProgramData% down, checked on handles and held while the record is read;
  // the record itself checked (owner, DACL: SYSTEM/Administrators only) on the handle it is read through.
  HeldChain chain;
  const DWORD held = HoldRecordChain(path, /*create=*/false, OwnerIsSystemOrAdministrators, &chain);
  if (held != 0) {
    if (win32) *win32 = held;
    return MachineStoreStatus::Read;
  }
  std::vector<uint8_t> sealed;
  const DWORD readErr = ReadHeldRecord(path, OwnerIsSystemOrAdministrators, &sealed);
  if (readErr != 0) {
    if (win32) *win32 = readErr;
    return MachineStoreStatus::Read;
  }
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

bool OwnerSidIsTrusted(const void* sid) { return owner_sid_trusted(const_cast<PSID>(sid)); }

MachineStoreStatus ValidateProtectedPath(const std::wstring& path, uint32_t* win32) {
  if (win32) *win32 = 0;
  bool missing = false;
  if (path_is_protected_object(path, &missing)) return MachineStoreStatus::Ok;
  if (win32) *win32 = missing ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED;
  return missing ? MachineStoreStatus::Read : MachineStoreStatus::Dacl;  // reparse/untrusted -> Dacl
}

std::string PreloginAllowedDirectoryUrl() {
#ifdef REMOTE60_STREAM_TEST_SEAM
  const char* fixture = std::getenv("GNLINK_STREAM_TEST_DIRECTORY");
  return fixture ? std::string(fixture) : std::string();
#else
  return kFixedDirectoryUrl;
#endif
}

#ifdef REMOTE60_STREAM_TEST_SEAM
MachineStoreStatus ReadAndOpenMachineRecordForTest(std::wstring* path, MachineRecord* out, uint32_t* win32) {
  if (win32) *win32 = 0;
  wchar_t buf[MAX_PATH * 2] = {};
  const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_MACHINE_RECORD", buf, MAX_PATH * 2);
  if (n == 0 || n >= MAX_PATH * 2) return MachineStoreStatus::Read;
  *path = std::wstring(buf, n);
  HANDLE f = CreateFileW(path->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) {
    if (win32) *win32 = GetLastError();
    return MachineStoreStatus::Read;
  }
  std::vector<uint8_t> sealed(static_cast<size_t>(GetFileSize(f, nullptr)));
  DWORD got = 0;
  const bool ok = sealed.empty() || ReadFile(f, sealed.data(), static_cast<DWORD>(sealed.size()), &got, nullptr);
  CloseHandle(f);
  if (!ok || got != sealed.size()) return MachineStoreStatus::Read;
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
#endif

}  // namespace remote60::native_poc::prelogin
