#pragma once

// Per-user autostart cleanup for the prelogin handoff (prelogin-service A1 section f / §3).
//
// When prelogin is enabled the SYSTEM service owns startup (it launches the user Host at login,
// replacing the elevated HKCU Run that never ran before login). The old per-user Run value is then
// cleaned -- but ONLY that user's value and ONLY after the service's durable enrollment is confirmed,
// so a failed enrollment never strips a working autostart with nothing to replace it. The decision is
// pure; the registry delete is a thin, parameterised live call so a test can target a throwaway subkey
// instead of the user's real Run key.

#include <cstddef>

namespace remote60::native_poc::prelogin {

// Should the per-user HKCU Run autostart be removed in favour of the service-owned startup? Only when
// the feature is enabled AND the service enrollment is confirmed. (Never remove before the new path
// exists -- Codex r1 §3 "새 startup 경로 성공 뒤".)
inline bool PreloginServiceOwnsUserStartup(bool preloginFeatureEnabled, bool serviceEnrollmentConfirmed) {
  return preloginFeatureEnabled && serviceEnrollmentConfirmed;
}

// Delete the given value names under rootKey\runSubkey (HKEY passed as void* to keep this header free of
// windows.h). Idempotent: an already-absent value or a missing Run key is success. Touches only that one
// user-hive key -- never enumerates or edits other users' hives. Returns true iff, after the call, none
// of the named values remain.
bool RemoveUserRunValues(void* rootKey, const wchar_t* runSubkey, const wchar_t* const* valueNames,
                         size_t count);

}  // namespace remote60::native_poc::prelogin
