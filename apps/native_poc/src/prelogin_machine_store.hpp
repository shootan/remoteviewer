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
  Write,      // creating the protected temp / writing / atomic replace failed
  Dacl,       // building the protected security descriptor / the protected parent failed
  Read,       // reading the file failed
  Unprotect,  // CryptUnprotectData failed (missing / tampered / wrong machine)
  Parse,      // the decrypted bytes are not a valid record
};

// Serialize -> DPAPI LOCAL_MACHINE protect -> write to a UNIQUE temp that is created WITH the protected
// (SYSTEM+Administrators-only) security descriptor FROM THE START (no window where the blob is readable),
// then atomically replace the final path. The protection is NOT optional -- there is no unprotected
// production path (Codex S1). `win32` (optional) gets GetLastError on failure. The plaintext/credential
// is never written unencrypted and never logged.
MachineStoreStatus SealAndWriteMachineRecord(const std::wstring& path, const MachineRecord& r,
                                             uint32_t* win32);

// Read -> DPAPI unprotect -> parse. A missing file, a tampered blob, or a blob sealed on another machine
// fails at Unprotect; a decrypted-but-malformed blob fails at Parse.
MachineStoreStatus ReadAndOpenMachineRecord(const std::wstring& path, MachineRecord* out, uint32_t* win32);

// Read back the file's DACL as an SDDL string (owner can always read the security descriptor). For the
// service to audit the on-disk protection and for tests to assert the ACL shape.
bool ReadDaclSddl(const std::wstring& path, std::wstring* outSddl);

// --- G5: trusted-ancestry validation (read AND create paths) ---------------------------------------
//
// Codex G5: fixing the create-time DACL left the READ path and a pre-existing parent unvalidated. A
// non-admin who pre-creates the directory stays its owner (keeps WRITE_DAC), and a reparse point can
// redirect the opened object -- machine-scope DPAPI is not a substitute for this origin/access control.
// Both the create and read paths now require: the object is NOT a reparse point AND is owned by SYSTEM or
// BUILTIN\Administrators. A hostile pre-existing path is rejected, not "repaired then trusted".

// Pure-ish: is `sid` the Local System or BUILTIN\Administrators SID? (exposed for the counter-example test)
bool OwnerSidIsTrusted(const void* sid);

// Open `path` (file or directory) WITHOUT following reparse points and verify it is a real, non-reparse
// object owned by a trusted principal. Returns Ok, Dacl (reparse or untrusted owner), or Read (cannot open).
MachineStoreStatus ValidateProtectedPath(const std::wstring& path, uint32_t* win32);

}  // namespace remote60::native_poc::prelogin
