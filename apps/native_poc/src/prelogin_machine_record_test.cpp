// Tests for the prelogin machine credential record (prelogin_machine_record.hpp). The serialization
// round-trips and rejects corruption; the binding/revocation validation enforces the §2 contract with
// its negative controls (origin/machine/account mismatch, revocation floor, disabled, schema, malformed).

#include "prelogin_machine_record.hpp"

#include <cstdio>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}

MachineRecord good() {
  MachineRecord r;
  r.schemaVersion = kMachineRecordSchema;
  r.accountId = 0xA0C0DE01ull;
  r.hostId = 0x1122334455667788ull;
  r.ownerGeneration = 5;
  r.enabled = true;
  r.directoryOrigin = "https://dir.example:443";
  r.machineId = "MACHINE-GUID-1111";
  r.credential = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};
  return r;
}

BindingContext ctxFor(const MachineRecord& r) {
  BindingContext c;
  c.currentMachineId = r.machineId;
  c.expectedAccountId = r.accountId;
  c.originAllowed = true;
  c.minOwnerGeneration = r.ownerGeneration;  // floor at the record's own generation = not revoked
  c.featureGateEnabled = true;
  return c;
}
}  // namespace

int main() {
  std::printf("prelogin machine record\n");

  std::printf("[serialization round trip + corruption rejection]\n");
  {
    MachineRecord r = good();
    auto bytes = SerializeMachineRecord(r);
    MachineRecord back;
    check(ParseMachineRecord(bytes.data(), bytes.size(), &back), "parse a serialized record");
    check(back.accountId == r.accountId && back.hostId == r.hostId && back.ownerGeneration == r.ownerGeneration &&
              back.enabled == r.enabled && back.directoryOrigin == r.directoryOrigin &&
              back.machineId == r.machineId && back.credential == r.credential,
          "every field round-trips");
    // Truncation and trailing garbage are rejected.
    check(!ParseMachineRecord(bytes.data(), bytes.size() - 1, &back), "truncated blob -> reject");
    auto extra = bytes;
    extra.push_back(0x00);
    check(!ParseMachineRecord(extra.data(), extra.size(), &back), "trailing garbage -> reject");
    auto badMagic = bytes;
    badMagic[0] ^= 0xff;
    check(!ParseMachineRecord(badMagic.data(), badMagic.size(), &back), "bad magic -> reject");
  }

  std::printf("[validation: the usable case]\n");
  {
    MachineRecord r = good();
    check(ValidateMachineRecord(r, ctxFor(r)) == RecordVerdict::Usable, "bound+enabled+not-revoked -> Usable");
  }

  std::printf("[validation: NEGATIVE controls]\n");
  {
    MachineRecord r = good();

    BindingContext c = ctxFor(r);
    c.originAllowed = false;
    check(ValidateMachineRecord(r, c) == RecordVerdict::OriginMismatch, "origin not listed/migratable -> OriginMismatch");

    c = ctxFor(r);
    c.currentMachineId = "MACHINE-GUID-9999";
    check(ValidateMachineRecord(r, c) == RecordVerdict::MachineMismatch, "machineId changed/copied -> MachineMismatch");

    c = ctxFor(r);
    c.expectedAccountId = 0xBEEFull;
    check(ValidateMachineRecord(r, c) == RecordVerdict::AccountMismatch, "different account -> AccountMismatch");

    c = ctxFor(r);
    c.minOwnerGeneration = r.ownerGeneration + 1;  // a newer floor was recorded (revoke / rollback)
    check(ValidateMachineRecord(r, c) == RecordVerdict::Revoked,
          "record below the owner-generation floor -> Revoked (no resurrection)");

    c = ctxFor(r);
    c.featureGateEnabled = false;
    check(ValidateMachineRecord(r, c) == RecordVerdict::Disabled, "feature gate off -> Disabled");

    MachineRecord off = good();
    off.enabled = false;
    check(ValidateMachineRecord(off, ctxFor(off)) == RecordVerdict::Disabled, "record disabled -> Disabled");

    MachineRecord newer = good();
    newer.schemaVersion = kMachineRecordSchema + 1;
    check(ValidateMachineRecord(newer, ctxFor(newer)) == RecordVerdict::SchemaUnsupported, "newer schema -> SchemaUnsupported");

    MachineRecord empty = good();
    empty.credential.clear();
    check(ValidateMachineRecord(empty, ctxFor(empty)) == RecordVerdict::Malformed, "no credential -> Malformed");

    MachineRecord noHost = good();
    noHost.hostId = 0;
    check(ValidateMachineRecord(noHost, ctxFor(noHost)) == RecordVerdict::Malformed, "zero hostId -> Malformed");
  }

  std::printf("[validation: a transient outage is NOT revocation]\n");
  {
    // The validator judges a record against its known-good binding only; it has no notion of network
    // reachability, so a transient outage cannot flip a Usable record to Revoked. (The caller keeps the
    // record and retries; only an explicit floor bump revokes.)
    MachineRecord r = good();
    check(ValidateMachineRecord(r, ctxFor(r)) == RecordVerdict::Usable,
          "same inputs still Usable (no hidden network dependency)");
  }

  std::printf("[directory credential codec carried in the credential bytes (r4 #3)]\n");
  {
    DirectoryCredential dc;
    dc.accountId = "acct-42";
    dc.hostId = "host-abcdef";
    dc.hostToken = "tok-0123456789";
    const auto bytes = PackDirectoryCredential(dc);
    DirectoryCredential back;
    check(UnpackDirectoryCredential(bytes, &back), "unpack a packed directory credential");
    check(back.accountId == dc.accountId && back.hostId == dc.hostId && back.hostToken == dc.hostToken,
          "accountId / hostId / hostToken round-trip");
    std::vector<uint8_t> extra = bytes;
    extra.push_back(0x00);
    check(!UnpackDirectoryCredential(extra, &back), "NEGATIVE: trailing garbage -> reject");
    std::vector<uint8_t> truncated(bytes.begin(), bytes.end() - 1);
    check(!UnpackDirectoryCredential(truncated, &back), "NEGATIVE: truncated -> reject");
    // Empty strings are valid (an un-enrolled record) but must round-trip exactly.
    DirectoryCredential empty, emptyBack;
    check(UnpackDirectoryCredential(PackDirectoryCredential(empty), &emptyBack) &&
              emptyBack.hostToken.empty(),
          "empty credential round-trips");
  }

  if (gFailures != 0) {
    std::printf("prelogin_machine_record_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_machine_record_test: PASS\n");
  return 0;
}
