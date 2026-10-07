// Tests for the prelogin machine credential store (prelogin_machine_store.hpp). Runs non-elevated in a
// throwaway TEMP file (never the product %ProgramData%\GNLink path). Proves: the DPAPI LOCAL_MACHINE
// seal/open round-trips a record; a tampered blob fails to open; a missing file reports Read; and the
// protected DACL, when applied, is SYSTEM+Administrators only (no Users/Everyone).
//
// What it does NOT prove (left for the SYSTEM/elevated G-phase): that a genuinely different, non-admin
// user is actually denied the bytes end to end -- here the ACL SHAPE is asserted, which is the testable
// part without a second account. The DPAPI scope being LOCAL_MACHINE is exactly why the ACL matters.

#include "prelogin_machine_store.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
int gInconclusive = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!cond) ++gFailures;
}
void note(const char* what, const char* why) {
  std::printf("  SKIP? %s  [%s]\n", what, why);
  ++gInconclusive;
}

std::wstring temp_path(const wchar_t* tag) {
  wchar_t dir[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, dir);
  std::wstring p = dir;
  p += L"gnlink_prelogin_test_";
  p += std::to_wstring(GetCurrentProcessId());
  p += L"_";
  p += tag;
  p += L".bin";
  return p;
}

MachineRecord sample() {
  MachineRecord r;
  r.accountId = 0xA0C0DE01ull;
  r.hostId = 0x1122334455667788ull;
  r.ownerGeneration = 9;
  r.enabled = true;
  r.directoryOrigin = "https://dir.example:443";
  r.machineId = "MACHINE-GUID-TEST";
  r.credential = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  return r;
}

bool contains(const std::wstring& s, const wchar_t* sub) { return s.find(sub) != std::wstring::npos; }
}  // namespace

int main() {
  std::printf("prelogin machine store (DPAPI LOCAL_MACHINE + protected DACL)\n");

  std::printf("[seal/open round trip + tamper + missing]\n");
  {
    const std::wstring path = temp_path(L"rt");
    uint32_t win32 = 0;
    MachineRecord r = sample();
    check(SealAndWriteMachineRecord(path, r, /*applyProtectedDacl=*/false, &win32) == MachineStoreStatus::Ok,
          "seal + atomic write (no DACL) -> Ok");
    MachineRecord back;
    check(ReadAndOpenMachineRecord(path, &back, &win32) == MachineStoreStatus::Ok, "read + DPAPI open -> Ok");
    check(back.accountId == r.accountId && back.hostId == r.hostId && back.ownerGeneration == r.ownerGeneration &&
              back.enabled == r.enabled && back.directoryOrigin == r.directoryOrigin &&
              back.machineId == r.machineId && back.credential == r.credential,
          "the opened record matches what was sealed");

    // Tamper: flip a byte in the middle of the sealed file -> DPAPI integrity must reject.
    {
      HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
      check(h != INVALID_HANDLE_VALUE, "reopen sealed file to tamper");
      if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz = {};
        GetFileSizeEx(h, &sz);
        LARGE_INTEGER mid;
        mid.QuadPart = sz.QuadPart / 2;
        SetFilePointerEx(h, mid, nullptr, FILE_BEGIN);
        uint8_t b = 0;
        DWORD io = 0;
        ReadFile(h, &b, 1, &io, nullptr);
        SetFilePointerEx(h, mid, nullptr, FILE_BEGIN);
        b ^= 0xFF;
        WriteFile(h, &b, 1, &io, nullptr);
        CloseHandle(h);
      }
      MachineRecord junk;
      check(ReadAndOpenMachineRecord(path, &junk, &win32) == MachineStoreStatus::Unprotect,
            "NEGATIVE: a tampered blob -> Unprotect (DPAPI tag rejects it)");
    }
    DeleteFileW(path.c_str());
    MachineRecord missing;
    check(ReadAndOpenMachineRecord(path, &missing, &win32) == MachineStoreStatus::Read,
          "NEGATIVE: a missing file -> Read");
  }

  std::printf("[protected DACL = SYSTEM + Administrators only]\n");
  {
    const std::wstring path = temp_path(L"dacl");
    uint32_t win32 = 0;
    MachineRecord r = sample();
    const MachineStoreStatus st = SealAndWriteMachineRecord(path, r, /*applyProtectedDacl=*/true, &win32);
    if (st == MachineStoreStatus::Dacl) {
      note("apply protected DACL", "SetNamedSecurityInfo denied for this token (verify under SYSTEM in G)");
    } else {
      check(st == MachineStoreStatus::Ok, "seal + write + apply protected DACL -> Ok");
      std::wstring sddl;
      check(ReadDaclSddl(path, &sddl), "read back the on-disk DACL");
      std::printf("        DACL = %ls\n", sddl.c_str());
      check(contains(sddl, L";SY)"), "grants Local System (SY)");
      check(contains(sddl, L";BA)"), "grants Administrators (BA)");
      check(!contains(sddl, L";WD)") && !contains(sddl, L";BU)") && !contains(sddl, L";AU)"),
            "NEGATIVE: no Everyone (WD) / Users (BU) / Authenticated-Users (AU) ACE");
    }
    DeleteFileW(path.c_str());
  }

  std::printf("\nsummary: failures=%d inconclusive=%d\n", gFailures, gInconclusive);
  if (gFailures != 0) { std::printf("prelogin_machine_store_test: FAIL\n"); return 1; }
  if (gInconclusive != 0) { std::printf("prelogin_machine_store_test: PASS-with-SKIPS\n"); return 2; }
  std::printf("prelogin_machine_store_test: PASS\n");
  return 0;
}
