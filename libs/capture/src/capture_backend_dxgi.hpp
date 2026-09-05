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

// Hardware-pointer report: position in this output's pixel space plus visibility. Fires on every
// acquire that carried a mouse update -- including pointer-only frames, which the content pipeline
// deliberately drops -- so a still screen can still show a moving remote cursor.
using DxgiDesktopPointerHandler = std::function<void(int32_t x, int32_t y, bool visible)>;

struct DxgiDesktopCaptureConfig {
  ID3D11Device* d3dDevice = nullptr;
  HMONITOR monitor = nullptr;
  uint32_t acquireTimeoutMs = 100;
  // After DXGI_ERROR_WAIT_TIMEOUT, sleep this long OUTSIDE AcquireNextFrame (no frame held, no
  // device lock) before re-entering it. Desktop duplication holds an internal, unfair D3D11 device
  // lock for the whole call, so on a still desktop an immediate re-entry starves every other user
  // of the device (readback worker, MF encoder). 0 = re-enter immediately (legacy behaviour).
  uint32_t acquireIdleSleepUs = 0;
  bool landscapeOnly = true;
  DxgiDesktopPointerHandler onPointer;  // optional; see DxgiDesktopPointerHandler
};

// accumulatedFrames is what desktop duplication reported for this acquire: the number of
// desktop updates it merged into this one frame. Zero means the desktop image did not
// change at all and only the pointer moved, which is not a new frame of content.
using DxgiDesktopFrameHandler =
    std::function<void(ID3D11Texture2D* texture, uint32_t width, uint32_t height,
                       uint32_t accumulatedFrames)>;
using DxgiDesktopLogHandler = std::function<void(const std::string& phase, const std::string& message)>;
using DxgiDesktopFallbackHandler = std::function<void(const std::string& reason)>;

// Coarse location of the DXGI capture worker within one AcquireNextFrame..ReleaseFrame cycle.
// The host's wedge watchdog reports this so a hang can be attributed to the actual blocking call
// (Acquire vs a resource QI vs ReleaseFrame) instead of guessing.
enum class CaptureWorkerPhase : uint32_t {
  Idle = 0,
  Loop,
  Report,
  Acquire,
  ResourceQI,
  TextureDesc,
  FrameHandler,
  Release,
  Exited,
};
const char* capture_worker_phase_name(CaptureWorkerPhase phase);

// Liveness snapshot of the DXGI capture worker, for the host's independent wedge watchdog. ageUs
// and phaseAgeUs are computed against the capture backend's OWN steady clock -- callers must not
// mix them with host QPC timestamps.
struct CaptureWorkerSnapshot {
  bool running = false;
  uint64_t generation = 0;
  uint64_t ageUs = 0;        // since the worker last made progress
  uint64_t phaseAgeUs = 0;   // since the current phase began
  CaptureWorkerPhase phase = CaptureWorkerPhase::Idle;
  uint64_t loopCount = 0;
  int32_t lastAcquireHr = 0;
  int32_t lastReleaseHr = 0;
  uint32_t lastAccumulatedFrames = 0;
};

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

  // Reads only atomics; safe to call from another thread while the worker runs. The progress block
  // outlives the Impl that Start() recreates, so a watchdog can keep one session reference across
  // capture restarts and rely on the generation field to tell episodes apart.
  CaptureWorkerSnapshot SnapshotWorker() const;

 private:
  struct Impl;
  struct WorkerProgress;
  // Declared before impl_ so it is destroyed AFTER it: the worker thread (owned by Impl) writes
  // to this block, so the block must outlive the thread.
  std::unique_ptr<WorkerProgress> progress_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace remote60::host
