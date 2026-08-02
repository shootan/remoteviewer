#pragma once

#include <windows.h>
#include <d3d11.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace remote60::host {

enum class DesktopCaptureBackend {
  Dxgi,
  Wgc,
  Gdi,
};

struct DxgiDesktopCaptureConfig {
  ID3D11Device* d3dDevice = nullptr;
  HMONITOR monitor = nullptr;
  uint32_t acquireTimeoutMs = 100;
  bool landscapeOnly = true;
};

using DxgiDesktopFrameHandler = std::function<void(ID3D11Texture2D* texture, uint32_t width, uint32_t height)>;
using DxgiDesktopLogHandler = std::function<void(const std::string& phase, const std::string& message)>;
using DxgiDesktopFallbackHandler = std::function<void(const std::string& reason)>;

class DxgiDesktopCaptureSession {
 public:
  DxgiDesktopCaptureSession();
  ~DxgiDesktopCaptureSession();

  DxgiDesktopCaptureSession(const DxgiDesktopCaptureSession&) = delete;
  DxgiDesktopCaptureSession& operator=(const DxgiDesktopCaptureSession&) = delete;

  bool Start(const DxgiDesktopCaptureConfig& config,
             DxgiDesktopFrameHandler onFrame,
             DxgiDesktopLogHandler onLog,
             DxgiDesktopFallbackHandler onFallback,
             std::string* detailOut);
  void Stop();

  uint32_t width() const;
  uint32_t height() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace remote60::host
