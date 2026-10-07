#pragma once

// Prelogin machine credential store (prelogin-service A1, contract section 2) -- the on-disk half.
//
// Seals a MachineRecord with DPAPI LOCAL_MACHINE scope and writes it atomically, optionally applying a
// protected DACL that grants only SYSTEM and Administrators (so another user on the box cannot read the
// machine blob even though LOCAL_MACHINE DPAPI alone would let them decrypt it). The service is the
// single writer; readers only ReadAndOpen. Windows-only (definitions in the .cpp).
//
// NOTE on scope vs ACL (Codex r1 §2 / MS DPAPI docs): LOCAL_MACHINE DPAPI is decryptable by ANY account
// on the same machine, so the restrictive DACL is load-bearing, not decoration. The ACL that fully
// resists a non-admin reader can only be *applied* by a sufficiently privileged writer (the SYSTEM
// service in production); this API applies it and also lets a caller read back the on-disk DACL to audit
// it.

#include <cstdint>
#include <string>

#include "prelogin_machine_record.hpp"

namespace remote60::native_poc::prelogin {

enum class MachineStoreStatus {
  Ok,
  Protect,    // CryptProtectData failed
  Write,      // writing / atomic replace failed
  Dacl,       // applying the protected DACL failed
  Read,       // reading the file failed
  Unprotect,  // CryptUnprotectData failed (missing / tampered / wrong machine)
  Parse,      // the decrypted bytes are not a valid record
};

// Serialize -> DPAPI LOCAL_MACHINE protect -> atomic write (temp + replace). If applyProtectedDacl, set
// a SYSTEM+Administrators-only protected DACL on the file. `win32` (optional) gets GetLastError on
// failure. The plaintext/credential is never written unencrypted and never logged.
MachineStoreStatus SealAndWriteMachineRecord(const std::wstring& path, const MachineRecord& r,
                                             bool applyProtectedDacl, uint32_t* win32);

// Read -> DPAPI unprotect -> parse. A missing file, a tampered blob, or a blob sealed on another machine
// fails at Unprotect; a decrypted-but-malformed blob fails at Parse.
MachineStoreStatus ReadAndOpenMachineRecord(const std::wstring& path, MachineRecord* out, uint32_t* win32);

// Read back the file's DACL as an SDDL string (owner can always read the security descriptor). For the
// service to audit the on-disk protection and for tests to assert the ACL shape.
bool ReadDaclSddl(const std::wstring& path, std::wstring* outSddl);

}  // namespace remote60::native_poc::prelogin
