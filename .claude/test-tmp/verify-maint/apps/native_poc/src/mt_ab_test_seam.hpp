#pragma once

// dxgi-wedge-fix r1 (Codex B1, Bⓐ1): the D3D multithread-protection A/B -- TEST BUILD ONLY.
//
// Compiled only into GNLinkStreamMtAb.exe (REMOTE60_MT_AB_TEST_SEAM); the shipped GNLinkStream has no such
// code, no switch and no file read (the installer gate checks the release binaries for the marker string).
//
// The arm is a one-line file beside the exe, GNLinkStream.mt-ab.txt: "on" or "off" (anything else, or no
// file: "off" -- the shipped behaviour). A file and not an environment variable: on the field PC the
// streaming host is started by GNLinkHost (itself started at logon), whose environment a person cannot set
// per run; a file in the install folder needs an administrator, like the swap of the exe itself.
//
// "on" calls ID3D11Multithread::SetMultithreadProtected(TRUE) on the capture device's immediate context
// right after D3D11CreateDevice -- before the MF encoder, the readback worker, WGC or the DXGI duplication
// touch it. "off" leaves the device exactly as the product creates it (it is never forced off). Either
// way the state AFTER is read back with GetMultithreadProtected (Set's return value is the state BEFORE,
// not success) and logged: [native-video-host][mt-ab] arm=<on|off> before=<0|1> after=<0|1|unknown>.

#ifdef REMOTE60_MT_AB_TEST_SEAM

#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace remote60::native_poc {

inline std::string mt_ab_arm_from_file() {
  char self[MAX_PATH * 2] = {};
  GetModuleFileNameA(nullptr, self, MAX_PATH * 2);
  std::string path(self);
  path = path.substr(0, path.find_last_of("\\/") + 1) + "GNLinkStream.mt-ab.txt";
  std::ifstream in(path);
  std::string arm;
  if (in) std::getline(in, arm);
  while (!arm.empty() && (arm.back() == '\r' || arm.back() == ' ')) arm.pop_back();
  return arm == "on" ? "on" : "off";
}

// Right after device creation, before anything else uses the device. Returns the log line.
inline std::string mt_ab_apply(ID3D11DeviceContext* context) {
  const std::string arm = mt_ab_arm_from_file();
  Microsoft::WRL::ComPtr<ID3D11Multithread> mt;
  if (!context || FAILED(context->QueryInterface(IID_PPV_ARGS(&mt))) || !mt) {
    return "[native-video-host][mt-ab] arm=" + arm + " before=unknown after=unknown (no ID3D11Multithread)\n";
  }
  const BOOL before = mt->GetMultithreadProtected();
  if (arm == "on") mt->SetMultithreadProtected(TRUE);  // its return value is the state BEFORE the call
  const BOOL after = mt->GetMultithreadProtected();
  char line[160];
  std::snprintf(line, sizeof(line), "[native-video-host][mt-ab] arm=%s before=%d after=%d\n", arm.c_str(),
                before ? 1 : 0, after ? 1 : 0);
  return line;
}

}  // namespace remote60::native_poc

#endif  // REMOTE60_MT_AB_TEST_SEAM
