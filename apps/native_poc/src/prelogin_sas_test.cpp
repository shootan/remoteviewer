// prelogin-ui r9 (R5): Ctrl+Alt+Del at the remote logon screen -- the wire, the pre-login allowlist, and
// the service's decision before SendSAS. Pure (the call itself, and what the logon screen does, are G).

#include <cstdio>
#include <string>

#include "prelogin_control_allowlist.hpp"
#include "prelogin_sas_wire.hpp"

namespace n = remote60::native_poc;
namespace pl = remote60::native_poc::prelogin;
using R = n::PreloginSasResult;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("Ctrl+Alt+Del at the logon screen (prelogin-ui r9, R5)\n\n");
  const auto req = n::make_sas_request(7);
  check("83 ControlPreloginControl: 16 bytes, type 83, op 1 (SAS), the request id",
        sizeof(req) == 16 && req.header.type == 83 && req.header.size == 16 && req.op == n::kPreloginControlOpSas &&
            req.requestId == 7);
  const auto st = n::make_prelogin_status(7, n::kPreloginControlOpSas, R::PolicyDenied, 2, n::kPreloginStatusFlagCadNotRequired);
  check("82 ControlPreloginStatus: 24 bytes, type 82, echoes the id, carries the result, win32 and the DisableCAD flag",
        sizeof(st) == 24 && st.header.type == 82 && st.requestId == 7 && st.result == 1 && st.win32 == 2 && st.flags == 1);
  check("the capability bit is 0x20000 (fixed by the verifier)", n::kCaptureFlagPreloginSasV1 == 0x20000u);
  check("before login: 83 and 82 are allowed through the control allowlist",
        pl::IsControlAllowedInPrelogin(n::MessageType::ControlPreloginControl) &&
            pl::IsControlAllowedInPrelogin(n::MessageType::ControlPreloginStatus));
  check("...while character text still is not", !pl::IsControlAllowedInPrelogin(n::MessageType::ControlInputText));

  std::printf("\n--- the service's decision ---\n");
  check("the console session, nobody logged on, policy 1 (services): sent", pl::DecideSas(true, false, 1) == R::Sent);
  check("...policy 3 (services and Ease of Access): sent", pl::DecideSas(true, false, 3) == R::Sent);
  check("policy NOT SET: tried, said unconfirmed (verifier r9) -- neither assumed allowed nor refused",
        pl::DecideSas(true, false, pl::kSasPolicyUnset) == R::SentUnconfirmed);
  check("policy 0 (none) or 2 (Ease of Access only): PolicyDenied",
        pl::DecideSas(true, false, 0) == R::PolicyDenied && pl::DecideSas(true, false, 2) == R::PolicyDenied);
  check("a caller that is not in the console session: NotAtLogon", pl::DecideSas(false, false, 1) == R::NotAtLogon);
  check("a user is logged on at the console: NotAtLogon (the logon screen is not the worker's)",
        pl::DecideSas(true, true, 1) == R::NotAtLogon);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
