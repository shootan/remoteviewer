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

bool capture_process_identity(uint32_t pid, ProcessTarget* out) {
  if (!out) return false;
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return false;
  out->pid = pid;
  out->creationTime = creation_time_of(h);
  out->imagePath = image_path_of(h);
  CloseHandle(h);
  // A creation time of zero would make every later comparison vacuous, so it is treated as a
  // failure to identify rather than as an identity.
  return out->creationTime != 0;
}

bool process_identity_matches(void* handle, const ProcessTarget& target) {
  HANDLE h = static_cast<HANDLE>(handle);
  if (!h) return false;
  if (target.creationTime == 0) return false;  // nothing to compare against
  if (creation_time_of(h) != target.creationTime) return false;
  // The creation time alone is already decisive in practice; the path is compared too because it
  // costs nothing and makes a deliberately crafted collision harder.
  if (!target.imagePath.empty() && image_path_of(h) != target.imagePath) return false;
  return true;
}

}  // namespace remote60::native_poc::update
