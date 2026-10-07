// prelogin-ui r9 (R5): Ctrl+Alt+Del at the remote logon screen -- the wire, the pre-login allowlist, and
// the service's decision before SendSAS. Pure (the call itself, and what the logon screen does, are G).

#include <cstdio>
#include <string>

#include "prelogin_console_probe.hpp"
#include "prelogin_control_allowlist.hpp"
#include "prelogin_sas_policy.hpp"

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
  using U = n::prelogin::UserTokenObservation;
  const auto value = [](uint32_t v) { return pl::ClassifySasPolicyRead(ERROR_SUCCESS, v); };
  const auto missing = pl::ClassifySasPolicyRead(ERROR_FILE_NOT_FOUND, 0);
  check("the console session, nobody logged on, policy 1 (services): sent",
        pl::DecideSas(true, U::NoInteractiveUser, 0, value(1)).result == R::Sent);
  check("...policy 3 (services and Ease of Access): sent", pl::DecideSas(true, U::NoInteractiveUser, 0, value(3)).result == R::Sent);
  check("policy NOT SET: tried, said unconfirmed (verifier r9) -- neither assumed allowed nor refused",
        pl::DecideSas(true, U::NoInteractiveUser, 0, missing).result == R::SentUnconfirmed);
  check("policy 0 (none) or 2 (Ease of Access only): PolicyDenied, with the value",
        pl::DecideSas(true, U::NoInteractiveUser, 0, value(0)).result == R::PolicyDenied &&
            pl::DecideSas(true, U::NoInteractiveUser, 0, value(2)).win32 == 2);
  const auto ffff = pl::DecideSas(true, U::NoInteractiveUser, 0, value(0xFFFFFFFFu));
  check("T3: a real DWORD 0xFFFFFFFF is a VALUE (refused), not \"not set\"",
        ffff.result == R::PolicyDenied && ffff.win32 == 0xFFFFFFFFu);
  const auto readErr = pl::DecideSas(true, U::NoInteractiveUser, 0, pl::ClassifySasPolicyRead(ERROR_ACCESS_DENIED, 0));
  check("T3: the policy could not be READ (access denied): Failed with that error -- not \"not set\", not sent",
        readErr.result == R::Failed && readErr.win32 == ERROR_ACCESS_DENIED);
  check("T3: ...a value of the wrong type is a read error too",
        pl::ClassifySasPolicyRead(ERROR_UNSUPPORTED_TYPE, 0).state == pl::SasPolicyRead::State::Error);
  const auto userErr = pl::DecideSas(true, U::QueryTransientError, ERROR_PRIVILEGE_NOT_HELD, value(1));
  check("T3: whether a user is logged on could not be told (privilege): Failed -- not taken for the logon screen",
        userErr.result == R::Failed && userErr.win32 == ERROR_PRIVILEGE_NOT_HELD);
  check("...and ClassifyTokenObservation is what reads it: only ERROR_NO_TOKEN is nobody",
        n::prelogin::ClassifyTokenObservation(false, ERROR_NO_TOKEN) == U::NoInteractiveUser &&
            n::prelogin::ClassifyTokenObservation(false, ERROR_ACCESS_DENIED) == U::QueryTransientError);
  check("a caller that is not in the console session: NotAtLogon",
        pl::DecideSas(false, U::NoInteractiveUser, 0, value(1)).result == R::NotAtLogon);
  check("a user is logged on at the console: NotAtLogon (the logon screen is not the worker's)",
        pl::DecideSas(true, U::HasUser, 0, value(1)).result == R::NotAtLogon);

  std::printf("\n--- the wrapper, on a real registry key of this test's own ---\n");
  const wchar_t* key = L"Software\\GNLinkSasPolicyTest";
  RegDeleteTreeW(HKEY_CURRENT_USER, key);
  check("T3 wrapper: no value -> Missing", pl::ReadSasPolicy(HKEY_CURRENT_USER, key, L"SoftwareSASGeneration").state ==
                                                 pl::SasPolicyRead::State::Missing);
  HKEY k = nullptr;
  RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
  const DWORD all = 0xFFFFFFFFu;
  RegSetValueExW(k, L"SoftwareSASGeneration", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&all), sizeof(all));
  const auto v = pl::ReadSasPolicy(HKEY_CURRENT_USER, key, L"SoftwareSASGeneration");
  check("T3 wrapper: DWORD 0xFFFFFFFF -> Value 0xFFFFFFFF", v.state == pl::SasPolicyRead::State::Value && v.value == 0xFFFFFFFFu);
  const wchar_t text[] = L"1";
  RegSetValueExW(k, L"SoftwareSASGeneration", 0, REG_SZ, reinterpret_cast<const BYTE*>(text), sizeof(text));
  const auto e = pl::ReadSasPolicy(HKEY_CURRENT_USER, key, L"SoftwareSASGeneration");
  check("T3 wrapper: a string where a DWORD belongs -> Error (not Missing)", e.state == pl::SasPolicyRead::State::Error,
        std::to_string(e.error));
  RegCloseKey(k);
  RegDeleteTreeW(HKEY_CURRENT_USER, key);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
