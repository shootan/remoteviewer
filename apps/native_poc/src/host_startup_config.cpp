// Host startup 1/5: process setup, REMOTE60_NATIVE_* env -> state structs, transport, the startup log
// lines, directory credentials, control-state defaults.
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

void startup_process_setup() {
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
}

int startup_configure_from_env(HostContext& hx) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& useRaw = hx.useRaw;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  inputRouter.injectionMode = parse_input_injection_mode(args.inputInjectionMode);
  inputRouter.injectionEnabled =
      args.enableInputInjection &&
      (inputRouter.injectionMode == InputInjectionMode::BackgroundMessage) &&
      !kInputPolicyForceBlock;
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
  capture.submitLimitEnabled =
      !env_truthy("REMOTE60_NATIVE_CAPTURE_SUBMIT_LIMIT_DISABLE");
  capture.submitEarlyTolerancePercent = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_SUBMIT_EARLY_TOLERANCE_PCT", 25, 0, 90);
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
  encoder.keyReqMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_MIN_INTERVAL_US", kKeyReqMinIntervalUsDefault, 10000, 1000000);
  encoder.keyReqTokenRefillUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_TOKEN_REFILL_US", kKeyReqTokenRefillUsDefault, 10000, 2000000);
  encoder.keyReqTokenCapacity = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_TOKEN_CAPACITY", kKeyReqTokenCapacityDefault, 1, 16);
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
  return 0;
}

int resolve_transport(const Args& args, bool useRaw, bool useH264, VideoTransport& transport) {
  std::string effectiveTransport = args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = useH264 ? "udp" : "tcp";
  }
  if (!parse_video_transport(effectiveTransport, &transport)) {
    std::cerr << "[native-video-host] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 15;
  }
  if (transport == VideoTransport::Udp && useRaw) {
    std::cerr << "[native-video-host] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 16;
  }
  return 0;
}

void startup_log_config(HostContext& hx) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& transport = hx.transport;
  auto& guardStalePreEncode = hx.guardStalePreEncode;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
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
    sender.pacePeakBps.store(
        static_cast<uint32_t>(std::min<uint64_t>(pacePeakBps, 4000000000ULL)),
        std::memory_order_relaxed);
    sender.keyframePacePeakBps.store(sender.udpKeyframePacePeakBps, std::memory_order_relaxed);
    std::cout << "[native-video-host] h264 pacing=" << (sender.noPacingH264 ? "off" : "on")
              << " udpPacePeakPercent=" << sender.udpPacePeakPercent
              << " udpPacePeakBps=" << sender.pacePeakBps.load(std::memory_order_relaxed)
              << " udpPacePeakFloorBps=" << sender.udpPacePeakFloorBps
              << " udpKeyframePacePeakBps="
              << sender.keyframePacePeakBps.load(std::memory_order_relaxed)
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
}

void startup_configure_session(HostContext& hx) {
  auto& args = hx.args;
  auto& transport = hx.transport;
  auto& clientSession = hx.clientSession;
  // Credentials may come from the command line or the environment. The environment is the
  // better place for the password: a command line is readable by any process on the machine.
  auto arg_or_env = [](const std::string& fromArgs, const char* envKey) -> std::string {
    if (!fromArgs.empty()) return fromArgs;
    const char* v = std::getenv(envKey);
    return v ? std::string(v) : std::string();
  };
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
}

void startup_configure_control_state(HostContext& hx) {
  auto& args = hx.args;
  auto& backend = hx.backend;
  auto& inputRouter = hx.inputRouter;
  auto& encoder = hx.encoder;
  // Which screen desktop mode shows. Zero is the primary, which is what it always was, so a
  // client that never selects one behaves exactly as before.
  backend.reqValue = desktop_capture_backend_code(desktop_capture_backend_from_env());
  encoder.ResetKeyRequestBucket();
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
}

}  // namespace remote60::native_poc
