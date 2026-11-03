// prelogin-ui r7 (H3): where the machine record may live, and how it is held while used.
//
// Pure: the rule over a security descriptor (owner + DACL) for an ancestor level and for the private
// levels (credential folder, record), with synthetic descriptors -- among them the case the r6 check
// passed: a trusted owner with a permissive DACL.
// Real folders (a test root this process makes): the chain is held and checked on handles; an UPPER level
// that is a junction is refused; permissive DACLs are refused; while the chain is held its folders cannot
// be renamed away (and can once it is released -- so the hold is what stops it); the record is read
// through its checked handle. The test's rule trusts this user besides SYSTEM/Administrators (a
// non-elevated test cannot make SYSTEM-owned folders); the product's rule is SYSTEM/Administrators only.

#include <winsock2.h>
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winioctl.h>

#include <cstddef>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "e2e_isolation.hpp"
#include "prelogin_store_chain.hpp"

namespace pl = remote60::native_poc::prelogin;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

std::wstring my_sid_string() {
  HANDLE tok = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
  DWORD need = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
  std::vector<BYTE> buf(need);
  GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
  CloseHandle(tok);
  LPWSTR s = nullptr;
  ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &s);
  std::wstring out = s;
  LocalFree(s);
  return out;
}

bool rule(const std::wstring& sddl, pl::ChainLevel level, const std::function<bool(PSID)>& trusted = pl::OwnerIsSystemOrAdministrators) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  const bool ok = pl::SecurityDescriptorAcceptable(sd, level, trusted);
  LocalFree(sd);
  return ok;
}

bool make_dir(const std::wstring& path, const std::wstring& sddl) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  const bool ok = CreateDirectoryW(path.c_str(), &sa) != FALSE;
  LocalFree(sd);
  return ok;
}

bool make_file(const std::wstring& path, const std::wstring& sddl, const std::string& content) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  LocalFree(sd);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD wrote = 0;
  const bool ok = WriteFile(f, content.data(), static_cast<DWORD>(content.size()), &wrote, nullptr) && wrote == content.size();
  CloseHandle(f);
  return ok;
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
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("machine-record path: checked and held (prelogin-ui r7, H3)\n\n");
  const auto anc = pl::ChainLevel::Ancestor;
  const auto priv = pl::ChainLevel::Private;
  std::printf("--- the rule, on descriptors ---\n");
  check("private, SYSTEM+Administrators only, owner Administrators: accepted", rule(L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)", priv));
  check("H3: private with a TRUSTED owner but Users may read: refused (the r6 check passed this)",
        !rule(L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)", priv));
  check("H3: private with a trusted owner, Everyone full control: refused", !rule(L"O:SYD:(A;;FA;;;SY)(A;;FA;;;WD)", priv));
  check("private owned by Users: refused", !rule(L"O:BUD:P(A;;FA;;;SY)(A;;FA;;;BA)", priv));
  check("no DACL at all (everyone everything): refused", !rule(L"O:SY", priv) && !rule(L"O:SY", anc));
  check("ancestor where Users may look inside: accepted", rule(L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x1200a9;;;BU)", anc));
  check("ancestor where Users may add entries (what %ProgramData% gives everyone): accepted",
        rule(L"O:SYD:(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x116;;;BU)", anc));
  check("H3: ancestor where Users may change its permissions (WRITE_DAC): refused",
        !rule(L"O:SYD:(A;;FA;;;SY)(A;;FA;;;BA)(A;;WD;;;BU)", anc));
  check("H3: ancestor where Users may delete children (FILE_DELETE_CHILD): refused",
        !rule(L"O:SYD:(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x40;;;BU)", anc));
  check("an inherit-only CREATOR OWNER entry (it applies to children) is not a right here",
        rule(L"O:SYD:(A;;FA;;;SY)(A;;FA;;;BA)(A;OICIIO;GA;;;CO)", anc));

  // lk-abcd C: the refusal says why. The 2026-11-03 product case: C:\ProgramData\GNLink\logs owned by
  // Administrators, protected DACL SY+BA full + ONE named user full -- refused as "win32 5" with no principal.
  std::printf("\n--- lk-abcd C: why it was refused ---\n");
  {
    const std::wstring me = my_sid_string();
    std::wstring why;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const std::wstring logsLike = L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + me + L")";
    ConvertStringSecurityDescriptorToSecurityDescriptorW(logsLike.c_str(), SDDL_REVISION_1, &sd, nullptr);
    const bool refused = !pl::SecurityDescriptorAcceptable(sd, priv, pl::OwnerIsSystemOrAdministrators, &why);
    LocalFree(sd);
    std::wprintf(L"      why=\"%ls\"\n", why.c_str());
    check("the logs-folder shape (BA owner, SY+BA+one user full, protected): refused, naming that user's SID",
          refused && why.find(me) != std::wstring::npos && why.find(L"private level") != std::wstring::npos);
    why.clear();
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BUD:P(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1, &sd, nullptr);
    const bool ownerRefused = !pl::SecurityDescriptorAcceptable(sd, priv, pl::OwnerIsSystemOrAdministrators, &why);
    LocalFree(sd);
    check("an untrusted owner: refused, naming the owner (S-1-5-32-545)",
          ownerRefused && why == L"owner S-1-5-32-545");
    why.clear();
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SY", SDDL_REVISION_1, &sd, nullptr);
    const bool noDacl = !pl::SecurityDescriptorAcceptable(sd, priv, pl::OwnerIsSystemOrAdministrators, &why);
    LocalFree(sd);
    check("no DACL: refused as \"no DACL\"", noDacl && why == L"no DACL");
    why = L"untouched";
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1, &sd, nullptr);
    const bool accepted = pl::SecurityDescriptorAcceptable(sd, priv, pl::OwnerIsSystemOrAdministrators, &why);
    LocalFree(sd);
    check("accepted: the reason is left alone", accepted && why == L"untouched");
  }

  std::printf("\n--- real folders ---\n");
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"store_chain")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring me = my_sid_string();
  PSID mine = nullptr;
  ConvertStringSidToSidW(me.c_str(), &mine);
  const auto trustMe = [mine](PSID s) { return pl::OwnerIsSystemOrAdministrators(s) || (s && EqualSid(s, mine)); };
  const std::wstring privateSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + me + L")";
  const std::wstring ancestorSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + me + L")(A;;0x1200a9;;;BU)";
  auto build = [&](const std::wstring& root) {
    return make_dir(root, ancestorSddl) && make_dir(root + L"\\GNLink", ancestorSddl) &&
           make_dir(root + L"\\GNLink\\credential", privateSddl) &&
           make_file(root + L"\\GNLink\\credential\\machine.cred", privateSddl, "sealed-blob");
  };
  {
    const std::wstring root = staging.path() + L"good";
    check("a good chain is built", build(root));
    pl::HeldChain held;
    const DWORD rc = pl::HoldRecordChain(root + L"\\GNLink\\credential\\machine.cred", false, trustMe, &held);
    check("the good chain is held", rc == 0, std::to_string(rc));
    std::vector<uint8_t> data;
    check("...and the record is read through its checked handle",
          pl::ReadHeldRecord(root + L"\\GNLink\\credential\\machine.cred", trustMe, &data) == 0 &&
              std::string(data.begin(), data.end()) == "sealed-blob");
    const BOOL movedWhileHeld = MoveFileExW((root + L"\\GNLink").c_str(), (root + L"\\GNLink.moved").c_str(), 0);
    check("H3: while held, the GNLink folder cannot be renamed away (no check/use gap)", !movedWhileHeld,
          std::to_string(GetLastError()));
    const BOOL credMoved = MoveFileExW((root + L"\\GNLink\\credential").c_str(), (root + L"\\GNLink\\cred2").c_str(), 0);
    check("H3: ...nor the credential folder", !credMoved);
    held.Release();
    const BOOL movedAfter = MoveFileExW((root + L"\\GNLink").c_str(), (root + L"\\GNLink.moved").c_str(), 0);
    check("...and once released it can (so the hold is what stopped it)", movedAfter != FALSE);
    if (movedAfter) MoveFileExW((root + L"\\GNLink.moved").c_str(), (root + L"\\GNLink").c_str(), 0);
    check("the product rule (SYSTEM/Administrators only) refuses this user's folders",
          pl::HoldRecordChain(root + L"\\GNLink\\credential\\machine.cred", false, pl::OwnerIsSystemOrAdministrators, &held) ==
              ERROR_ACCESS_DENIED);
  }
  {
    // H3: an UPPER level (GNLink itself) is a junction to a chain that would pass.
    const std::wstring elsewhere = staging.path() + L"elsewhere";
    check("an acceptable chain elsewhere", build(elsewhere));
    const std::wstring root = staging.path() + L"upper";
    check("a root whose GNLink is a junction to it", make_dir(root, ancestorSddl) &&
                                                       make_junction(root + L"\\GNLink", elsewhere + L"\\GNLink"));
    pl::HeldChain held;
    const DWORD rc = pl::HoldRecordChain(root + L"\\GNLink\\credential\\machine.cred", false, trustMe, &held);
    check("H3: an UPPER-level junction is refused (not followed)", rc == ERROR_ACCESS_DENIED, std::to_string(rc));
    check("...it failed at that level", held.failedAt == root + L"\\GNLink");
  }
  {
    const std::wstring root = staging.path() + L"loose";
    check("a chain whose credential folder others may read", make_dir(root, ancestorSddl) &&
                                                               make_dir(root + L"\\GNLink", ancestorSddl) &&
                                                               make_dir(root + L"\\GNLink\\credential", privateSddl + L"(A;;FR;;;BU)"));
    pl::HeldChain held;
    check("H3: refused at the credential folder (trusted owner, permissive DACL)",
          pl::HoldRecordChain(root + L"\\GNLink\\credential\\machine.cred", false, trustMe, &held) == ERROR_ACCESS_DENIED &&
              held.failedAt == root + L"\\GNLink\\credential");
  }
  {
    const std::wstring root = staging.path() + L"loosefile";
    check("a good chain with a record others may read", make_dir(root, ancestorSddl) && make_dir(root + L"\\GNLink", ancestorSddl) &&
                                                          make_dir(root + L"\\GNLink\\credential", privateSddl) &&
                                                          make_file(root + L"\\GNLink\\credential\\machine.cred",
                                                                    privateSddl + L"(A;;FR;;;BU)", "x"));
    pl::HeldChain held;
    std::vector<uint8_t> data;
    check("H3: the record itself with a permissive DACL is not read",
          pl::HoldRecordChain(root + L"\\GNLink\\credential\\machine.cred", false, trustMe, &held) == 0 &&
              pl::ReadHeldRecord(root + L"\\GNLink\\credential\\machine.cred", trustMe, &data) == ERROR_ACCESS_DENIED &&
              data.empty());
  }
  {
    // Fresh: the GNLink levels do not exist yet and are created with the product's DACLs -- SYSTEM and
    // Administrators only (GNLink: others may look inside). This non-elevated user is in neither, so it can
    // make GNLink (the root lets it) but nothing inside it: the creation stops at the credential folder. The
    // service (SYSTEM) creates both; that round trip needs an elevated run and is not made here.
    const std::wstring root = staging.path() + L"fresh";
    check("a root alone", make_dir(root, ancestorSddl));
    pl::HeldChain held;
    const DWORD rc = pl::HoldRecordChain(root + L"\\GNLink\\credential\\machine.cred", true, trustMe, &held);
    check("missing GNLink is created; inside it this user (not SYSTEM/Administrators) may create nothing",
          rc == ERROR_ACCESS_DENIED && held.failedAt == root + L"\\GNLink\\credential",
          std::to_string(rc) + " at " + std::string(held.failedAt.begin(), held.failedAt.end()));
    PSECURITY_DESCRIPTOR sd = nullptr;
    PACL dacl = nullptr;
    std::wstring sddl;
    if (GetNamedSecurityInfoW((root + L"\\GNLink").c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                              nullptr, &sd) == ERROR_SUCCESS) {
      LPWSTR str = nullptr;
      if (ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &str, nullptr)) {
        sddl = str;
        LocalFree(str);
      }
      LocalFree(sd);
    }
    check("...and the GNLink it made carries the product's DACL exactly (protected; SY, BA full; BU look inside)",
          sddl == L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;;0x1200a9;;;BU)", std::string(sddl.begin(), sddl.end()));
    held.Release();
    // Let the scratch cleanup remove it: this user owns it, so it may put a DACL back on it.
    PSECURITY_DESCRIPTOR open = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(ancestorSddl.c_str(), SDDL_REVISION_1, &open, nullptr);
    BOOL present = FALSE, defaulted = FALSE;
    PACL mineDacl = nullptr;
    GetSecurityDescriptorDacl(open, &present, &mineDacl, &defaulted);
    SetNamedSecurityInfoW(const_cast<wchar_t*>((root + L"\\GNLink").c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                          nullptr, nullptr, mineDacl, nullptr);
    LocalFree(open);
  }
  // ---- lk-abcd C: the private level reset (HoldRecordChainRepairingPrivate), on folders of this test only
  const auto dacl_of = [](const std::wstring& path) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    PACL dacl = nullptr;
    std::wstring sddl;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &sd) ==
        ERROR_SUCCESS) {
      LPWSTR str = nullptr;
      if (ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &str, nullptr)) {
        sddl = str;
        LocalFree(str);
      }
      LocalFree(sd);
    }
    return sddl;
  };
  const auto give_back = [&](const std::wstring& path) {  // this user owns it: put its own DACL back for cleanup
    PSECURITY_DESCRIPTOR open = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(privateSddl.c_str(), SDDL_REVISION_1, &open, nullptr);
    BOOL present = FALSE, defaulted = FALSE;
    PACL d = nullptr;
    GetSecurityDescriptorDacl(open, &present, &d, &defaulted);
    SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, d,
                          nullptr);
    LocalFree(open);
  };
  std::printf("\n--- lk-abcd C: the worker-log folder shape, reset to the private DACL ---\n");
  {
    // The 2026-11-03 product shape, on this test's folders: a trusted-owned log folder whose protected DACL
    // also gives one more principal (here BUILTIN\Users) full control.
    const std::wstring root = staging.path() + L"repair";
    const std::wstring logs = root + L"\\GNLink\\logs";
    const std::wstring logPath = logs + L"\\prelogin_worker.log";
    check("a chain whose log folder gives Users full control",
          make_dir(root, ancestorSddl) && make_dir(root + L"\\GNLink", ancestorSddl) &&
              make_dir(logs, privateSddl + L"(A;OICI;FA;;;BU)"));
    pl::HeldChain held;
    const DWORD plain = pl::HoldRecordChain(logPath, false, trustMe, &held);
    check("NEGATIVE CONTROL: the plain hold refuses it (at the log folder) and changes nothing",
          plain == ERROR_ACCESS_DENIED && held.failedAt == logs && dacl_of(logs).find(L";;;BU)") != std::wstring::npos,
          std::to_string(plain));
    std::vector<std::wstring> removed;
    const DWORD rc = pl::HoldRecordChainRepairingPrivate(logPath, false, trustMe, &held, &removed);
    const std::wstring after = dacl_of(logs);
    std::wprintf(L"      after=\"%ls\"\n", after.c_str());
    check("lk-abcd C: THE REPAIRING HOLD RESETS THE LOG FOLDER AND HOLDS THE CHAIN", rc == 0, std::to_string(rc));
    // SetSecurityInfo marks a DACL it sets auto-inherited (AI); the entries are what matter.
    check("...the folder's DACL is now exactly SYSTEM + Administrators, protected",
          after == L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)" || after == L"D:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",
          std::string(after.begin(), after.end()));
    check("...and what it took away is named: this user (trusted by this test's rule, so not listed) kept nothing, "
          "Users (S-1-5-32-545) is the one removed grant",
          removed.size() == 1 && removed[0] == L"S-1-5-32-545");
    held.Release();
    // What was taken away is real: this user is neither SYSTEM nor Administrators (the service is SYSTEM), and
    // now cannot even list the folder -- the hold's own open is refused.
    std::vector<std::wstring> again;
    const DWORD second = pl::HoldRecordChainRepairingPrivate(logPath, false, trustMe, &held, &again);
    check("...after it, this (non-SYSTEM, non-admin) user cannot open the folder any more (win32 5) -- nothing re-granted",
          second == ERROR_ACCESS_DENIED && again.empty(), std::to_string(second));
    held.Release();
    give_back(logs);
  }
  {
    // An ancestor that fails is never repaired around: the product rule does not trust this user's GNLink.
    const std::wstring root = staging.path() + L"repair-anc";
    const std::wstring logs = root + L"\\GNLink\\logs";
    check("a chain owned by this user, its log folder loose too",
          make_dir(root, ancestorSddl) && make_dir(root + L"\\GNLink", ancestorSddl) &&
              make_dir(logs, privateSddl + L"(A;OICI;FA;;;BU)"));
    pl::HeldChain held;
    std::vector<std::wstring> removed;
    const DWORD rc =
        pl::HoldRecordChainRepairingPrivate(logs + L"\\prelogin_worker.log", false, pl::OwnerIsSystemOrAdministrators, &held, &removed);
    check("lk-abcd C NEGATIVE: an untrusted ANCESTOR is refused there -- the log folder is not touched",
          rc == ERROR_ACCESS_DENIED && held.failedAt != logs && removed.empty() &&
              dacl_of(logs).find(L";;;BU)") != std::wstring::npos,
          std::to_string(rc) + " at " + std::string(held.failedAt.begin(), held.failedAt.end()));
  }
  {
    // A log folder that is a junction is refused, not followed, and its target is not reset.
    const std::wstring target = staging.path() + L"repair-target";
    const std::wstring root = staging.path() + L"repair-junction";
    check("a loose folder elsewhere, and a chain whose log folder is a junction to it",
          make_dir(target, privateSddl + L"(A;OICI;FA;;;BU)") && make_dir(root, ancestorSddl) &&
              make_dir(root + L"\\GNLink", ancestorSddl) && make_junction(root + L"\\GNLink\\logs", target));
    pl::HeldChain held;
    std::vector<std::wstring> removed;
    const DWORD rc = pl::HoldRecordChainRepairingPrivate(root + L"\\GNLink\\logs\\prelogin_worker.log", false, trustMe, &held,
                                                         &removed);
    check("lk-abcd C NEGATIVE: a junction log folder is refused, nothing removed, its target's DACL unchanged",
          rc == ERROR_ACCESS_DENIED && removed.empty() && dacl_of(target).find(L";;;BU)") != std::wstring::npos,
          std::to_string(rc));
    RemoveDirectoryW((root + L"\\GNLink\\logs").c_str());  // the junction itself, not its target
  }

  LocalFree(mine);
  check("the scratch directory is cleaned up", staging.Remove(), staging.why());
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
