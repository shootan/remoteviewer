// Whether a process is still the one that was captured.
//
// Its own translation unit because two separate places need the same answer and must give the same
// answer: the quiesce/rollback side in update_effects.cpp, and the relaunch side, which has to
// decide whether something it is about to start is already running. Two implementations of "the
// same process" would eventually disagree, and the disagreement would show up as a second copy of
// the product.
//
// A PID is not an identity. It is reused, sometimes within seconds, so everything here compares the
// creation time -- which is unique per process for as long as anyone can hold a handle to it -- and
// the image path on top of that.

#include <windows.h>

#include <iterator>
#include <string>

#include "update_effects.hpp"

namespace remote60::native_poc::update {
namespace {

uint64_t creation_time_of(HANDLE process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
  ULARGE_INTEGER v{};
  v.LowPart = created.dwLowDateTime;
  v.HighPart = created.dwHighDateTime;
  return v.QuadPart;
}

std::wstring image_path_of(HANDLE process) {
  wchar_t buffer[MAX_PATH * 2]{};
  DWORD size = static_cast<DWORD>(std::size(buffer));
  if (!QueryFullProcessImageNameW(process, 0, buffer, &size)) return {};
  return std::wstring(buffer, size);
}

}  // namespace

OpenFailure classify_open_error(uint32_t win32Error) {
  // ERROR_INVALID_PARAMETER is what OpenProcess returns for a pid that is not a process -- an id
  // that never existed, or one whose process has exited and whose last handle has been released.
  // That is the outcome stopping a process is FOR, so it is forgiven.
  //
  // Everything else is refused, and the list of what "everything else" contains is the reason this
  // is written as one allowed value rather than as a set of denied ones:
  //
  //   ERROR_ACCESS_DENIED (5)      running, and out of this updater's reach
  //   ERROR_NOT_ENOUGH_MEMORY (8)  the call failed for reasons that say nothing about the process
  //   ERROR_INVALID_HANDLE (6)     a bug on our side, which is not evidence about theirs
  //   anything at all               unseen, and therefore unknown
  //
  // A denied-set version of this ("not access denied means gone") passed every test that could be
  // written against a real machine, because only 5 and 87 can be produced on demand from an
  // ordinary session. It was wrong for every other value, and the table test is what says so.
  return win32Error == ERROR_INVALID_PARAMETER ? OpenFailure::NotAProcess : OpenFailure::Unknown;
}

const char* open_failure_name(OpenFailure f) {
  switch (f) {
    case OpenFailure::NotAProcess: return "not-a-process";
    case OpenFailure::Unknown: return "unknown";
  }
  return "?";
}

bool capture_process_identity(uint32_t pid, ProcessTarget* out, IdentityFailure* why) {
  if (why) *why = IdentityFailure::None;
  if (!out) return false;
  SetLastError(ERROR_SUCCESS);
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) {
    // One shared rule, not a fourth copy of it. See classify_open_error.
    if (why) {
      *why = (classify_open_error(GetLastError()) == OpenFailure::NotAProcess)
                 ? IdentityFailure::Gone
                 : IdentityFailure::Unknowable;
    }
    return false;
  }
  out->pid = pid;
  out->creationTime = creation_time_of(h);
  out->imagePath = image_path_of(h);
  CloseHandle(h);
  // A creation time of zero would make every later comparison vacuous, so it is treated as a
  // failure to identify rather than as an identity. The handle DID open, so the process is there:
  // that is "cannot see it", not "gone".
  if (out->creationTime == 0) {
    if (why) *why = IdentityFailure::Unknowable;
    return false;
  }
  return true;
}

const char* identity_match_name(IdentityMatch m) {
  switch (m) {
    case IdentityMatch::Same: return "same";
    case IdentityMatch::Different: return "different";
    case IdentityMatch::Unknown: return "unknown";
  }
  return "?";
}

IdentityMatch process_identity_check(void* handle, const ProcessTarget& target) {
  HANDLE h = static_cast<HANDLE>(handle);
  if (!h) return IdentityMatch::Unknown;
  // Nothing was recorded to compare against, so no comparison can be made. This used to read as
  // "different", which a caller then read as "ours has exited".
  if (target.creationTime == 0) return IdentityMatch::Unknown;

  const uint64_t created = creation_time_of(h);
  // A creation time of zero here is GetProcessTimes failing, not a process created at the epoch.
  if (created == 0) return IdentityMatch::Unknown;
  if (created != target.creationTime) return IdentityMatch::Different;

  // The creation time has already matched, and that is decisive: it is unique per process for as
  // long as anyone holds a handle. The path is compared on top because it costs nothing and makes
  // a deliberately crafted collision harder -- but an UNREADABLE path does not undo the match.
  //
  // Reading it as Unknown was wrong in the dangerous direction: QueryFullProcessImageName fails
  // for a process that has exited, so every legitimately departed target became "cannot tell" and
  // blocked the update it should have allowed. A path is evidence against only when it can be read
  // and differs.
  if (!target.imagePath.empty()) {
    const std::wstring path = image_path_of(h);
    if (!path.empty() && path != target.imagePath) return IdentityMatch::Different;
  }
  return IdentityMatch::Same;
}

bool process_identity_matches(void* handle, const ProcessTarget& target) {
  return process_identity_check(handle, target) == IdentityMatch::Same;
}

}  // namespace remote60::native_poc::update
