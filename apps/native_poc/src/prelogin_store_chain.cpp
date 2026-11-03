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

// lk-abcd C: a SID as S-1-..., for a refusal's log line (never a secret).
std::wstring sid_text(PSID sid) {
  if (!sid || !IsValidSid(sid)) return L"(none)";
  LPWSTR s = nullptr;
  if (!ConvertSidToStringSidW(sid, &s) || !s) return L"(unprintable)";
  std::wstring out(s);
  LocalFree(s);
  return out;
}

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
                       bool checkDacl, std::wstring* why = nullptr) {
  BY_HANDLE_FILE_INFORMATION bi{};
  if (!GetFileInformationByHandle(h, &bi)) {
    if (why) *why = L"file information unreadable";
    return false;
  }
  if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    if (why) *why = L"reparse point";
    return false;
  }
  if (directory != ((bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) {
    if (why) *why = directory ? L"not a directory" : L"a directory";
    return false;
  }
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr, nullptr,
                      nullptr, nullptr, &sd) != ERROR_SUCCESS) {
    if (why) *why = L"security unreadable";
    return false;
  }
  bool ok = false;
  if (checkDacl) {
    ok = SecurityDescriptorAcceptable(sd, level, ownerTrusted, why);
  } else {
    PSID owner = nullptr;
    BOOL defaulted = FALSE;
    ok = GetSecurityDescriptorOwner(sd, &owner, &defaulted) && ownerTrusted(owner);
    if (!ok && why) *why = L"owner " + sid_text(owner);
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

bool SecurityDescriptorAcceptable(PSECURITY_DESCRIPTOR sd, ChainLevel level, const std::function<bool(PSID)>& ownerTrusted,
                                  std::wstring* why) {
  auto fail = [why](std::wstring w) {
    if (why) *why = std::move(w);
    return false;
  };
  if (!sd) return fail(L"no security descriptor");
  PSID owner = nullptr;
  BOOL defaulted = FALSE;
  if (!GetSecurityDescriptorOwner(sd, &owner, &defaulted)) return fail(L"owner unreadable");
  if (!ownerTrusted(owner)) return fail(L"owner " + sid_text(owner));
  BOOL present = FALSE;
  PACL dacl = nullptr;
  if (!GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted)) return fail(L"DACL unreadable");
  if (!present || !dacl) return fail(L"no DACL");  // no DACL = everyone has everything
  ACL_SIZE_INFORMATION info{};
  if (!GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation)) return fail(L"DACL unreadable");
  for (DWORD i = 0; i < info.AceCount; ++i) {
    void* raw = nullptr;
    if (!GetAce(dacl, i, &raw)) return fail(L"ACE unreadable");
    const auto* header = static_cast<ACE_HEADER*>(raw);
    if (header->AceFlags & INHERIT_ONLY_ACE) continue;  // applies to children only
    if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;  // denying never lets anyone in
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)  // anything unusual: not trusted
      return fail(L"unusual ACE type " + std::to_wstring(header->AceType));
    const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
    PSID who = const_cast<DWORD*>(&ace->SidStart);
    if (ownerTrusted(who)) continue;  // the trusted principals (product: SYSTEM, Administrators)
    const ACCESS_MASK mask = ace->Mask;
    wchar_t hex[16];
    swprintf_s(hex, L"0x%08lx", static_cast<unsigned long>(mask));
    // nobody else, for anything, on a private level
    if (level == ChainLevel::Private) return fail(L"ACE for " + sid_text(who) + L" mask " + hex + L" on a private level");
    if (mask & kAncestorForbidden) return fail(L"ACE for " + sid_text(who) + L" mask " + hex + L" (write/delete/permission)");
  }
  return true;
}

HeldChain::~HeldChain() { Release(); }

void HeldChain::Release() {
  for (HANDLE h : handles_) CloseHandle(h);
  handles_.clear();
}

namespace {

// lk-abcd C: the SIDs a private level's DACL grants anything to, other than the trusted principals (what a reset
// to kPrivateSddl takes away). An ACE of an unusual type is named by its type.
std::vector<std::wstring> foreign_grants(PSECURITY_DESCRIPTOR sd, const std::function<bool(PSID)>& ownerTrusted) {
  std::vector<std::wstring> out;
  BOOL present = FALSE, defaulted = FALSE;
  PACL dacl = nullptr;
  if (!GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) || !present || !dacl) return out;
  ACL_SIZE_INFORMATION info{};
  if (!GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation)) return out;
  for (DWORD i = 0; i < info.AceCount; ++i) {
    void* raw = nullptr;
    if (!GetAce(dacl, i, &raw)) continue;
    const auto* header = static_cast<ACE_HEADER*>(raw);
    if (header->AceFlags & INHERIT_ONLY_ACE) continue;
    if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
      out.push_back(L"(ACE type " + std::to_wstring(header->AceType) + L")");
      continue;
    }
    PSID who = const_cast<DWORD*>(&static_cast<ACCESS_ALLOWED_ACE*>(raw)->SidStart);
    if (!ownerTrusted(who)) out.push_back(sid_text(who));
  }
  return out;
}

// lk-abcd C: the private level `path` (already opened and refused on `heldHandle`) is reset to kPrivateSddl's DACL,
// protected -- ONLY when it is a directory, not a reparse point, and owned by a trusted principal; the refusal
// was then about who else the DACL lets in, and taking those grants away only tightens it. Its handle is opened
// without following a reparse point and checked to be the same object as the held one. Returns 0 when reset
// (*removed: the SIDs taken away), or the reason it was not.
DWORD reset_private_dacl(const std::wstring& path, HANDLE heldHandle, const std::function<bool(PSID)>& ownerTrusted,
                         std::vector<std::wstring>* removed, std::wstring* why) {
  HANDLE h = open_no_follow(path, READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, true);
  if (h == INVALID_HANDLE_VALUE) {
    const DWORD e = GetLastError();
    *why = L"repair: open for WRITE_DAC failed (win32 " + std::to_wstring(e) + L")";
    return e ? e : ERROR_ACCESS_DENIED;
  }
  BY_HANDLE_FILE_INFORMATION a{}, b{};
  const bool same = GetFileInformationByHandle(h, &a) && GetFileInformationByHandle(heldHandle, &b) &&
                    a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
                    a.nFileIndexLow == b.nFileIndexLow;
  if (!same || (a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    CloseHandle(h);
    *why = !same ? L"repair: not the object that was checked" : L"repair: a reparse point or not a directory";
    return ERROR_ACCESS_DENIED;
  }
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr, nullptr,
                      nullptr, nullptr, &sd) != ERROR_SUCCESS) {
    CloseHandle(h);
    *why = L"repair: security unreadable";
    return ERROR_ACCESS_DENIED;
  }
  PSID owner = nullptr;
  BOOL defaulted = FALSE;
  if (!GetSecurityDescriptorOwner(sd, &owner, &defaulted) || !ownerTrusted(owner)) {
    *why = L"repair: not done -- owner " + sid_text(owner) + L" is not trusted";
    LocalFree(sd);
    CloseHandle(h);
    return ERROR_ACCESS_DENIED;
  }
  *removed = foreign_grants(sd, ownerTrusted);
  LocalFree(sd);
  PSECURITY_DESCRIPTOR priv = nullptr;
  BOOL daclPresent = FALSE;
  PACL dacl = nullptr;
  DWORD rc = ERROR_ACCESS_DENIED;
  if (ConvertStringSecurityDescriptorToSecurityDescriptorW(kPrivateSddl, SDDL_REVISION_1, &priv, nullptr) &&
      GetSecurityDescriptorDacl(priv, &daclPresent, &dacl, &defaulted) && daclPresent && dacl) {
    rc = SetSecurityInfo(h, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                         nullptr, dacl, nullptr);
  }
  if (priv) LocalFree(priv);
  CloseHandle(h);
  if (rc != ERROR_SUCCESS) {
    *why = L"repair: SetSecurityInfo failed (win32 " + std::to_wstring(rc) + L")";
    removed->clear();
  }
  return rc;
}

}  // namespace

DWORD HoldRecordChainImpl(const std::wstring& recordPath, bool create, const std::function<bool(PSID)>& ownerTrusted,
                          HeldChain* held, std::vector<std::wstring>* repairedSids);

DWORD HoldRecordChain(const std::wstring& recordPath, bool create, const std::function<bool(PSID)>& ownerTrusted,
                      HeldChain* held) {
  return HoldRecordChainImpl(recordPath, create, ownerTrusted, held, nullptr);
}

DWORD HoldRecordChainRepairingPrivate(const std::wstring& recordPath, bool create,
                                      const std::function<bool(PSID)>& ownerTrusted, HeldChain* held,
                                      std::vector<std::wstring>* repairedSids) {
  repairedSids->clear();
  return HoldRecordChainImpl(recordPath, create, ownerTrusted, held, repairedSids);
}

// The loop both share (HeldChain's friend: it fills the held handles).
DWORD HoldRecordChainImpl(const std::wstring& recordPath, bool create, const std::function<bool(PSID)>& ownerTrusted,
                          HeldChain* held, std::vector<std::wstring>* repairedSids) {
  held->Release();
  held->failedAt.clear();
  held->failedWhy.clear();
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
        held->failedWhy = L"create failed";
        const DWORD e = GetLastError();
        held->Release();
        return e ? e : ERROR_ACCESS_DENIED;
      }
      h = open_no_follow(l.path, kHoldAccess, FILE_SHARE_READ | FILE_SHARE_WRITE, true);
    }
    if (h == INVALID_HANDLE_VALUE) {
      const DWORD e = GetLastError();
      held->failedAt = l.path;
      held->failedWhy = L"open failed";
      held->Release();
      return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? ERROR_PATH_NOT_FOUND : e;
    }
    held->handles_.push_back(h);
    if (!handle_acceptable(h, /*directory=*/true, l.level, ownerTrusted, l.checkDacl, &held->failedWhy)) {
      // lk-abcd C: a private level refused for its DACL, its ancestors passed and still held: reset it to the
      // private DACL when the caller asked for that (and it may be: reset_private_dacl's conditions), then check
      // it again on the held handle. Anything else is refused as before.
      if (repairedSids && l.level == ChainLevel::Private) {
        std::wstring repairWhy;
        const std::wstring before = held->failedWhy;
        if (reset_private_dacl(l.path, h, ownerTrusted, repairedSids, &repairWhy) == 0 &&
            handle_acceptable(h, /*directory=*/true, l.level, ownerTrusted, l.checkDacl, &held->failedWhy)) {
          held->failedWhy.clear();
          continue;
        }
        held->failedWhy = before + L"; " + repairWhy;
      }
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
