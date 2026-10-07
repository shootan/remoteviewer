#pragma once

// Durable prelogin owner-generation FLOOR (prelogin-service S2 / r6 G6).
//
// The floor lives OUTSIDE the sealed record, in HKLM\SOFTWARE\GNLink\PreloginOwnerFloor (QWORD, a
// SYSTEM/Admins key), so restoring an old on-disk blob cannot roll a revoke back: disable/revoke raises
// the floor above the record's ownerGeneration, and any record below the floor validates as Revoked.
//
// G6: reading it must DISTINGUISH a genuinely-absent value (never revoked -> floor 0, OK) from a real
// read error (value present but unreadable / access problem). An error is NOT floor 0 -- callers treat a
// failed read as fail-safe (feature off / worker stays offline), never fail-open. Both the SYSTEM service
// and the SYSTEM --prelogin worker read the same floor through this one definition.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

namespace remote60::native_poc::prelogin {

inline constexpr wchar_t kOwnerFloorKey[] = L"SOFTWARE\\GNLink";
inline constexpr wchar_t kOwnerFloorValue[] = L"PreloginOwnerFloor";

// Returns true and sets *out when the floor is KNOWN: the value is present (*out = N) or genuinely absent
// (*out = 0). Returns false on a real read error (present-but-unreadable / access denied) -- the caller
// must then fail safe, not assume floor 0.
inline bool ReadPreloginOwnerFloor(uint64_t* out) {
  uint64_t v = 0;
  DWORD cb = sizeof(v);
  const LSTATUS st = RegGetValueW(HKEY_LOCAL_MACHINE, kOwnerFloorKey, kOwnerFloorValue,
                                  RRF_RT_REG_QWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &v, &cb);
  if (st == ERROR_SUCCESS) {
    if (out) *out = v;
    return true;
  }
  if (st == ERROR_FILE_NOT_FOUND) {  // never revoked yet -> a real, known floor of 0
    if (out) *out = 0;
    return true;
  }
  return false;  // any other status (access denied, wrong type, corruption) -> unknown, fail safe
}

// r9 R1/R2: "pre-login access must be OFF" -- a DWORD next to the floor, set before a sign-out's Disable and
// at uninstall, cleared by the service once the revoke really happened (prelogin_admin_pipe.hpp). Only ever
// turns access off, so a stray mark fails safe. 1 = set, 0 = not set, -1 = unreadable (callers: as set).
inline constexpr wchar_t kRevokePendingValue[] = L"PreloginRevokePending";

inline int ReadRevokePendingAt(HKEY root, const wchar_t* subkey) {
  DWORD v = 0;
  DWORD cb = sizeof(v);
  const LSTATUS st = RegGetValueW(root, subkey, kRevokePendingValue, RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr,
                                  &v, &cb);
  if (st == ERROR_SUCCESS) return v != 0 ? 1 : 0;
  if (st == ERROR_FILE_NOT_FOUND) return 0;
  return -1;
}

inline bool WriteRevokePendingAt(HKEY root, const wchar_t* subkey, bool set) {
  if (!set) {
    const LSTATUS st = RegDeleteKeyValueW(root, subkey, kRevokePendingValue);
    return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
  }
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return false;
  }
  const DWORD one = 1;
  const bool ok = RegSetValueExW(key, kRevokePendingValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one),
                                 sizeof(one)) == ERROR_SUCCESS;
  RegCloseKey(key);
  return ok;
}

// The product's mark (HKLM, SYSTEM/Administrators).
inline int ReadPreloginRevokePending() { return ReadRevokePendingAt(HKEY_LOCAL_MACHINE, kOwnerFloorKey); }

}  // namespace remote60::native_poc::prelogin
