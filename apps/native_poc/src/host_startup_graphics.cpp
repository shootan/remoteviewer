// Host startup 4/5: WinRT / WGC / Media Foundation / D3D bring-up, capture target (window criteria,
// monitor, capture item, size), encode geometry + ABR / M9 ladders, H.264 encoder init.
//
// Host split refactor Phase 2-12: moved verbatim out of main() (native_video_host_main.cpp); see
// host_startup.hpp for the call order and HostContext (host_main_loop.hpp) for the shared state.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "mf_h264_codec.hpp"
#include "bind_port_candidates.hpp"
#include "capture_cadence_gate.hpp"
#include "d3d_capture_readback.hpp"
#include "directory_client.hpp"
#include "encode_resolution_ladder.hpp"
#include "gdi_capture_process.hpp"
#include "json_profile.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "secure_input_broker.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "capture_backend_dxgi.hpp"
#include "host_string_util.hpp"
#include "host_log.hpp"
#include "host_args.hpp"
#include "host_bgra_scale.hpp"
#include "host_bottleneck.hpp"
#include "host_frame_state.hpp"
#include "host_gpu_scaler.hpp"
#include "host_window_enum.hpp"
#include "host_capture_device.hpp"
#include "host_net_io.hpp"
#include "host_input_inject.hpp"
#include "host_frame_gate.hpp"
#include "host_abr.hpp"
#include "host_kick.hpp"
#include "host_client_metrics.hpp"
#include "host_backend_policy.hpp"
#include "host_watchdog.hpp"
#include "host_input_router.hpp"
#include "host_encoded_sender.hpp"
#include "host_session.hpp"
#include "host_encoder_manager.hpp"
#include "host_stats.hpp"
#include "host_capture_session.hpp"
#include "host_control_session.hpp"
#include "host_main_loop.hpp"
#include "host_startup.hpp"

#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT
#define REMOTE60_NATIVE_ENCODED_EXPERIMENT 0
#endif

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::host::DesktopCaptureBackend;
using remote60::host::DxgiDesktopCaptureConfig;
using remote60::host::DxgiDesktopCaptureSession;

namespace remote60::native_poc {

int startup_init_graphics(HostContext& hx) {
  auto& useH264 = hx.useH264;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  auto& res = hx.res;
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  if (!GraphicsCaptureSession::IsSupported()) {
    std::cerr << "[native-video-host] WGC not supported\n";
    closesocket(clientSession.clientSock);
    if (clientSession.listenSock != INVALID_SOCKET) closesocket(clientSession.listenSock);
    return 6;
  }

  if (useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-host] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      closesocket(clientSession.clientSock);
      if (clientSession.listenSock != INVALID_SOCKET) closesocket(clientSession.listenSock);
      return 12;
    }
    encoder.mfStarted = true;
  }

  HRESULT hr = create_d3d11_device_for_primary_monitor(&res.d3d, &res.ctx, &res.fl);
  if (FAILED(hr)) {
    std::cerr << "[native-video-host] D3D11CreateDevice failed\n";
    closesocket(clientSession.clientSock);
    if (clientSession.listenSock != INVALID_SOCKET) closesocket(clientSession.listenSock);
    if (encoder.mfStarted) MFShutdown();
    return 7;
  }
  if (useH264) {
    (void)encoder.codec.set_d3d11_device(res.d3d.Get());
  }
  if (capture.gpuScalerRequested) {
    capture.gpuScalerHealthy = res.gpuScaler.initialize(res.d3d.Get(), res.ctx.Get(), &res.d3dContextMu);
    std::cout << "[native-video-host] gpuScalerRequested=1 gpuScalerReady="
              << (capture.gpuScalerHealthy ? 1 : 0) << "\n";
  }
  return 0;
}

int startup_select_capture_target(HostContext& hx) {
  auto& args = hx.args;
  auto& item = hx.item;
  auto& backend = hx.backend;
  auto& inputRouter = hx.inputRouter;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  capture.windowCriteria.pid = args.captureWindowPid;
  for (const auto& name : parse_csv_lower(args.captureWindowProcess)) {
    capture.windowCriteria.processNamesLower.insert(name);
  }
  capture.windowCriteria.titleNeedleLower = wide_lower(utf8_to_wide(trim_ascii(args.captureWindowTitle)));
  capture.selectionLockedByConfig = capture.windowCriteria.enabled() || inputRouter.targetCriteria.enabled();
  capture.windowSelectionLocked.store(capture.selectionLockedByConfig, std::memory_order_release);
  capture.windowTargetConfigured = capture.windowCriteria.enabled();
  backend.requested = desktop_capture_backend_from_env();
  backend.active = backend.requested;
  // A demotion away from the requested backend is temporary until proven otherwise; these pace
  // the attempts to get back to it. First retry is quick because the usual causes -- a UAC prompt
  // being answered, RDP disconnecting -- clear in seconds; the ceiling keeps a machine that
  // genuinely cannot use the requested backend from restarting capture forever.
  backend.retryDelayUs = kDesktopBackendRetryMinUs;
  // P2 secure-desktop stable gate. A demotion to WGC (UAC prompt, lock screen, RDP) used to be
  // climbed back on a bare 3s timer, so every retry deadline that fired while the secure desktop
  // was still up spent a restart_capture_session (pipeline flush + forced IDR) that failed at once
  // -- E_ACCESSDENIED churn. Now the promotion is additionally gated on the interactive DEFAULT
  // desktop having been up continuously for kDesktopDefaultStableUs, probed uncached at a bounded
  // cadence, with one final uncached check the instant before the restart to close the
  // probe->restart TOCTOU window. A genuine promotion failure (e.g. RDP: default desktop stable but
  // primary duplication still unavailable) is NOT deferred here -- it falls through to the existing
  // exponential backoff, which is the right owner for a backend that truly cannot start.
  capture.windowClientOnlyActive = args.captureWindowClientOnly;
  if (capture.windowTargetConfigured && find_capture_window(capture.windowCriteria, &capture.windowInfo)) {
    capture.windowModeActive = true;
    std::cout << "[native-video-host] capture-window target hwnd=0x" << std::hex
              << reinterpret_cast<uintptr_t>(capture.windowInfo.hwnd) << std::dec
              << " pid=" << capture.windowInfo.pid
              << " process=" << (capture.windowInfo.processName.empty() ? "unknown" : capture.windowInfo.processName)
              << " title=" << (capture.windowInfo.title.empty() ? "<empty>" : wide_to_utf8(capture.windowInfo.title))
              << " clientOnly=" << (args.captureWindowClientOnly ? 1 : 0)
              << "\n";
  } else if (capture.windowTargetConfigured) {
    std::cout << "[native-video-host] capture-window target not found; fallback=monitor"
              << " pidFilter=" << args.captureWindowPid
              << " processFilter=" << trim_ascii(args.captureWindowProcess)
              << " titleFilter=" << trim_ascii(args.captureWindowTitle)
              << "\n";
  }
  capture.selectedWindowId.store(capture.windowModeActive ? hwnd_to_id(capture.windowInfo.hwnd) : 0u,
                              std::memory_order_release);
  capture.targetFlags.store((capture.windowModeActive ? 0x1u : 0x0u) |
                                   ((capture.windowModeActive && capture.windowClientOnlyActive) ? 0x2u : 0x0u),
                               std::memory_order_relaxed);
  capture.targetPid.store(capture.windowModeActive ? capture.windowInfo.pid : 0u, std::memory_order_relaxed);
  capture.targetHwnd.store(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
                                  capture.windowModeActive ? capture.windowInfo.hwnd : nullptr)),
                              std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lk(capture.metaMu);
    capture.targetProcess =
        (capture.windowModeActive && !capture.windowInfo.processName.empty()) ? capture.windowInfo.processName : "monitor";
    capture.targetTitle =
        (capture.windowModeActive && !capture.windowInfo.title.empty()) ? wide_to_utf8(capture.windowInfo.title)
                                                                       : std::string{};
  }

  capture.monitorInfo = primary_monitor_info();
  if (!capture.monitorInfo.has_value()) {
    std::cerr << "[native-video-host] primary monitor query failed\n";
    closesocket(clientSession.clientSock);
    if (clientSession.listenSock != INVALID_SOCKET) closesocket(clientSession.listenSock);
    if (encoder.mfStarted) MFShutdown();
    return 8;
  }
  if (!capture.windowModeActive && backend.requested == DesktopCaptureBackend::Dxgi &&
      capture.monitorInfo->width < capture.monitorInfo->height) {
    backend.active = DesktopCaptureBackend::Wgc;
    std::cout << "[native-video-host] rotation_unsupported fallback_reason=rotation_unsupported\n";
  }
  // Tell the SYSTEM agent where the captured pixels live. Without it the agent can only assume,
  // and its old assumption -- the primary monitor -- put every click on the wrong screen when the
  // prompt opened somewhere else.
  inputRouter.broker.SetTargetRect(capture.monitorInfo->originX, capture.monitorInfo->originY, capture.monitorInfo->width,
                                  capture.monitorInfo->height);

  if (capture.windowModeActive || backend.active == DesktopCaptureBackend::Wgc) {
    item = capture.windowModeActive
               ? CreateItemForPrimaryMonitor(capture.windowInfo.hwnd, "CreateForWindow(target-window)")
               : CreateItemForPrimaryMonitor();
    if (!item) {
      std::cerr << "[native-video-host] capture item create failed\n";
      closesocket(clientSession.clientSock);
      if (clientSession.listenSock != INVALID_SOCKET) closesocket(clientSession.listenSock);
      if (encoder.mfStarted) MFShutdown();
      return 8;
    }
    capture.size = item.Size();
    capture.width = static_cast<uint32_t>(capture.size.Width);
    capture.height = static_cast<uint32_t>(capture.size.Height);
  } else {
    capture.width = capture.monitorInfo->width;
    capture.height = capture.monitorInfo->height;
    capture.size.Width = static_cast<int32_t>(capture.width);
    capture.size.Height = static_cast<int32_t>(capture.height);
  }
  if (capture.width < 2 || capture.height < 2) {
    std::cerr << "[native-video-host] invalid capture size\n";
    closesocket(clientSession.clientSock);
    if (clientSession.listenSock != INVALID_SOCKET) closesocket(clientSession.listenSock);
    if (encoder.mfStarted) MFShutdown();
    return 9;
  }
  std::cout << "[native-video-host] desktop_backend="
            << (capture.windowModeActive ? "wgc_window" : desktop_capture_backend_name(backend.active))
            << " capture=" << capture.width << "x" << capture.height << "\n";
  return 0;
}

void startup_configure_encode_geometry(HostContext& hx) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  encoder.encodeW = capture.width;
  encoder.encodeH = capture.height;
  if (useH264) {
    choose_h264_encode_size(args, capture.width, capture.height, &encoder.encodeW, &encoder.encodeH, &rate.autoFallback720);
  }

  // Whether the ladder, rather than the source size, is currently deciding the resolution. Held
  // across runtime changes so the band between the two thresholds can return the previous answer.
  rate.encodeLadderReduced = rate.autoFallback720;
  rate.abrHighW = encoder.encodeW;
  rate.abrHighH = encoder.encodeH;
  rate.abrMidW = rate.abrHighW;
  rate.abrMidH = rate.abrHighH;
  rate.abrLowW = rate.abrHighW;
  rate.abrLowH = rate.abrHighH;
  if (useH264) {
    choose_abr_720_size(rate.abrHighW, rate.abrHighH, &rate.abrLowW, &rate.abrLowH);
  }
  rate.abrHasLowerResolution = (rate.abrLowW < rate.abrHighW || rate.abrLowH < rate.abrHighH);
  rate.abrHighBitrate = args.bitrate;
  rate.abrMidBitrate = std::min<uint32_t>(
      rate.abrHighBitrate, std::max<uint32_t>(2000000u, (rate.abrHighBitrate * 75u) / 100u));
  rate.abrLowBitrate = std::min<uint32_t>(
      rate.abrHighBitrate, std::max<uint32_t>(1500000u, (rate.abrHighBitrate * 55u) / 100u));
  rate.abrHasMidProfile = (rate.abrMidBitrate < rate.abrHighBitrate);
  rate.abrHasLowProfile = rate.abrHasLowerResolution || (rate.abrLowBitrate < rate.abrMidBitrate);
  rate.m9BitrateLevel0 = rate.abrHighBitrate;
  rate.m9BitrateLevel1 = std::min<uint32_t>(
      rate.m9BitrateLevel0, std::max<uint32_t>(1500000u, (rate.m9BitrateLevel0 * 80u) / 100u));
  rate.m9BitrateLevel2 = std::min<uint32_t>(
      rate.m9BitrateLevel1, std::max<uint32_t>(1200000u, (rate.m9BitrateLevel0 * 65u) / 100u));
  rate.m9BitrateLevel3 = std::min<uint32_t>(
      rate.m9BitrateLevel2, std::max<uint32_t>(900000u, (rate.m9BitrateLevel0 * 50u) / 100u));
  rate.m9FpsLevel0 = args.fps;
  rate.m9FpsLevel1 = args.fps;
  rate.m9FpsLevel2 = std::max<uint32_t>(20u, (args.fps * 80u) / 100u);
  rate.m9FpsLevel3 = std::max<uint32_t>(15u, (args.fps * 67u) / 100u);
  rate.m9WidthLevel0 = rate.abrHighW;
  rate.m9HeightLevel0 = rate.abrHighH;
  rate.m9WidthLevel1 = rate.abrHighW;
  rate.m9HeightLevel1 = rate.abrHighH;
  rate.m9WidthLevel2 = rate.abrHighW;
  rate.m9HeightLevel2 = rate.abrHighH;
  rate.m9WidthLevel3 = rate.abrLowW;
  rate.m9HeightLevel3 = rate.abrLowH;
  encoder.activeEncodeW = rate.abrHighW;
  encoder.activeEncodeH = rate.abrHighH;
  // Nominal (pre-aspect-fit) encode box of the current quality level, and the source size
  // the active encode dimensions were fitted against.
  encoder.nominalEncodeW = rate.abrHighW;
  encoder.nominalEncodeH = rate.abrHighH;
  encoder.encodeSourceW = capture.width;
  encoder.encodeSourceH = capture.height;
  // Refit debounce: candidate geometry and how long it has been stable.
  encoder.activeFps = args.fps;
  // What the user asked for, as distinct from whatever the encoder is running at this
  // moment: overview mode lowers the active values on purpose, and restoring focus from
  // "whatever is active" would restore the lowered ones. Only an explicit runtime tune of
  // the same field moves a ceiling -- a bitrate-only tune falls back to active values for
  // its fps/keyint arguments, and those must not leak in here.
  rate.userFpsCeiling = args.fps;
  rate.userKeyintCeiling = args.keyint;
  encoder.activeBitrate = rate.abrHighBitrate;
  // Field A/B override for the keyframe interval (0 = off). Every ~1s a 120-160KB IDR was
  // measured holding the previous frame an extra tick on 75% of key presents (the user's
  // "periodically shows the previous frame"); this pins keyint (e.g. 120) without touching the
  // client, winning over both the CLI default and runtime tunes.
  encoder.keyintOverride = env_u32_clamped("REMOTE60_NATIVE_KEYINT_OVERRIDE", 0, 0, 600);
  encoder.activeKeyint = encoder.keyintOverride != 0 ? encoder.keyintOverride : args.keyint;
  encoder.activeFrameIntervalUs =
      std::max<uint64_t>(1, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, encoder.activeFps)));
  encoder.activePacingFrameIntervalUs = encoder.activeFrameIntervalUs;
  capture.submitMinIntervalUs = encoder.activeFrameIntervalUs;
  // Picks which offered frames reach the encoder, and how evenly. Guarded by its own mutex
  // because capture callbacks can arrive on more than one thread across backends.
  frameGating.staticIntervalUs =
      std::max<uint64_t>(encoder.activeFrameIntervalUs, std::max<uint64_t>(1, 1000000ULL / frameGating.staticFps));
  inputRouter.domainW.store(encoder.activeEncodeW, std::memory_order_release);
  inputRouter.domainH.store(encoder.activeEncodeH, std::memory_order_release);
  // Submit latch for encoder.forceKeyNext. The async MFT can hold the key output for a few inputs, and
  // forcing EVERY input in the meantime produced trains of 4-5 consecutive 40-160KB IDRs per
  // request (measured at 17:24:26/32/52 in the field log). One forced input per request: stamped
  // on submit, cleared when a key is accepted into the send path (on UDP that is the send-queue
  // enqueue, not the wire; a failed send re-forces via barrier recovery), and timing out (300ms)
  // so a lost key retries.
  encoder.RefreshFrameIntervals(capture, frameGating);
  // Declared before every lambda that references them. FrameState precedes the pipeline so
  // the worker's publish callback never outlives what it writes into.
  // Asynchronous readback ring: the capture callback only submits a GPU copy; a worker maps
  // finished copies and publishes them. The publish function is assigned below, before the
  // first create_staging call.
  // Encoder OUTPUT-liveness heartbeat. The main-loop liveness watchdog only tracks loop iteration
  // progress (watchdog.mainLoopProgressUs), which keeps advancing even when the async hardware MFT accepts
  // input every call but emits no output access unit -- an output-starvation wedge that freezes the
  // video while the loop still spins and the watchdog stays green. These track real encoder output
  // progress so that stall is observable (and, later, recoverable). Diagnostic-only in this commit.
                                               // a from-startup encoder that never emits one AU)
  // Async-event counters accumulated ACROSS the current no-output streak (reset when output
  // resumes) so one anomaly line can tell a host event-driving bug (NeedInput accrues, HaveOutput
  // stays 0) from a genuine vendor/hardware stall, rather than showing only the last call's counts.
  // Clears the CURRENT starvation episode (not the lifetime totals). Must run whenever the encoder
  // is shut down + reinitialized or the stream (re)activates, otherwise a no-output streak left over
  // from the previous encoder -- or a long stream-inactive gap -- would inflate noOutputAgeUs and
  // fire a false starvation log on the fresh encoder's first inputs.
}

int startup_init_encoder(HostContext& hx) {
  auto& useH264 = hx.useH264;
  auto& rate = hx.rate;
  auto& backend = hx.backend;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  auto& res = hx.res;
  if (useH264) {
    if (!encoder.codec.initialize(encoder.activeEncodeW, encoder.activeEncodeH, encoder.activeFps, encoder.activeBitrate, encoder.activeKeyint)) {
      std::cerr << "[native-video-host] H264 encoder initialize failed\n";
      closesocket(clientSession.clientSock);
      if (clientSession.listenSock != INVALID_SOCKET) closesocket(clientSession.listenSock);
      if (encoder.mfStarted) MFShutdown();
      return 13;
    }
    encoder.ResetTimelineAnchors(capture);
    const std::string requestedEncoderBackend = env_string_or_empty("REMOTE60_NATIVE_ENCODER_BACKEND");
    const std::string requestedEncoderBackendPrint =
        requestedEncoderBackend.empty() ? "default(mft_auto)" : requestedEncoderBackend;
    const std::string backendFallbackReason =
        backend_fallback_reason(requestedEncoderBackend, encoder.codec.backend_name());
    std::cout << "[native-video-host] H264 encoder backend=" << encoder.codec.backend_name()
              << " backendRequested=" << requestedEncoderBackendPrint
              << " backendResolved=" << encoder.codec.backend_name()
              << " backendFallbackReason=" << backendFallbackReason
              << " hw=" << (encoder.codec.using_hardware() ? 1 : 0)
              << " captureSize=" << capture.width << "x" << capture.height
              << " encodeSize=" << encoder.activeEncodeW << "x" << encoder.activeEncodeH
              << " auto720=" << (rate.autoFallback720 ? 1 : 0)
              << " abrMidProfile=" << rate.abrMidW << "x" << rate.abrMidH
              << " abrMidBitrate=" << rate.abrMidBitrate
              << " abrLowProfile=" << rate.abrLowW << "x" << rate.abrLowH
              << " abrLowBitrate=" << rate.abrLowBitrate
              << "\n";
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
  res.d3d.As(&dxgi);
  winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), res.inspectable.put()));
  res.d3dDevice = res.inspectable.as<IDirect3DDevice>();
  return 0;
}

}  // namespace remote60::native_poc
