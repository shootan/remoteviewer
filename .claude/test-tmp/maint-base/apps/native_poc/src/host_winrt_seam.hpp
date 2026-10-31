#pragma once

// prelogin-field D2 (Codex codex_prelogin_field_d3.md): the two WinRT calls of the streaming host's startup that the
// pre-login worker no longer makes -- GraphicsCaptureSession::IsSupported (startup_init_graphics) and the WinRT
// D3D wrapper (startup_init_encoder) -- with the test build's way of watching them.
//
// TEST BUILD ONLY (REMOTE60_STREAM_TEST_SEAM, GNLinkStreamMigrationTest):
//   GNLINK_STREAM_TEST_WINRT_TRAP=1        either call, when reached, throws std::runtime_error naming it -- so a
//                                          run that must not reach them fails loudly (exit 49, step named) if it does
//   GNLINK_STREAM_TEST_WGC_SUPPORTED=false IsSupported answers false
//   GNLINK_STREAM_TEST_WGC_SUPPORTED=throw IsSupported throws winrt::hresult_error(E_ACCESSDENIED)
// The shipped GNLinkStream compiles none of it (build gate: "stream-test-seam" / GNLINK_STREAM_TEST_).

#include <winrt/Windows.Graphics.Capture.h>

#include <stdexcept>
#include <string>

#ifdef REMOTE60_STREAM_TEST_SEAM
#include <windows.h>
#endif

namespace remote60::native_poc {

#ifdef REMOTE60_STREAM_TEST_SEAM
inline std::string winrt_seam_env(const char* name) {
  char v[64] = {};
  return GetEnvironmentVariableA(name, v, sizeof(v)) > 0 ? std::string(v) : std::string();
}
#endif

// Called right before each of the two calls.
inline void HostWinrtTrap(const char* which) {
#ifdef REMOTE60_STREAM_TEST_SEAM
  // "1": both calls; "probe": IsSupported only; "wrapper": the D3D wrapper only.
  const std::string t = winrt_seam_env("GNLINK_STREAM_TEST_WINRT_TRAP");
  const bool isProbe = std::string(which).find("IsSupported") != std::string::npos;
  if (t == "1" || (t == "probe" && isProbe) || (t == "wrapper" && !isProbe)) {
    throw std::runtime_error(std::string("stream-test-seam: WinRT trap reached: ") + which);
  }
#else
  (void)which;
#endif
}

// GraphicsCaptureSession::IsSupported, as the product asks it (the test build may answer for it).
inline bool HostWgcIsSupported() {
#ifdef REMOTE60_STREAM_TEST_SEAM
  const std::string v = winrt_seam_env("GNLINK_STREAM_TEST_WGC_SUPPORTED");
  if (v == "false") return false;
  if (v == "throw") throw winrt::hresult_error(E_ACCESSDENIED, L"stream-test-seam: IsSupported threw");
#endif
  return winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
}

}  // namespace remote60::native_poc
