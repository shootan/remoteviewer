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

namespace {

// TimestampPrefixBuf moved to host_log.hpp (host split refactor Phase 0-8); see the using-declaration
// below.

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::native_poc::ControlLink;
using remote60::native_poc::TcpControlLink;
using remote60::native_poc::UdpControlChannel;
using remote60::native_poc::UdpControlLink;
using remote60::native_poc::ControlInputAckMessage;
using remote60::native_poc::ControlInputEventMessage;
using remote60::native_poc::ControlInputTextMessage;
using remote60::native_poc::ControlStreamStateMessage;
using remote60::native_poc::ControlClientMetricsMessage;
using remote60::native_poc::ControlRequestKeyFrameMessage;
using remote60::native_poc::ControlRuntimeEncoderConfigMessage;
using remote60::native_poc::ControlDesktopBackendRequestMessage;
using remote60::native_poc::ControlCaptureModeRequestMessage;
using remote60::native_poc::ControlWindowEntry;
using remote60::native_poc::ControlMonitorListMessage;
using remote60::native_poc::ControlMonitorListRequestMessage;
using remote60::native_poc::ControlMonitorSelectMessage;
// String utilities extracted to host_string_util.hpp (Phase 0). These using-declarations keep every
// existing unqualified call site (in this anonymous namespace and in main) resolving unchanged.
using remote60::native_poc::trim_ascii;
using remote60::native_poc::ascii_lower;
using remote60::native_poc::utf8_to_wide;
using remote60::native_poc::wide_to_utf8;
using remote60::native_poc::wide_lower;
using remote60::native_poc::hr_hex;
using remote60::native_poc::parse_csv_lower;
using remote60::native_poc::base_name_lower;
// Logging / power keepalive extracted to host_log.hpp (Phase 0-8).
using remote60::native_poc::TimestampPrefixBuf;
using remote60::native_poc::wake_display_for_remote_session;
using remote60::native_poc::HostPowerKeepalive;
// Args record + env/number parsing helpers extracted to host_args.hpp (Phase 0-7a).
using remote60::native_poc::Args;
using remote60::native_poc::parse_u32;
using remote60::native_poc::env_string_or_empty;
using remote60::native_poc::env_truthy;
using remote60::native_poc::env_u32_clamped;
using remote60::native_poc::parse_args;
// CPU BGRA geometry/scaling + picker thumbnail extracted to host_bgra_scale.hpp/.cpp (Phase 0-2).
using remote60::native_poc::clamp_even_dim;
using remote60::native_poc::fit_size_preserving_aspect;
using remote60::native_poc::choose_h264_encode_size;
using remote60::native_poc::choose_abr_720_size;
using remote60::native_poc::estimate_bgra_change_permille;
using remote60::native_poc::box_halve_bgra;
using remote60::native_poc::resize_bgra_bilinear;
using remote60::native_poc::capture_window_thumbnail;
// Bottleneck-stage telemetry records + FrameState extracted to host_bottleneck.hpp /
// host_frame_state.hpp (Phase 0-10).
using remote60::native_poc::HostBottleneckStage;
using remote60::native_poc::D3DReadbackTiming;
using remote60::native_poc::update_host_bottleneck_stage;
using remote60::native_poc::detect_host_bottleneck_stage;
using remote60::native_poc::encoder_api_path_code;
using remote60::native_poc::FrameState;
// D3D11 VideoProcessor scaler extracted to host_gpu_scaler.hpp (Phase 0-3).
using remote60::native_poc::GpuBgraScaler;
// Window / monitor enumeration and capture-window lookup extracted to host_window_enum.hpp/.cpp (Phase 0-4).
using remote60::native_poc::hwnd_to_id;
using remote60::native_poc::window_id_to_hwnd;
using remote60::native_poc::WindowListEntry;
using remote60::native_poc::MonitorListEntry;
using remote60::native_poc::get_window_process_name;
using remote60::native_poc::get_window_class_name;
using remote60::native_poc::get_window_title;
using remote60::native_poc::describe_input_target;
using remote60::native_poc::should_exclude_window_process;
using remote60::native_poc::should_include_window;
using remote60::native_poc::window_content_extent;
using remote60::native_poc::enumerate_monitors;
using remote60::native_poc::enumerate_shareable_windows;
using remote60::native_poc::find_window_by_id;
using remote60::native_poc::CaptureWindowCriteria;
using remote60::native_poc::CaptureWindowInfo;
using remote60::native_poc::match_capture_window;
using remote60::native_poc::find_capture_window;
using remote60::native_poc::find_capture_window_input_target;
using remote60::native_poc::find_top_level_window_at_point;
// Backend request matching, desktop backend codes, crop, WGC item + D3D device creation extracted to
// host_capture_device.hpp/.cpp (Phase 0-6).
using remote60::native_poc::backend_request_is_any;
using remote60::native_poc::backend_request_satisfied;
using remote60::native_poc::backend_request_is_vendor_specific;
using remote60::native_poc::backend_fallback_reason;
using remote60::native_poc::desktop_capture_backend_from_env;
using remote60::native_poc::desktop_capture_backend_from_code;
using remote60::native_poc::desktop_capture_backend_code;
using remote60::native_poc::desktop_capture_backend_name;
using remote60::native_poc::compute_window_client_crop;
using remote60::native_poc::PrimaryMonitorInfo;
using remote60::native_poc::primary_monitor_info;
using remote60::native_poc::CreateItemForPrimaryMonitor;
using remote60::native_poc::SurfaceToTexture;
using remote60::native_poc::create_d3d11_device_for_primary_monitor;
// Socket I/O + UDP chunk sender (pacing, FEC, epoch abort) extracted to host_net_io.hpp/.cpp (Phase 0-5).
using remote60::native_poc::WinsockScope;
using remote60::native_poc::resolve_bind_address;
using remote60::native_poc::send_all;
using remote60::native_poc::SendPathStats;
using remote60::native_poc::send_all_timed;
using remote60::native_poc::recv_all;
using remote60::native_poc::recv_discard;
using remote60::native_poc::kUdpReceiveBufferBytes;
using remote60::native_poc::gUdpPacePeakBitrateBps;
using remote60::native_poc::gUdpVideoFecInterleaved;
using remote60::native_poc::gUdpKeyframePacePeakBitrateBps;
using remote60::native_poc::udp_pace_wait_until;
using remote60::native_poc::udp_pace_budget_us;
using remote60::native_poc::UdpSendOutcome;
using remote60::native_poc::send_udp_chunks_impl;
using remote60::native_poc::send_udp_chunks;
using remote60::native_poc::send_udp_chunks_timed;
// Viewer input -> Win32 injection (window/desktop paths, secure-desktop probe) extracted to
// host_input_inject.hpp/.cpp (Phase 0-1).
using remote60::native_poc::InputInjectionMode;
using remote60::native_poc::parse_input_injection_mode;
using remote60::native_poc::input_injection_mode_name;
using remote60::native_poc::DesktopInputState;
using remote60::native_poc::interactive_desktop_is_default_uncached;
using remote60::native_poc::interactive_desktop_is_default;
using remote60::native_poc::InputInjectResult;
using remote60::native_poc::InputFailStage;
using remote60::native_poc::input_fail_stage_name;
using remote60::native_poc::inject_background_input_event;
using remote60::native_poc::apply_input_text_message;
// State structs extracted to their own headers (Phase 2-0); each becomes a class in Phase 2.
using remote60::native_poc::FrameGatingState;
using remote60::native_poc::RateControlState;
using remote60::native_poc::KickState;
using remote60::native_poc::ClientMetricsSnapshot;
using remote60::native_poc::DesktopBackendState;
using remote60::native_poc::MainLoopPhase;
using remote60::native_poc::kExitMainLoopWatchdog;
using remote60::native_poc::kExitDxgiWorkerWedge;
using remote60::native_poc::WatchdogState;
using remote60::native_poc::InputRouterState;
using remote60::native_poc::EncodedSendItem;
using remote60::native_poc::SenderState;
using remote60::native_poc::SessionState;
using remote60::native_poc::Nv12PendingRelease;
using remote60::native_poc::EncoderState;
using remote60::native_poc::HostStats;
using remote60::native_poc::BootstrapFrameCache;
using remote60::native_poc::CaptureState;
using remote60::native_poc::CaptureResources;
using remote60::native_poc::kQueueWaitTimeoutUsDefault;
using remote60::native_poc::kQueueWaitTimeoutUsMin;
using remote60::native_poc::WindowSelectionTxn;
using remote60::native_poc::ControlSessionServer;
// Tuning constants, loop context and stages extracted to host_main_loop.hpp/.cpp (Phase 3).
using remote60::native_poc::kInputPolicyForceBlock;
using remote60::native_poc::kMaxEncodedFrameAgeUs;
using remote60::native_poc::kMaxConsecutiveStaleEncodedFrames;
using remote60::native_poc::kCaptureFramePoolBuffersDefault;
using remote60::native_poc::kMaxPreEncodeFrameAgeUs;
using remote60::native_poc::kHostUserFeedbackWarnUs;
using remote60::native_poc::kHostUserFeedbackMinIntervalUs;
using remote60::native_poc::kCaptureStallKeepaliveIntervalUs;
using remote60::native_poc::kCaptureCallbackStallRestartUs;
using remote60::native_poc::kCaptureCallbackRestartCooldownUs;
using remote60::native_poc::kCaptureFrozenWarnUs;
using remote60::native_poc::kCaptureFrozenRestartUs;
using remote60::native_poc::kCaptureFrozenPollStreakMin;
using remote60::native_poc::kCaptureFrozenEscalationWindowUs;
using remote60::native_poc::kReadbackDrainWarmupUs;
using remote60::native_poc::kReadbackDrainConsecutiveSecMin;
using remote60::native_poc::kReadbackDrainPendingAgeUs;
using remote60::native_poc::kReadbackDrainDropBurstMin;
using remote60::native_poc::kCaptureInputMinPushPerSecDefault;
using remote60::native_poc::kCaptureInputStallConsecutiveSecDefault;
using remote60::native_poc::kCaptureInputStallWarmupSecDefault;
using remote60::native_poc::kFrameGatingStaticFpsDefault;
using remote60::native_poc::kFrameGatingStaticThresholdPermilleDefault;
using remote60::native_poc::kFrameGatingEnterFramesDefault;
using remote60::native_poc::kFrameGatingExitFramesDefault;
using remote60::native_poc::kFrameGatingSampleTargetDefault;
using remote60::native_poc::kKeyReqMinIntervalUsDefault;
using remote60::native_poc::kKeyReqTokenRefillUsDefault;
using remote60::native_poc::kKeyReqTokenCapacityDefault;
using remote60::native_poc::kDesktopBackendRetryMinUs;
using remote60::native_poc::kDesktopBackendRetryMaxUs;
using remote60::native_poc::kDesktopDefaultStableUs;
using remote60::native_poc::kDesktopDefaultProbeIntervalUs;
using remote60::native_poc::kEncodeRefitSettleUs;
using remote60::native_poc::kWgcContentSettleUs;
using remote60::native_poc::kCaptureIdleDetachDelayUs;
using remote60::native_poc::kCaptureReattachRetryMinUs;
using remote60::native_poc::kCaptureReattachRetryMaxUs;
using remote60::native_poc::Flow;
using remote60::native_poc::HostContext;
using remote60::native_poc::TickContext;
using remote60::native_poc::restart_capture_session;
using remote60::native_poc::DxgiWatchdogJoiner;
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
using remote60::native_poc::update_u64_max;
using remote60::native_poc::ControlWindowListMessage;
using remote60::native_poc::ControlWindowListRequestMessage;
using remote60::native_poc::ControlWindowSelectMessage;
using remote60::native_poc::ControlWindowSelectedMessage;
using remote60::native_poc::ControlWindowThumbnailHeader;
using remote60::native_poc::ControlWindowThumbnailRequestMessage;
using remote60::native_poc::ControlPingMessage;
using remote60::native_poc::ControlPongMessage;
using remote60::native_poc::H264EncodeFrameStats;
using remote60::native_poc::EncodedFrameHeader;
using remote60::native_poc::H264AccessUnit;
using remote60::native_poc::H264Encoder;
using remote60::native_poc::GdiCaptureProcess;
using remote60::native_poc::GdiCaptureProcessConfig;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::RawFrameHeader;
using remote60::native_poc::UdpCodec;
using remote60::native_poc::UdpHelloPacket;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;
using remote60::native_poc::SecureInputBrokerClient;
using remote60::native_poc::VideoTransport;
using remote60::native_poc::bgra_to_nv12;
using remote60::native_poc::clamp_udp_mtu;
using remote60::native_poc::parse_video_transport;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::video_transport_name;
using remote60::host::DesktopCaptureBackend;
using remote60::host::DxgiDesktopCaptureConfig;
using remote60::host::DxgiDesktopCaptureSession;
namespace json_profile = remote60::native_poc::json_profile;

#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT
#define REMOTE60_NATIVE_ENCODED_EXPERIMENT 0
#endif

// wake_display_for_remote_session / HostPowerKeepalive moved to host_log.hpp (host split refactor
// Phase 0-8). Brought back into unqualified scope by the using-declarations near the top of the
// anonymous namespace.

// HostBottleneckStage / D3DReadbackTiming / update_host_bottleneck_stage / detect_host_bottleneck_stage /
// encoder_api_path_code moved to host_bottleneck.hpp (host split refactor Phase 0-10). Brought back
// into unqualified scope by the using-declarations near the top of the anonymous namespace.

// String utilities (trim_ascii/ascii_lower/utf8<->wide/wide_lower/hr_hex/parse_csv_lower/
// base_name_lower) moved to host_string_util.hpp (host split refactor Phase 0). Brought back into
// unqualified scope by the using-declarations near the top of the anonymous namespace.

// hwnd_to_id / window_id_to_hwnd / WindowListEntry / MonitorListEntry / should_*_window /
// window_content_extent / enumerate_monitors / enumerate_shareable_windows / find_window_by_id moved
// to host_window_enum.hpp/.cpp (host split refactor Phase 0-4). Brought back into unqualified scope
// by the using-declarations near the top of the anonymous namespace.

// backend_request_is_any/satisfied/is_vendor_specific / backend_fallback_reason moved to
// host_capture_device.hpp/.cpp (host split refactor Phase 0-6).

// get_window_process_name / get_window_class_name / get_window_title / describe_input_target /
// CaptureWindowCriteria / CaptureWindowInfo / match_capture_window / find_capture_window* /
// find_top_level_window_at_point moved to host_window_enum.hpp/.cpp (host split refactor Phase 0-4).

// InputInjectionMode / DesktopInputState / interactive_desktop_is_default(_uncached) / InputInjectResult /
// InputFailStage / inject_background_input_event / apply_input_text_message (and their private Win32
// helpers) moved to host_input_inject.hpp/.cpp (host split refactor Phase 0-1). Brought back into
// unqualified scope by the using-declarations near the top of the anonymous namespace.

// compute_window_client_crop / CreateItemForPrimaryMonitor / SurfaceToTexture moved to
// host_capture_device.hpp/.cpp (host split refactor Phase 0-6).

// resolve_bind_address moved to host_net_io.hpp/.cpp (host split refactor Phase 0-5). WinsockScope was a
// byte-identical private copy of native_socket.hpp's; the using-declaration now names the shared one.

// struct Args + parse_u32/env_truthy/env_u32_clamped moved to host_args.hpp (host split refactor
// Phase 0-7a). Brought back into unqualified scope by the using-declarations near the top of the
// anonymous namespace. parse_args followed in Phase 0-7b (host_args.cpp).

// desktop_capture_backend_from_env/from_code/code/name / PrimaryMonitorInfo / primary_monitor_info /
// create_d3d11_device_for_primary_monitor moved to host_capture_device.hpp/.cpp (host split refactor
// Phase 0-6). Brought back into unqualified scope by the using-declarations near the top of the
// anonymous namespace.

// parse_args moved to host_args.hpp/.cpp (host split refactor Phase 0-7b); see the using-declaration
// near the top of the anonymous namespace. The REMOTE60_NATIVE_* env prelude at the top of main()
// stays here until Phase 1 folds it into the state structs.

// clamp_even_dim / fit_size_preserving_aspect / choose_h264_encode_size / choose_abr_720_size /
// estimate_bgra_change_permille / box_halve_bgra / resize_bgra_bilinear / capture_window_thumbnail
// moved to host_bgra_scale.hpp/.cpp (host split refactor Phase 0-2). Brought back into unqualified
// scope by the using-declarations near the top of the anonymous namespace.

// struct GpuBgraScaler moved to host_gpu_scaler.hpp (host split refactor Phase 0-3); see the
// using-declaration near the top of the anonymous namespace.

// send_all_timed / SendPathStats / kUdpReceiveBufferBytes / gUdpPace* / (send_all / recv_all / recv_discard
// were byte-identical private copies of native_socket.hpp's and now resolve to the shared ones) /
// udp_pace_* / UdpSendOutcome / send_udp_chunks(_impl/_timed) moved to host_net_io.hpp/.cpp (host
// split refactor Phase 0-5). Brought back into unqualified scope by the using-declarations near the
// top of the anonymous namespace.

// FrameState moved to host_frame_state.hpp (host split refactor Phase 0-10); see the
// using-declaration near the top of the anonymous namespace.

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
  WatchdogState watchdog;
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
  ControlSessionServer controlServer(args, stop, clientSession, capture, clientMetrics, encoder,
                                     inputRouter, backend, windowSelectionTxn);
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
                   encoder, stats, capture, res};

  // Startup, in the monolith's order (host_startup.hpp). Each step is one former block of main();
  // the ones that can fail return the exit code main() used to return at that point.
  if (const int rc = startup_configure_from_env(host)) return rc;
  if (!ws.ok) {
    std::cerr << "[native-video-host] WSAStartup failed\n";
    return 1;
  }
  if (const int rc = resolve_transport(args, useRaw, useH264, transport)) return rc;
  startup_log_config(host);
  startup_configure_session(host);
  if (const int rc = startup_connect_client(host)) return rc;
  startup_configure_control_state(host);
  startup_start_control_threads(host, controlServer);
  if (const int rc = startup_init_graphics(host)) return rc;
  if (const int rc = startup_select_capture_target(host)) return rc;
  startup_configure_encode_geometry(host);
  if (const int rc = startup_init_encoder(host)) return rc;
  startup_start_dxgi_watchdog(host, dxgiWatchdogStop, dxgiWorkerWatchdog);
  if (const int rc = startup_create_readback(host)) return rc;
  if (const int rc = startup_start_capture(host)) return rc;
  startup_start_main_loop_watchdog(host);

  // One tick = the twelve stages of host_main_loop.cpp, in order. A stage that used to
  // `continue`/`break`/`return` from the loop body reports it through Flow.
  while (!stop.load()) {
    TickContext tc;
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
    RUN_STAGE(stage_stats);
  }

  shutdown_host(host);
  std::cout << "[native-video-host] done\n";
  return 0;
}
