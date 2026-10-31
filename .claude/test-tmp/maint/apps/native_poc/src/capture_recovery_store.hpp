#pragma once

// dxgi-wedge-fix r1 (Codex B2/B3/B4): the DXGI quarantine, remembered across supervisor runs.
//
// Field incident: a user Host that wedged DXGI (exit 44) was put on WGC for the rest of that supervisor
// run only (host_recovery_policy.hpp). The next start of GNLinkHost -- a reboot, a sign-in, an update --
// went back to DXGI and the first session wedged again ("every first connect"). This keeps the decision,
// per adapter + driver, for 7 days after the LAST real wedge.
//
// Where: <ProgramData from the known folder>\GNLink\capture-recovery\<adapter key>.rec, one small file
// per adapter. The folder chain is held and checked exactly as the machine credential's is
// (prelogin_store_chain: no reparse point at any level, GNLink not deletable/re-permissionable by others,
// capture-recovery and every file in it SYSTEM + Administrators only). A file must also have one link.
// Writes go through a CREATE_NEW temporary in the same protected folder and a handle rename over the
// target, under a lock file inside that folder (concurrent supervisors of two signed-in users serialize
// there; a Global\ mutex could be squatted by anyone). Nothing follows a link; nothing outside the
// folder is written or removed.
//
// What a record holds -- and nothing else: schema, adapter key, driver version, first/last wedge, the
// wedge count, expiry (= last + 7 days; never extended by a start, a session or a dxgi request).
// A record is REFUSED (and the reason logged) when it is malformed, of another schema, over 4 KiB, has
// times that run backwards or lie in the future, or fails the security check; the supervisor then
// behaves as before this file existed (its in-run policy still holds). A record APPLIES only when its
// adapter is present with the same driver and it has not expired.
//
// Only the user Host supervisor uses this. The pre-login worker never reads it (Codex B4).

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "capture_adapter_identity.hpp"

namespace remote60::native_poc::capture_recovery {

inline constexpr int kSchema = 1;
inline constexpr uint64_t kQuarantineSeconds = 7ull * 24 * 3600;
inline constexpr size_t kRecordMaxBytes = 4096;
inline constexpr size_t kMaxRecords = 16;
// A record "from the future" by more than this is refused (clock skew allowance).
inline constexpr uint64_t kFutureSkewSeconds = 300;

struct Record {
  std::string adapterKey;
  std::string driver;
  uint64_t firstUnix = 0;
  uint64_t lastUnix = 0;
  uint64_t expiresUnix = 0;
  uint32_t wedges = 0;
};

// ---- pure --------------------------------------------------------------------------------------
std::string FormatRecord(const Record& r);
// Empty `why` on success; otherwise a short reason (malformed / schema / size / times / key).
bool ParseRecord(const std::string& text, Record* out, std::string* why);
// The record after one more real exit 44 on `adapter` at `nowUnix`. A different driver starts over.
Record NextRecord(const Record* existing, const CaptureAdapterIdentity& adapter, uint64_t nowUnix);

struct StartupDecision {
  bool quarantine = false;
  std::string adapterKey;  // the record that applied
  std::string driver;
  uint64_t expiresUnix = 0;
  std::vector<std::string> notes;  // one per record that did not apply, and why
};
// Which (if any) stored record applies to the adapters present now.
StartupDecision DecideAtStartup(const std::vector<Record>& records, const std::vector<CaptureAdapterIdentity>& present,
                                uint64_t nowUnix);
// "persisted:key=<16>:driver=<v>:expires=<unix>" -- the child's REMOTE60_CAPTURE_DXGI_QUARANTINE value.
std::string PersistedReason(const StartupDecision& d);

uint64_t NowUnix();

// ---- the folder ---------------------------------------------------------------------------------
// <ProgramData>\GNLink\capture-recovery; empty when the known folder cannot be resolved.
std::wstring ProductFolder();

class Store {
 public:
  // `folder` = <root>\GNLink\capture-recovery. `ownerTrusted` decides who may own/hold rights (the
  // product: SYSTEM + Administrators). `fileSddl`/`folderSddl`: what new files / a new folder get (null =
  // the product's SYSTEM + Administrators only); a test that is not elevated names itself as well.
  Store(std::wstring folder, std::function<bool(PSID)> ownerTrusted, const wchar_t* fileSddl = nullptr);

  // Every acceptable record (at most kMaxRecords), and a note for each one refused. 0 or the win32
  // error of the folder itself (ERROR_PATH_NOT_FOUND when nothing was ever written).
  DWORD LoadAll(std::vector<Record>* out, std::vector<std::string>* refused) const;
  // Records one real exit 44 on `adapter`. 0, or the win32 error; `why` says what failed.
  DWORD RecordWedge(const CaptureAdapterIdentity& adapter, uint64_t nowUnix, Record* written, std::string* why) const;

 private:
  std::wstring folder_;
  std::function<bool(PSID)> ownerTrusted_;
  std::wstring fileSddl_;
};

}  // namespace remote60::native_poc::capture_recovery
