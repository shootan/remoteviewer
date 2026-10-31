#include "capture_backend_dxgi.hpp"

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include "dxgi_output_selection.hpp"

namespace remote60::host {
namespace {

std::string hresult_hex(HRESULT hr) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
  return oss.str();
}

// Adapter and device names are the only wide strings here and they are ASCII in practice, so a
// byte-wise narrowing is enough to put them in a log line.
std::string narrow(const wchar_t* text) {
  std::string out;
  if (!text) return out;
  for (const wchar_t* cursor = text; *cursor; ++cursor) {
    out.push_back(*cursor < 128 ? static_cast<char>(*cursor) : '?');
  }
  return out;
}

// The capture backend's single monotonic clock. The worker heartbeat and the SnapshotWorker age
// calculation must both use it; mixing it with the host's QPC clock would produce nonsense ages.
uint64_t steady_now_us() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

const char* capture_worker_phase_name(CaptureWorkerPhase phase) {
  switch (phase) {
    case CaptureWorkerPhase::Idle: return "idle";
    case CaptureWorkerPhase::Loop: return "loop";
    case CaptureWorkerPhase::Report: return "report";
    case CaptureWorkerPhase::Acquire: return "acquire";
    case CaptureWorkerPhase::ResourceQI: return "resource_qi";
    case CaptureWorkerPhase::TextureDesc: return "texture_desc";
    case CaptureWorkerPhase::FrameHandler: return "frame_handler";
    case CaptureWorkerPhase::Release: return "release";
    case CaptureWorkerPhase::Exited: return "exited";
  }
  return "unknown";
}

// Stable heartbeat block. Lives on the session (not the Impl that Start recreates) so the host
// wedge watchdog can hold one reference across capture restarts; every timestamp here is the
// backend steady clock (steady_now_us).
struct DxgiDesktopCaptureSession::WorkerProgress {
  std::atomic<bool> running{false};
  std::atomic<uint64_t> generation{0};
  std::atomic<uint64_t> lastProgressUs{0};
  std::atomic<uint32_t> phase{static_cast<uint32_t>(CaptureWorkerPhase::Idle)};
  std::atomic<uint64_t> phaseStartedUs{0};
  std::atomic<uint64_t> loopCount{0};
  std::atomic<int32_t> lastAcquireHr{0};
  std::atomic<int32_t> lastReleaseHr{0};
  std::atomic<uint32_t> lastAccumulatedFrames{0};
};

struct DxgiDesktopCaptureSession::Impl {
  DxgiDesktopCaptureConfig config;
  DxgiDesktopFrameHandler onFrame;
  DxgiDesktopLogHandler onLog;
  DxgiDesktopFallbackHandler onFallback;
  std::atomic<bool> stopRequested{false};
  std::thread worker;
  WorkerProgress* progress = nullptr;  // session-owned; set by Start before the worker spawns
  Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
  HMONITOR monitor = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;

  void log(const std::string& phase, const std::string& message) const {
    if (onLog) onLog(phase, message);
  }

  static DxgiOutputInfo make_info(uint32_t adapterIndex, uint32_t outputIndex,
                                  const wchar_t* adapterDescription,
                                  const DXGI_OUTPUT_DESC& desc, uint64_t adapterLuid) {
    DxgiOutputInfo info;
    info.adapterIndex = adapterIndex;
    info.outputIndex = outputIndex;
    info.adapterDescription = narrow(adapterDescription);
    info.deviceName = narrow(desc.DeviceName);
    info.left = desc.DesktopCoordinates.left;
    info.top = desc.DesktopCoordinates.top;
    info.right = desc.DesktopCoordinates.right;
    info.bottom = desc.DesktopCoordinates.bottom;
    info.attachedToDesktop = desc.AttachedToDesktop != 0;
    info.monitorId = reinterpret_cast<uint64_t>(desc.Monitor);
    info.rotatedPortrait = desc.Rotation == DXGI_MODE_ROTATION_ROTATE90 ||
                           desc.Rotation == DXGI_MODE_ROTATION_ROTATE270;
    info.adapterLuid = adapterLuid;
    return info;
  }

  // Enumerates every adapter, not only the one the D3D device sits on.
  //
  // Looking at just the device's adapter is what made RDP unrecoverable: connecting moves the
  // desktop onto the Microsoft Remote Display Adapter, the device stays on the physical GPU, and
  // an output belonging to another adapter cannot be duplicated. From inside the device's own
  // adapter that is indistinguishable from "no output found", so recreate retried forever
  // against a monitor that was never going to be there.
  bool enumerate_outputs(std::vector<DxgiOutputInfo>* infos,
                         std::vector<Microsoft::WRL::ComPtr<IDXGIOutput>>* handles,
                         std::string* detailOut) {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
      if (detailOut) *detailOut = "dxgi_factory_failed_" + hresult_hex(hr);
      return false;
    }
    for (UINT adapterIndex = 0;; ++adapterIndex) {
      Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
      if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
      if (!adapter) continue;
      DXGI_ADAPTER_DESC1 adapterDesc{};
      (void)adapter->GetDesc1(&adapterDesc);
      for (UINT outputIndex = 0;; ++outputIndex) {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
        if (!output) continue;
        DXGI_OUTPUT_DESC desc{};
        if (FAILED(output->GetDesc(&desc))) continue;

        infos->push_back(make_info(adapterIndex, outputIndex, adapterDesc.Description, desc,
                                   (static_cast<uint64_t>(adapterDesc.AdapterLuid.HighPart)
                                    << 32) |
                                       adapterDesc.AdapterLuid.LowPart));
        handles->push_back(output);
      }
    }
    return true;
  }

  /** LUID of the adapter the D3D device was created on, or 0 when it cannot be determined. */
  uint64_t device_adapter_luid() const {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3dDevice.As(&dxgiDevice)) || !dxgiDevice) return 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter)) || !adapter) return 0;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(adapter->GetDesc(&desc))) return 0;
    return (static_cast<uint64_t>(desc.AdapterLuid.HighPart) << 32) | desc.AdapterLuid.LowPart;
  }

  // Outputs belonging to the adapter the D3D device was created on. This is the only set that can
  // actually be duplicated with this device, and it is the path every healthy session takes.
  bool enumerate_device_adapter_outputs(std::vector<DxgiOutputInfo>* infos,
                                        std::vector<Microsoft::WRL::ComPtr<IDXGIOutput>>* handles,
                                        std::string* detailOut) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice.As(&dxgiDevice);
    if (FAILED(hr) || !dxgiDevice) {
      if (detailOut) *detailOut = "dxgi_device_qi_failed_" + hresult_hex(hr);
      return false;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr) || !adapter) {
      if (detailOut) *detailOut = "dxgi_adapter_failed_" + hresult_hex(hr);
      return false;
    }
    DXGI_ADAPTER_DESC adapterDesc{};
    (void)adapter->GetDesc(&adapterDesc);

    for (UINT idx = 0;; ++idx) {
      Microsoft::WRL::ComPtr<IDXGIOutput> output;
      hr = adapter->EnumOutputs(idx, &output);
      if (hr == DXGI_ERROR_NOT_FOUND) break;
      if (FAILED(hr) || !output) break;
      DXGI_OUTPUT_DESC desc{};
      if (FAILED(output->GetDesc(&desc))) continue;
      infos->push_back(make_info(0, idx, adapterDesc.Description, desc,
                                 (static_cast<uint64_t>(adapterDesc.AdapterLuid.HighPart) << 32) |
                                     adapterDesc.AdapterLuid.LowPart));
      handles->push_back(output);
    }
    return true;
  }

  bool resolve_output(Microsoft::WRL::ComPtr<IDXGIOutput1>* outOutput1,
                      DXGI_OUTPUT_DESC* outDesc,
                      std::string* detailOut) {
    if (!outOutput1 || !outDesc) {
      if (detailOut) *detailOut = "dxgi_bad_output_arg";
      return false;
    }

    const HMONITOR targetMonitor =
        config.monitor ? config.monitor : MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    const auto targetId = reinterpret_cast<uint64_t>(targetMonitor);

    // The device's own adapter first, exactly as before. Enumerating every adapter on a healthy
    // start cost two failed runs out of six when it was tried the other way round -- creating a
    // DXGI factory and walking the whole display topology is not free and is not needed while
    // the desktop is where the device can see it.
    std::vector<DxgiOutputInfo> own;
    std::vector<Microsoft::WRL::ComPtr<IDXGIOutput>> ownHandles;
    if (!enumerate_device_adapter_outputs(&own, &ownHandles, detailOut)) return false;

    const auto selection = select_dxgi_output(own, targetId, config.landscapeOnly);
    if (selection.found) {
      Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
      const size_t chosen = selection.output.outputIndex;
      if (chosen >= ownHandles.size()) {
        if (detailOut) *detailOut = "dxgi_output_handle_missing";
        return false;
      }
      const HRESULT hr = ownHandles[chosen].As(&output1);
      if (FAILED(hr) || !output1) {
        if (detailOut) *detailOut = "dxgi_output1_qi_failed_" + hresult_hex(hr);
        return false;
      }
      if (selection.reason != DxgiSelectionReason::MonitorMatch) {
        log("capture", std::string("dxgi_output_match=") +
                           dxgi_selection_reason_name(selection.reason) + " output=" +
                           selection.output.deviceName);
      }
      DXGI_OUTPUT_DESC desc{};
      (void)ownHandles[chosen]->GetDesc(&desc);
      *outOutput1 = std::move(output1);
      *outDesc = desc;
      return true;
    }

    // Only now, having already failed, is it worth asking where the desktop went. Answering that
    // is what separates "recreate and carry on" from the permanent demotion to a backend that
    // cannot see the desktop either.
    const std::string ownReason =
        std::string("dxgi_select_") + dxgi_selection_reason_name(selection.reason);
    std::vector<DxgiOutputInfo> all;
    std::vector<Microsoft::WRL::ComPtr<IDXGIOutput>> allHandles;
    std::string enumerateDetail;
    if (enumerate_outputs(&all, &allHandles, &enumerateDetail)) {
      const uint64_t deviceLuid = device_adapter_luid();
      // Nothing on the device's own adapter was usable. Dump the whole topology so a headless
      // host (no physical monitor, RDP gone, an idle virtual display) says WHY every output was
      // rejected instead of hiding behind one reason code. An indirect display with no active
      // consumer reports detached (zero extent), which is exactly what select_dxgi_output filters
      // out -- and the single "no_usable_output" line could never tell that apart from a genuine
      // adapter move. (Diagnostics only: this path already failed; it does not change behaviour.)
      log("capture", "dxgi_no_usable_output reason=" +
                         std::string(dxgi_selection_reason_name(selection.reason)) +
                         " deviceLuid=" + std::to_string(deviceLuid) +
                         " outputs=" + std::to_string(all.size()));
      for (const auto& o : all) {
        log("capture",
            "dxgi_output adapter=\"" + o.adapterDescription + "\" luid=" +
                std::to_string(o.adapterLuid) + " name=" + o.deviceName + " extent=" +
                std::to_string(o.right - o.left) + "x" + std::to_string(o.bottom - o.top) +
                " attached=" + std::to_string(o.attachedToDesktop ? 1 : 0) + " portrait=" +
                std::to_string(o.rotatedPortrait ? 1 : 0) + " onDeviceAdapter=" +
                std::to_string((deviceLuid != 0 && o.adapterLuid == deviceLuid) ? 1 : 0));
      }
      const auto wide = select_dxgi_output(all, targetId, config.landscapeOnly);
      if (wide.found && deviceLuid != 0 && wide.output.adapterLuid != deviceLuid) {
        // Under RDP the desktop composes onto the Microsoft Remote Display Adapter while the
        // device stays on the physical GPU. Retrying the same device against it can never work;
        // the answer is a new device on that adapter.
        log("capture", "dxgi_desktop_moved adapter=" + wide.output.adapterDescription +
                           " output=" + wide.output.deviceName);
        if (detailOut) *detailOut = "dxgi_adapter_changed";
        return false;
      }
    }

    if (detailOut) *detailOut = ownReason;
    return false;
  }

  bool recreate_duplication(std::string* detailOut) {
    duplication.Reset();

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    DXGI_OUTPUT_DESC desc{};
    if (!resolve_output(&output1, &desc, detailOut)) return false;

    HRESULT hr = output1->DuplicateOutput(d3dDevice.Get(), &duplication);
    if (FAILED(hr) || !duplication) {
      if (detailOut) *detailOut = "dxgi_duplicate_output_failed_" + hresult_hex(hr);
      return false;
    }

    monitor = desc.Monitor;
    width = static_cast<uint32_t>(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
    height = static_cast<uint32_t>(desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
    return true;
  }

  void request_fallback(const std::string& reason) {
    log("capture", "fallback_reason=" + reason);
    if (onFallback) onFallback(reason);
  }

  // Desktop duplication reports changes, not a cadence, and it reports them only while no
  // frame is held. So the questions that decide the capture rate are: how often did an
  // acquire return nothing, how many desktop updates did the OS have to accumulate because
  // we were busy, and how long were we holding a frame. AccumulatedFrames answers the middle
  // one exactly -- anything above 1 is an update we were not there to collect.
  struct AcquireStats {
    uint64_t windowStartUs = 0;
    uint32_t acquires = 0;
    uint32_t timeouts = 0;
    uint32_t accumulatedTotal = 0;
    uint32_t accumulatedMax = 0;
    uint32_t coalescedAcquires = 0;  // acquires that carried more than one update
    // P0 (input serialization diagnosis, #351): the existing `acquires` mixes desktop-content and
    // pointer-only frames. Split them so a drag shows the REAL screen-change rate. content =
    // LastPresentTime!=0; pointer-only = LastPresentTime==0 && LastMouseUpdateTime!=0. If a drag
    // makes the client SEND ~1/RTT and this contentAcquires tracks it, the source is not the limit.
    uint32_t contentAcquires = 0;
    uint32_t pointerOnlyAcquires = 0;
    uint64_t holdTotalUs = 0;
    uint64_t holdMaxUs = 0;
  } stats;

  static uint64_t now_us() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
  }

  void report_stats_if_due(uint64_t nowUs) {
    if (stats.windowStartUs == 0) {
      stats.windowStartUs = nowUs;
      return;
    }
    if (nowUs - stats.windowStartUs < 1000000ULL) return;
    std::ostringstream oss;
    oss << "dxgi-acquire acquires=" << stats.acquires << " timeouts=" << stats.timeouts
        << " dxgiContentAcquires=" << stats.contentAcquires
        << " dxgiPointerOnly=" << stats.pointerOnlyAcquires
        << " coalesced=" << stats.coalescedAcquires
        << " accumTotal=" << stats.accumulatedTotal << " accumMax=" << stats.accumulatedMax
        << " holdAvgUs=" << (stats.acquires ? stats.holdTotalUs / stats.acquires : 0)
        << " holdMaxUs=" << stats.holdMaxUs;
    log("capture", oss.str());
    stats = AcquireStats{};
    stats.windowStartUs = nowUs;
  }

  void run() {
    // RAII: every exit path (recreate failure, texture QI failure, size change, handler exception,
    // stop) must publish "worker exited" so the host wedge watchdog never mistakes a cleanly-
    // stopped worker for a hang. Named-return and each early `return` all unwind through this.
    // (Codex-reviewed 2026-08-25.)
    struct RunningGuard {
      WorkerProgress* p;
      ~RunningGuard() {
        if (!p) return;
        const uint64_t t = steady_now_us();
        p->phaseStartedUs.store(t, std::memory_order_release);
        p->lastProgressUs.store(t, std::memory_order_release);
        p->phase.store(static_cast<uint32_t>(CaptureWorkerPhase::Exited), std::memory_order_release);
        p->running.store(false, std::memory_order_release);
      }
    } runningGuard{progress};

    auto enterPhase = [&](CaptureWorkerPhase ph) {
      if (!progress) return;
      // phaseStartedUs before phase so a reader that sees the new phase also sees a start no later
      // than it -- phaseAge is then an upper bound, never a stale-large value.
      progress->phaseStartedUs.store(steady_now_us(), std::memory_order_release);
      progress->phase.store(static_cast<uint32_t>(ph), std::memory_order_release);
    };
    auto markProgress = [&]() {
      if (progress) progress->lastProgressUs.store(steady_now_us(), std::memory_order_release);
    };

    while (!stopRequested.load()) {
      enterPhase(CaptureWorkerPhase::Report);
      report_stats_if_due(now_us());
      markProgress();

      enterPhase(CaptureWorkerPhase::Acquire);
      DXGI_OUTDUPL_FRAME_INFO frameInfo{};
      Microsoft::WRL::ComPtr<IDXGIResource> resource;
      const HRESULT hr = duplication->AcquireNextFrame(config.acquireTimeoutMs, &frameInfo, &resource);
      if (progress) progress->lastAcquireHr.store(static_cast<int32_t>(hr), std::memory_order_release);
      markProgress();
      if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // The routine 100ms idle path -- markProgress above already refreshed the heartbeat, so a
        // static desktop keeps the worker "young" and never trips the wedge watchdog.
        ++stats.timeouts;
        // Leave a guaranteed lock-free window before re-entering AcquireNextFrame: the call holds
        // an internal, unfair D3D11 device lock for its whole duration (Sunshine display_base.cpp
        // documents the same starvation and sleeps after a timeout), and once WGC/MF have turned
        // multithread protection on for the shared device, an immediate re-entry starves the
        // readback worker and the encoder -- measured 100-500ms readback waits on a still desktop
        // right after a WGC round trip (GNLink 0.2.98). Success path untouched: a frame is never
        // held here and ReleaseFrame timing is unchanged.
        if (config.acquireIdleSleepUs > 0 && !stopRequested.load()) {
          std::this_thread::sleep_for(std::chrono::microseconds(config.acquireIdleSleepUs));
        }
        continue;
      }
      if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_ACCESS_DENIED) {
        log("capture", "duplication_recreate begin reason=" + hresult_hex(hr));
        std::string recreateDetail;
        if (!recreate_duplication(&recreateDetail)) {
          log("capture", "duplication_recreate end success=0 detail=" + recreateDetail);
          request_fallback(recreateDetail);
          return;
        }
        log("capture", "duplication_recreate end success=1 width=" + std::to_string(width) +
                           " height=" + std::to_string(height));
        continue;
      }
      if (FAILED(hr)) {
        request_fallback("dxgi_acquire_failed_" + hresult_hex(hr));
        return;
      }

      ++stats.acquires;
      // P0 (#351): classify this acquire. A content frame carries new desktop pixels
      // (LastPresentTime!=0); a pointer-only frame carries just a cursor move. Hot path: counter
      // increments only, no log/mutex.
      if (frameInfo.LastPresentTime.QuadPart != 0) {
        ++stats.contentAcquires;
      } else if (frameInfo.LastMouseUpdateTime.QuadPart != 0) {
        ++stats.pointerOnlyAcquires;
      }
      if (progress) {
        progress->lastAccumulatedFrames.store(frameInfo.AccumulatedFrames, std::memory_order_release);
      }
      if (frameInfo.AccumulatedFrames > 0) {
        stats.accumulatedTotal += frameInfo.AccumulatedFrames;
        if (frameInfo.AccumulatedFrames > stats.accumulatedMax) {
          stats.accumulatedMax = frameInfo.AccumulatedFrames;
        }
        if (frameInfo.AccumulatedFrames > 1) ++stats.coalescedAcquires;
      }
      // Forward hardware-pointer reports even when the frame carries no desktop update:
      // pointer-only frames are dropped downstream (the pointer is never composited), so this
      // side channel is the only way a still screen can show the remote cursor moving.
      if (config.onPointer && frameInfo.LastMouseUpdateTime.QuadPart != 0) {
        config.onPointer(frameInfo.PointerPosition.Position.x,
                         frameInfo.PointerPosition.Position.y,
                         frameInfo.PointerPosition.Visible != FALSE);
      }
      const uint64_t acquiredAtUs = now_us();

      bool frameHeld = true;
      auto releaseFrame = [&]() {
        if (!frameHeld || !duplication) return;
        enterPhase(CaptureWorkerPhase::Release);
        const HRESULT relHr = duplication->ReleaseFrame();
        if (progress) progress->lastReleaseHr.store(static_cast<int32_t>(relHr), std::memory_order_release);
        markProgress();
        frameHeld = false;
        const uint64_t heldUs = now_us() - acquiredAtUs;
        stats.holdTotalUs += heldUs;
        if (heldUs > stats.holdMaxUs) stats.holdMaxUs = heldUs;
      };

      enterPhase(CaptureWorkerPhase::ResourceQI);
      Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
      const HRESULT texHr = resource.As(&texture);
      markProgress();
      if (FAILED(texHr) || !texture) {
        releaseFrame();
        request_fallback("dxgi_frame_texture_qi_failed_" + hresult_hex(texHr));
        return;
      }

      enterPhase(CaptureWorkerPhase::TextureDesc);
      D3D11_TEXTURE2D_DESC desc{};
      texture->GetDesc(&desc);
      markProgress();
      if (desc.Width != width || desc.Height != height) {
        releaseFrame();
        request_fallback("dxgi_frame_size_changed");
        return;
      }

      enterPhase(CaptureWorkerPhase::FrameHandler);
      try {
        if (onFrame) onFrame(texture.Get(), width, height, frameInfo.AccumulatedFrames);
      } catch (...) {
        releaseFrame();
        request_fallback("dxgi_frame_handler_exception");
        return;
      }
      markProgress();

      releaseFrame();
      if (progress) progress->loopCount.fetch_add(1, std::memory_order_acq_rel);
      enterPhase(CaptureWorkerPhase::Loop);
    }
  }
};

DxgiDesktopCaptureSession::DxgiDesktopCaptureSession()
    : progress_(std::make_unique<WorkerProgress>()), impl_(std::make_unique<Impl>()) {}

DxgiDesktopCaptureSession::~DxgiDesktopCaptureSession() {
  Stop();
}

bool DxgiDesktopCaptureSession::Start(const DxgiDesktopCaptureConfig& config,
                                      DxgiDesktopFrameHandler onFrame,
                                      DxgiDesktopLogHandler onLog,
                                      DxgiDesktopFallbackHandler onFallback,
                                      std::string* detailOut) {
  Stop();
  impl_ = std::make_unique<Impl>();
  impl_->config = config;
  impl_->onFrame = std::move(onFrame);
  impl_->onLog = std::move(onLog);
  impl_->onFallback = std::move(onFallback);
  impl_->d3dDevice = config.d3dDevice;
  if (!impl_->d3dDevice) {
    if (detailOut) *detailOut = "dxgi_missing_device";
    impl_.reset();
    return false;
  }

  std::string recreateDetail;
  if (!impl_->recreate_duplication(&recreateDetail)) {
    if (detailOut) *detailOut = recreateDetail;
    impl_.reset();
    return false;
  }

  // Publish a fresh heartbeat BEFORE the worker spawns so a watchdog poll that races the spawn sees
  // a young running worker, not a stale block from a prior episode. Bump generation so any warn
  // latched against the previous worker is discarded.
  impl_->progress = progress_.get();
  const uint64_t startUs = steady_now_us();
  progress_->phase.store(static_cast<uint32_t>(CaptureWorkerPhase::Loop), std::memory_order_release);
  progress_->phaseStartedUs.store(startUs, std::memory_order_release);
  progress_->lastProgressUs.store(startUs, std::memory_order_release);
  progress_->loopCount.store(0, std::memory_order_release);
  progress_->lastAcquireHr.store(0, std::memory_order_release);
  progress_->lastReleaseHr.store(0, std::memory_order_release);
  progress_->lastAccumulatedFrames.store(0, std::memory_order_release);
  progress_->generation.fetch_add(1, std::memory_order_acq_rel);
  progress_->running.store(true, std::memory_order_release);
  try {
    impl_->worker = std::thread([impl = impl_.get()]() { impl->run(); });
  } catch (...) {
    // Spawn failed: roll the running flag back so the watchdog does not treat a never-started
    // worker as wedged.
    progress_->running.store(false, std::memory_order_release);
    if (detailOut) *detailOut = "dxgi_worker_thread_spawn_failed";
    impl_.reset();
    return false;
  }
  if (detailOut) *detailOut = "ok";
  return true;
}

void DxgiDesktopCaptureSession::Stop() {
  if (!impl_) return;
  impl_->stopRequested = true;
  if (impl_->worker.joinable()) impl_->worker.join();
  impl_->duplication.Reset();
}

CaptureWorkerSnapshot DxgiDesktopCaptureSession::SnapshotWorker() const {
  CaptureWorkerSnapshot s;
  if (!progress_) return s;
  s.running = progress_->running.load(std::memory_order_acquire);
  s.generation = progress_->generation.load(std::memory_order_acquire);
  s.phase = static_cast<CaptureWorkerPhase>(progress_->phase.load(std::memory_order_acquire));
  const uint64_t lastProgressUs = progress_->lastProgressUs.load(std::memory_order_acquire);
  const uint64_t phaseStartedUs = progress_->phaseStartedUs.load(std::memory_order_acquire);
  const uint64_t now = steady_now_us();
  s.ageUs = (now > lastProgressUs) ? (now - lastProgressUs) : 0;
  s.phaseAgeUs = (now > phaseStartedUs) ? (now - phaseStartedUs) : 0;
  s.loopCount = progress_->loopCount.load(std::memory_order_acquire);
  s.lastAcquireHr = progress_->lastAcquireHr.load(std::memory_order_acquire);
  s.lastReleaseHr = progress_->lastReleaseHr.load(std::memory_order_acquire);
  s.lastAccumulatedFrames = progress_->lastAccumulatedFrames.load(std::memory_order_acquire);
  return s;
}

uint32_t DxgiDesktopCaptureSession::width() const {
  return impl_ ? impl_->width : 0;
}

uint32_t DxgiDesktopCaptureSession::height() const {
  return impl_ ? impl_->height : 0;
}

}  // namespace remote60::host
