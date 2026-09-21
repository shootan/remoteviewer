#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// Same, Different, or Unknown -- and never two of those collapsed into one.
// (updater-abandon-race r3, item B)
//
// The bool this replaced returned false for "the handle belongs to somebody else" and for "the
// question could not be answered" alike, and every caller read false as "our target has already
// exited". On a machine where the query fails, that turns an unanswered question into permission
// to swap files under a process that is still running.
//
// The counter-examples below inject the failure rather than waiting for one: a target whose
// recorded creation time is absent, and a handle that cannot answer.

#include <windows.h>

#include <iostream>
#include <string>

#include "update_effects.hpp"

using namespace remote60::native_poc::update;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

const char* nm(IdentityMatch m) { return identity_match_name(m); }

}  // namespace

int main() {
  std::cout << "update_identity_match_test\n";

  ProcessTarget self;
  check("this process identifies", capture_process_identity(GetCurrentProcessId(), &self));

  HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                         GetCurrentProcessId());
  check("a handle to it was opened", h != nullptr);

  // Same: the ordinary case.
  check("the same process is Same", process_identity_check(h, self) == IdentityMatch::Same,
        nm(process_identity_check(h, self)));

  // Different: a creation time that cannot belong to this process.
  {
    ProcessTarget other = self;
    other.creationTime = self.creationTime + 1;
    check("a different creation time is Different",
          process_identity_check(h, other) == IdentityMatch::Different,
          nm(process_identity_check(h, other)));
  }

  // Different: a readable path that disagrees.
  {
    ProcessTarget other = self;
    other.imagePath = L"C:\\Windows\\System32\\nowhere-at-all.exe";
    check("a readable path that disagrees is Different",
          process_identity_check(h, other) == IdentityMatch::Different,
          nm(process_identity_check(h, other)));
  }

  // Unknown: nothing recorded to compare against. This is the one the bool got wrong.
  {
    ProcessTarget blank = self;
    blank.creationTime = 0;
    check("no recorded creation time is Unknown, not Different",
          process_identity_check(h, blank) == IdentityMatch::Unknown,
          nm(process_identity_check(h, blank)));
    check("...and the bool form refuses it",
          !process_identity_matches(h, blank));
  }

  // Unknown: no handle at all.
  {
    check("a null handle is Unknown",
          process_identity_check(nullptr, self) == IdentityMatch::Unknown,
          nm(process_identity_check(nullptr, self)));
  }

  // And the direction that had to stay permissive: a matching creation time with a path that
  // cannot be read is still Same. QueryFullProcessImageName fails for a process that has exited,
  // and calling that Unknown would block every update whose target had properly gone -- the
  // opposite failure, and the one my first version of this introduced.
  {
    ProcessTarget pathed = self;
    pathed.imagePath = self.imagePath.empty() ? L"C:\\some\\path.exe" : self.imagePath;
    check("a matching creation time with a readable, equal path is Same",
          process_identity_check(h, pathed) == IdentityMatch::Same,
          nm(process_identity_check(h, pathed)));
  }

  if (h) CloseHandle(h);

  // The three names are distinct, or a log cannot report which happened.
  check("the three answers have three names",
        std::string(nm(IdentityMatch::Same)) != nm(IdentityMatch::Different) &&
            std::string(nm(IdentityMatch::Different)) != nm(IdentityMatch::Unknown) &&
            std::string(nm(IdentityMatch::Same)) != nm(IdentityMatch::Unknown));

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
