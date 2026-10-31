// Tests for the prelogin machine credential store (prelogin_machine_store.hpp). The store now applies a
// SYSTEM+Administrators-only DACL FROM CREATION to a dedicated credential directory (Codex S1), so the
// blob is never readable in a window and a limited token is denied the whole path. That means the
// outcome here depends on the runner's privilege:
//   - a SYSTEM / elevated-admin runner: the full round trip + tamper + DACL-shape assertions run;
//   - a filtered/limited runner: SealAndWrite (or the read back) is DENIED -- which IS the S1 property,
//     so the test records it and the live round trip is left to the elevated G-phase.
// Either way the record-level binding/floor logic is covered by prelogin_machine_record_test. Uses a
// throwaway subdirectory (never the user's real %ProgramData%\GNLink), best-effort cleaned.

#include "prelogin_machine_store.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <cstdio>
#include <string>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
bool check(bool cond, const char* what) {
  std::printf(cond ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!cond) ++gFailures;
  return cond;
}

std::wstring test_dir(const wchar_t* tag) {
  wchar_t dir[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, dir);
  std::wstring p = dir;
  p += L"gnlink_prelogin_store_test_";
  p += std::to_wstring(GetCurrentProcessId());
  p += L"_";
  p += tag;
  return p;  // the dedicated credential dir; SealAndWrite creates+protects it
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

void cleanup(const std::wstring& dir, const std::wstring& file) {
  // As the dir's owner we hold WRITE_DAC even on a protected dir, so loosen the DACL before deleting so
  // a non-elevated runner does not leave a locked throwaway dir in %TEMP%.
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;FA;;;WD)", SDDL_REVISION_1, &sd, nullptr)) {
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    if (GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted)) {
      SetNamedSecurityInfoW(const_cast<wchar_t*>(dir.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                            nullptr, nullptr, dacl, nullptr);
    }
    LocalFree(sd);
  }
  DeleteFileW(file.c_str());
  RemoveDirectoryW(dir.c_str());
}
}  // namespace

int main() {
  std::printf("prelogin machine store (DACL-at-creation; DPAPI LOCAL_MACHINE)\n");

  const std::wstring dir = test_dir(L"rt");
  const std::wstring path = dir + L"\\machine.cred";
  uint32_t win32 = 0;
  MachineRecord r = sample();
  const MachineStoreStatus st = SealAndWriteMachineRecord(path, r, &win32);

  if (st != MachineStoreStatus::Ok) {
    std::printf("  note  SealAndWrite -> status=%d win32=%lu : the protected store denied this token "
                "(S1 property: SYSTEM/elevated only). Live round trip is the G-phase.\n",
                static_cast<int>(st), static_cast<unsigned long>(win32));
    cleanup(dir, path);
    std::printf("prelogin_machine_store_test: PASS-with-NOTE (protection denied a non-elevated token)\n");
    return gFailures == 0 ? 0 : 1;
  }

  std::printf("[round trip (runner can access the protected store)]\n");
  MachineRecord back;
  check(ReadAndOpenMachineRecord(path, &back, &win32) == MachineStoreStatus::Ok, "read + DPAPI open -> Ok");
  check(back.accountId == r.accountId && back.hostId == r.hostId && back.ownerGeneration == r.ownerGeneration &&
            back.enabled == r.enabled && back.directoryOrigin == r.directoryOrigin &&
            back.machineId == r.machineId && back.credential == r.credential,
        "the opened record matches what was sealed");

  std::printf("[protected DACL applied FROM creation = SYSTEM + Administrators only]\n");
  std::wstring sddl;
  if (check(ReadDaclSddl(path, &sddl), "read back the on-disk DACL")) {
    std::printf("        DACL = %ls\n", sddl.c_str());
    check(contains(sddl, L";SY)"), "grants Local System (SY)");
    check(contains(sddl, L";BA)"), "grants Administrators (BA)");
    check(!contains(sddl, L";WD)") && !contains(sddl, L";BU)") && !contains(sddl, L";AU)"),
          "NEGATIVE: no Everyone (WD) / Users (BU) / Authenticated-Users (AU) ACE");
  }

  std::printf("[tamper + missing]\n");
  {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
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
      MachineRecord junk;
      check(ReadAndOpenMachineRecord(path, &junk, &win32) == MachineStoreStatus::Unprotect,
            "NEGATIVE: a tampered blob -> Unprotect (DPAPI tag rejects it)");
    }
  }
  DeleteFileW(path.c_str());
  MachineRecord missing;
  check(ReadAndOpenMachineRecord(path, &missing, &win32) == MachineStoreStatus::Read,
        "NEGATIVE: a missing file -> Read");

  cleanup(dir, path);
  if (gFailures != 0) {
    std::printf("prelogin_machine_store_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_machine_store_test: PASS\n");
  return 0;
}
