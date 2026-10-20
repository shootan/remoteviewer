// Counter-example tests for the prelogin machine-store trusted-ancestry validation (G5).
//
// Codex G5: the create-time DACL fix left the READ path and a pre-existing parent unvalidated -- a reparse
// redirect or an untrusted owner could slip through. These checks are deterministic and need NO elevation
// (unlike the round-trip in prelogin_machine_store_test, which depends on the runner's privilege):
//   - OwnerSidIsTrusted accepts ONLY Local System / BUILTIN\Administrators, rejects Everyone / Users.
//   - ValidateProtectedPath REFUSES a directory junction (reparse redirect) at the credential path.
//   - ValidateProtectedPath's owner verdict agrees with the path's actual owner (both directions).
// No real credential is written.

#include "prelogin_machine_store.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <winioctl.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
bool check(bool cond, const char* what) {
  std::printf(cond ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!cond) ++gFailures;
  return cond;
}

std::wstring temp_name(const wchar_t* tag) {
  wchar_t dir[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, dir);
  return std::wstring(dir) + L"gnlink_g5_" + std::to_wstring(GetCurrentProcessId()) + L"_" + tag;
}

// A well-known SID in a heap buffer (caller frees with free()).
PSID well_known(WELL_KNOWN_SID_TYPE t) {
  DWORD n = SECURITY_MAX_SID_SIZE;
  PSID p = std::malloc(n);
  if (p && CreateWellKnownSid(t, nullptr, p, &n)) return p;
  std::free(p);
  return nullptr;
}

bool path_owner_trusted(const std::wstring& path) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  PSID owner = nullptr;
  if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr,
                            nullptr, nullptr, &sd) != ERROR_SUCCESS)
    return false;
  const bool t = owner && OwnerSidIsTrusted(owner);
  if (sd) LocalFree(sd);
  return t;
}

// Mount-point reparse layout (REPARSE_DATA_BUFFER lives in the WDK ntifs.h, not the user-mode SDK).
#pragma pack(push, 1)
struct MountPointReparse {
  DWORD ReparseTag;
  WORD ReparseDataLength;
  WORD Reserved;
  WORD SubstituteNameOffset;
  WORD SubstituteNameLength;
  WORD PrintNameOffset;
  WORD PrintNameLength;
  WCHAR PathBuffer[1];
};
#pragma pack(pop)

// Create a directory junction (reparse mount point) `link` -> `target`. Mount points need no privilege.
bool make_junction(const std::wstring& link, const std::wstring& target) {
  if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
  HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  const std::wstring subst = L"\\??\\" + target;
  const std::wstring print = target;
  const size_t substBytes = subst.size() * sizeof(wchar_t);
  const size_t printBytes = print.size() * sizeof(wchar_t);
  const size_t pathBuf = substBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);
  const size_t headerToPath = offsetof(MountPointReparse, PathBuffer);  // 8 header + 8 WORD fields = 16
  std::vector<uint8_t> raw(headerToPath + pathBuf, 0);
  auto* rp = reinterpret_cast<MountPointReparse*>(raw.data());
  rp->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  rp->ReparseDataLength = static_cast<WORD>(8 /*4 WORD fields*/ + pathBuf);
  rp->SubstituteNameOffset = 0;
  rp->SubstituteNameLength = static_cast<WORD>(substBytes);
  rp->PrintNameOffset = static_cast<WORD>(substBytes + sizeof(wchar_t));
  rp->PrintNameLength = static_cast<WORD>(printBytes);
  std::memcpy(rp->PathBuffer, subst.c_str(), substBytes + sizeof(wchar_t));
  std::memcpy(reinterpret_cast<uint8_t*>(rp->PathBuffer) + substBytes + sizeof(wchar_t), print.c_str(),
              printBytes + sizeof(wchar_t));
  // Input size = 8-byte header (tag+len+reserved) + ReparseDataLength; that is exactly raw.size() here.
  DWORD ret = 0;
  const bool ok =
      DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, raw.data(), static_cast<DWORD>(raw.size()), nullptr, 0,
                      &ret, nullptr) != FALSE;
  CloseHandle(h);
  return ok;
}
}  // namespace

int main() {
  std::printf("prelogin machine store G5: trusted-ancestry (owner + reparse)\n");

  // --- OwnerSidIsTrusted: accept SYSTEM / Administrators, reject Everyone / Users ------------------
  if (PSID sys = well_known(WinLocalSystemSid)) {
    check(OwnerSidIsTrusted(sys), "owner = Local System -> trusted");
    std::free(sys);
  }
  if (PSID adm = well_known(WinBuiltinAdministratorsSid)) {
    check(OwnerSidIsTrusted(adm), "owner = BUILTIN\\Administrators -> trusted");
    std::free(adm);
  }
  if (PSID world = well_known(WinWorldSid)) {
    check(!OwnerSidIsTrusted(world), "NEGATIVE: owner = Everyone -> NOT trusted");
    std::free(world);
  }
  if (PSID users = well_known(WinBuiltinUsersSid)) {
    check(!OwnerSidIsTrusted(users), "NEGATIVE: owner = BUILTIN\\Users -> NOT trusted");
    std::free(users);
  }

  // --- ValidateProtectedPath REFUSES a reparse redirect at the credential path --------------------
  const std::wstring target = temp_name(L"target");
  const std::wstring link = temp_name(L"junction");
  CreateDirectoryW(target.c_str(), nullptr);
  if (make_junction(link, target)) {
    uint32_t win32 = 0;
    const MachineStoreStatus st = ValidateProtectedPath(link, &win32);
    check(st == MachineStoreStatus::Dacl,
          "NEGATIVE: a directory junction at the credential path -> Dacl (reparse refused)");
    RemoveDirectoryW(link.c_str());
  } else {
    std::printf("  note  could not create a junction here (%lu); reparse counter-example SKIPPED\n",
                GetLastError());
    RemoveDirectoryW(link.c_str());
  }
  RemoveDirectoryW(target.c_str());

  // --- ValidateProtectedPath owner verdict agrees with the real owner (both directions) -----------
  const std::wstring plain = temp_name(L"plain");
  if (CreateDirectoryW(plain.c_str(), nullptr)) {
    uint32_t win32 = 0;
    const bool ownerTrusted = path_owner_trusted(plain);
    const MachineStoreStatus st = ValidateProtectedPath(plain, &win32);
    // A plain (non-reparse) dir is Ok IFF its owner is trusted. On a non-elevated runner the owner is the
    // user (untrusted) -> Dacl: that is exactly the "pre-created parent by a non-admin is rejected" case.
    check(st == (ownerTrusted ? MachineStoreStatus::Ok : MachineStoreStatus::Dacl),
          ownerTrusted ? "a trusted-owner plain dir -> Ok"
                       : "NEGATIVE: an untrusted-owner (non-admin pre-created) plain dir -> Dacl");
    RemoveDirectoryW(plain.c_str());
  }

  // Missing path is distinguished from a present-but-hostile one.
  {
    uint32_t win32 = 0;
    check(ValidateProtectedPath(temp_name(L"nope"), &win32) == MachineStoreStatus::Read,
          "a missing path -> Read (distinct from a hostile present object)");
  }

  if (gFailures != 0) { std::printf("prelogin_machine_store_g5_test: FAIL (%d)\n", gFailures); return 1; }
  std::printf("prelogin_machine_store_g5_test: PASS\n");
  return 0;
}
