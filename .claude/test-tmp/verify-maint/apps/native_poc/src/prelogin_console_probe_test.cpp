// Tests for the prelogin console observation layer (prelogin_console_probe.hpp). The pure classifiers
// carry the security-critical invariants (access/privilege failure != no-user; Default != standard
// user; Limited without a linked token is a query error, not a downgrade). A live self-check classifies
// THIS process's token and cross-checks the A0 measurement (this admin account = Limited + linked Full).
// (prelogin-service A1 section 1; Codex r3 section 1.)

#include "prelogin_console_probe.hpp"

#include <cstdio>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}

PrivilegeFacts facts(bool queryFailed, int elev, bool linked, bool admin) {
  PrivilegeFacts f;
  f.queryFailed = queryFailed;
  f.elevationType = elev;
  f.hasLinkedFull = linked;
  f.adminEffective = admin;
  return f;
}
}  // namespace

int main() {
  std::printf("prelogin console observation classifiers\n");

  std::printf("[ClassifyPrivilege: Q4 matrix]\n");
  check(ClassifyPrivilege(facts(false, kElevationTypeFull, false, true)) == UserPrivilege::AdminFull,
        "Full + admin effective -> AdminFull");
  check(ClassifyPrivilege(facts(false, kElevationTypeLimited, true, true)) == UserPrivilege::AdminLimitedLinked,
        "Limited + linked Full + admin effective -> AdminLimitedLinked");
  check(ClassifyPrivilege(facts(false, kElevationTypeDefault, false, true)) == UserPrivilege::AdminDefaultEffective,
        "Default + admin effective -> AdminDefaultEffective (NOT standard user)");

  std::printf("[ClassifyPrivilege: NEGATIVE controls]\n");
  check(ClassifyPrivilege(facts(true, kElevationTypeFull, false, true)) == UserPrivilege::PrivilegeQueryError,
        "any query failure -> PrivilegeQueryError (never a guess)");
  check(ClassifyPrivilege(facts(false, kElevationTypeDefault, false, false)) == UserPrivilege::Unsupported,
        "Default + NOT admin effective (standard user / deny-only) -> Unsupported");
  check(ClassifyPrivilege(facts(false, kElevationTypeFull, false, false)) == UserPrivilege::Unsupported,
        "even Full with no effective admin -> Unsupported (defensive)");
  check(ClassifyPrivilege(facts(false, kElevationTypeLimited, false, true)) == UserPrivilege::PrivilegeQueryError,
        "Limited WITHOUT a linked token -> PrivilegeQueryError (transient, not a downgrade)");
  check(ClassifyPrivilege(facts(false, 99, false, true)) == UserPrivilege::PrivilegeQueryError,
        "unknown elevation type -> PrivilegeQueryError");

  std::printf("[ClassifyTokenObservation: only no-token is authoritative no-user]\n");
  check(ClassifyTokenObservation(true, 0) == UserTokenObservation::HasUser, "success -> HasUser");
  check(ClassifyTokenObservation(false, kErrNoToken) == UserTokenObservation::NoInteractiveUser,
        "ERROR_NO_TOKEN -> NoInteractiveUser");
  check(ClassifyTokenObservation(false, kErrAccessDenied) == UserTokenObservation::QueryTransientError,
        "NEGATIVE: ERROR_ACCESS_DENIED -> Transient (NOT no-user)");
  check(ClassifyTokenObservation(false, kErrPrivilegeNotHeld) == UserTokenObservation::QueryTransientError,
        "NEGATIVE: ERROR_PRIVILEGE_NOT_HELD (no SE_TCB) -> Transient (NOT no-user)");
  check(ClassifyTokenObservation(false, 123456) == UserTokenObservation::QueryTransientError,
        "NEGATIVE: an arbitrary failure -> Transient");

  std::printf("[live self-check: this process's token]\n");
  PrivilegeFacts self = GatherCurrentProcessPrivilegeFacts();
  std::printf("        queryFailed=%d elevationType=%d hasLinkedFull=%d adminEffective=%d -> privilege=%d\n",
              self.queryFailed ? 1 : 0, self.elevationType, self.hasLinkedFull ? 1 : 0,
              self.adminEffective ? 1 : 0, static_cast<int>(ClassifyPrivilege(self)));
  check(!self.queryFailed, "gathering this process's own token facts succeeds");
  check(ClassifyPrivilege(self) != UserPrivilege::PrivilegeQueryError,
        "this process classifies to a concrete privilege (not a query error)");
  // This harness runs under an administrator account; cross-check the A0 finding without being brittle
  // about exactly which admin variant the runner happens to have.
  if (self.adminEffective) {
    const UserPrivilege p = ClassifyPrivilege(self);
    check(p == UserPrivilege::AdminFull || p == UserPrivilege::AdminLimitedLinked ||
              p == UserPrivilege::AdminDefaultEffective,
          "admin-effective token classifies to one of the supported Admin* privileges");
  } else {
    std::printf("        (note: runner token is not admin-effective; Unsupported is the correct result here)\n");
  }

  if (gFailures != 0) {
    std::printf("prelogin_console_probe_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_console_probe_test: PASS\n");
  return 0;
}
