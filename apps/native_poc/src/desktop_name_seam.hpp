#pragma once

// capture-lifecycle r1: the input desktop's name as a TEST BUILD of the host (REMOTE60_STREAM_TEST_SEAM,
// GNLinkStreamMigrationTest) shows it. That host runs on this user's "Default" desktop; a test that needs
// the logon desktop or a secure desktop in front names it here instead of switching this PC's real input
// desktop under the person using it:
//   GNLINK_STREAM_TEST_DESKTOP_ALIAS=<name>          the real "Default" reads as <name> -- the pre-login
//                                                     worker serving "Winlogon" from the test user's desktop
//   GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT=<event>  while that named event is set the input desktop reads
//                                                     as "Winlogon" -- a UAC prompt's two edges for the host
// What it cannot stand in for: the pixels. The capture still sees the real Default desktop.
// The product build has neither: the real name, unchanged.

#include <windows.h>

#include <cwchar>
#include <string>

namespace remote60::native_poc {

#ifdef REMOTE60_STREAM_TEST_SEAM
namespace desktop_name_seam_detail {
inline const std::wstring& Alias() {
  static const std::wstring alias = [] {
    wchar_t b[64] = {};
    const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_DESKTOP_ALIAS", b, 64);
    return (n > 0 && n < 64) ? std::wstring(b) : std::wstring();
  }();
  return alias;
}
inline HANDLE SecureEvent() {
  static const HANDLE ev = [] {
    wchar_t b[128] = {};
    const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT", b, 128);
    return (n > 0 && n < 128) ? CreateEventW(nullptr, TRUE, FALSE, b) : static_cast<HANDLE>(nullptr);
  }();
  return ev;
}
}  // namespace desktop_name_seam_detail
#endif

// `real`: the name read from the real input desktop ("" when it could not be read).
inline std::wstring SeamInputDesktopName(const std::wstring& real) {
#ifdef REMOTE60_STREAM_TEST_SEAM
  if (HANDLE ev = desktop_name_seam_detail::SecureEvent()) {
    if (WaitForSingleObject(ev, 0) == WAIT_OBJECT_0) return L"Winlogon";
  }
  const std::wstring& alias = desktop_name_seam_detail::Alias();
  if (!alias.empty() && _wcsicmp(real.c_str(), L"Default") == 0) return alias;
#endif
  return real;
}

// The name a check against the REAL input desktop must use for `shown` (capture_backend_dxgi, a library
// without the seam, compares the real one): the alias mapped back to "Default".
inline std::wstring SeamRealDesktopName(const std::wstring& shown) {
#ifdef REMOTE60_STREAM_TEST_SEAM
  const std::wstring& alias = desktop_name_seam_detail::Alias();
  if (!alias.empty() && _wcsicmp(shown.c_str(), alias.c_str()) == 0) return L"Default";
#endif
  return shown;
}

}  // namespace remote60::native_poc
