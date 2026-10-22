#pragma once

// dxgi-wedge-fix r1 (Codex B3): which GPU a capture ran on, in a form that survives a reboot.
//
// The DXGI quarantine is remembered per adapter + driver. The key must be the same in the streaming
// host (which knows the adapter its capture device sits on) and in the supervisor (which, at its next
// start, looks for that adapter among the ones present) -- so both compute it here.
//
//   key    = FNV-1a 64 of "v<vendor> d<device> s<subsys> r<revision>|<adapter description>", 16 hex.
//            Not the LUID: a LUID is reissued at every boot. Two identical cards in one PC share a key;
//            the quarantine then covers both, which errs toward WGC, never toward a wedge.
//   driver = the user-mode driver version (IDXGIAdapter::CheckInterfaceSupport), "a.b.c.d". A driver
//            update gets a fresh chance at DXGI.
//
// The streaming host prints it once per capture device:
//   [native-video-host][capture-adapter] key=<16 hex> driver=<a.b.c.d> vendor=0x<4> device=0x<4>
// and the supervisor reads that line off the child's stdout (parse_capture_adapter_line). Nothing else
// in the line is trusted: the key and driver are checked for shape before they are used.

#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>

#include <cstdint>
#include <string>
#include <vector>

namespace remote60::native_poc {

struct CaptureAdapterIdentity {
  std::string key;     // 16 lowercase hex
  std::string driver;  // "a.b.c.d"
  uint32_t vendorId = 0;
  uint32_t deviceId = 0;
  bool valid() const;
};

// Pure: the key for these descriptor fields.
std::string capture_adapter_key(uint32_t vendorId, uint32_t deviceId, uint32_t subSysId, uint32_t revision,
                                const std::wstring& description);
// Pure: "a.b.c.d" from CheckInterfaceSupport's UMD version.
std::string capture_adapter_driver_string(LARGE_INTEGER umdVersion);
// Pure shape checks (what a record or a child line may carry).
bool capture_adapter_key_shape_ok(const std::string& key);
bool capture_adapter_driver_shape_ok(const std::string& driver);

// The adapter a D3D11 device was created on; invalid() for a software adapter or any failure.
CaptureAdapterIdentity identify_device_adapter(ID3D11Device* device);
CaptureAdapterIdentity identify_adapter(IDXGIAdapter1* adapter);
// Every hardware adapter present now (the supervisor's startup match).
std::vector<CaptureAdapterIdentity> enumerate_present_adapters();

// The child's line (with trailing newline), and its strict parse. False for anything else.
std::string capture_adapter_log_line(const CaptureAdapterIdentity& id);
bool parse_capture_adapter_line(const std::string& line, CaptureAdapterIdentity* out);

}  // namespace remote60::native_poc
