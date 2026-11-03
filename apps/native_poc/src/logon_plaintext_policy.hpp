#pragma once

// Plaintext keys and text at a logon screen (lk-abcd D). Pure; no windows.h.
//
// Until 0.2.163 a password reached a lock or pre-login screen only through the sealed "Windows 로그인" path:
// the viewer held characters back (prelogin_input_gate.hpp), the host dropped them (prelogin_input_filter.hpp,
// lock_input_policy.hpp), and the service refused them again before its agent typed on Winlogon.
//
// The user's decision (2026-11-03): typing on the lock screen and on the pre-login screen is allowed, as plain
// keys and text. The input channel is plaintext (poc_protocol.hpp ControlInputEvent / ControlInputText) and the
// user accepts that. The sealed "Windows 로그인" path stays as it was, alongside.
//
// The decision is these two constants, read by the host and the service (and advertised to viewers as
// kCaptureFlagLogonPlaintextV1, so a viewer follows what the host it talks to actually does -- an older host
// keeps the old rule, and a viewer keeps holding back for it). The refusal code they switch off is kept and
// tested: turning one back to false restores the earlier boundary as it was.

namespace remote60::native_poc::logon_plaintext {

// A logged-on host's own session at its lock screen.
constexpr bool kAcceptAtLockScreen = true;
// The pre-login worker (--prelogin) on the logon desktop.
constexpr bool kAcceptAtPrelogin = true;

}  // namespace remote60::native_poc::logon_plaintext
