#include "update_job_guard.hpp"

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>

namespace remote60::native_poc::update {
namespace {

/** A SID that frees itself. There are three of them below and each has an early return. */
class WellKnownSid {
 public:
  explicit WellKnownSid(WELL_KNOWN_SID_TYPE type) {
    DWORD size = SECURITY_MAX_SID_SIZE;
    buffer_.resize(size);
    if (!CreateWellKnownSid(type, nullptr, buffer_.data(), &size)) buffer_.clear();
  }
  PSID get() { return buffer_.empty() ? nullptr : buffer_.data(); }

 private:
  std::basic_string<unsigned char> buffer_;
};

/** Write-ish access. A caller who can add a file or change one can replace what we are about to run. */
constexpr DWORD kDangerousWrite =
    FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | WRITE_DAC |
    WRITE_OWNER | DELETE | GENERIC_WRITE | GENERIC_ALL;

/**
 * True when `sid` is granted any of `kDangerousWrite` by an allow entry in `dacl`.
 *
 * Deliberately crude: it does not evaluate the full access-check algorithm, only whether a
 * principal that should have no write access appears with one. Being crude in this direction is
 * the safe one -- it can refuse a directory that would have been fine, and it does not accept one
 * that would not.
 */
bool grants_write_to(PACL dacl, PSID sid) {
  if (!dacl || !sid) return false;
  for (DWORD i = 0; i < dacl->AceCount; ++i) {
    void* entry = nullptr;
    if (!GetAce(dacl, i, &entry)) continue;
    auto* header = static_cast<ACE_HEADER*>(entry);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
    auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(entry);
    if (!EqualSid(reinterpret_cast<PSID>(&ace->SidStart), sid)) continue;
    if ((ace->Mask & kDangerousWrite) != 0) return true;
  }
  return false;
}

}  // namespace

const char* launch_guard_verdict_name(LaunchGuardVerdict verdict) {
  switch (verdict) {
    case LaunchGuardVerdict::Ok: return "ok";
    case LaunchGuardVerdict::WouldDieWithParent: return "would-die-with-parent";
    case LaunchGuardVerdict::WorkDirNotProtected: return "work-dir-not-protected";
    case LaunchGuardVerdict::WorkDirIsReparsePoint: return "work-dir-is-reparse-point";
    case LaunchGuardVerdict::Undetermined: return "undetermined";
  }
  return "unknown";
}

JobKind job_kind_of_current_process() {
  BOOL inJob = FALSE;
  if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob)) return JobKind::Unknown;
  if (!inJob) return JobKind::NotInJob;

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  DWORD returned = 0;
  // Queried through the process rather than a job handle: this process may not hold one, and the
  // question is about the job it is already in.
  if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits), &returned)) {
    // In a job whose terms are unknown. Not treated as harmless -- an unknown limit is not the
    // same as an absent one.
    return JobKind::Unknown;
  }
  return (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
             ? JobKind::InKillingJob
             : JobKind::InHarmlessJob;
}

LaunchGuardVerdict judge_launch(JobKind kind, bool breakawaySucceeded) {
  if (breakawaySucceeded) return LaunchGuardVerdict::Ok;
  switch (kind) {
    case JobKind::NotInJob:
    case JobKind::InHarmlessJob:
      // Breakaway was unnecessary. Refusing here would fail on machines where nothing was ever at
      // risk, which is how a safety check gets switched off.
      return LaunchGuardVerdict::Ok;
    case JobKind::InKillingJob:
      // The case the whole check exists for. Proceeding would let the host take the updater down
      // with it -- while stopping the host is the next thing that happens.
      return LaunchGuardVerdict::WouldDieWithParent;
    case JobKind::Unknown:
      // Could not establish it. Not a pass: the cost of being wrong here is an update that dies
      // half way through replacing files.
      return LaunchGuardVerdict::Undetermined;
  }
  return LaunchGuardVerdict::Undetermined;
}

LaunchGuardVerdict check_work_directory(const std::wstring& path, std::string* detail) {
  const auto say = [detail](const char* text) {
    if (detail) *detail = text;
  };

  const DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    // Not there yet is fine -- the caller creates it, and a directory this process creates under
    // %ProgramFiles% inherits an administrators-only ACL.
    say("the directory does not exist yet");
    return LaunchGuardVerdict::Ok;
  }
  if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    say("the path exists and is not a directory");
    return LaunchGuardVerdict::WorkDirNotProtected;
  }
  if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    // A junction or symlink means the name does not decide where writes land, and whoever can
    // repoint it decides what an elevated process executes.
    say("the directory is a reparse point");
    return LaunchGuardVerdict::WorkDirIsReparsePoint;
  }

  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                            nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS) {
    say("the directory's permissions could not be read");
    return LaunchGuardVerdict::Undetermined;
  }
  // A NULL DACL grants everyone everything. It is not "no restrictions recorded"; it is no
  // restrictions.
  if (!dacl) {
    if (descriptor) LocalFree(descriptor);
    say("the directory has a null DACL, which grants everyone access");
    return LaunchGuardVerdict::WorkDirNotProtected;
  }

  WellKnownSid users(WinBuiltinUsersSid);
  WellKnownSid everyone(WinWorldSid);
  WellKnownSid authenticated(WinAuthenticatedUserSid);
  WellKnownSid interactive(WinInteractiveSid);

  LaunchGuardVerdict verdict = LaunchGuardVerdict::Ok;
  if (!users.get() || !everyone.get() || !authenticated.get() || !interactive.get()) {
    say("the well-known principals could not be resolved");
    verdict = LaunchGuardVerdict::Undetermined;
  } else if (grants_write_to(dacl, users.get()) || grants_write_to(dacl, everyone.get()) ||
             grants_write_to(dacl, authenticated.get()) ||
             grants_write_to(dacl, interactive.get())) {
    // Someone who is not an administrator can put a file here or change one. This process is
    // about to copy an executable in and run it elevated.
    say("a non-administrator principal has write access to the directory");
    verdict = LaunchGuardVerdict::WorkDirNotProtected;
  } else {
    say("the directory is administrators-only");
  }

  if (descriptor) LocalFree(descriptor);
  return verdict;
}

}  // namespace remote60::native_poc::update
