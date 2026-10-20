// See prelogin_store_chain.hpp (prelogin-ui r7, H3).

#include "prelogin_store_chain.hpp"

#include <aclapi.h>
#include <sddl.h>

namespace remote60::native_poc::prelogin {

namespace {

constexpr ACCESS_MASK kAncestorForbidden = DELETE | WRITE_DAC | WRITE_OWNER | FILE_DELETE_CHILD | GENERIC_WRITE |
                                           GENERIC_ALL;
// SYSTEM + Administrators only.
constexpr wchar_t kPrivateSddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
// SYSTEM + Administrators full; everyone else may look inside (nothing more).
constexpr wchar_t kAncestorSddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;;0x1200a9;;;BU)";

bool sid_is(PSID sid, WELL_KNOWN_SID_TYPE type) {
  BYTE buf[SECURITY_MAX_SID_SIZE];
  DWORD n = sizeof(buf);
  return CreateWellKnownSid(type, nullptr, buf, &n) && EqualSid(sid, buf);
}

std::wstring parent_of(const std::wstring& p) {
  const size_t slash = p.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash);
}

HANDLE open_no_follow(const std::wstring& path, DWORD access, DWORD share, bool directory) {
  return CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING,
                     FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
}

// The opened object is what it claims (not a redirect, a directory when it should be) and its security
// passes the rule.
bool handle_acceptable(HANDLE h, bool directory, ChainLevel level, const std::function<bool(PSID)>& ownerTrusted,
                       bool checkDacl) {
  BY_HANDLE_FILE_INFORMATION bi{};
  if (!GetFileInformationByHandle(h, &bi)) return false;
  if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
  if (directory != ((bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) return false;
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr, nullptr,
                      nullptr, nullptr, &sd) != ERROR_SUCCESS) {
    return false;
  }
  bool ok = false;
  if (checkDacl) {
    ok = SecurityDescriptorAcceptable(sd, level, ownerTrusted);
  } else {
    PSID owner = nullptr;
    BOOL defaulted = FALSE;
    ok = GetSecurityDescriptorOwner(sd, &owner, &defaulted) && ownerTrusted(owner);
  }
  LocalFree(sd);
  return ok;
}

bool create_with(const std::wstring& path, const wchar_t* sddl) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
  // The creator owns it: SYSTEM for the service (an elevated administrator: Administrators). Anyone else
  // would own it -- and fail the owner rule, as they should.
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  const bool ok = CreateDirectoryW(path.c_str(), &sa) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS;
  LocalFree(sd);
  return ok;
}

}  // namespace

bool OwnerIsSystemOrAdministrators(PSID owner) {
  return owner && IsValidSid(owner) && (sid_is(owner, WinLocalSystemSid) || sid_is(owner, WinBuiltinAdministratorsSid));
}

bool SecurityDescriptorAcceptable(PSECURITY_DESCRIPTOR sd, ChainLevel level, const std::function<bool(PSID)>& ownerTrusted) {
  if (!sd) return false;
  PSID owner = nullptr;
  BOOL defaulted = FALSE;
  if (!GetSecurityDescriptorOwner(sd, &owner, &defaulted) || !ownerTrusted(owner)) return false;
  BOOL present = FALSE;
  PACL dacl = nullptr;
  if (!GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted)) return false;
  if (!present || !dacl) return false;  // no DACL = everyone has everything
  ACL_SIZE_INFORMATION info{};
  if (!GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation)) return false;
  for (DWORD i = 0; i < info.AceCount; ++i) {
    void* raw = nullptr;
    if (!GetAce(dacl, i, &raw)) return false;
    const auto* header = static_cast<ACE_HEADER*>(raw);
    if (header->AceFlags & INHERIT_ONLY_ACE) continue;  // applies to children only
    if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;  // denying never lets anyone in
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) return false;  // anything unusual: not trusted
    const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
    PSID who = const_cast<DWORD*>(&ace->SidStart);
    if (ownerTrusted(who)) continue;  // the trusted principals (product: SYSTEM, Administrators)
    const ACCESS_MASK mask = ace->Mask;
    if (level == ChainLevel::Private) return false;  // nobody else, for anything
    if (mask & kAncestorForbidden) return false;
  }
  return true;
}

HeldChain::~HeldChain() { Release(); }

void HeldChain::Release() {
  for (HANDLE h : handles_) CloseHandle(h);
  handles_.clear();
}

DWORD HoldRecordChain(const std::wstring& recordPath, bool create, const std::function<bool(PSID)>& ownerTrusted,
                      HeldChain* held) {
  held->Release();
  held->failedAt.clear();
  const std::wstring credential = parent_of(recordPath);
  const std::wstring gnlink = parent_of(credential);
  const std::wstring root = parent_of(gnlink);
  if (credential.empty() || gnlink.empty() || root.empty()) return ERROR_INVALID_PARAMETER;
  struct Level {
    std::wstring path;
    ChainLevel level;
    bool checkDacl;
    const wchar_t* createSddl;
  };
  // The root (%ProgramData%) lets everyone add entries by design: only its identity and owner are held to.
  const Level levels[] = {{root, ChainLevel::Ancestor, false, nullptr},
                          {gnlink, ChainLevel::Ancestor, true, kAncestorSddl},
                          {credential, ChainLevel::Private, true, kPrivateSddl}};
  for (const Level& l : levels) {
    // Held without FILE_SHARE_DELETE: while this handle is open nobody can rename or remove the level. It
    // asks for FILE_LIST_DIRECTORY on purpose: an open for attributes / READ_CONTROL only is not checked
    // against sharing at all, and would hold nothing (r7 H3 test: the credential folder renamed under it).
    constexpr DWORD kHoldAccess = FILE_LIST_DIRECTORY | READ_CONTROL | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
    HANDLE h = open_no_follow(l.path, kHoldAccess, FILE_SHARE_READ | FILE_SHARE_WRITE, true);
    if (h == INVALID_HANDLE_VALUE && create && l.createSddl &&
        (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) {
      if (!create_with(l.path, l.createSddl)) {
        held->failedAt = l.path;
        const DWORD e = GetLastError();
        held->Release();
        return e ? e : ERROR_ACCESS_DENIED;
      }
      h = open_no_follow(l.path, kHoldAccess, FILE_SHARE_READ | FILE_SHARE_WRITE, true);
    }
    if (h == INVALID_HANDLE_VALUE) {
      const DWORD e = GetLastError();
      held->failedAt = l.path;
      held->Release();
      return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? ERROR_PATH_NOT_FOUND : e;
    }
    held->handles_.push_back(h);
    if (!handle_acceptable(h, /*directory=*/true, l.level, ownerTrusted, l.checkDacl)) {
      held->failedAt = l.path;
      held->Release();
      return ERROR_ACCESS_DENIED;
    }
  }
  return 0;
}

DWORD ReadHeldRecord(const std::wstring& recordPath, const std::function<bool(PSID)>& ownerTrusted,
                     std::vector<uint8_t>* out) {
  out->clear();
  HANDLE h = open_no_follow(recordPath, GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, false);
  if (h == INVALID_HANDLE_VALUE) return GetLastError();
  if (!handle_acceptable(h, /*directory=*/false, ChainLevel::Private, ownerTrusted, true)) {
    CloseHandle(h);
    return ERROR_ACCESS_DENIED;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(h, &size) || size.QuadPart > 64 * 1024 * 1024) {
    const DWORD e = GetLastError();
    CloseHandle(h);
    return e ? e : ERROR_FILE_TOO_LARGE;
  }
  out->resize(static_cast<size_t>(size.QuadPart));
  DWORD got = 0;
  const bool ok = out->empty() || (ReadFile(h, out->data(), static_cast<DWORD>(out->size()), &got, nullptr) && got == out->size());
  const DWORD e = ok ? 0 : GetLastError();
  CloseHandle(h);
  if (!ok) out->clear();
  return ok ? 0 : (e ? e : ERROR_READ_FAULT);
}

}  // namespace remote60::native_poc::prelogin
