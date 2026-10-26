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
#include "frame_signal_collector.hpp"
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
// Why each desktop backend was demoted, as one record behind fallbackReasonMu; read with
// CaptureState::SnapshotFallbackReasons(). (Phase 4: BackendFallbackInfo.)
struct BackendFallbackInfo {
  std::string dxgi;
  std::string gdi;
};

// The capture target as the control thread is allowed to see it: the atomics and the two
// metaMu-guarded strings, taken together. (Phase 4: SnapshotTarget.)
struct CaptureTargetSnapshot {
  uint64_t streamGeneration = 0;
  uint64_t selectedWindowId = 0;
  uint32_t selectedMonitorId = 0;
  uint64_t targetHwnd = 0;
  uint32_t pid = 0;
  uint32_t flags = 0;
  uint32_t rebindCount = 0;
  std::string process = "monitor";
  std::string title;
};

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
  // capture-lifecycle r1: the input desktop it was published under, and that desktop's generation.
  std::wstring desktopName;
  uint64_t desktopGeneration = 0;
  // capture-lifecycle r2 (Codex N1): the attachment that made it (its cookie) -- a cached picture is only ever
  // the current attachment's.
  uint64_t attachmentCookie = 0;
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
  // Prelogin (prelogin-service r4 #1): set once at startup from Args::preloginMode. When true the DXGI
  // capture worker binds its thread to the input desktop (and the GDI worker is told the same), so the
  // pre-login secure desktop is captured. Default false keeps the post-login host unchanged.
  bool preloginAttachInputDesktop = false;
  // r9 W2 (--prelogin only): the desktop the worker serves (prelogin_logon_desktop.hpp), and whether the
  // input desktop is it right now (the desktop guard updates it; input is injected only while true).
  std::wstring preloginServingDesktop;
  std::atomic<bool> preloginDesktopServing{false};
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
  // The picked screen's stable device name, not an enum index. Written by the main loop under
  // metaMu (monitor-select, the restart's fallback); the monitor-list reply reads it under metaMu.
  std::wstring selectedMonitorDevice;
  // cross-thread: selection / capture-mode requests from the control thread, consumed by main.
  std::atomic<uint64_t> streamGenerationState{1};
  // Flush epoch (P11): bumped by EncoderState::ResetTimelineAnchors, i.e. on every capture flush,
  // restart, confirmed geometry change and encoder reset. Tags encoder inputs (H264Encoder FIFO)
  // so an AU the async MFT emits from a pre-flush input is recognised at the emit stage and the
  // new epoch's first send is its own IDR (host_epoch_gate.hpp).
  std::atomic<uint64_t> inputEpoch{1};
  // What the capture is targeting, as one record for the control thread's secure-input dispatch
  // (P9, host_secure_target_rect.hpp): window mode + the HMONITOR the session opened + the
  // stream generation, with a version that moves whenever any of them changes. Written by the
  // main loop (sync_input_target_rect), read under inputTargetMu; a reader's copy is valid for
  // one event only -- the dispatch re-reads the version before it sends.
  struct InputTargetSnapshot {
    uint64_t version = 0;
    bool windowMode = false;
    uint64_t monitorHandle = 0;
    uint64_t streamGeneration = 0;
  };
  std::mutex inputTargetMu;
  InputTargetSnapshot inputTarget;
  std::atomic<bool> windowSelectionLocked{false};
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
  // r12: the WGC FrameArrived delegate only signals this; its worker drains the pool and publishes.
  // Owned per attachment: created in AttachFrameArrived, stopped (bounded) in DetachCaptureSession.
  std::shared_ptr<FrameSignalCollector> frameCollector;
  std::atomic<bool> frameHeartbeatEnabled{false};
  std::atomic<bool> dxgiFallbackRequested{false};
  std::atomic<bool> gdiFallbackRequested{false};
  uint64_t sessionStartedUs = 0;
  uint64_t restartCount = 0;
  // Main-loop retry state outlives the request flag and a failed attachment.
  bool restartPending = false;
  uint64_t restartRetryAtUs = 0;
  uint32_t restartFailures = 0;
  bool dxgiStarted = false;
  bool gdiStarted = false;
  std::mutex fallbackReasonMu;
  BackendFallbackInfo fallbackReasons;
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
  // r13: readback completions PublishFrame dropped because their attachment cookie was not the live
  // one (a frame whose Submit began under a previous attachment). The stats line prints it.
  std::atomic<uint64_t> staleAttachmentDrops{0};
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
  // capture-lifecycle r1 (PS-A and UA-1 -- Codex §2/§4: one contract). The input desktop as the main loop's
  // tracker last CONFIRMED it (two agreeing uncached looks), and how many times it has changed. A frame is
  // cached with the desktop it was published under; a change clears the cache, and frames whose callback
  // came before the change was seen are not cached at all. A look that could not tell is no change.
  std::wstring desktopName;                          // under bootstrapCacheMu (tracker writes, publish reads)
  std::atomic<uint64_t> desktopGeneration{0};
  // capture-lifecycle r2 (Codex N1): the desktop generation the CURRENT attachment was made under (stamped when
  // it is made -- DetachCaptureSession, which every (re)attach runs first). A frame carries its attachment's
  // cookie; with that cookie current, this is the frame's own provenance. A confirmed change makes it stale at
  // once: nothing of that attachment is adopted (raw queue or cache) until a new attachment is made.
  std::atomic<uint64_t> attachmentDesktopGen{0};
  // An unknown look, or a first look at another name: not an edge (no restart, never "locked"), but the
  // picture's continuity is no longer known -- the cache is dropped and nothing is cached until a look sees the
  // confirmed desktop again.
  std::atomic<bool> desktopSuspect{false};
  std::atomic<uint64_t> provenanceRefused{0};        // frames refused at adoption (log rate limit)
  std::atomic<uint64_t> firstAdoptedLoggedCookie{0};  // r2: the attachment whose first adopted frame was logged
  std::atomic<uint64_t> desktopChangeDetectedUs{0};
  std::wstring desktopCandidate;                     // main loop only
  uint32_t desktopCandidateSeen = 0;
  uint64_t desktopProbeAtUs = 0;
  static constexpr uint64_t kDesktopProbeIntervalUs = 100000;
  // PS-A: the bounded full-frame reacquire when nothing cached may be shown (main loop only). An episode
  // ends with the next real published frame of the current provenance (PublishFrame clears
  // fullFrameEpisodeOpen -- a refused frame never does).
  // r2 (Codex N4): an episode belongs to ONE source: desktop generation, stream generation, control session and
  // the source boundary below. Any of them moved -> a new source, a new bounded budget; the same source keeps
  // its budget across its own reacquire (DXGI duplication remakes, the full-frame WGC restart).
  struct FullFrameSourceKey {
    uint64_t desktopGeneration = 0;
    uint64_t streamGeneration = 0;
    uint64_t sessionEpoch = 0;
    uint64_t sourceBoundary = 0;
    bool operator==(const FullFrameSourceKey& o) const {
      return desktopGeneration == o.desktopGeneration && streamGeneration == o.streamGeneration &&
             sessionEpoch == o.sessionEpoch && sourceBoundary == o.sourceBoundary;
    }
    bool operator!=(const FullFrameSourceKey& o) const { return !(*this == o); }
  };
  FullFrameSourceKey fullFrameEpisodeKey;
  uint64_t fullFrameEpisodes = 0;  // for the log
  // Bumped by every capture (re)attach (DetachCaptureSession) -- idle reattach, backend switch, climb-back,
  // desktop rebuild, selection, geometry -- EXCEPT the full-frame WGC restart, the episode's own reacquire
  // (fullFrameOwnRestart, set around it by the backend stage). Main loop only.
  uint64_t fullFrameSourceBoundary = 0;
  bool fullFrameOwnRestart = false;
  uint64_t fullFrameAskedAtUs = 0;
  uint32_t fullFrameAsks = 0;
  std::atomic<bool> fullFrameEpisodeOpen{false};
  std::atomic<bool> wgcFullFrameRestartWanted{false};  // WGC: the main loop's bounded restart does it
  // When the current capture attachment began (DetachCaptureSession stamps it: every (re)attach detaches
  // first). A fresh attachment's own first frame is on its way: no full-frame ask in the first
  // kFullFrameAttachGraceUs (a second restart of a capture 10 ms old helps nobody).
  std::atomic<uint64_t> lastAttachUs{0};
  // capture-lifecycle r1 (UA-2): the backend as the main loop last applied it, for the control thread's
  // ControlDesktopBackendStatus (90) -- requested | effective<<16 | active<<32 | flags<<48 (wire codes) -- how
  // many times it changed, and the (epoch<<32 | seq) of the last backend request the main loop handled.
  std::atomic<uint64_t> backendStatusPacked{0};
  std::atomic<uint32_t> backendStatusChanges{0};
  std::atomic<uint64_t> backendAppliedKey{0};
  // capture-lifecycle r2 (Codex N2/N3): the owner epoch backendStatusPacked was published for (stored after it,
  // read around it by the control thread), and the (epoch<<32 | seq) of the last backend CHANGE a control thread
  // posted (a question is answered NotYetApplied while that one is not yet handled).
  std::atomic<uint64_t> backendStatusEpoch{0};
  std::atomic<uint64_t> backendChangePostedKey{0};
  static constexpr uint64_t kFullFrameAttachGraceUs = 500000;
  static constexpr uint64_t kFullFrameAskIntervalUs = 1000000;
  static constexpr uint32_t kMaxFullFrameAsks = 3;
  // Idle detach (no client) and reattach backoff.
  bool idleDetached = false;
  uint64_t idleDetachAtUs = 0;
  uint64_t reattachRetryAtUs = 0;
  uint64_t reattachRetryDelayUs = 0;

  // --- behaviour (Phase 2-1: former main() lambdas set/copy_*_fallback_reason,
  //     describe_active_capture_target) ---
  void SetDxgiFallbackReason(const std::string& reason) {
    std::lock_guard<std::mutex> lock(fallbackReasonMu);
    fallbackReasons.dxgi = reason;
  }
  void SetGdiFallbackReason(const std::string& reason) {
    std::lock_guard<std::mutex> lock(fallbackReasonMu);
    fallbackReasons.gdi = reason;
  }
  // The one lock-correct way to read the cadence gate's counters from another thread.
  // (Ledger H-05.)
  CaptureCadenceGate::Counters SnapshotCadenceCounters() {
    std::lock_guard<std::mutex> lk(cadenceMu);
    return cadenceGate.SnapshotCounters();
  }
  // Both reasons in one lock acquisition, replacing the two Copy* accessors: a reader that took
  // the lock twice could describe a pair of states that never coexisted.
  // (Phase 4: BackendFallbackInfo.)
  BackendFallbackInfo SnapshotFallbackReasons() {
    std::lock_guard<std::mutex> lock(fallbackReasonMu);
    return fallbackReasons;
  }
  // The capture target as one consistent record. The control thread answers pongs, window lists
  // and status replies from this; it used to read four atomics and then take metaMu for the two
  // strings, so a selection landing mid-read could report a new pid with the old title.
  // (Phase 4: SnapshotTarget.)
  CaptureTargetSnapshot SnapshotTarget() {
    CaptureTargetSnapshot t;
    t.streamGeneration = streamGenerationState.load(std::memory_order_acquire);
    t.selectedWindowId = selectedWindowId.load(std::memory_order_acquire);
    t.selectedMonitorId = selectedMonitorId.load(std::memory_order_acquire);
    t.targetHwnd = targetHwnd.load(std::memory_order_acquire);
    t.pid = targetPid.load(std::memory_order_acquire);
    t.flags = targetFlags.load(std::memory_order_acquire);
    t.rebindCount = rebindCount.load(std::memory_order_acquire);
    {
      std::lock_guard<std::mutex> lk(metaMu);
      t.process = targetProcess;
      t.title = targetTitle;
    }
    return t;
  }
  // One-line description of the current capture target for log lines.
  std::string DescribeActiveTarget() {
    const CaptureTargetSnapshot t = SnapshotTarget();
    std::ostringstream oss;
    oss << " streamGen=" << t.streamGeneration
        << " selectedId=" << t.selectedWindowId
        << " targetHwnd=0x" << std::hex << t.targetHwnd << std::dec
        << " pid=" << t.pid
        << " process=" << t.process
        << " title=" << (t.title.empty() ? "<empty>" : t.title);
    return oss.str();
  }

  // --- behaviour (Phase 2-4: former main() capture lambdas; bodies in host_capture_session.cpp) ---
  // (Re)create the staging/readback ring for a srcW x srcH capture surface.
  bool CreateStaging(CaptureResources& res, EncoderState& encoder, bool useH264, uint32_t srcW, uint32_t srcH);
  // Hand a captured texture to the readback ring (capture callback thread).
  // r13 (Codex C1): `sourceAttachmentCookie` is the attachment the TEXTURE came from -- stamped by
  // the caller from the value it captured when its session was attached, never re-read here. A
  // detach that crosses the caller's own cookie check and this call bumps the live cookie; stamping
  // the live value would put the new cookie on old pixels and walk them past PublishFrame's guard.
  // 0 = no attachment identity (never checked).
  void PublishCapturedTexture(CaptureResources& res, ID3D11Texture2D* src, uint64_t callbackUs,
                              uint64_t sourceCaptureUs, uint64_t captureAgeAtCallbackUs,
                              uint64_t captureClockSkewUs, bool hasNewContent,
                              uint64_t sourceAttachmentCookie);
  // Subscribe the WGC FrameArrived callback on the current pool. False (r13, Codex C3) = the
  // collector could not start; nothing is subscribed and the attachment must not be published as
  // started -- the caller detaches and lets the normal restart retry run.
  bool AttachFrameArrived(CaptureResources& res, SessionState& clientSession, std::atomic<bool>& stop,
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
                              uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height, uint64_t auEpoch = 0, uint64_t currentEpoch = 0);
  // Serve the cached bootstrap frame for a trailing kick / static refresh, if still valid.
  // capture-lifecycle r1: *why says what a refusal was -- only the desktop that may be shown, with nothing
  // of it cached (or nothing of the current target), is worth asking the capture for a whole frame.
  enum class KickMiss : uint8_t { None, DesktopNotShown, NothingCached, NotCurrent };
  bool KickTryFill(SessionState& clientSession, KickState& kick, std::shared_ptr<std::vector<uint8_t>>& outPayload,
                   uint32_t& outW, uint32_t& outH, uint32_t& outStride, uint64_t nowUs, KickMiss* why = nullptr);
  // capture-lifecycle r1: the input desktop tracker (main loop; bounded cadence). True when a change was
  // confirmed just now: the cache is cleared and the generation moved.
  bool TrackInputDesktop(uint64_t nowUs);
  // The desktop the stream may show now: the served logon desktop before login, Default after.
  bool DesktopMayBeShown(const std::wstring& name, bool known) const;
  // Drop the cached frame (its continuity is no longer known). Any thread.
  void InvalidateBootstrapCache(const char* why);
  uint64_t EffectiveQueueWaitTimeoutUs(EncoderState& encoder);
  // Readback-worker publish callback body (main() binds res.capturePublishFn to it).
  void PublishFrame(CaptureResources& res, HostStats& stats, std::shared_ptr<std::vector<uint8_t>> payload,
                    uint32_t frameW, uint32_t frameH, uint32_t stride, const CaptureFrameMeta& meta,
                    uint64_t gpuPendingUs, uint64_t workerMapUs, uint64_t workerMemcpyUs);
};

}  // namespace remote60::native_poc
