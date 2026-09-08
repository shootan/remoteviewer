// Two conditions that used to be assumed, and the fact that neither of them now defaults to yes.
//
// Both were "warn and carry on" before. An updater that could not break away from a job that kills
// its children was allowed to proceed and would have died part way through replacing files, with
// nothing running to put them back. And a working directory that already existed was accepted
// whatever its permissions, so an attacker who created it first would have had an elevated process
// copy an executable into a place they control and then run it.
//
// The job cases are pure and cover every combination. The directory cases use real directories in
// a temp tree, including one deliberately made writable by ordinary users, because the check is
// about what an ACL actually says.

#include "update_job_guard.hpp"

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>

#include <iostream>
#include <string>

namespace {

using namespace remote60::native_poc::update;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::wstring temp_dir(const wchar_t* tag) {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  wchar_t path[MAX_PATH]{};
  swprintf(path, MAX_PATH, L"%sgnlink-guard-%lu-%s", base, GetCurrentProcessId(), tag);
  CreateDirectoryW(path, nullptr);
  return path;
}

/** Gives BUILTIN\Users write access, which is exactly what must be refused. */
bool make_user_writable(const std::wstring& path) {
  // D:P sets a protected DACL: (A;OICI;GA;;;BU) grants Users everything, (A;OICI;GA;;;BA) keeps
  // administrators able to clean it up afterwards.
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;OICI;GA;;;BU)(A;OICI;GA;;;BA)", SDDL_REVISION_1, &descriptor, nullptr)) {
    return false;
  }
  BOOL present = FALSE;
  BOOL defaulted = FALSE;
  PACL dacl = nullptr;
  bool ok = false;
  if (GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) && present) {
    ok = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
                               DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                               nullptr, nullptr, dacl, nullptr) == ERROR_SUCCESS;
  }
  LocalFree(descriptor);
  return ok;
}

/** Administrators and SYSTEM only -- what %ProgramFiles% looks like. */
bool make_admin_only(const std::wstring& path) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;OICI;GA;;;BA)(A;OICI;GA;;;SY)", SDDL_REVISION_1, &descriptor, nullptr)) {
    return false;
  }
  BOOL present = FALSE;
  BOOL defaulted = FALSE;
  PACL dacl = nullptr;
  bool ok = false;
  if (GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) && present) {
    ok = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
                               DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                               nullptr, nullptr, dacl, nullptr) == ERROR_SUCCESS;
  }
  LocalFree(descriptor);
  return ok;
}

}  // namespace

int main() {
  // ---------------------------------------------------------------- surviving the parent

  {
    // Breakaway worked. Nothing else matters.
    check("breakaway succeeded: proceed",
          judge_launch(JobKind::InKillingJob, true) == LaunchGuardVerdict::Ok);
    check("...whatever the job was",
          judge_launch(JobKind::Unknown, true) == LaunchGuardVerdict::Ok &&
              judge_launch(JobKind::NotInJob, true) == LaunchGuardVerdict::Ok);
  }

  {
    // Breakaway failed but was never needed. Refusing here would fail on machines where nothing
    // was at risk, which is how a safety check ends up being switched off.
    check("no job, no breakaway needed",
          judge_launch(JobKind::NotInJob, false) == LaunchGuardVerdict::Ok);
    check("a job that does not kill on close is harmless",
          judge_launch(JobKind::InHarmlessJob, false) == LaunchGuardVerdict::Ok);
  }

  {
    // The case the check exists for. Proceeding would let the host take the updater down with it,
    // and stopping the host is the next thing that happens.
    check("in a killing job with no breakaway: refuse",
          judge_launch(JobKind::InKillingJob, false) == LaunchGuardVerdict::WouldDieWithParent,
          launch_guard_verdict_name(judge_launch(JobKind::InKillingJob, false)));
  }

  {
    // Could not establish it. Not a pass: the cost of guessing wrong is an update that dies part
    // way through replacing files.
    check("an unknown job is not treated as harmless",
          judge_launch(JobKind::Unknown, false) == LaunchGuardVerdict::Undetermined,
          launch_guard_verdict_name(judge_launch(JobKind::Unknown, false)));
  }

  {
    // Stated as a property: of the eight combinations, the only ones that proceed either broke
    // away or were never in danger. A new JobKind cannot be added without deciding which it is.
    int proceeding = 0;
    const JobKind kinds[] = {JobKind::NotInJob, JobKind::InHarmlessJob, JobKind::InKillingJob,
                             JobKind::Unknown};
    for (JobKind kind : kinds) {
      for (bool broke : {false, true}) {
        if (judge_launch(kind, broke) != LaunchGuardVerdict::Ok) continue;
        ++proceeding;
        check("every proceeding case is safe",
              broke || kind == JobKind::NotInJob || kind == JobKind::InHarmlessJob,
              std::string("kind=") + std::to_string(static_cast<int>(kind)));
      }
    }
    check("six of the eight combinations proceed", proceeding == 6, std::to_string(proceeding));
  }

  {
    // The real machine, whatever it happens to be. Not asserting a particular answer -- asserting
    // that the question can be answered at all, since Unknown forbids proceeding.
    const JobKind kind = job_kind_of_current_process();
    check("this process's job can be classified", kind != JobKind::Unknown,
          std::to_string(static_cast<int>(kind)));
  }

  // ---------------------------------------------------------------- the working directory

  {
    std::string why;
    check("a directory that does not exist yet is fine",
          check_work_directory(L"C:\\gnlink-guard-no-such-directory-12345", &why) ==
              LaunchGuardVerdict::Ok,
          why);
  }

  {
    const std::wstring dir = temp_dir(L"open");
    if (make_user_writable(dir)) {
      std::string why;
      const LaunchGuardVerdict verdict = check_work_directory(dir, &why);
      // The whole point. An elevated process must not copy an executable into a directory an
      // ordinary user can write, and then run it.
      check("a user-writable directory is refused",
            verdict == LaunchGuardVerdict::WorkDirNotProtected,
            std::string(launch_guard_verdict_name(verdict)) + ": " + why);
      check("and the reason says so", why.find("write access") != std::string::npos, why);
    } else {
      check("a user-writable directory is refused", false,
            "could not set a test ACL -- needs to be run where the temp tree is writable");
    }
    (void)SetNamedSecurityInfoW(const_cast<wchar_t*>(dir.c_str()), SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                nullptr, nullptr, nullptr, nullptr);
    RemoveDirectoryW(dir.c_str());
  }

  {
    const std::wstring dir = temp_dir(L"locked");
    if (make_admin_only(dir)) {
      std::string why;
      const LaunchGuardVerdict verdict = check_work_directory(dir, &why);
      // The control. Without it, "refused" above could mean the check refuses everything.
      check("an administrators-only directory is accepted", verdict == LaunchGuardVerdict::Ok,
            std::string(launch_guard_verdict_name(verdict)) + ": " + why);
    } else {
      check("an administrators-only directory is accepted", false, "could not set a test ACL");
    }
    // The protected DACL this case installs can outlive the process that set it, so the
    // inheritance is put back before the directory is removed -- otherwise the test leaves
    // undeletable directories in %TEMP%, which it did.
    (void)SetNamedSecurityInfoW(const_cast<wchar_t*>(dir.c_str()), SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                nullptr, nullptr, nullptr, nullptr);
    RemoveDirectoryW(dir.c_str());
  }

  {
    const std::wstring dir = temp_dir(L"file");
    RemoveDirectoryW(dir.c_str());
    HANDLE file = CreateFileW(dir.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      CloseHandle(file);
      std::string why;
      check("a path that is a file, not a directory, is refused",
            check_work_directory(dir, &why) == LaunchGuardVerdict::WorkDirNotProtected, why);
      DeleteFileW(dir.c_str());
    }
  }

  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
