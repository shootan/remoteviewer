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
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);
  // Prefix every host log line with a wall-clock timestamp so it aligns with the client log.
  // Static so the filtering buffers outlive every logging thread for the life of the process.
  static TimestampPrefixBuf coutTsBuf(std::cout.rdbuf());
  std::cout.rdbuf(&coutTsBuf);
  static TimestampPrefixBuf cerrTsBuf(std::cerr.rdbuf());
  std::cerr.rdbuf(&cerrTsBuf);

  // The host normally runs behind a tray app with no foreground boost. Keep capture,
  // conversion, and encode deadlines above ordinary UI/background work; opt out for A/B or
  // constrained systems with REMOTE60_NATIVE_NORMAL_PRIORITY=1.
  if (!env_truthy("REMOTE60_NATIVE_NORMAL_PRIORITY")) {
    const BOOL processPriorityOk =
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    const BOOL threadPriorityOk =
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    std::cout << "[native-video-host] latency-priority processAboveNormal="
              << (processPriorityOk ? 1 : 0)
              << " mainThreadAboveNormal=" << (threadPriorityOk ? 1 : 0) << "\n";
  }

  const Args args = parse_args(argc, argv);
  // Viewer input routing: injection mode, SYSTEM broker, counters, cursor forwarder (InputRouterState, Phase 1-9).
  InputRouterState inputRouter;
  inputRouter.injectionMode = parse_input_injection_mode(args.inputInjectionMode);
  inputRouter.injectionEnabled =
      args.enableInputInjection &&
      (inputRouter.injectionMode == InputInjectionMode::BackgroundMessage) &&
      !kInputPolicyForceBlock;
  const bool useRaw = (args.codec == "raw");
  const bool useH264 = (args.codec == "h264");
  const bool guardStaleEncoded = env_truthy("REMOTE60_NATIVE_GUARD_STALE_ENCODED");
  // Encoded-frame sender queue/thread, UDP peer, media barrier, wire counters (SenderState, Phase 1-2).
  SenderState sender;
  sender.noPacingH264 = env_truthy("REMOTE60_NATIVE_H264_NO_PACING");
  // Spread each frame's datagrams over the wire instead of bursting them. Expressed as a
  // percentage of the average bitrate: 500 means a frame may leave at up to 5x the
  // average rate. 0 restores the old unthrottled burst.
  sender.udpPacePeakPercent =
      env_u32_clamped("REMOTE60_NATIVE_UDP_PACE_PEAK_PERCENT", 500, 0, 2000);
  // A percentage alone is too slow at low user bitrates: 4 Mbps * 5 can take longer than
  // one 30 fps period to deliver a normal motion frame plus FEC. Keep packets paced, but
  // finish ordinary frames within the frame budget.
  sender.udpPacePeakFloorBps = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_PACE_PEAK_FLOOR_BPS", 40000000, 0, 1000000000);
  sender.udpKeyframePacePeakBps = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_KEYFRAME_PACE_PEAK_BPS", 100000000, 0, 1000000000);
  // Holding an encoded frame back to enforce even send spacing costs exactly what it holds:
  // measured end-to-end latency p95 went 4ms -> 31ms at 30fps when this was enabled
  // unconditionally, and rose further when the hold also pushed the next frame's deadline.
  // The H4 sender queue is already capped at two frames with keyframe supersede, so a
  // catch-up burst can only ever be a couple of frames; smoothing it is not worth a frame
  // period of latency. Off by default; the cap below re-enables bounded smoothing.
  // Encoded-frame sender queue/thread, UDP peer, media barrier, wire counters (SenderState, Phase 1-2).
  sender.maxCadenceHoldUs =
      env_u32_clamped("REMOTE60_NATIVE_SENDER_MAX_CADENCE_HOLD_US", 0, 0, 33000);
  sender.cadenceSmoothing = sender.maxCadenceHoldUs > 0;
  // The capture-submit limiter keeps 60Hz callbacks from flooding a 30fps encode, but it
  // rejects rather than defers, and a rejected desktop-duplication frame is lost for good.
  // Widening the early tolerance lets a slightly-early callback through instead of leaving a
  // double-length gap on screen; the disable switch exists to measure the limiter's cost.
  // Capture target/selection requests, geometry, backend flags, WGC settle gate, DXGI cursor,
  // publish timestamps, bootstrap cache, idle/reattach backoff (CaptureState, Phase 1-3a).
  CaptureState capture;
  capture.submitLimitEnabled =
      !env_truthy("REMOTE60_NATIVE_CAPTURE_SUBMIT_LIMIT_DISABLE");
  capture.submitEarlyTolerancePercent = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_SUBMIT_EARLY_TOLERANCE_PCT", 25, 0, 90);
  const bool guardStalePreEncode = env_truthy("REMOTE60_NATIVE_GUARD_STALE_PREENCODE");
  // ABR profile ladder + M9 level ladder config and runtime state (RateControlState, Phase 1-6).
  RateControlState rate;
  rate.abrEnabled = useH264 && !env_truthy("REMOTE60_NATIVE_ABR_DISABLE");
  rate.abrQualityFirst = env_truthy("REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST");
  rate.m9Enabled = useH264 && env_truthy("REMOTE60_NATIVE_M9_ENABLE");
  rate.m9Apply = rate.m9Enabled && env_truthy("REMOTE60_NATIVE_M9_APPLY");
  rate.m9CooldownSec = env_u32_clamped("REMOTE60_NATIVE_M9_COOLDOWN_SEC", 4, 1, 60);
  rate.m9DownRequireSec = env_u32_clamped("REMOTE60_NATIVE_M9_DOWN_REQUIRE_SEC", 2, 1, 20);
  rate.m9UpRequireSec = env_u32_clamped("REMOTE60_NATIVE_M9_UP_REQUIRE_SEC", 8, 1, 60);
  rate.m9DecodedFpsFloorX100 = env_u32_clamped("REMOTE60_NATIVE_M9_DECODED_FPS_FLOOR_X100", 2000, 500, 12000);
  rate.m9DecodedFpsRecoverX100 = env_u32_clamped(
      "REMOTE60_NATIVE_M9_DECODED_FPS_RECOVER_X100", 2500, 500, 12000);
  rate.m9QueueDepthHighFrames = env_u32_clamped("REMOTE60_NATIVE_M9_QUEUE_DEPTH_HIGH_FRAMES", 4, 1, 120);
  rate.m9QueueDepthLowFrames = env_u32_clamped("REMOTE60_NATIVE_M9_QUEUE_DEPTH_LOW_FRAMES", 1, 0, 120);
  rate.m9UdpDropPmHigh = env_u32_clamped("REMOTE60_NATIVE_M9_UDP_DROP_PM_HIGH", 120, 1, 1000);
  rate.m9UdpDropPmLow = env_u32_clamped("REMOTE60_NATIVE_M9_UDP_DROP_PM_LOW", 30, 0, 1000);
  rate.m9LatencyHighUs = env_u32_clamped("REMOTE60_NATIVE_M9_LATENCY_HIGH_US", 140000, 10000, 1000000);
  rate.m9LatencyLowUs = env_u32_clamped("REMOTE60_NATIVE_M9_LATENCY_LOW_US", 90000, 10000, 1000000);
  rate.m9TailHighUs = env_u32_clamped("REMOTE60_NATIVE_M9_TAIL_HIGH_US", 110000, 10000, 1000000);
  rate.m9TailLowUs = env_u32_clamped("REMOTE60_NATIVE_M9_TAIL_LOW_US", 70000, 10000, 1000000);
  // Static-frame gating config + loop state (FrameGatingState, Phase 1-7).
  FrameGatingState frameGating;
  frameGating.enabled = useH264 && !env_truthy("REMOTE60_NATIVE_FRAME_GATING_DISABLE");
  frameGating.staticFps = env_u32_clamped(
      "REMOTE60_NATIVE_STATIC_SCENE_FPS", kFrameGatingStaticFpsDefault, 1, 30);
  frameGating.staticThresholdPermille = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_STATIC_THRESHOLD_PM",
      kFrameGatingStaticThresholdPermilleDefault, 1, 400);
  frameGating.enterFrames = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_ENTER_FRAMES", kFrameGatingEnterFramesDefault, 1, 120);
  frameGating.exitFrames = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_EXIT_FRAMES", kFrameGatingExitFramesDefault, 1, 30);
  frameGating.sampleTarget = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_SAMPLE_TARGET", kFrameGatingSampleTargetDefault, 128, 16384);
  // MFT wrapper, active encode geometry/rate, tune requests, key-request bucket, force-key latch,
  // NV12 surface bookkeeping, starvation heartbeat, encode counters (EncoderState, Phase 1-5).
  EncoderState encoder;
  encoder.keyReqMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_MIN_INTERVAL_US", kKeyReqMinIntervalUsDefault, 10000, 1000000);
  encoder.keyReqTokenRefillUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_TOKEN_REFILL_US", kKeyReqTokenRefillUsDefault, 10000, 2000000);
  encoder.keyReqTokenCapacity = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_TOKEN_CAPACITY", kKeyReqTokenCapacityDefault, 1, 16);
  // Capture liveness watchdogs + main-loop liveness stamps (WatchdogState, Phase 1-11).
  WatchdogState watchdog;
  // Trailing-edge kick / static refresh / selection-first-keyframe state (KickState, Phase 1-8).
  KickState kick;
  watchdog.inputMinPushPerSec = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_INPUT_MIN_PUSH_PER_SEC", kCaptureInputMinPushPerSecDefault, 1, 120);
  watchdog.inputStallConsecutiveSec = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_INPUT_STALL_SEC", kCaptureInputStallConsecutiveSecDefault, 1, 30);
  watchdog.inputStallWarmupSec = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_INPUT_STALL_WARMUP_SEC", kCaptureInputStallWarmupSecDefault, 0, 60);
  capture.stallKeepaliveIntervalUsOverride = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_STALL_KEEPALIVE_INTERVAL_US", 0, 0,
      static_cast<uint32_t>(kCaptureStallKeepaliveIntervalUs));
  capture.queueWaitTimeoutUsOverride = env_u32_clamped(
      "REMOTE60_NATIVE_QUEUE_WAIT_TIMEOUT_US", 0, 0,
      static_cast<uint32_t>(kQueueWaitTimeoutUsDefault));
  capture.gpuScalerRequested = useH264 && !env_truthy("REMOTE60_NATIVE_DISABLE_GPU_SCALER");
  capture.framePoolBuffers = kCaptureFramePoolBuffersDefault;
  if (const char* poolEnv = std::getenv("REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS")) {
    const int requested = std::atoi(poolEnv);
    if (requested >= 1 && requested <= 4) {
      capture.framePoolBuffers = requested;
    }
  }
  encoder.experimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  encoder.tuneMode = [&]() {
    const char* raw = std::getenv("REMOTE60_NATIVE_ENCODER_TUNE_MODE");
    if (!raw || !*raw) return std::string("low_latency");
    return ascii_lower(trim_ascii(std::string(raw)));
  }();

  if (!useRaw && !useH264) {
    std::cerr << "[native-video-host] unsupported codec: " << args.codec << " (supported: raw,h264)\n";
    return 11;
  }
  if (useH264 && !encoder.experimentEnabled) {
    std::cerr << "[native-video-host] unsupported codec: " << args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 11;
  }

  WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-host] WSAStartup failed\n";
    return 1;
  }
  std::string effectiveTransport = args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = useH264 ? "udp" : "tcp";
  }
  VideoTransport transport = VideoTransport::Tcp;
  if (!parse_video_transport(effectiveTransport, &transport)) {
    std::cerr << "[native-video-host] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 15;
  }
  if (transport == VideoTransport::Udp && useRaw) {
    std::cerr << "[native-video-host] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 16;
  }

  // Print every candidate, not just the first: with a fallback list the port this line names is
  // a request, and "udp bound port=" below is what actually happened.
  std::cout << "[native-video-host] waiting client bindPort=";
  if (args.bindPortCandidates.size() > 1) {
    for (size_t i = 0; i < args.bindPortCandidates.size(); ++i) {
      if (i) std::cout << ",";
      std::cout << args.bindPortCandidates[i];
    }
  } else {
    std::cout << args.bindPort;
  }
  std::cout << " transport=" << video_transport_name(transport)
            << " fps=" << args.fps;
  if (useH264) std::cout << " bitrate=" << args.bitrate;
  std::cout << " seconds=" << args.seconds << "\n";
  if (useH264) {
    const uint64_t pacePeakBps = sender.noPacingH264
                                     ? 0ULL
                                     : std::max<uint64_t>(
                                           sender.udpPacePeakFloorBps,
                                           (static_cast<uint64_t>(args.bitrate) *
                                            sender.udpPacePeakPercent) /
                                               100ULL);
    gUdpPacePeakBitrateBps.store(
        static_cast<uint32_t>(std::min<uint64_t>(pacePeakBps, 4000000000ULL)),
        std::memory_order_relaxed);
    gUdpKeyframePacePeakBitrateBps.store(sender.udpKeyframePacePeakBps,
                                        std::memory_order_relaxed);
    std::cout << "[native-video-host] h264 pacing=" << (sender.noPacingH264 ? "off" : "on")
              << " udpPacePeakPercent=" << sender.udpPacePeakPercent
              << " udpPacePeakBps=" << gUdpPacePeakBitrateBps.load(std::memory_order_relaxed)
              << " udpPacePeakFloorBps=" << sender.udpPacePeakFloorBps
              << " udpKeyframePacePeakBps="
              << gUdpKeyframePacePeakBitrateBps.load(std::memory_order_relaxed)
              << " stalePreEncodeGuard=" << (guardStalePreEncode ? 1 : 0)
              << " capturePoolBuffers=" << capture.framePoolBuffers
              << " encoderTuneMode=" << encoder.tuneMode
              << " abr=" << (rate.abrEnabled ? "on" : "off")
              << " abrMode=" << (rate.abrQualityFirst ? "quality-first" : "default")
              << " frameGating=" << (frameGating.enabled ? "on" : "off")
              << " staticSceneFps=" << frameGating.staticFps
              << " gatingStaticPm=" << frameGating.staticThresholdPermille
              << " m9=" << (rate.m9Enabled ? "on" : "off")
              << " m9Mode=" << (rate.m9Apply ? "apply" : "dry-run")
              << " keyReqMinUs=" << encoder.keyReqMinIntervalUs
              << " keyReqBucketCap=" << encoder.keyReqTokenCapacity
              << " captureInputMinPushPerSec=" << watchdog.inputMinPushPerSec
              << " captureInputStallSec=" << watchdog.inputStallConsecutiveSec
              << " captureInputWarmupSec=" << watchdog.inputStallWarmupSec
              << " captureIdlePollIntervalUs="
              << (capture.stallKeepaliveIntervalUsOverride > 0
                      ? static_cast<uint64_t>(capture.stallKeepaliveIntervalUsOverride)
                      : std::max<uint64_t>(kQueueWaitTimeoutUsMin, (1000000ULL / std::max<uint64_t>(1, args.fps))))
              << " queueWaitTimeoutUs="
              << (capture.queueWaitTimeoutUsOverride > 0 ? static_cast<uint64_t>(capture.queueWaitTimeoutUsOverride)
                                                : std::max<uint64_t>(kQueueWaitTimeoutUsMin,
                                                                     (1000000ULL / std::max<uint64_t>(1, args.fps)) /
                                                                         4ULL))
              << "\n";
  }
  if (kInputPolicyForceBlock) {
    std::cout << "[native-video-host] input injection blocked by compile-time policy\n";
  } else if (!args.enableInputInjection) {
    std::cout << "[native-video-host] input injection disabled (enableInputInjection=false)\n";
  } else if (!inputRouter.injectionEnabled) {
    std::cout << "[native-video-host] input injection disabled (unsupported mode) mode="
              << args.inputInjectionMode << "\n";
  } else {
    std::cout << "[native-video-host] input injection enabled mode="
              << input_injection_mode_name(inputRouter.injectionMode)
              << " targetPid=" << args.inputTargetPid
              << " targetProcess=" << trim_ascii(args.inputTargetProcess)
              << " targetTitle=" << trim_ascii(args.inputTargetTitle)
              << "\n";
  }

  // Credentials may come from the command line or the environment. The environment is the
  // better place for the password: a command line is readable by any process on the machine.
  auto arg_or_env = [](const std::string& fromArgs, const char* envKey) -> std::string {
    if (!fromArgs.empty()) return fromArgs;
    const char* v = std::getenv(envKey);
    return v ? std::string(v) : std::string();
  };
  // Client session sockets, directory agent/auth, session epoch, control threads (SessionState, Phase 1-1).
  SessionState clientSession;
  clientSession.directoryUrl = arg_or_env(args.directoryUrl, "REMOTE60_DIRECTORY_URL");
  clientSession.directoryId = arg_or_env(args.directoryId, "REMOTE60_DIRECTORY_ID");
  clientSession.directoryPw = arg_or_env(args.directoryPw, "REMOTE60_DIRECTORY_PW");
  if (!clientSession.directoryUrl.empty() && transport != VideoTransport::Udp) {
    std::cerr << "[native-video-host] directory requires transport=udp; ignoring directory url\n";
  }

  // A second listener on the legacy port for clients that dial this PC by address; see the bind
  // site. `clientSession.retiredSock` holds whichever socket the handshake did not choose but that still has an
  // owner -- the directory agent captured the primary socket and keeps heartbeating on it even
  // when a LAN client wins the handshake.
  // The port the media socket actually landed on. It only differs from args.bindPort when a
  // fallback candidate was used, and the directory must publish this one rather than the request.
  clientSession.mediaBindPort = args.bindPort;
  // Which of the three things a Hello can be. The caller needs the distinction because a first
  // Hello and its retransmissions are indistinguishable at the endpoint level -- and, behind a
  // relay, so are two entirely different clients.
  enum class DirectoryHello { Rejected, Retransmit, NewSession };
  auto classify_directory_hello = [&](const std::string& token,
                                      const sockaddr_in& peer) -> DirectoryHello {
    if (token.empty()) return DirectoryHello::Rejected;
    {
      std::lock_guard<std::mutex> lock(clientSession.directoryAuthMu);
      if (!clientSession.directoryToken.empty() && token == clientSession.directoryToken &&
          peer.sin_addr.s_addr == clientSession.directoryIpNet) {
        // A controller reconnect creates a new UDP socket/port. The already-proven opaque
        // capability remains the session credential, while the first authenticated source IP
        // (which can differ from the directory-observed endpoint under hairpin NAT) stays bound.
        // This is also what makes retransmission safe: the capability itself is single-use, so
        // without the cache the client's second Hello would be refused.
        return DirectoryHello::Retransmit;
      }
    }
    if (!clientSession.directoryAgent.AuthorizePeer(token, peer)) return DirectoryHello::Rejected;
    {
      std::lock_guard<std::mutex> lock(clientSession.directoryAuthMu);
      clientSession.directoryToken = token;
      clientSession.directoryIpNet = peer.sin_addr.s_addr;
    }
    return DirectoryHello::NewSession;
  };
  auto authorize_directory_session = [&](const std::string& token,
                                         const sockaddr_in& peer) -> bool {
    return classify_directory_hello(token, peer) != DirectoryHello::Rejected;
  };


  // After a backlog drop every delta references frames that never went out; shipping them
  // paints macroblock corruption on the client until the next IDR. They are held back here
  // until the requested keyframe actually passes through.
  // Session media barrier. Bumped (under sender.mu) by the rollover transaction in pump_udp_hello;
  // read by the sender at dequeue to fence any item stamped for a previous session. Every item is
  // stamped with this value when enqueued. Starts at 1 to match clientSession.epoch.
  // Set by the sender thread when a same-epoch transport error re-armed the barrier. The main loop
  // consumes it at its top -> encoder.forceKeyNext + arm_trailing_kick, because sender.requestKey is only
  // consumed after a real frame is popped: on a static desktop no new frame arrives to carry it, so
  // the recovery IDR would never be produced. This is the only barrier-recovery signal that works
  // when the screen is not changing. encoder.forceKeyNext must never be written from the sender thread.
  // IDR telemetry written by the sender thread (per current media epoch): when the first key AU of
  // this session hit the wire, and the size/chunk count of the last key AU sent. Reset by the
  // rollover transaction so they describe the current session, not the previous one. Diagnostic
  // only -- never wired into ABR evidence.
  // The reader thread owns the peer address; the render loop picks up changes through these.
  if (transport == VideoTransport::Tcp) {
    clientSession.listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSession.listenSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] listen socket create failed\n";
      return 2;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(args.bindPort);
    local.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
    if (bind(clientSession.listenSock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
      std::cerr << "[native-video-host] bind failed port=" << args.bindPort << "\n";
      closesocket(clientSession.listenSock);
      return 3;
    }
    if (listen(clientSession.listenSock, 1) != 0) {
      std::cerr << "[native-video-host] listen failed\n";
      closesocket(clientSession.listenSock);
      return 4;
    }

    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    clientSession.clientSock = accept(clientSession.listenSock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    if (clientSession.clientSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] accept failed\n";
      closesocket(clientSession.listenSock);
      clientSession.listenSock = INVALID_SOCKET;
      return 5;
    }

    int noDelay = 1;
    setsockopt(clientSession.clientSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  } else {
    clientSession.clientSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSession.clientSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] udp socket create failed\n";
      return 2;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
    // Walk the candidates in order and keep the first that binds. A failed bind leaves the
    // socket unbound, so the next attempt can reuse it.
    std::vector<uint16_t> portCandidates = args.bindPortCandidates;
    if (portCandidates.empty()) portCandidates.push_back(args.bindPort);
    bool udpBound = false;
    for (const uint16_t candidate : portCandidates) {
      local.sin_port = htons(candidate);
      if (bind(clientSession.clientSock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0) {
        clientSession.mediaBindPort = candidate;
        udpBound = true;
        break;
      }
      std::cerr << "[native-video-host] udp bind failed port=" << candidate << "; trying next\n";
    }
    if (!udpBound) {
      std::cerr << "[native-video-host] udp bind failed on every candidate port\n";
      closesocket(clientSession.clientSock);
      return 3;
    }
    std::cout << "[native-video-host] udp bound port=" << clientSession.mediaBindPort << "\n";

    // Keep the last candidate listening as well, so dialling this PC by address still works.
    //
    // The candidate list exists to move the host onto a port restrictive networks allow, and
    // moving it is exactly what breaks the other way in: both clients default to the legacy port
    // when someone types an address by hand. The directory path is unaffected -- it dials
    // hostPublicUdpPort, which follows whatever the primary socket was given -- but a LAN user
    // has nothing telling them the port changed.
    //
    // Only the handshake watches both. Whichever socket the Hello arrives on becomes the media
    // socket and everything downstream is unchanged, so a session that never uses this listener
    // behaves exactly as it did before.
    const uint16_t lanPort =
        portCandidates.size() > 1 ? portCandidates.back() : 0;
    if (lanPort != 0 && lanPort != clientSession.mediaBindPort) {
      clientSession.lanSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (clientSession.lanSock != INVALID_SOCKET) {
        sockaddr_in lanAddr{};
        lanAddr.sin_family = AF_INET;
        lanAddr.sin_port = htons(lanPort);
        lanAddr.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
        if (bind(clientSession.lanSock, reinterpret_cast<const sockaddr*>(&lanAddr), sizeof(lanAddr)) == 0) {
          std::cout << "[native-video-host] lan direct-dial listener port=" << lanPort << "\n";
        } else {
          // Not fatal: the primary socket is the one that matters, and the usual reason this
          // fails is another GNLink host already holding the legacy port.
          std::cout << "[native-video-host] lan direct-dial listener unavailable port=" << lanPort
                    << "\n";
          closesocket(clientSession.lanSock);
          clientSession.lanSock = INVALID_SOCKET;
        }
      }
    }

    // The directory agent shares this socket on purpose: the public address it publishes has
    // to be the one NAT maps for the media stream, and that is a property of this socket.
    if (!clientSession.directoryUrl.empty()) {
      remote60::native_poc::directory::HostAgentConfig dirCfg;
      dirCfg.url = clientSession.directoryUrl;
      dirCfg.accountId = clientSession.directoryId;
      dirCfg.password = clientSession.directoryPw;
      dirCfg.hostName = args.directoryHostName;
      dirCfg.observeUdpPort = args.directoryObservePort;
      dirCfg.localUdpPort = clientSession.mediaBindPort;
      // The legacy/alternate listener from N6. Publishing it is what lets a client whose network
      // filters the primary port have something else to dial.
      dirCfg.alternateUdpPort = lanPort;
      dirCfg.heartbeatSeconds = env_u32_clamped("REMOTE60_DIRECTORY_HEARTBEAT_SEC", 25, 5, 300);
      std::string dirError;
      const bool started = clientSession.directoryAgent.Start(
          dirCfg,
          [clientSock = clientSession.clientSock](const void* data, size_t len, const sockaddr_in& to) {
            (void)sendto(clientSock, static_cast<const char*>(data), static_cast<int>(len), 0,
                         reinterpret_cast<const sockaddr*>(&to), sizeof(to));
          },
          &dirError);
      if (!started) {
        // Not fatal: direct LAN connections still work, so say why and carry on.
        std::cerr << "[native-video-host] directory disabled: " << dirError << "\n";
      } else {
        std::cout << "[native-video-host] directory agent started url=" << clientSession.directoryUrl << "\n";
      }
    }

    for (;;) {
      // Wait on the primary and, when present, the legacy direct-dial listener. Reading only the
      // primary would leave a LAN client's Hello sitting unanswered forever.
      SOCKET readySock = clientSession.clientSock;
      if (clientSession.lanSock != INVALID_SOCKET) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(clientSession.clientSock, &readSet);
        FD_SET(clientSession.lanSock, &readSet);
        timeval wait{};
        wait.tv_sec = 1;
        const int ready = select(0, &readSet, nullptr, nullptr, &wait);
        if (ready == 0) continue;
        if (ready == SOCKET_ERROR) {
          std::cerr << "[native-video-host] udp handshake select failed err=" << WSAGetLastError()
                    << "\n";
          closesocket(clientSession.clientSock);
          return 5;
        }
        // The primary wins a tie: it is the one the directory published.
        readySock = FD_ISSET(clientSession.clientSock, &readSet) ? clientSession.clientSock : clientSession.lanSock;
      }

      // Big enough for the directory's observation reply; a datagram larger than the buffer
      // would be dropped with WSAEMSGSIZE and taken for a handshake failure.
      uint8_t rx[kUdpReceiveBufferBytes];
      sockaddr_in peer{};
      int peerLen = sizeof(peer);
      const int n = recvfrom(readySock, reinterpret_cast<char*>(rx), sizeof(rx), 0,
                             reinterpret_cast<sockaddr*>(&peer), &peerLen);
      // Zero-length datagrams are legal (NAT keepalives, scanners) and must not end the
      // process while it waits for a real client.
      if (n == 0) continue;
      if (n < 0) {
        const int err = WSAGetLastError();
        if (err == WSAEMSGSIZE || err == WSAECONNRESET) continue;
        std::cerr << "[native-video-host] udp handshake recv failed err=" << err << "\n";
        closesocket(clientSession.clientSock);
        return 5;
      }
      UdpHelloPacket hello{};
      bool isHello = n >= static_cast<int>(sizeof(UdpHelloPacket));
      if (isHello) {
        std::memcpy(&hello, rx, sizeof(hello));
        isHello = hello.magic == remote60::native_poc::kMagic &&
                  hello.kind == static_cast<uint16_t>(UdpPacketKind::Hello) &&
                  hello.version == remote60::native_poc::kUdpProtocolVersion &&
                  (hello.features & remote60::native_poc::kUdpFeatureVideoFec) != 0;
      }
      if (!isHello) {
        // Only the primary socket carries directory traffic; the legacy listener never had a
        // punch or an observation sent to it, and feeding it in would let unrelated LAN noise
        // interrupt the heartbeat.
        if (readySock == clientSession.clientSock) {
          (void)clientSession.directoryAgent.ConsumeUdpPacket(rx, static_cast<size_t>(n), peer);
        }
        continue;
      }

      gUdpVideoFecInterleaved.store(
          (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved) != 0,
          std::memory_order_relaxed);

      UdpHelloPacket ack{};
      ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
      ack.features = remote60::native_poc::kUdpFeatureVideoFec |
                     (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved);
      size_t tokenLen = 0;
      while (tokenLen < sizeof(hello.authToken) && hello.authToken[tokenLen] != '\0') ++tokenLen;
      if (tokenLen > 0) {
        const std::string authToken(hello.authToken, hello.authToken + tokenLen);
        if (!authorize_directory_session(authToken, peer)) {
          std::cerr << "[native-video-host] rejected udp hello with invalid directory capability\n";
          continue;
        }
        clientSession.directoryAuthenticated.store(true, std::memory_order_release);
        ack.features |= remote60::native_poc::kUdpFeatureDirectoryAuth;
      }
      (void)sendto(readySock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                   reinterpret_cast<const sockaddr*>(&peer), peerLen);

      // The socket that answered becomes the media socket for the rest of the session, so
      // everything downstream keeps using clientSession.clientSock exactly as before.
      if (readySock != clientSession.clientSock) {
        std::cout << "[native-video-host] client arrived on the lan direct-dial listener; "
                     "media moves to port "
                  << lanPort << "\n";
        // The directory agent captured the primary socket and must keep heartbeating on it, so
        // it is retired rather than closed -- otherwise the host drops off the directory the
        // moment someone connects over the LAN.
        clientSession.retiredSock = clientSession.clientSock;
        clientSession.clientSock = clientSession.lanSock;
        clientSession.lanSock = INVALID_SOCKET;
      } else if (clientSession.lanSock != INVALID_SOCKET) {
        closesocket(clientSession.lanSock);
        clientSession.lanSock = INVALID_SOCKET;
      }

      sender.udpPeer = peer;
      sender.udpPeerReady = true;
      {
        std::lock_guard<std::mutex> lk(sender.mu);
        sender.peer = peer;
        sender.peerReady = true;
      }
      sender.udpPeerIpNet.store(peer.sin_addr.s_addr, std::memory_order_release);
      sender.udpPeerPortNet.store(peer.sin_port, std::memory_order_release);
      break;
    }
    // Stays blocking: a dedicated reader thread now owns receives, and control messages must
    // not wait for the next render-loop iteration. The timeout only exists so that thread can
    // notice shutdown.
    (void)remote60::native_poc::set_recv_timeout(clientSession.clientSock, 200);
  }

  if (clientSession.directoryAuthenticated.load(std::memory_order_acquire)) {
    std::string secureInputStatus;
    const std::wstring servicePath = remote60::native_poc::sibling_executable_path(
        L"GNLinkInputService.exe");
    const bool secureInputReady =
        inputRouter.broker.EnsureInstalledAndConnected(servicePath, &secureInputStatus);
    std::cout << "[native-video-host] secure-input ready=" << (secureInputReady ? 1 : 0)
              << " status=" << secureInputStatus << "\n";
  }

  if (transport == VideoTransport::Udp && args.tcpSendBufKb == 0) {
    const int sendBuf = 1024 * 1024;
    (void)setsockopt(clientSession.clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }
  if (args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
    setsockopt(clientSession.clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }
  int effectiveSendBuf = 0;
  int effectiveSendBufLen = sizeof(effectiveSendBuf);
  (void)getsockopt(clientSession.clientSock, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
  std::cout << "[native-video-host] client connected transport=" << video_transport_name(transport) << "\n";
  std::cout << "[native-video-host] socket sndbuf=" << effectiveSendBuf << " bytes\n";

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
  // Which screen desktop mode shows. Zero is the primary, which is what it always was, so a
  // client that never selects one behaves exactly as before.
  backend.reqValue = desktop_capture_backend_code(desktop_capture_backend_from_env());
  // Control-thread <-> main-loop window selection handshake (WindowSelectionTxn, host_control_session.hpp).
  WindowSelectionTxn windowSelectionTxn;
  encoder.keyReqTokens = static_cast<double>(encoder.keyReqTokenCapacity);
  inputRouter.targetCriteria.pid = args.inputTargetPid;
  for (const auto& name : parse_csv_lower(args.inputTargetProcess)) {
    inputRouter.targetCriteria.processNamesLower.insert(name);
  }
  inputRouter.targetCriteria.titleNeedleLower = wide_lower(utf8_to_wide(trim_ascii(args.inputTargetTitle)));
  // Input desktop is routed on a cached (~250ms) default/secure check; when a UAC prompt or lock
  // rises between refreshes an event lands on ordinary SendInput and fails. These split that
  // failure by its real cause instead of piling every miss into inputRouter.injectFail:
  //   inputRouter.freshProbeSecure  -- cached-default event failed, an uncached re-probe found the desktop
  //                             actually secure (the stale-cache case)
  //   inputRouter.freshProbeReroute -- of those, the ones the SYSTEM broker then landed on the retry
  //   inputRouter.injectFailDefault -- cached-default event failed AND an uncached re-probe still says
  //                             default: a genuine failure on the interactive desktop
  // Per-stage failure counters (which API the direct injection died in; see InputFailStage).
  //   inputRouter.defaultBrokerFallback -- of the genuine default-desktop failures (e.g. SetCursorPos
  //                                 denied because the control thread's desktop association is
  //                                 not the input desktop), how many were retried via the SYSTEM
  //                                 agent, which does SetThreadDesktop before SetCursorPos+SendInput
  //   inputRouter.defaultBrokerQueued   -- of those, how many the broker WROTE to the agent's pipe. This
  //                                 is a queue success, NOT proof the input landed: the agent does
  //                                 not ACK, so real delivery is confirmed only by the service log
  //                                 (%ProgramData%\GNLink\secure_input.log) and the user. Named
  //                                 "Queued" on purpose so it is never read as "Delivered".
  //   inputRouter.defaultBrokerPipeFail -- the pipe write itself failed (agent absent / broken pipe)

  // Control conversation handler (ControlSessionServer, Phase 2-2); one Serve() per connected viewer.
  ControlSessionServer controlServer(args, stop, clientSession, capture, clientMetrics, encoder,
                                     inputRouter, backend, windowSelectionTxn);

  if (args.controlPort > 0) {
    clientSession.controlListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSession.controlListenSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] control listen socket create failed port=" << args.controlPort << "\n";
    } else {
      sockaddr_in ctlLocal{};
      ctlLocal.sin_family = AF_INET;
      ctlLocal.sin_port = htons(args.controlPort);
      ctlLocal.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
      if (bind(clientSession.controlListenSock, reinterpret_cast<const sockaddr*>(&ctlLocal), sizeof(ctlLocal)) != 0 ||
          listen(clientSession.controlListenSock, 1) != 0) {
        std::cerr << "[native-video-host] control bind/listen failed port=" << args.controlPort << "\n";
        closesocket(clientSession.controlListenSock);
        clientSession.controlListenSock = INVALID_SOCKET;
      } else {
        std::cout << "[native-video-host] control waiting port=" << args.controlPort << "\n";
        clientSession.controlThread = std::thread([&]() {
          while (!stop.load()) {
            sockaddr_in cpeer{};
            int cpeerLen = sizeof(cpeer);
            SOCKET acceptedSock = accept(clientSession.controlListenSock, reinterpret_cast<sockaddr*>(&cpeer), &cpeerLen);
            if (acceptedSock == INVALID_SOCKET) {
              if (stop.load()) break;
              Sleep(50);
              continue;
            }
            clientSession.controlClientSock = acceptedSock;
            int ctlNoDelay = 1;
            setsockopt(acceptedSock, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
            std::cout << "[native-video-host][control] client connected\n";
            {
              TcpControlLink link(acceptedSock);
              controlServer.Serve(link);
            }
            if (acceptedSock != INVALID_SOCKET) {
              shutdown(acceptedSock, SD_BOTH);
              closesocket(acceptedSock);
            }
            {
              SOCKET expected = acceptedSock;
              clientSession.controlClientSock.compare_exchange_strong(expected, INVALID_SOCKET);
            }
            std::cout << "[native-video-host][control] tcp client disconnected\n";
          }
        });
      }
    }
  }

  // Control over the media socket. A client that arrived through the directory service has no
  // way to open a TCP connection back to us, so the same dispatch is also served here; a LAN
  // client that prefers TCP simply never sends control datagrams and this stays idle.

  // ---------------------------------------------------------------- session epoch
  //
  // A session begins when a Hello presents a capability we have not seen before, and that is the
  // only reliable signal there is. The endpoint is not one: through a relay every client reaches
  // us from the same address and port, so "the peer changed" stays false forever and the second
  // client inherits the first one's control channel -- where its messages are acknowledged and
  // then dropped, because their sequence numbers look like ones already delivered.
  //
  // The epoch serialises the handover. The reader raises it and waits; the dispatcher resets the
  // channel, re-enters its session loop (which is also what turns the stream back on) and
  // publishes that it is ready; only then does the reader answer the Hello. Since the client
  // repeats its Hello until it sees an Ack, nothing it sends can arrive before the reset.
  // Starts at one, not zero: control is only wired up after the handshake loop above has already
  // accepted a Hello, so by the time the dispatcher starts there is a session waiting for it.

  if (transport == VideoTransport::Udp) {
    clientSession.udpControlChannel.Configure(
        [&](const void* data, size_t len) -> bool {
          const uint32_t ip = sender.udpPeerIpNet.load(std::memory_order_acquire);
          const uint16_t port = sender.udpPeerPortNet.load(std::memory_order_acquire);
          if (ip == 0 || port == 0) return false;
          sockaddr_in to{};
          to.sin_family = AF_INET;
          to.sin_addr.s_addr = ip;
          to.sin_port = port;
          return sendto(clientSession.clientSock, static_cast<const char*>(data), static_cast<int>(len), 0,
                        reinterpret_cast<const sockaddr*>(&to), sizeof(to)) > 0;
        },
        remote60::native_poc::kUdpControlStreamHostToClient,
        remote60::native_poc::kUdpControlStreamClientToHost, args.udpMtu);

    clientSession.udpReaderThread = std::thread([&]() {
      // Startup barrier. The dispatcher's first Reset races this thread: if the client's first
      // ControlData lands here first, OnPacket ACKs it into rxReady_, then the dispatcher's
      // Reset wipes rxReady_ -- and the client, holding an ACK, never retransmits. The serve
      // loop then starves for its full 10s read timeout ("ended reason=none") with a 40-70%
      // field hit rate. Hold this thread off the socket until the dispatcher has published
      // clientSession.controlReadyEpoch for the current epoch; datagrams meanwhile wait, unharmed, in the
      // kernel socket buffer. wait_for (not wait) so shutdown cannot strand us if no one
      // signals the cv after stop.
      {
        std::unique_lock<std::mutex> lock(clientSession.epochMu);
        while (!stop.load() &&
               clientSession.controlReadyEpoch.load(std::memory_order_acquire) <
                   clientSession.epoch.load(std::memory_order_acquire)) {
          clientSession.epochCv.wait_for(lock, std::chrono::milliseconds(50));
        }
      }
      int lastLoggedRecvError = 0;
      while (!stop.load()) {
        uint8_t rx[kUdpReceiveBufferBytes];
        sockaddr_in peer{};
        int peerLen = sizeof(peer);
        const int n = recvfrom(clientSession.clientSock, reinterpret_cast<char*>(rx), sizeof(rx), 0,
                               reinterpret_cast<sockaddr*>(&peer), &peerLen);
        // A zero-length datagram is legal and arrives from NAT keepalives and port scanners.
        // It used to fall into the error path below and end this thread, after which no Hello
        // was ever read again: video kept streaming to the previous peer while every new
        // client connected its control channel and then watched nothing arrive.
        if (n == 0) continue;
        if (n < 0) {
          const int err = WSAGetLastError();
          if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK || err == WSAEMSGSIZE ||
              err == WSAECONNRESET) {
            // Nothing arrived, or one datagram was malformed. Keep the retransmit timers moving
            // so a stalled transfer still recovers while the link is quiet.
            clientSession.udpControlChannel.Tick();
            continue;
          }
          // This thread is the only reader of hellos; while the process lives it must too.
          // Whatever went wrong with one receive, the socket itself outlives it.
          if (err != lastLoggedRecvError) {
            lastLoggedRecvError = err;
            std::cout << "[native-video-host] udp reader recv error err=" << err
                      << " (continuing)\n";
          }
          clientSession.udpControlChannel.Tick();
          Sleep(50);
          continue;
        }
        const size_t len = static_cast<size_t>(n);

        UdpHelloPacket hello{};
        if (len >= sizeof(UdpHelloPacket)) {
          std::memcpy(&hello, rx, sizeof(hello));
          if (hello.magic == remote60::native_poc::kMagic &&
              hello.kind == static_cast<uint16_t>(UdpPacketKind::Hello) &&
              hello.version == remote60::native_poc::kUdpProtocolVersion &&
              (hello.features & remote60::native_poc::kUdpFeatureVideoFec) != 0) {
            gUdpVideoFecInterleaved.store(
                (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved) != 0,
                std::memory_order_relaxed);

            UdpHelloPacket ack{};
            ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
            ack.features =
                remote60::native_poc::kUdpFeatureVideoFec |
                (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved);
            size_t tokenLen = 0;
            while (tokenLen < sizeof(hello.authToken) && hello.authToken[tokenLen] != '\0') {
              ++tokenLen;
            }
            bool directoryAuthenticated = false;
            bool newSession = false;
            if (tokenLen > 0) {
              const std::string authToken(hello.authToken, hello.authToken + tokenLen);
              const auto kind = classify_directory_hello(authToken, peer);
              if (kind == DirectoryHello::Rejected) {
                std::cerr << "[native-video-host] rejected reconnect hello with invalid directory capability\n";
                continue;
              }
              newSession = (kind == DirectoryHello::NewSession);
              directoryAuthenticated = true;
              ack.features |= remote60::native_poc::kUdpFeatureDirectoryAuth;
              std::string secureInputStatus;
              (void)inputRouter.broker.EnsureInstalledAndConnected(
                  remote60::native_poc::sibling_executable_path(
                      L"GNLinkInputService.exe"),
                  &secureInputStatus);
            } else if (clientSession.directoryAuthenticated.load(std::memory_order_acquire)) {
              // Do not let an unauthenticated LAN Hello take over or de-authorize an active
              // directory session. Direct-LAN mode remains available before authentication.
              std::cerr << "[native-video-host] rejected unauthenticated reconnect during directory session\n";
              continue;
            }
            clientSession.directoryAuthenticated.store(directoryAuthenticated,
                                                std::memory_order_release);
            const bool changed =
                sender.udpPeerIpNet.load(std::memory_order_acquire) != peer.sin_addr.s_addr ||
                sender.udpPeerPortNet.load(std::memory_order_acquire) != peer.sin_port;
            // An unauthenticated LAN client has no capability to compare, so the endpoint is all
            // there is to go on. It is a weaker signal -- an app restart that lands on the same
            // port is invisible -- but the relay, which is what makes endpoints ambiguous, only
            // ever carries authenticated sessions.
            const bool startsSession = directoryAuthenticated ? newSession : changed;
            if (changed) {
              sender.udpPeerIpNet.store(peer.sin_addr.s_addr, std::memory_order_release);
              sender.udpPeerPortNet.store(peer.sin_port, std::memory_order_release);
            }
            if (startsSession) {
              // Even when the endpoint is unchanged: a new client has a new decoder, and sending
              // it deltas against frames it never saw leaves it grey until the next keyframe.
              sender.udpPeerChanged.store(true, std::memory_order_release);
              const uint64_t epoch = clientSession.BeginEpoch();
              std::cout << "[native-video-host][control] session epoch=" << epoch
                        << (changed ? " peer=new" : " peer=same") << "\n";
              clientSession.AwaitControlReady(epoch);
            }
            // Answered last, so that by the time the client believes it is connected the control
            // channel behind this endpoint is already the new session's.
            (void)sendto(clientSession.clientSock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                         reinterpret_cast<const sockaddr*>(&peer), peerLen);
            continue;
          }
        }

        if (clientSession.udpControlChannel.OnPacket(rx, len)) continue;
        (void)clientSession.directoryAgent.ConsumeUdpPacket(rx, len, peer);
      }
      clientSession.udpControlChannel.Close(remote60::native_poc::ControlCloseReason::Shutdown);
      clientSession.epochCv.notify_all();
    });

    // One dispatcher for the life of the process, serving one session after another. It used to
    // serve exactly one: any failed read returned from serve_control_session and the thread
    // exited for good, taking the stream with it (the session teardown clears
    // clientSession.streamControlActive, and only re-entry restores it). A client that merely walked out of
    // Wi-Fi range was enough to leave the host answering handshakes and nothing else.
    clientSession.udpControlThread = std::thread([&]() {
      uint64_t servedEpoch = 0;
      for (;;) {
        {
          std::unique_lock<std::mutex> lock(clientSession.epochMu);
          clientSession.epochCv.wait(lock, [&] {
            return stop.load() || clientSession.epoch.load(std::memory_order_acquire) > servedEpoch;
          });
        }
        if (stop.load()) break;
        servedEpoch = clientSession.epoch.load(std::memory_order_acquire);
        // Reset belongs here rather than in the reader: this is the thread that owns the
        // channel's read side, so nothing is being consumed while the queues are cleared.
        clientSession.udpControlChannel.Reset();
        {
          std::lock_guard<std::mutex> lock(clientSession.epochMu);
          clientSession.controlReadyEpoch.store(servedEpoch, std::memory_order_release);
        }
        clientSession.epochCv.notify_all();

        // The read timeout is what lets the host notice a client that simply vanished. The
        // channel only declares peer-lost while it has something to retransmit; a client that
        // dies between requests leaves nothing outstanding, and a blocking read sat here for
        // the rest of the process with the stream still marked active -- capturing, encoding,
        // and sending to nobody. The client pings about once a second, so ten silent seconds
        // is a client that is gone, not one that is slow.
        UdpControlLink link(&clientSession.udpControlChannel, 10000);
        controlServer.Serve(link);
        // Closed is not finished. Retransmits running out means this client is gone, which is
        // the ordinary end of a session and the reason to wait for the next one.
        std::cout << "[native-video-host][control] udp control session ended epoch=" << servedEpoch
                  << " reason=" << remote60::native_poc::to_string(clientSession.udpControlChannel.CloseReason())
                  << "\n";
      }
    });
  }

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

  // RAII / WinRT / D3D capture objects (CaptureResources, Phase 2-4); created below at the same points as before.
  CaptureResources res;
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

  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
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
  int32_t poppedNv12Slot = -1;
  uint64_t poppedNv12Generation = 0;




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

  winrt::event_token token{};
  // Independent DXGI capture-worker wedge watchdog. Kept OUT of the main-loop watchdog because the
  // field failure (15:05, 2026-08-25) was the worker hung inside a DXGI call while a user "select"
  // parked main in restart_capture_session -> Stop().join() waiting on that same worker -- the main
  // tick was blocked too, so only an independent thread can break it. Shares no lock/GPU with
  // capture; reads only the worker's atomic heartbeat (backend steady clock) and TerminateProcess
  // (44)s a worker stuck > 5s so the supervisor rebuilds the process with a fresh D3D device.
  // Joined (never detached) before dxgiCaptureSession is destroyed -- it references the session's
  // progress block. (Codex-reviewed 2026-08-25.)
  std::atomic<bool> dxgiWatchdogStop{false};
  std::thread dxgiWorkerWatchdog([&dxgiCaptureSession = res.dxgiCaptureSession, &dxgiWatchdogStop]() {
    constexpr uint64_t kWorkerWarnUs = 3'000'000;   // structured warn; likely a transient
    constexpr uint64_t kWorkerKillUs = 5'000'000;   // ~50x the 100ms Acquire timeout -> genuine wedge
    uint64_t warnedGeneration = std::numeric_limits<uint64_t>::max();
    HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
    auto emit = [&](const char* rec, int n) {
      if (herr && herr != INVALID_HANDLE_VALUE && n > 0) {
        DWORD wrote = 0;
        WriteFile(herr, rec, static_cast<DWORD>(n), &wrote, nullptr);
      }
    };
    while (!dxgiWatchdogStop.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      if (dxgiWatchdogStop.load(std::memory_order_acquire)) break;
      const auto snap = dxgiCaptureSession.SnapshotWorker();
      if (!snap.running) {
        warnedGeneration = std::numeric_limits<uint64_t>::max();
        continue;
      }
      if (snap.ageUs >= kWorkerKillUs) {
        char rec[320];
        const int n = std::snprintf(
            rec, sizeof(rec),
            "[native-video-host][dxgi-watchdog] dxgi-worker-wedge phase=%s phaseAgeUs=%llu "
            "ageUs=%llu generation=%llu loopCount=%llu acquireHr=0x%08lX releaseHr=0x%08lX "
            "accumulated=%u; terminating (exit 44) for supervisor relaunch\n",
            remote60::host::capture_worker_phase_name(snap.phase),
            static_cast<unsigned long long>(snap.phaseAgeUs),
            static_cast<unsigned long long>(snap.ageUs),
            static_cast<unsigned long long>(snap.generation),
            static_cast<unsigned long long>(snap.loopCount),
            static_cast<unsigned long>(static_cast<uint32_t>(snap.lastAcquireHr)),
            static_cast<unsigned long>(static_cast<uint32_t>(snap.lastReleaseHr)),
            static_cast<unsigned>(snap.lastAccumulatedFrames));
        emit(rec, n);
        TerminateProcess(GetCurrentProcess(), kExitDxgiWorkerWedge);
      } else if (snap.ageUs >= kWorkerWarnUs) {
        if (warnedGeneration != snap.generation) {
          warnedGeneration = snap.generation;  // warn once per worker episode
          char rec[320];
          const int n = std::snprintf(
              rec, sizeof(rec),
              "[native-video-host][dxgi-watchdog] dxgi-worker slow phase=%s phaseAgeUs=%llu "
              "ageUs=%llu generation=%llu loopCount=%llu acquireHr=0x%08lX releaseHr=0x%08lX\n",
              remote60::host::capture_worker_phase_name(snap.phase),
              static_cast<unsigned long long>(snap.phaseAgeUs),
              static_cast<unsigned long long>(snap.ageUs),
              static_cast<unsigned long long>(snap.generation),
              static_cast<unsigned long long>(snap.loopCount),
              static_cast<unsigned long>(static_cast<uint32_t>(snap.lastAcquireHr)),
              static_cast<unsigned long>(static_cast<uint32_t>(snap.lastReleaseHr)));
          emit(rec, n);
        }
      } else {
        // Progress resumed within this generation; re-arm so a later stall in the same episode warns.
        warnedGeneration = std::numeric_limits<uint64_t>::max();
      }
    }
  });
  struct DxgiWatchdogJoiner {
    std::atomic<bool>* stopFlag;
    std::thread* th;
    ~DxgiWatchdogJoiner() {
      stopFlag->store(true, std::memory_order_release);
      if (th->joinable()) th->join();
    }
  } dxgiWatchdogJoiner{&dxgiWatchdogStop, &dxgiWorkerWatchdog};
  // Frozen-ring self-heal state (DXGI/WGC). Streak guards against a single slow poll; the last
  // restart timestamp lets a refreeze inside the window escalate to a full process restart.
  // Rate-limit the "readback slow" warn to one line/sec with the window peak. Under a GPU-heavy
  // game the oldest-pending age oscillates in [250ms, 2s) every frame, and the old warn-once latch
  // was cleared by the 2s-restart else-branch below, so it re-fired ~60x/sec -- the log spam was
  // itself a perturbation (Codex 2026-08-25).
  // Telemetry for the frozen-ring self-heal, so a real-GPU run can tell whether B-1 is actually the
  // fix (oldest-pending age climbs to the 2s restart threshold) or whether the age keeps clearing at
  // 50-100ms and the starvation lives in the readback path itself (the surface-only bypass, B-2).
  // The age is a per-interval peak because a once-per-second sample of the instantaneous age would
  // miss a spike that the restart logic (which polls every loop) does see.
  // Peak GpuPending count over the interval, next to the age peak: a frozen ring pins this at the
  // ring size while the age climbs, whereas a merely busy ring churns it low. The instantaneous
  // age/count are also emitted, so a print catches both the interval's worst and the current state.
  // Restarts driven specifically by the frozen ring, kept apart from watchdog.deadRestartCount, which
  // also counts the GDI callback-stall watchdog -- mixing them would blur which path actually fired.
  // (A refreeze inside the escalation window is a distinct outcome, but it exits the process, so it
  // shows up in the refroze log line rather than a counter that no later stats print would carry.)
  // Readback-throughput soft-watchdog state (see kReadbackDrainWarmupUs). The trigger is over
  // per-1s-window deltas, so the cumulative sources (cadence accepts, staging-busy drops,
  // superseded drops) are diffed against the previous tick's snapshot every tick -- not every
  // print. The oldest-pending peak is accumulated at loop frequency next to the frozen-ring peak
  // (a once-per-second sample would miss a spike the loop-rate poll sees) and reset each tick.
  // The consecutive-second counter debounces a single slow window; the last drain-restart
  // timestamp lets a recurrence inside the frozen-ring escalation window escalate to a process
  // restart. streamActiveSinceUs anchors a warmup after a client (re)attaches.
  uint64_t streamActiveSinceUs = 0;

  // Capture attachment (session) cookie. Bumped by capture.DetachCaptureSession(res, token) on the main thread
  // before any pool recreate; a capture callback or readback completion that began under the
  // previous attachment sees the change and drops its frame instead of stamping it with the
  // post-recreate target/generation. Hardens the recreate transition race.
  // WGC ContentSize gate. A WGC frame-pool surface is a FIXED buffer size (capture.width x
  // capture.height, chosen at pool creation); frame.ContentSize() is the actual content region and
  // shrinks/grows with the window. The callback records a mismatching content size here and drops
  // the frame; the main thread settles then recreates the pool at the new size (the callback thread
  // must never recreate capture resources itself).
  // Main-thread-only settle tracking + recreate telemetry for the WGC ContentSize gate.
  capture.stagingSlotCount =
      std::max<uint32_t>(3u, static_cast<uint32_t>(capture.framePoolBuffers + 1));

  // Per-interval / lifetime pipeline statistics for the stats line (HostStats, Phase 1-12).
  HostStats stats;
  // Hardware-cursor state from the DXGI backend (pointer-only frames are dropped by the content
  // pipeline, so without this side channel the remote cursor freezes on a still screen). Written
  // by the capture thread, drained by the main loop's ~30Hz latest-wins UDP cursor sender.
  // Timestamp (qpc) of the last frame actually published to the encoder ring, set in
  // capturePublishFn on a valid payload -- distinct from capture.lastCallbackUs, which is the capture time.
  // The stats line reports this as lastPublishAgeUs (diagnostic only). Deliberately not reset on a
  // restart: the age then honestly shows the publish gap and snaps back on the first new publish,
  // which is exactly the recovery signal we want to see after a frozen-ring restart.

  // Worker-thread side: a finished readback becomes the latest frame. Timing fields keep
  // their FrameState names so downstream logs stay parseable; their meaning under the async
  // pipeline is documented at each assignment.
  res.capturePublishFn = [&capture, &res, &stats](std::shared_ptr<std::vector<uint8_t>> payload, uint32_t frameW,
                                                  uint32_t frameH, uint32_t stride,
                                                  const remote60::native_poc::CaptureFrameMeta& meta,
                                                  uint64_t gpuPendingUs, uint64_t workerMapUs, uint64_t workerMemcpyUs) {
    capture.PublishFrame(res, stats, std::move(payload), frameW, frameH, stride, meta, gpuPendingUs, workerMapUs,
                         workerMemcpyUs);
  };

  // Capture-callback side: size check, a cheap crop-rect query, then a single GPU copy
  // submit. No Map, no memcpy, no allocation -- the DXGI duplication frame is released the
  // moment this returns instead of being held across a synchronous readback.

  if (!capture.CreateStaging(res, encoder, useH264, capture.width, capture.height)) {
    std::cerr << "[native-video-host] capture readback pipeline create failed\n";
    closesocket(clientSession.clientSock);
    if (clientSession.listenSock != INVALID_SOCKET) closesocket(clientSession.listenSock);
    if (encoder.mfStarted) MFShutdown();
    return 10;
  }




  // Liveness state for the main-loop watchdog (declared before restart_capture_session so it can
  // flag its own slow phase). watchdog.mainLoopProgressUs is bumped each loop iteration; the watchdog reads
  // it plus the current phase and never touches a lock or the GPU.
  watchdog.mainLoopPhase = static_cast<uint32_t>(MainLoopPhase::Startup);
  watchdog.mainLoopProgressUs = qpc_now_us();

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


  if (!restart_capture_session(host)) {
    std::cerr << "[native-video-host] capture session start failed\n";
    res.captureReadback.Shutdown();
    closesocket(clientSession.clientSock);
    if (clientSession.listenSock != INVALID_SOCKET) closesocket(clientSession.listenSock);
    if (encoder.mfStarted) MFShutdown();
    return 10;
  }
  powerKeepalive.SetStreaming(clientSession.streamControlActive.load(std::memory_order_acquire), true);

  startUs = qpc_now_us();
  nextTickUs = startUs;
  // For encoded path, latency is prioritized over strict send pacing.
  // Raw path keeps legacy pacing to avoid excessive CPU/bandwidth burst.
  // Encoded capture callbacks are already phase-limited to encoder.activeFps before GPU readback.
  // A second independent main-loop clock periodically woke just before the callback, waited
  // only a quarter-frame, then slept to its next tick; the meanwhile-arriving frame was
  // overwritten by the following callback. Consume encoded frames directly from the CV so
  // every accepted 30 Hz capture reaches the encoder. Raw mode still needs its own clock.
 
  captureWindowRebindIntervalUs =
      static_cast<uint64_t>(std::max<uint32_t>(200, args.captureWindowRebindIntervalMs)) * 1000ULL;
  nextCaptureWindowCheckUs = startUs + captureWindowRebindIntervalUs;
  stats.nextAtUs = startUs + 1000000ULL;
  // Every rate in the stats line is computed over a one-second window, so the window is not
  // widened -- only the printing is decimated. Each printed line still describes a true
  // second; there are just fewer of them. At the old every-second cadence a streaming day
  // wrote hundreds of megabytes through the host log; set 1 to watch a session closely.
  stats.printEverySec =
      env_u32_clamped("REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC", 30, 1, 3600);
  // Encoded frames the sender queue policy discarded (backlog resync or waiting for the
  // forced IDR). These are the frames a viewer experiences as a freeze.
  // Session media barrier / IDR telemetry (encode-thread side). encoder.forceKeyInputCount and
  // sender.nonKeyAuWhileWaiting reset per print interval; sender.firstKeyEnqueuedUs is per media epoch (reset by
  // the rollover transaction). Goal: tell "encoder never produced a key" apart from "key produced
  // but lost in UDP assembly". Diagnostic only -- never fed to ABR.
 
  // The capture lifecycle used to be "start once, stop at exit". Everything between -- a client
  // disconnecting, another connecting an hour later -- left DXGI duplication (or WGC after a
  // fallback) acquiring frames at full desktop rate for nobody, which is what starved RDP
  // sessions into single-digit frame rates until the process was killed. Capture now detaches
  // after the stream has been inactive for a grace period, and reattaches on the active edge.
  // The grace period exists because the picker also parks the stream: tearing down DXGI for a
  // two-second visit to the target list would make every return visibly slow.
  capture.reattachRetryDelayUs = kCaptureReattachRetryMinUs;
  // Receives now happen on their own thread so a control message never waits for the next
  // frame; this just adopts a peer change the reader has already handled.
  // ~30Hz latest-wins cursor forwarder (UdpCursorPosPacket). Desktop-DXGI only: WGC composites
  // the cursor into the frames themselves, and a window target has its own coordinate space.
  // Sends on movement/visibility change, plus a 250ms heartbeat while visible so the viewer's
  // stale-hide timeout does not blank a stationary cursor. Unreliable by design; no resend.


  sender.StartThread(transport, useH264, args, clientSession);


  // --- Trailing-edge encoder kick (host main/encode thread only) ----------------------------
  // The async H.264 MFT holds the most recent input frame until the NEXT input arrives, so on a
  // still screen the last real capture (the state after a drag-release, a right-click menu, the
  // first frame after connect) stays stuck inside the encoder and never reaches the wire. This kick
  // supplies exactly one "next input" on a trailing edge: every real frame (re)arms a 150ms timer,
  // so continuous motion just pushes the deadline out (zero synthetic frames); only when changes
  // stop does the timer fire and resubmit the cached last raw frame once, flushing the held frame
  // out. A kick is cancelled the moment the latest real input is observed coming out of the encoder
  // (its capture timestamp on an emitted AU) or -- on a fresh media barrier -- the epoch's first key
  // AU reaches the wire. Kicks are kept out of ABR/rate evidence: a single sparse frame is not a
  // congestion signal. This is NOT a periodic keepalive -- nothing is sent while the screen is quiet.
  // Periodic static refresh cadence (0 = off). On a genuinely still screen the pipeline sends
  // nothing at all, so the viewer's picture silently ages and looks dead; this re-serves the
  // cached frame as a cheap P-frame at a low rate. Milliseconds via env for field tuning.
  kick.staticRefreshIntervalUs =
      static_cast<uint64_t>(env_u32_clamped("REMOTE60_NATIVE_STATIC_REFRESH_MS", 1000, 0, 10000)) *
      1000ULL;
  // Validate the cache against the live capture identity and the CURRENT secure-desktop state, then
  // fill the loop's frame locals from it. Returns false (leaving the screen black) if anything is
  // stale, mismatched, or the desktop is locked/secure -- better black than a wrong picture.

  // Dedicated liveness watchdog. It shares no lock or GPU with the capture/encode/send threads, so
  // it stays responsive when they wedge inside a driver/MFT call (the failure seen in the field:
  // the whole main loop stopped, control threads kept running, and nothing recovered because the
  // in-loop self-heal never ran and the supervisor only relaunches on a crash). It never calls into
  // D3D (a device-wide hang could block that too) -- it only reads the atomics the main loop last
  // stored, writes one raw record via WriteFile (not iostream, whose lock a hung main may hold), and
  // TerminateProcess()es so the supervisor relaunches a fresh child. ExitProcess/normal return are
  // avoided: they run DLL detach / join the hung threads and would re-hang.
  std::thread mainLoopWatchdog([&]() {
    constexpr uint64_t kHangNormalUs = 10'000'000;   // Loop / EncodeCall
    constexpr uint64_t kHangSlowUs = 20'000'000;     // CaptureRestart / Startup (legit slow)
    constexpr uint64_t kStartupGraceUs = 30'000'000;  // device/encoder bring-up before arming
    const uint64_t watchdogStartUs = qpc_now_us();
    while (!stop.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      if (stop.load(std::memory_order_acquire)) break;
      const uint64_t now = qpc_now_us();
      if (now - watchdogStartUs < kStartupGraceUs) continue;
      const uint32_t phase = watchdog.mainLoopPhase.load(std::memory_order_acquire);
      const uint64_t progressUs = watchdog.mainLoopProgressUs.load(std::memory_order_acquire);
      const uint64_t ageUs = now > progressUs ? now - progressUs : 0;
      const uint64_t threshold =
          (phase == static_cast<uint32_t>(MainLoopPhase::CaptureRestart) ||
           phase == static_cast<uint32_t>(MainLoopPhase::Startup))
              ? kHangSlowUs
              : kHangNormalUs;
      if (ageUs >= threshold) {
        char rec[192];
        const int n = std::snprintf(
            rec, sizeof(rec),
            "[native-video-host][watchdog] main-loop hang phase=%u ageUs=%llu lastSeq=%llu; "
            "terminating (exit 43) for supervisor relaunch\n",
            phase, static_cast<unsigned long long>(ageUs),
            static_cast<unsigned long long>(watchdog.mainLoopLastSeq.load(std::memory_order_acquire)));
        HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
        if (herr && herr != INVALID_HANDLE_VALUE && n > 0) {
          DWORD wrote = 0;
          WriteFile(herr, rec, static_cast<DWORD>(n), &wrote, nullptr);
        }
        TerminateProcess(GetCurrentProcess(), kExitMainLoopWatchdog);
      }
    }
  });
  mainLoopWatchdog.detach();

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

  stop = true;
  res.frame.cv.notify_all();
  windowSelectionTxn.cv.notify_all();
  {
    SOCKET ctlSock = clientSession.controlClientSock.exchange(INVALID_SOCKET);
    if (ctlSock != INVALID_SOCKET) {
      shutdown(ctlSock, SD_BOTH);
      closesocket(ctlSock);
    }
  }
  if (clientSession.controlListenSock != INVALID_SOCKET) {
    closesocket(clientSession.controlListenSock);
    clientSession.controlListenSock = INVALID_SOCKET;
  }
  if (clientSession.controlThread.joinable()) clientSession.controlThread.join();
  // Close before joining: the control session is parked in a blocking read, and the reader
  // thread is parked in recvfrom until its receive timeout expires. The dispatcher now outlives
  // any one session, so it also has to be woken from the wait it parks in between them.
  clientSession.udpControlChannel.Close(remote60::native_poc::ControlCloseReason::Shutdown);
  clientSession.epochCv.notify_all();
  if (clientSession.udpControlThread.joinable()) clientSession.udpControlThread.join();
  if (clientSession.udpReaderThread.joinable()) clientSession.udpReaderThread.join();
  capture.DetachCaptureSession(res, token);
  // Stop the readback worker while everything its publish callback touches is still alive;
  // relying on destructor order would tear down FrameState first.
  res.captureReadback.Shutdown();
  // The sender still holds clientSession.clientSock; stop it before the socket closes.
  sender.stop.store(true, std::memory_order_release);
  sender.cv.notify_all();
  if (sender.thread.joinable()) sender.thread.join();
  if (clientSession.clientSock != INVALID_SOCKET) {
    closesocket(clientSession.clientSock);
    clientSession.clientSock = INVALID_SOCKET;
  }
  if (clientSession.listenSock != INVALID_SOCKET) {
    closesocket(clientSession.listenSock);
    clientSession.listenSock = INVALID_SOCKET;
  }
  if (useH264) {
    encoder.codec.shutdown();
    if (encoder.mfStarted) MFShutdown();
  }
  std::cout << "[native-video-host] done\n";
  return 0;
}
