#pragma once

// The service's side of Ctrl+Alt+Del (prelogin-ui r9 R5, r10 T3): reading SoftwareSASGeneration and the
// decision before SendSAS. Windows only -- the wire (prelogin_sas_wire.hpp) stays portable for the shared
// client core the Android app builds.

#include <windows.h>

#include <cstdint>

#include "prelogin_sas_wire.hpp"
#include "prelogin_supervisor_state.hpp"  // UserTokenObservation (ClassifyTokenObservation's answer)

namespace remote60::native_poc {

namespace prelogin {

// r10 T3: SoftwareSASGeneration as read -- the value is not set, a DWORD (any value, 0xFFFFFFFF included),
// or the read failed (access, type, IO). The three are never folded into one another.
struct SasPolicyRead {
  enum class State { Missing, Value, Error } state = State::Error;
  uint32_t value = 0;  // Value
  uint32_t error = 0;  // Error: the win32 status
};

// RegGetValueW's answer, classified (the wrapper below; tested on its own).
inline SasPolicyRead ClassifySasPolicyRead(LSTATUS status, uint32_t value) {
  SasPolicyRead r;
  if (status == ERROR_SUCCESS) {
    r.state = SasPolicyRead::State::Value;
    r.value = value;
  } else if (status == ERROR_FILE_NOT_FOUND) {
    r.state = SasPolicyRead::State::Missing;  // the value (or its key) is simply not there
  } else {
    r.state = SasPolicyRead::State::Error;
    r.error = static_cast<uint32_t>(status);
  }
  return r;
}

// Reads it where the policy lives (the service: HKLM ...\Policies\System; a test: its own key).
inline SasPolicyRead ReadSasPolicy(HKEY root, const wchar_t* subkey, const wchar_t* name) {
  DWORD value = 0;
  DWORD cb = sizeof(value);
  const LSTATUS st = RegGetValueW(root, subkey, name, RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &value, &cb);
  return ClassifySasPolicyRead(st, value);
}

struct SasDecision {
  PreloginSasResult result = PreloginSasResult::Failed;
  uint32_t win32 = 0;  // PolicyDenied: the policy value; Failed: the error
};

// The service's decision before it calls SendSAS. Pure. `user` is ClassifyTokenObservation's answer for
// the console session: only an AUTHORITATIVE "no interactive user" is the logon screen; a query that
// failed for any other reason is not taken for it (Failed, retry).
inline SasDecision DecideSas(bool callerIsConsoleSession, UserTokenObservation user, uint32_t userQueryError,
                             const SasPolicyRead& policy) {
  if (!callerIsConsoleSession) return {PreloginSasResult::NotAtLogon, 0};
  if (user == UserTokenObservation::HasUser) return {PreloginSasResult::NotAtLogon, 0};
  if (user != UserTokenObservation::NoInteractiveUser) return {PreloginSasResult::Failed, userQueryError};
  switch (policy.state) {
    case SasPolicyRead::State::Error:
      return {PreloginSasResult::Failed, policy.error};
    case SasPolicyRead::State::Missing:
      // Unset: neither assumed to allow it nor to forbid it -- tried, said to be unconfirmed (verifier r9).
      return {PreloginSasResult::SentUnconfirmed, 0};
    case SasPolicyRead::State::Value:
      if (policy.value == 1 || policy.value == 3) return {PreloginSasResult::Sent, 0};  // services may
      return {PreloginSasResult::PolicyDenied, policy.value};  // 0, 2, and anything else -- 0xFFFFFFFF too
  }
  return {PreloginSasResult::Failed, ERROR_INVALID_DATA};
}

}  // namespace prelogin
}  // namespace remote60::native_poc
