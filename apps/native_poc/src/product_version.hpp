#pragma once

namespace remote60::native_poc {

// The single place the product version is written.
//
// It is shown in the host window and stamped into the installer's uninstall entry, and those two
// have to agree or the number on screen stops being evidence. The reason the window shows it at
// all is to answer "did the update actually take?" -- a question that came up because several
// releases in a row were diagnostic-only and looked identical from outside. A version that could
// drift from the binary it labels would be worse than showing nothing.
// inline (C++17): ONE definition for the whole program. A plain namespace-scope constexpr array has internal linkage --
// every translation unit that takes its address gets its own copy, and the release parity gate counts copies
// (gnlink_release_manifest.py CARRIERS: GNLinkStream 1). The pre-login worker log header made GNLinkStream 2.
inline constexpr wchar_t kProductVersion[] = L"0.2.161";

}  // namespace remote60::native_poc
