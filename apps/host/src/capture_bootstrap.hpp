#pragma once

#include <string>

namespace remote60::host {

struct CaptureProbeResult {
  bool d3d11Ok = false;
  bool wgcSupported = false;
  std::string detail;
};

CaptureProbeResult probe_capture_stack();

}  // namespace remote60::host
