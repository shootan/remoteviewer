#pragma once

// Where the machine record may live, and how it is held while used (prelogin-ui r7, H3).
//
// The record is <root>\GNLink\credential\machine.cred, root = %ProgramData%. Before r7 only the record's
// direct folder was looked at, only its OWNER, through a handle closed at once -- the path was then used
// again by name. Now:
//   * every level is opened WITHOUT following a reparse point and checked on that handle: the root (not a
//     redirect, a trusted owner), GNLink (also: nobody else may delete, rename, re-permission or take
//     it -- adding entries is what %ProgramData% allows everyone, and is not a way in), credential and the
//     record (also: nobody but SYSTEM and Administrators has ANY right on them);
//   * the folder handles stay open, without delete sharing, for as long as the record is read or written,
//     so no level can be renamed or replaced between the check and the use; the record itself is read
//     through the handle that was checked.
// The trusted principals -- who may own a level and who may hold rights on it -- are SYSTEM and
// Administrators (r6 G5); a test may pass its own rule (adding itself) to exercise the rest with folders it
// can create.

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace remote60::native_poc::prelogin {

enum class ChainLevel { Ancestor, Private };

// The pure rule over a security descriptor (owner + DACL): see above. `ownerTrusted` decides the owner.
// lk-abcd C: `why`, when given, is told what failed -- "owner <SID>", "no DACL", "unusual ACE type N", or
// "ACE for <SID> mask 0x..." -- so a refusal names the principal instead of only "win32 5".
bool SecurityDescriptorAcceptable(PSECURITY_DESCRIPTOR sd, ChainLevel level, const std::function<bool(PSID)>& ownerTrusted,
                                  std::wstring* why = nullptr);
// The product's owner rule: SYSTEM or BUILTIN\Administrators.
bool OwnerIsSystemOrAdministrators(PSID owner);

// The folders of a record path held open and checked. Closing it releases them.
class HeldChain {
 public:
  HeldChain() = default;
  HeldChain(const HeldChain&) = delete;
  HeldChain& operator=(const HeldChain&) = delete;
  ~HeldChain();
  void Release();
  std::wstring failedAt;  // the level that failed, for the log
  std::wstring failedWhy;  // lk-abcd C: and why (SecurityDescriptorAcceptable's reason, or the step), for the log
 private:
  friend DWORD HoldRecordChain(const std::wstring&, bool, const std::function<bool(PSID)>&, HeldChain*);
  friend DWORD HoldRecordChainImpl(const std::wstring&, bool, const std::function<bool(PSID)>&, HeldChain*,
                                   std::vector<std::wstring>*);
  std::vector<HANDLE> handles_;
};

// Opens and checks root, root\GNLink, root\GNLink\credential for `recordPath`
// (= root\GNLink\credential\<name>), creating the two GNLink levels when `create` (credential with
// SYSTEM+Administrators-only access, GNLink with read for everyone else). 0, or the win32 error
// (ERROR_ACCESS_DENIED for a level that fails the rule, ERROR_PATH_NOT_FOUND for a missing level when
// not creating).
DWORD HoldRecordChain(const std::wstring& recordPath, bool create, const std::function<bool(PSID)>& ownerTrusted,
                      HeldChain* held);

// lk-abcd C: the same, except that a PRIVATE level (the record's own folder) refused for its DACL is reset to the
// private DACL (SYSTEM + Administrators, protected) and checked again -- only when that folder is a directory,
// not a reparse point, owned by a trusted principal, with its ancestors passed and held. *repairedSids: the SIDs
// the reset took away (empty when nothing was reset). The owner rule, the ancestors, a reparse point: refused as
// by HoldRecordChain. For the pre-login worker's log folder (2026-11-03: one user's full-control ACE there kept
// prelogin_worker.log unwritten since 10-22).
DWORD HoldRecordChainRepairingPrivate(const std::wstring& recordPath, bool create,
                                      const std::function<bool(PSID)>& ownerTrusted, HeldChain* held,
                                      std::vector<std::wstring>* repairedSids);

// Opens the record itself (the chain must be held) for reading, checks it as Private on its handle and
// reads it through that handle. 0, or the win32 error (ERROR_FILE_NOT_FOUND when there is none).
DWORD ReadHeldRecord(const std::wstring& recordPath, const std::function<bool(PSID)>& ownerTrusted,
                     std::vector<uint8_t>* out);

}  // namespace remote60::native_poc::prelogin
