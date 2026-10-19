#pragma once

// Prelogin machine credential record (prelogin-service A1, contract section 2).
//
// A machine-scoped credential lets the SYSTEM service re-register the host with the directory BEFORE
// any user logs in (the per-user host.json lives in a profile SYSTEM cannot read). This header is the
// PURE record: its schema, its canonical byte serialization, and -- the security-critical part -- the
// binding/revocation validation that decides whether a stored record may be used right now.
//
// The validation encodes the contract's §2 rules as testable logic:
//   - bind to directory origin + accountId + machineId + hostId (mirrors directory_client LoadCache);
//   - an explicit `enabled` + a monotonic `ownerGeneration` so a feature-OFF, an account change, a
//     server revoke, or an updater/rollback that discarded the credential cannot be silently resurrected
//     by an older on-disk copy (a record below the known floor is Revoked, not Usable);
//   - a temporary network outage is NOT revocation -- that is the caller's concern (this only judges the
//     record against the known-good binding), so nothing here invalidates on a transient failure.
//
// Pure and portable: no windows.h, no DPAPI. The on-disk sealing/ACL is prelogin_machine_store.{hpp,cpp}.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace remote60::native_poc::prelogin {

constexpr uint32_t kMachineRecordMagic = 0x524D4C47u;  // "GLMR"
constexpr uint32_t kMachineRecordSchema = 1u;

struct MachineRecord {
  uint32_t schemaVersion = kMachineRecordSchema;
  uint64_t accountId = 0;            // hash of the GNLink account the credential belongs to
  uint64_t hostId = 0;               // directory host id
  uint64_t ownerGeneration = 0;      // bumped on enable/disable/revoke; monotonic (resurrection guard)
  bool enabled = false;              // prelogin remote access explicitly enabled by an admin
  std::string directoryOrigin;       // canonical origin the credential was enrolled against
  std::string machineId;             // HKLM MachineGuid at enrollment
  std::vector<uint8_t> credential;   // the host credential (e.g. hostToken) bytes; NEVER logged
};

enum class RecordVerdict {
  Usable,             // bound to this machine/account/origin, enabled, not revoked
  Malformed,          // missing machineId/credential/hostId, or a bad buffer
  SchemaUnsupported,  // a schema this build does not understand
  OriginMismatch,     // enrolled against an origin not currently listed/migratable
  MachineMismatch,    // enrolled on a different machine (MachineGuid changed / copied blob)
  AccountMismatch,    // belongs to a different GNLink account than the one configured
  Revoked,            // ownerGeneration is below the known floor (feature off / revoked / rolled back)
  Disabled,           // enrolled + bound but prelogin is not currently enabled
};

// What the caller already resolved about the live environment (so this stays pure).
struct BindingContext {
  std::string currentMachineId;   // HKLM MachineGuid now
  uint64_t expectedAccountId = 0; // 0 = unspecified (accept any account's record)
  bool originAllowed = false;     // caller ran directory_client origin classification: listed/migratable
  uint64_t minOwnerGeneration = 0;// floor: a record below this was superseded/revoked and must not resurrect
  bool featureGateEnabled = true; // the live feature switch (off -> Disabled regardless of the record)
};

// The single decision. Order matters and is the contract: schema/shape, then identity binding
// (origin/machine/account), then the revocation floor, then enablement.
inline RecordVerdict ValidateMachineRecord(const MachineRecord& r, const BindingContext& ctx) {
  if (r.schemaVersion != kMachineRecordSchema) return RecordVerdict::SchemaUnsupported;
  if (r.machineId.empty() || r.credential.empty() || r.hostId == 0) return RecordVerdict::Malformed;
  if (!ctx.originAllowed) return RecordVerdict::OriginMismatch;
  if (r.machineId != ctx.currentMachineId) return RecordVerdict::MachineMismatch;
  if (ctx.expectedAccountId != 0 && r.accountId != ctx.expectedAccountId) return RecordVerdict::AccountMismatch;
  if (r.ownerGeneration < ctx.minOwnerGeneration) return RecordVerdict::Revoked;
  if (!r.enabled || !ctx.featureGateEnabled) return RecordVerdict::Disabled;
  return RecordVerdict::Usable;
}

// --- canonical serialization (explicit little-endian; no struct memcpy) -----------------------------
namespace detail {
inline void put_u32(std::vector<uint8_t>* o, uint32_t v) {
  for (int i = 0; i < 4; ++i) o->push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}
inline void put_u64(std::vector<uint8_t>* o, uint64_t v) {
  for (int i = 0; i < 8; ++i) o->push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}
inline void put_bytes(std::vector<uint8_t>* o, const uint8_t* p, size_t n) {
  put_u32(o, static_cast<uint32_t>(n));
  o->insert(o->end(), p, p + n);
}
inline bool get_u32(const uint8_t* b, size_t n, size_t* off, uint32_t* v) {
  if (*off + 4 > n) return false;
  *v = 0;
  for (int i = 0; i < 4; ++i) *v |= static_cast<uint32_t>(b[*off + i]) << (8 * i);
  *off += 4;
  return true;
}
inline bool get_u64(const uint8_t* b, size_t n, size_t* off, uint64_t* v) {
  if (*off + 8 > n) return false;
  *v = 0;
  for (int i = 0; i < 8; ++i) *v |= static_cast<uint64_t>(b[*off + i]) << (8 * i);
  *off += 8;
  return true;
}
inline bool get_bytes(const uint8_t* b, size_t n, size_t* off, std::vector<uint8_t>* out) {
  uint32_t len = 0;
  if (!get_u32(b, n, off, &len)) return false;
  if (*off + len > n) return false;
  out->assign(b + *off, b + *off + len);
  *off += len;
  return true;
}
}  // namespace detail

inline std::vector<uint8_t> SerializeMachineRecord(const MachineRecord& r) {
  std::vector<uint8_t> o;
  detail::put_u32(&o, kMachineRecordMagic);
  detail::put_u32(&o, r.schemaVersion);
  detail::put_u64(&o, r.accountId);
  detail::put_u64(&o, r.hostId);
  detail::put_u64(&o, r.ownerGeneration);
  detail::put_u32(&o, r.enabled ? 1u : 0u);
  detail::put_bytes(&o, reinterpret_cast<const uint8_t*>(r.directoryOrigin.data()), r.directoryOrigin.size());
  detail::put_bytes(&o, reinterpret_cast<const uint8_t*>(r.machineId.data()), r.machineId.size());
  detail::put_bytes(&o, r.credential.data(), r.credential.size());
  return o;
}

// Returns false on any truncation / bad magic -- a corrupt or foreign blob never yields a half-built
// record the caller might trust.
inline bool ParseMachineRecord(const uint8_t* b, size_t n, MachineRecord* out) {
  size_t off = 0;
  uint32_t magic = 0;
  if (!detail::get_u32(b, n, &off, &magic) || magic != kMachineRecordMagic) return false;
  MachineRecord r;
  uint32_t enabled = 0;
  std::vector<uint8_t> origin, machine;
  if (!detail::get_u32(b, n, &off, &r.schemaVersion)) return false;
  if (!detail::get_u64(b, n, &off, &r.accountId)) return false;
  if (!detail::get_u64(b, n, &off, &r.hostId)) return false;
  if (!detail::get_u64(b, n, &off, &r.ownerGeneration)) return false;
  if (!detail::get_u32(b, n, &off, &enabled)) return false;
  if (!detail::get_bytes(b, n, &off, &origin)) return false;
  if (!detail::get_bytes(b, n, &off, &machine)) return false;
  if (!detail::get_bytes(b, n, &off, &r.credential)) return false;
  if (off != n) return false;  // trailing garbage -> reject
  r.enabled = enabled != 0;
  r.directoryOrigin.assign(origin.begin(), origin.end());
  r.machineId.assign(machine.begin(), machine.end());
  *out = std::move(r);
  return true;
}

// --- directory credential carried inside the record's `credential` bytes ---------------------------
// The directory client identifies a host by STRING accountId / hostId / hostToken (host.json), which the
// SYSTEM prelogin worker cannot read from the per-user cache. So the machine record's opaque `credential`
// blob carries exactly those three strings; the record's directoryOrigin is the URL. This keeps the
// record struct stable while giving the worker the real directory credential to adopt (r4 #3).
struct DirectoryCredential {
  std::string accountId;
  std::string hostId;
  std::string hostToken;
};

inline std::vector<uint8_t> PackDirectoryCredential(const DirectoryCredential& c) {
  std::vector<uint8_t> o;
  detail::put_bytes(&o, reinterpret_cast<const uint8_t*>(c.accountId.data()), c.accountId.size());
  detail::put_bytes(&o, reinterpret_cast<const uint8_t*>(c.hostId.data()), c.hostId.size());
  detail::put_bytes(&o, reinterpret_cast<const uint8_t*>(c.hostToken.data()), c.hostToken.size());
  return o;
}

inline bool UnpackDirectoryCredential(const std::vector<uint8_t>& b, DirectoryCredential* out) {
  size_t off = 0;
  std::vector<uint8_t> a, h, t;
  if (!detail::get_bytes(b.data(), b.size(), &off, &a)) return false;
  if (!detail::get_bytes(b.data(), b.size(), &off, &h)) return false;
  if (!detail::get_bytes(b.data(), b.size(), &off, &t)) return false;
  if (off != b.size()) return false;  // trailing garbage -> reject
  out->accountId.assign(a.begin(), a.end());
  out->hostId.assign(h.begin(), h.end());
  out->hostToken.assign(t.begin(), t.end());
  return true;
}

}  // namespace remote60::native_poc::prelogin
