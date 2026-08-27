#pragma once

// Capture session state (BootstrapFrameCache, CaptureState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "capture_backend_dxgi.hpp"
#include "capture_cadence_gate.hpp"
#include "d3d_capture_readback.hpp"
#include "gdi_capture_process.hpp"
#include "host_frame_state.hpp"
#include "host_gpu_scaler.hpp"
#include "host_capture_device.hpp"
#include "host_stats.hpp"
#include "host_window_enum.hpp"

namespace remote60::native_poc {

struct DesktopBackendState;
struct EncoderState;
struct FrameGatingState;
struct HostStats;
struct KickState;
struct SessionState;

// Static-screen bootstrap cache: a memory-only copy of the last raw frame actually published,
// plus the identity of the capture that produced it. On a static desktop DXGI AcquireNextFrame
// just times out after a (re)start, so the forced keyframe has nothing to encode and a fresh
// viewer sits black for seconds. Keeping the last-good frame lets the main loop re-encode it once
// as an IDR so the picture paints immediately. Written ONLY from a real capture publish
// (capturePublishFn) and deliberately NOT touched by flush_capture_pipeline_state, so it survives
// a flush and a reattach can still use it.
struct BootstrapFrameCache {
  std::shared_ptr<std::vector<uint8_t>> payload;  // BGRA pixels, post-crop
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint64_t captureQpcUs = 0;      // real capture time, for replay-age telemetry only
  uint64_t streamGeneration = 0;  // capture generation that produced these pixels
  bool windowMode = false;
  uint64_t selectedWindowId = 0;
  uint64_t targetHwnd = 0;
  uint32_t targetPid = 0;
  uint32_t srcCaptureWidth = 0;   // pre-crop capture source dims (meta.width/height)
  uint32_t srcCaptureHeight = 0;
  uint32_t consoleSessionId = 0;  // WTS active console session at capture time
};

// Capture session state (Phase 1-3a state struct): everything the capture path shares that is
// plain data or an atomic/mutex -- the current target (window/monitor/desktop) as the control
// thread reports it, the client's selection/mode requests, capture geometry and cadence gate,
// backend start/fallback flags with their reasons, the WGC ContentSize settle gate, the DXGI
// hardware-cursor side channel, publish/pop timestamps, the static-screen bootstrap cache and
// the idle-detach / reattach backoff. The RAII/WinRT/D3D objects (device, frame pool, item,
// DXGI/GDI sessions, readback pipeline, FrameState) deliberately stay locals of main() until
// Phase 2's CaptureSession class gives them an explicit lifetime. See the comment blocks in
// main() for the rationale behind each group.
// thread: main loop owns the plain fields; atomics are the callback/control <-> main signals;
// metaMu guards target*/selected* for the control thread's list/status replies; cadenceMu guards
// the cadence gate (callbacks arrive on more than one thread); fallbackReasonMu guards the two
// reason strings; resourceMu serialises pool/readback recreation; bootstrapCacheMu guards the
// bootstrap frame.

// Capture resources (Phase 2-4): the RAII / WinRT / D3D objects the capture path owns -- the D3D
// device + immediate context (shared with the GPU scaler under d3dContextMu), the readback ring and
// its publish callback, the WinRT interop device, the WGC frame pool + session, and the DXGI / GDI
// backend sessions. Declared in the same order as the former main() locals so their destruction
// order is unchanged; main() still creates the device / interop device into them at the original
// points and owns the capture item, the FrameArrived event token and the DXGI worker watchdog.
// thread: main loop creates/destroys; the readback worker and capture callbacks use frame,
// captureReadback and the D3D context (under d3dContextMu) while a session is attached.
// Frame-queue wait bounds shared by main() and CaptureState::EffectiveQueueWaitTimeoutUs.
constexpr uint64_t kQueueWaitTimeoutUsDefault = 100000;  // 100ms
constexpr uint64_t kQueueWaitTimeoutUsMin = 5000;  // 5ms

struct CaptureResources {
  Microsoft::WRL::ComPtr<ID3D11Device> d3d;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
  std::mutex d3dContextMu;
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  GpuBgraScaler gpuScaler;
  // FrameState precedes the pipeline so the worker's publish callback never outlives what it
  // writes into.
  FrameState frame;
  D3dCaptureReadbackPipeline captureReadback;
  D3dCaptureReadbackPipeline::PublishFn capturePublishFn;
  winrt::com_ptr<::IInspectable> inspectable;
  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice d3dDevice{nullptr};
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
  remote60::host::DxgiDesktopCaptureSession dxgiCaptureSession;
  GdiCaptureProcess gdiCaptureProcess;
};
struct CaptureState {
  // Env config (REMOTE60_NATIVE_CAPTURE_*/QUEUE_WAIT_*/DISABLE_GPU_SCALER), fixed after startup.
  bool submitLimitEnabled = true;
  uint32_t submitEarlyTolerancePercent = 0;
  uint32_t stallKeepaliveIntervalUsOverride = 0;
  uint32_t queueWaitTimeoutUsOverride = 0;
  bool gpuScalerRequested = false;
  bool gpuScalerHealthy = false;
  int framePoolBuffers = 0;
  // Current capture target as published to the control thread (window list / status replies).
  std::atomic<uint32_t> targetPid{0};
  std::atomic<uint32_t> targetFlags{0};
  std::atomic<uint32_t> rebindCount{0};
  std::atomic<uint64_t> targetHwnd{0};
  std::mutex metaMu;
  std::string targetProcess = "monitor";
  std::string targetTitle;
  std::atomic<uint64_t> selectedWindowId{0};
  std::atomic<uint32_t> selectedMonitorId{0};
  // cross-thread: selection / capture-mode requests from the control thread, consumed by main.
  std::atomic<uint32_t> monitorSelectRequested{0};
  std::atomic<bool> monitorSelectPending{false};
  std::atomic<uint64_t> streamGenerationState{1};
  std::atomic<bool> windowSelectionLocked{false};
  std::atomic<bool> modeReqPending{false};
  std::atomic<uint32_t> modeReqSeq{0};
  std::atomic<uint16_t> modeReqMode{0};
  std::atomic<uint32_t> modeReqXPermille{5000};
  std::atomic<uint32_t> modeReqYPermille{5000};
  // Window target (from --capture-window-* / the picker).
  CaptureWindowCriteria windowCriteria{};
  bool selectionLockedByConfig = false;
  bool windowTargetConfigured = false;
  std::atomic<bool> windowModeActive{false};
  std::atomic<bool> windowClientOnlyActive{false};
  CaptureWindowInfo windowInfo{};
  // Geometry and submit cadence.
  std::optional<PrimaryMonitorInfo> monitorInfo;
  uint32_t width = 0;
  uint32_t height = 0;
  winrt::Windows::Graphics::SizeInt32 size{};
  std::atomic<uint64_t> submitMinIntervalUs{0};
  std::atomic<uint64_t> nextSubmitUs{0};
  remote60::native_poc::CaptureCadenceGate cadenceGate;
  std::mutex cadenceMu;
  int64_t timelineOriginUs = -1;
  // Backend session flags, restart accounting and fallback reasons.
  std::atomic<bool> sessionReady{false};
  std::atomic<bool> dxgiFallbackRequested{false};
  std::atomic<bool> gdiFallbackRequested{false};
  uint64_t sessionStartedUs = 0;
  uint64_t restartCount = 0;
  bool dxgiStarted = false;
  bool gdiStarted = false;
  std::mutex fallbackReasonMu;
  std::string dxgiFallbackReason;
  std::string gdiFallbackReason;
  std::mutex resourceMu;
  std::atomic<uint32_t> sizeChangePending{0};
  // Attachment cookie: bumped by detach_capture_session() before any pool recreate so a callback or
  // readback that began under the previous attachment drops its frame.
  std::atomic<uint64_t> attachmentCookie{1};
  // WTS console session this attachment belongs to, stamped alongside the cookie. The bootstrap
  // cache records it per frame so a kick can refuse to repaint pixels captured in a different
  // console session (fast user switch / lock). It is stamped at attach rather than queried per
  // publish because WTSGetActiveConsoleSessionId is a syscall and the publish path runs at up to
  // 60Hz -- and it used to run inside bootstrapCacheMu, holding the lock across it. A switch
  // while attached tears the capture down and re-attaches, which re-stamps; until then the stale
  // stamp fails the live comparison in KickTryFill, which is the safe direction. (Ledger H-18.)
  std::atomic<uint32_t> attachedConsoleSessionId{0};
  // WGC ContentSize gate (callback records the mismatch; main settles then recreates the pool).
  std::atomic<uint32_t> wgcContentSizeMismatchPending{0};
  std::atomic<uint32_t> wgcPendingContentW{0};
  std::atomic<uint32_t> wgcPendingContentH{0};
  std::atomic<uint64_t> wgcContentSizeMismatchDrops{0};
  uint32_t wgcSettleTrackW = 0;
  uint32_t wgcSettleTrackH = 0;
  uint64_t wgcSettleSinceUs = 0;
  uint64_t wgcPoolRecreates = 0;
  uint32_t stagingSlotCount = 0;
  // Hardware-cursor side channel from the DXGI backend (capture thread writes, main drains).
  std::atomic<int32_t> dxgiPointerX{0};
  std::atomic<int32_t> dxgiPointerY{0};
  std::atomic<bool> dxgiPointerVisible{false};
  std::atomic<uint64_t> dxgiPointerGeneration{0};  // stream generation the sample belongs to
  std::atomic<uint64_t> dxgiPointerUpdateUs{0};
  std::atomic<int64_t> clockOffsetUs{std::numeric_limits<int64_t>::max()};
  // Publish / pop timestamps.
  std::atomic<uint64_t> lastPopFrameVersion{0};
  std::atomic<uint64_t> lastCallbackUs{0};
  std::atomic<uint64_t> lastPublishUs{0};   // last frame actually published to the encoder ring
  std::atomic<uint64_t> lastCaptureUsForInterval{0};
  std::atomic<uint64_t> firstCallbackLoggedGeneration{0};
  // Static-screen bootstrap cache (last raw frame actually published; survives a flush).
  std::mutex bootstrapCacheMu;
  BootstrapFrameCache bootstrapCache;
  // Idle detach (no client) and reattach backoff.
  bool idleDetached = false;
  uint64_t idleDetachAtUs = 0;
  uint64_t reattachRetryAtUs = 0;
  uint64_t reattachRetryDelayUs = 0;

  // --- behaviour (Phase 2-1: former main() lambdas set/copy_*_fallback_reason,
  //     describe_active_capture_target) ---
  void SetDxgiFallbackReason(const std::string& reason) {
    std::lock_guard<std::mutex> lock(fallbackReasonMu);
    dxgiFallbackReason = reason;
  }
  void SetGdiFallbackReason(const std::string& reason) {
    std::lock_guard<std::mutex> lock(fallbackReasonMu);
    gdiFallbackReason = reason;
  }
  // The one lock-correct way to read the cadence gate's counters from another thread.
  // (Ledger H-05.)
  CaptureCadenceGate::Counters SnapshotCadenceCounters() {
    std::lock_guard<std::mutex> lk(cadenceMu);
    return cadenceGate.SnapshotCounters();
  }
  std::string CopyDxgiFallbackReason() {
    std::lock_guard<std::mutex> lock(fallbackReasonMu);
    return dxgiFallbackReason;
  }
  std::string CopyGdiFallbackReason() {
    std::lock_guard<std::mutex> lock(fallbackReasonMu);
    return gdiFallbackReason;
  }
  // One-line description of the current capture target for log lines.
  std::string DescribeActiveTarget() {
    const uint64_t hwnd = targetHwnd.load(std::memory_order_acquire);
    const uint32_t pid = targetPid.load(std::memory_order_acquire);
    std::string process = "monitor";
    std::string title;
    {
      std::lock_guard<std::mutex> lk(metaMu);
      process = targetProcess;
      title = targetTitle;
    }
    std::ostringstream oss;
    oss << " streamGen=" << streamGenerationState.load(std::memory_order_acquire)
        << " selectedId=" << selectedWindowId.load(std::memory_order_acquire)
        << " targetHwnd=0x" << std::hex << hwnd << std::dec
        << " pid=" << pid
        << " process=" << process
        << " title=" << (title.empty() ? "<empty>" : title);
    return oss.str();
  }

  // --- behaviour (Phase 2-4: former main() capture lambdas; bodies in host_capture_session.cpp) ---
  // (Re)create the staging/readback ring for a srcW x srcH capture surface.
  bool CreateStaging(CaptureResources& res, EncoderState& encoder, bool useH264, uint32_t srcW, uint32_t srcH);
  // Hand a captured texture to the readback ring (capture callback thread).
  void PublishCapturedTexture(CaptureResources& res, ID3D11Texture2D* src, uint64_t callbackUs,
                              uint64_t sourceCaptureUs, uint64_t captureAgeAtCallbackUs,
                              uint64_t captureClockSkewUs, bool hasNewContent);
  // Subscribe the WGC FrameArrived callback on the current pool.
  void AttachFrameArrived(CaptureResources& res, SessionState& clientSession, std::atomic<bool>& stop,
                          winrt::event_token& token);
  // Tear down the current capture attachment (WGC pool/session, DXGI or GDI backend).
  void DetachCaptureSession(CaptureResources& res, winrt::event_token& token);
  // Full restart: detach, resolve the backend, rebuild the pool/readback, reattach.
  bool RestartCaptureSessionImpl(CaptureResources& res, DesktopBackendState& backend, SessionState& clientSession,
                                 EncoderState& encoder, std::atomic<bool>& stop, bool useH264,
                                 winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item, winrt::event_token& token);
  // Drop everything queued for the encoder after a target/backend/size change.
  void FlushCapturePipelineState(CaptureResources& res, FrameGatingState& frameGating, HostStats& stats, const char* reason);
  void LogFirstSentGeneration(CaptureResources& res, HostStats& stats, const char* path, uint64_t streamGeneration,
                              uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height);
  // Serve the cached bootstrap frame for a trailing kick / static refresh, if still valid.
  bool KickTryFill(SessionState& clientSession, KickState& kick, std::shared_ptr<std::vector<uint8_t>>& outPayload,
                   uint32_t& outW, uint32_t& outH, uint32_t& outStride, uint64_t nowUs);
  uint64_t EffectiveQueueWaitTimeoutUs(EncoderState& encoder);
  // Readback-worker publish callback body (main() binds res.capturePublishFn to it).
  void PublishFrame(CaptureResources& res, HostStats& stats, std::shared_ptr<std::vector<uint8_t>> payload,
                    uint32_t frameW, uint32_t frameH, uint32_t stride, const CaptureFrameMeta& meta,
                    uint64_t gpuPendingUs, uint64_t workerMapUs, uint64_t workerMemcpyUs);
};

}  // namespace remote60::native_poc
