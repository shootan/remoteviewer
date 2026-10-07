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
#include "clipboard_monitor.hpp"
#include "host_clip_image.hpp"
#include "host_file_copy.hpp"
#if defined(REMOTE60_CLIP_IMAGE_TEST_SINK)
#include "host_clip_image_test_sink.hpp"
#endif
#include "host_control_session.hpp"
#include "host_main_loop.hpp"
#include "host_startup.hpp"

namespace {
// Only what main() itself names. Everything else that used to live in this file is in the host_*
// modules (see docs/호스트_분할_리팩터_계획.md for the map); the startup / shutdown functions are in
// host_startup.hpp, the loop stages in host_main_loop.hpp.
using remote60::native_poc::HostPowerKeepalive;
using remote60::native_poc::Args;
using remote60::native_poc::env_truthy;
using remote60::native_poc::parse_args;
using remote60::native_poc::WinsockScope;
using remote60::native_poc::FrameGatingState;
using remote60::native_poc::RateControlState;
using remote60::native_poc::KickState;
using remote60::native_poc::ClientMetricsSnapshot;
using remote60::native_poc::DesktopBackendState;
using remote60::native_poc::WatchdogState;
using remote60::native_poc::InputRouterState;
using remote60::native_poc::SenderState;
using remote60::native_poc::SessionState;
using remote60::native_poc::EncoderState;
using remote60::native_poc::HostStats;
using remote60::native_poc::CaptureState;
using remote60::native_poc::CaptureResources;
using remote60::native_poc::WindowSelectionTxn;
using remote60::native_poc::ControlSessionServer;
using remote60::native_poc::HostContext;
using remote60::native_poc::TickContext;
using remote60::native_poc::DxgiWatchdogJoiner;
using remote60::native_poc::MainLoopWatchdogThread;
using remote60::native_poc::MainLoopMailbox;
using remote60::native_poc::startup_process_setup;
using remote60::native_poc::startup_configure_from_env;
using remote60::native_poc::resolve_transport;
using remote60::native_poc::startup_log_config;
using remote60::native_poc::startup_configure_session;
using remote60::native_poc::startup_connect_client;
using remote60::native_poc::startup_configure_control_state;
using remote60::native_poc::startup_start_control_threads;
using remote60::native_poc::startup_init_graphics;
using remote60::native_poc::startup_select_capture_target;
using remote60::native_poc::startup_configure_encode_geometry;
using remote60::native_poc::startup_init_encoder;
using remote60::native_poc::startup_start_dxgi_watchdog;
using remote60::native_poc::startup_create_readback;
using remote60::native_poc::startup_start_capture;
using remote60::native_poc::startup_start_main_loop_watchdog;
using remote60::native_poc::shutdown_host;
using remote60::native_poc::VideoTransport;
}  // namespace

// Run one main-loop stage and honour the flow it reports. Deliberately not a do/while(0) macro:
// the continue/break must act on the enclosing while (!stop) loop.
#define RUN_STAGE(fn)                                                       \
  {                                                                         \
    const remote60::native_poc::Flow f_ = fn(host, tc);                     \
    if (f_ == remote60::native_poc::Flow::Continue) continue;               \
    if (f_ == remote60::native_poc::Flow::Break) break;                     \
    if (f_ == remote60::native_poc::Flow::Return) return host.exitCode;     \
  }

int main(int argc, char** argv) {
  // A remote host cannot wake itself after Windows enters S3. Keep the machine reachable
  // while the host is running; the display requirement is enabled only for an active stream.
  HostPowerKeepalive powerKeepalive;
  // Outlive every capture/encoder/session owner, including their destructors after shutdown_host.
  // A GPU resource Release may block too; disarming before those destructors left a teardown gap.
  WatchdogState watchdog;
  MainLoopWatchdogThread mainLoopWatchdog;
  startup_process_setup();

  const Args args = parse_args(argc, argv);
  // Viewer input routing: injection mode, SYSTEM broker, counters, cursor forwarder (InputRouterState, Phase 1-9).
  InputRouterState inputRouter;
  const bool useRaw = (args.codec == "raw");
  const bool useH264 = (args.codec == "h264");
  const bool guardStaleEncoded = env_truthy("REMOTE60_NATIVE_GUARD_STALE_ENCODED");
  // Encoded-frame sender queue/thread, UDP peer, media barrier, wire counters (SenderState, Phase 1-2).
  SenderState sender;
  // Capture target/selection requests, geometry, backend flags, WGC settle gate, DXGI cursor,
  // publish timestamps, bootstrap cache, idle/reattach backoff (CaptureState, Phase 1-3a).
  CaptureState capture;
  const bool guardStalePreEncode = env_truthy("REMOTE60_NATIVE_GUARD_STALE_PREENCODE");
  // ABR profile ladder + M9 level ladder config and runtime state (RateControlState, Phase 1-6).
  RateControlState rate;
  // Static-frame gating config + loop state (FrameGatingState, Phase 1-7).
  FrameGatingState frameGating;
  // MFT wrapper, active encode geometry/rate, tune requests, key-request bucket, force-key latch,
  // NV12 surface bookkeeping, starvation heartbeat, encode counters (EncoderState, Phase 1-5).
  EncoderState encoder;
  // Capture liveness watchdogs + main-loop liveness stamps (WatchdogState, Phase 1-11).
  // Trailing-edge kick / static refresh / selection-first-keyframe state (KickState, Phase 1-8).
  KickState kick;
  WinsockScope ws;
  VideoTransport transport = VideoTransport::Tcp;
  // Client session sockets, directory agent/auth, session epoch, control threads (SessionState, Phase 1-1).
  SessionState clientSession;
  std::atomic<bool> stop{false};
  // Split of what happened while a security prompt or the lock screen was in front. inputRouter.events
  // alone cannot answer it: the fallback path reports success whether or not the click reached
  // anything, so a dead session and a working one produce identical numbers.
  // P2 desktop-backend promotion (WGC -> requested DXGI climb-back). Lifetime totals, never
  // per-second reset: a session-shape summary is more useful than a rate for a rare transition.
  // Desktop capture backend request/active/backoff/secure-gate/promotion telemetry (DesktopBackendState, Phase 1-4).
  DesktopBackendState backend;
  // Viewer-reported metrics + keyframe requests, control thread -> main loop (ClientMetricsSnapshot, Phase 1-10).
  ClientMetricsSnapshot clientMetrics;
  // Control-thread <-> main-loop window selection handshake (WindowSelectionTxn, host_control_session.hpp).
  WindowSelectionTxn windowSelectionTxn;
  // Control conversation handler (ControlSessionServer, Phase 2-2); one Serve() per connected viewer.
  // Requests the control / sender / reader threads post for the loop to act on (Phase 4).
  MainLoopMailbox mailbox;
  // Clipboard text sync (K1). Default on; REMOTE60_CLIPBOARD_SYNC=0 turns the whole feature off
  // (the host then never advertises the capability, so no viewer sends clipboard messages). The
  // monitor thread is started below; the destructor stops it on every exit path.
  remote60::native_poc::HostClipboardHub clipboardHub;
  const bool clipboardSyncEnabled = []() {
    const std::string v = remote60::native_poc::env_string_or_empty("REMOTE60_CLIPBOARD_SYNC");
    return !(v == "0" || v == "false" || v == "off");
  }();
  if (clipboardSyncEnabled) clipboardHub.Start();
  // Clipboard image v1 (direction A, viewer -> host). Rides the clipboard sync switch: off with it,
  // and only advertised while the hub runs. REMOTE60_CLIPBOARD_IMAGE=0 turns images alone off.
  // Started with the UDP control channel (host_startup_control.cpp); stopped in shutdown_host.
  const bool clipboardImageEnabled = clipboardSyncEnabled && []() {
    const std::string v = remote60::native_poc::env_string_or_empty("REMOTE60_CLIPBOARD_IMAGE");
    return !(v == "0" || v == "false" || v == "off");
  }();
  remote60::native_poc::HubClipImagePublisher clipImagePublisher(&clipboardHub);
  remote60::native_poc::HostClipImagePublisher* clipImagePublisherInUse =
      clipboardImageEnabled ? &clipImagePublisher : nullptr;
#if defined(REMOTE60_CLIP_IMAGE_TEST_SINK)
  // TEST BUILD ONLY (GNLinkStreamClipSink): received images go to a folder instead of a clipboard,
  // so a host under test next to the user's session never writes the user's clipboard. Absent from
  // GNLinkStream.exe (clip_image_build_gate_test).
  const std::wstring clipSinkDir = [] {
    wchar_t buf[MAX_PATH] = L"";
    const DWORD n = GetEnvironmentVariableW(L"REMOTE60_CLIP_IMAGE_TEST_SINK_DIR", buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
  }();
  remote60::native_poc::FileSinkClipImagePublisher clipSinkPublisher(clipSinkDir);
  if (!clipSinkDir.empty()) {
    clipImagePublisherInUse = &clipSinkPublisher;
    std::cout << "[native-video-host][clip-image] TEST SINK publisher (no clipboard)\n";
  }
#endif
  remote60::native_poc::HostClipImageService clipImageService(clipImagePublisherInUse);
  // One bulk per session: the image service and file copy take it in turn (bulk_arbiter.hpp).
  remote60::native_poc::BulkArbiter hostBulkArbiter;
  clipImageService.SetBulkArbiter(&hostBulkArbiter);
  if (clipImagePublisherInUse) {
    clientSession.clipImage = &clipImageService;
    clientSession.onClipImageSessionEnd = [&clipImageService](uint64_t epoch) { clipImageService.OnSessionEnd(epoch); };
  }
  // File copy (t-zdmsd4gb), both directions. Rides the clipboard sync switch like images;
  // REMOTE60_CLIPBOARD_FILES=0 turns files alone off. The helper is started as the interactive user
  // (the linked token) when a paste or a copy needs it -- no High / SYSTEM fallback; if that is
  // impossible the offer / paste is refused. This PC's copies of files (CF_HDROP) reach it as path
  // strings from the clipboard monitor; the R->P sender yields while video is queued.
  const bool clipboardFilesEnabled = clipboardSyncEnabled && []() {
    const std::string v = remote60::native_poc::env_string_or_empty("REMOTE60_CLIPBOARD_FILES");
    return !(v == "0" || v == "false" || v == "off");
  }();
  remote60::native_poc::HostFileCopyService fileCopyService;
  // Destroyed before fileCopyService: the monitor thread stops calling into it first.
  struct FileListenerDetach {
    remote60::native_poc::HostClipboardHub& hub;
    ~FileListenerDetach() { hub.SetFileListener(nullptr); }
  } fileListenerDetach{clipboardHub};
#if defined(REMOTE60_CLIP_IMAGE_TEST_SINK)
  // TEST BUILD ONLY (GNLinkStreamClipSink): R->P files from a folder instead of the user's clipboard
  // (FileCopyTestSource), with clipboard sync off. Absent from GNLinkStream.exe (clip_image_build_gate_test).
  remote60::native_poc::FileCopyTestSource fileTestSource([] {
    wchar_t buf[MAX_PATH] = L"";
    const DWORD n = GetEnvironmentVariableW(L"REMOTE60_FILE_COPY_TEST_SOURCE_DIR", buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
  }());
  if (fileTestSource.Enabled() && !clipboardFilesEnabled) {
    remote60::native_poc::HostFileCopyService::Config fcfg;
    fcfg.enabled = true;
    fcfg.launcher = fileTestSource.Launcher();
    fcfg.videoBusy = [&sender]() {
      std::lock_guard<std::mutex> lock(sender.mu);
      return !sender.queue.empty();
    };
    fileCopyService.Configure(fcfg, &hostBulkArbiter);
    clientSession.fileCopy = &fileCopyService;
    clientSession.onFileCopySessionEnd = [&fileCopyService](uint64_t epoch) { fileCopyService.OnSessionEnd(epoch); };
    fileTestSource.Start(&fileCopyService);
    std::cout << "[native-video-host][file-copy] TEST SOURCE (no clipboard)\n";
  } else if (clipboardFilesEnabled && [] {
               wchar_t v[8] = L"";
               return GetEnvironmentVariableW(L"REMOTE60_FILE_COPY_TEST_HELPER_AS_SELF", v, 8) > 0 && v[0] == L'1';
             }()) {
    // TEST BUILD ONLY: a host on a private window station, not elevated (the transfer-bar e2e) --
    // its file-copy helper runs as this same user on this station, instead of the shell token's
    // launch the shipped host uses. Everything else is the product path.
    remote60::native_poc::HostFileCopyService::Config fcfg;
    fcfg.enabled = true;
    fcfg.launcher = [&clipboardHub](remote60::native_poc::file_copy::HelperLink* link, std::string* why) {
      wchar_t path[MAX_PATH] = L"";
      const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
      std::wstring exe(path, n);
      exe = exe.substr(0, exe.find_last_of(L'\\') + 1) + L"GNLinkClipHelper.exe";
      const bool ok = remote60::native_poc::file_copy::launch_file_copy_helper_as_self(exe, nullptr, L"", link, why);
      if (ok) clipboardHub.NoteOwnHelper(link->helper_pid());
      return ok;
    };
    fcfg.videoBusy = [&sender]() {
      std::lock_guard<std::mutex> lock(sender.mu);
      return !sender.queue.empty();
    };
    fileCopyService.Configure(fcfg, &hostBulkArbiter);  // the product block below then finds it configured
    std::cout << "[native-video-host][file-copy] TEST HELPER AS SELF (private station)\n";
  }
#endif
  if (clipboardFilesEnabled) {
    remote60::native_poc::HostFileCopyService::Config fcfg;
    fcfg.enabled = true;
    const std::wstring helperExe = [] {
      wchar_t path[MAX_PATH] = L"";
      GetModuleFileNameW(nullptr, path, MAX_PATH);
      std::wstring s = path;
      return s.substr(0, s.find_last_of(L'\\') + 1) + L"GNLinkClipHelper.exe";
    }();
    fcfg.launcher = [helperExe, &clipboardHub](remote60::native_poc::file_copy::HelperLink* link, std::string* why) {
      // A refusal is logged by the service with `why` (class, stage, error, token facts); a start is
      // logged here with the same token facts, so the field log shows what the helper ran as.
      std::string tokens;
      const bool ok = remote60::native_poc::file_copy::launch_file_copy_helper(helperExe, link, why, 10000, &tokens);
      if (ok) {
        std::cout << "[native-video-host][file-copy] helper started pid=" << link->helper_pid() << " (" << tokens << ")\n";
        // Its clipboard writes are this host's own, not a copy made at this PC (paste on demand r4).
        clipboardHub.NoteOwnHelper(link->helper_pid());
      }
      return ok;
    };
    fcfg.videoBusy = [&sender]() {
      std::lock_guard<std::mutex> lock(sender.mu);
      return !sender.queue.empty();
    };
    fileCopyService.Configure(fcfg, &hostBulkArbiter);
    // Called on the monitor thread right after the hub counted the change (if it was a copy): the
    // copy generation read here is that change's (paste on demand r7, the offer's order).
    clipboardHub.SetFileListener([&fileCopyService, &clipboardHub](uint64_t seq, std::vector<std::wstring> paths) {
      fileCopyService.OnHostClipboard(seq, std::move(paths), clipboardHub.CopyGen());
    });
    clientSession.fileCopy = &fileCopyService;
    clientSession.onFileCopySessionEnd = [&fileCopyService](uint64_t epoch) { fileCopyService.OnSessionEnd(epoch); };
  }
  ControlSessionServer controlServer(args, stop, clientSession, capture, clientMetrics, encoder,
                                     inputRouter, backend, windowSelectionTxn, mailbox,
                                     clipboardSyncEnabled ? &clipboardHub : nullptr);
  // RAII / WinRT / D3D capture objects (CaptureResources, Phase 2-4); created below at the same points as before.
  CaptureResources res;
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  int32_t poppedNv12Slot = -1;
  uint64_t poppedNv12Generation = 0;
  winrt::event_token token{};
  // DXGI capture-worker wedge watchdog thread (started by startup_start_dxgi_watchdog once the
  // encoder is up); the joiner stops and joins it before res.dxgiCaptureSession is destroyed.
  std::atomic<bool> dxgiWatchdogStop{false};
  std::thread dxgiWorkerWatchdog;
  DxgiWatchdogJoiner dxgiWatchdogJoiner{&dxgiWatchdogStop, &dxgiWorkerWatchdog};
  // Main-loop liveness watchdog. Declared here -- after `watchdog` and `stop`, which its thread
  // reads -- so reverse destruction order joins the thread before that state dies. It used to be
  // detached, which left a ~1s window after main() returned where it read freed stack. (H-06)
  uint64_t streamActiveSinceUs = 0;
  // Per-interval / lifetime pipeline statistics for the stats line (HostStats, Phase 1-12).
  HostStats stats;
  // Main-loop timing / pacing values that the stages share (declared here so HostContext can bind
  // them; startUs and the derived values are stamped below, right where they used to be declared).
  uint64_t startUs = 0;
  uint64_t nextTickUs = 0;
  const bool paceByTick = useRaw;
  uint64_t captureWindowRebindIntervalUs = 0;
  uint64_t nextCaptureWindowCheckUs = 0;
  bool streamActiveApplied = true;
  // Everything the loop stages and helpers reach (HostContext, Phase 3). Assembled once; the members
  // are references to the objects above, named like them.
  HostContext host{args, useH264, useRaw, transport, stop, guardStaleEncoded, guardStalePreEncode,
                   paceByTick, startUs, nextTickUs, captureWindowRebindIntervalUs,
                   nextCaptureWindowCheckUs, streamActiveApplied, streamActiveSinceUs, poppedNv12Slot,
                   poppedNv12Generation, powerKeepalive, item, token, windowSelectionTxn, frameGating,
                   rate, kick, clientMetrics, backend, watchdog, inputRouter, sender, clientSession,
                   encoder, stats, capture, res, mailbox};

  // Startup, in the monolith's order (host_startup.hpp). Each step is one former block of main();
  // the ones that can fail return the exit code main() used to return at that point.
  try {
  if (const int rc = startup_configure_from_env(host)) return rc;
  if (!ws.ok) {
    std::cerr << "[native-video-host] WSAStartup failed\n";
    return 1;
  }
  if (const int rc = resolve_transport(args, useRaw, useH264, transport)) return rc;
  watchdog.MarkMainProgress(remote60::native_poc::MainLoopPhase::Startup);
  startup_start_main_loop_watchdog(host, mainLoopWatchdog);
  remote60::native_poc::HostRunGuard runGuard{host};
  startup_log_config(host);
  startup_configure_session(host);
  if (const int rc = startup_connect_client(host)) return rc;
  watchdog.MarkMainProgress(remote60::native_poc::MainLoopPhase::Startup);
  startup_configure_control_state(host);
  startup_start_control_threads(host, controlServer);
  if (const int rc = startup_init_graphics(host)) return rc;
  if (const int rc = startup_select_capture_target(host)) return rc;
  startup_configure_encode_geometry(host);
  if (const int rc = startup_init_encoder(host)) return rc;
  startup_start_dxgi_watchdog(host, dxgiWatchdogStop, dxgiWorkerWatchdog);
  if (const int rc = startup_create_readback(host)) return rc;
  if (const int rc = startup_start_capture(host)) return rc;

  // One tick = the twelve stages of host_main_loop.cpp, in order. A stage that used to
  // `continue`/`break`/`return` from the loop body reports it through Flow.
  //
  // stage_stats runs FIRST, and that position is load-bearing (ledger H-10). It carries the 1s
  // housekeeping -- the stats line, the ABR/M9 decision, the readback-drain soft watchdog and
  // the GDI low-push capture restart -- and any Flow::Continue from an earlier stage skips
  // everything after it. As the last stage it was therefore skipped by the two most common
  // exits in the whole loop: the frame-queue wait timing out (stage_pop_frame) and the async
  // MFT returning no access unit (encode_send_h264). Both of those are exactly what a stalled
  // pipeline looks like, so the self-heal that exists to notice a stall could not run while one
  // was happening. Nothing here reads this tick's frame, so running before the pop costs only a
  // one-tick shift in which window the tick's own counters land in.
  while (!stop.load()) {
    TickContext tc;
    RUN_STAGE(stage_stats);
    RUN_STAGE(stage_time_limit);
    RUN_STAGE(stage_backend);
    RUN_STAGE(stage_stream_active);
    RUN_STAGE(stage_runtime_tune);
    RUN_STAGE(stage_selection);
    RUN_STAGE(stage_geometry);
    RUN_STAGE(stage_watchdogs);
    RUN_STAGE(stage_pace);
    RUN_STAGE(stage_pop_frame);
    RUN_STAGE(stage_gate_static);
    RUN_STAGE(stage_encode_send);
  }

  shutdown_host(host);
  runGuard.active = false;
  std::cout << "[native-video-host] done\n";
  return 0;
  } catch (...) {
    // Catching outside HostRunGuard ensures a real unwind, including partially started workers.
    return 49;
  }
}
