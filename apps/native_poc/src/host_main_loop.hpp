#pragma once

// Host main loop: the per-tick stages of the capture -> encode -> send pipeline, the context they
// share, and the tuning constants.
//
// Role:    HostContext is the set of references main() assembles once (args, flags, the twelve
//          state structs, the capture resources, the few remaining main() locals); TickContext
//          holds the per-iteration values that used to be locals of the loop body. Each
//          stage_* function is one former section of the loop body, in call order; it returns
//          Flow::Continue / Break / Return where the old code said continue / break / return.
// Thread:  main encode loop only; the stages touch other threads only through the state structs.
// Callers: native_video_host_main.cpp (RUN_STAGE sequence inside while (!stop)).
//
// Host split refactor Phase 3: bodies moved verbatim from native_video_host_main.cpp.

#include <winsock2.h>
#include <windows.h>

#include <winrt/Windows.Graphics.Capture.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "host_abr.hpp"
#include "host_args.hpp"
#include "host_backend_policy.hpp"
#include "host_capture_session.hpp"
#include "host_client_metrics.hpp"
#include "host_control_session.hpp"
#include "host_encoded_sender.hpp"
#include "host_encoder_manager.hpp"
#include "host_frame_gate.hpp"
#include "host_input_router.hpp"
#include "host_kick.hpp"
#include "host_log.hpp"
#include "host_main_loop_mailbox.hpp"
#include "host_session.hpp"
#include "host_stats.hpp"
#include "host_watchdog.hpp"
#include "native_video_transport.hpp"

namespace remote60::native_poc {

// --- tuning constants (formerly file-scope / main()-local constexprs of native_video_host_main.cpp) ---
constexpr bool kInputPolicyForceBlock = false;
constexpr uint64_t kMaxEncodedFrameAgeUs = 250000;  // 250ms
constexpr uint32_t kMaxConsecutiveStaleEncodedFrames = 8;
constexpr int kCaptureFramePoolBuffersDefault = 2;
constexpr uint64_t kMaxPreEncodeFrameAgeUs = 25000;  // 25ms
constexpr uint64_t kHostUserFeedbackWarnUs = 90000;  // 90ms
// 10s, up from 1s: a static scene with frame gating on trips the send-interval detector on
// nearly every frame, and at one 2.5KB line per second that alone wrote ~9MB per streaming
// hour. One line per ten seconds still names the bottleneck while a user is feeling it.
constexpr uint64_t kHostUserFeedbackMinIntervalUs = 10000000;
constexpr uint64_t kCaptureStallKeepaliveIntervalUs = 1000000;  // 1s
constexpr uint64_t kCaptureCallbackStallRestartUs = 1200000;  // 1.2s
constexpr uint64_t kCaptureCallbackRestartCooldownUs = 3000000;  // 3s
// DXGI/WGC frozen-ring self-heal. These backends are change-driven, so the callback-stall
// watchdog above deliberately skips them -- silence on a static desktop is normal. But a ring
// that has frozen under GPU contention (submits stuck in GpuPending, their completion query never
// signalling) is distinguishable from an idle one by the age of its oldest pending submit: an idle
// ring enqueues nothing, so its oldest-pending age is 0. 250ms is telemetry only; past 2s over two
// consecutive polls the ring is dead and a same-device capture restart is due. If it refreezes
// within 60s the device itself is wedged, so we exit and let the supervisor rebuild the process.
constexpr uint64_t kCaptureFrozenWarnUs = 250000;                // 250ms
constexpr uint64_t kCaptureFrozenRestartUs = 2000000;            // 2s
constexpr uint32_t kCaptureFrozenPollStreakMin = 2;
constexpr uint64_t kCaptureFrozenEscalationWindowUs = 60000000;  // 60s
// Readback-throughput soft watchdog (DXGI/WGC). A GPU->CPU readback that drains slowly under GPU
// contention sits in the blind zone between the two hard self-heals above: the capture thread
// keeps ACQUIRING and the cadence gate keeps accepting frames (so the callback-stall/capture-dead
// watchdog stays silent), while the ring publishes almost nothing and its oldest-pending age peaks
// *below* the 2s frozen-ring threshold (so that watchdog never fires either). It is caught instead
// by watching per-1s windows where the gate accepted a real rate but the pipeline published
// almost none, corroborated by either an elevated (but sub-2s) pending age or a burst of
// staging-busy/superseded drops. First trip restarts capture+readback on the same device like the
// frozen-ring path; a recurrence inside the same 60s window escalates to a process restart for a
// fresh D3D device. These are intentionally softer than the frozen-ring thresholds -- the point is
// to cover the case the 2s hard threshold misses -- and the frozen-ring path is left untouched.
constexpr uint64_t kReadbackDrainWarmupUs = 4000000;            // 4s after start/restart/reattach
constexpr uint32_t kReadbackDrainConsecutiveSecMin = 3;         // consecutive 1s windows
constexpr uint64_t kReadbackDrainPendingAgeUs = 250000;         // 250ms window peak
constexpr uint32_t kReadbackDrainDropBurstMin = 3;             // busy+superseded delta / window
constexpr uint32_t kCaptureInputMinPushPerSecDefault = 10;
constexpr uint32_t kCaptureInputStallConsecutiveSecDefault = 3;
constexpr uint32_t kCaptureInputStallWarmupSecDefault = 4;
constexpr uint32_t kFrameGatingStaticFpsDefault = 8;
constexpr uint32_t kFrameGatingStaticThresholdPermilleDefault = 6;
constexpr uint32_t kFrameGatingEnterFramesDefault = 10;
constexpr uint32_t kFrameGatingExitFramesDefault = 2;
constexpr uint32_t kFrameGatingSampleTargetDefault = 2048;
constexpr uint32_t kKeyReqMinIntervalUsDefault = 120000;  // 120ms
constexpr uint32_t kKeyReqTokenRefillUsDefault = 300000;  // 300ms / token
constexpr uint32_t kKeyReqTokenCapacityDefault = 3;
constexpr uint64_t kEncodeRefitSettleUs = 400000;  // 0.4 s of stable size before re-init
constexpr uint64_t kWgcContentSettleUs = 100000;  // 0.1s of a stable content size before recreate
constexpr uint64_t kCaptureIdleDetachDelayUs = 5'000'000;
constexpr uint64_t kCaptureReattachRetryMinUs = 250'000;
constexpr uint64_t kCaptureReattachRetryMaxUs = 5'000'000;

// What a stage asks the loop to do next.
enum class Flow { Next, Continue, Break, Return };

// Everything the loop stages and the helper functions reach from main(). Members are named
// exactly like the main() locals they alias so the moved bodies read unchanged.
struct HostContext {
  const Args& args;
  const bool useH264;
  const bool useRaw;
  const VideoTransport& transport;  // bound to main()'s, which resolve_transport() fills after assembly (2-12)
  std::atomic<bool>& stop;
  const bool guardStaleEncoded;
  const bool guardStalePreEncode;
  const bool paceByTick;
  uint64_t& startUs;
  uint64_t& nextTickUs;
  uint64_t& captureWindowRebindIntervalUs;
  uint64_t& nextCaptureWindowCheckUs;
  bool& streamActiveApplied;
  uint64_t& streamActiveSinceUs;
  int32_t& poppedNv12Slot;
  uint64_t& poppedNv12Generation;
  HostPowerKeepalive& powerKeepalive;
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item;
  winrt::event_token& token;
  WindowSelectionTxn& windowSelectionTxn;
  FrameGatingState& frameGating;
  RateControlState& rate;
  KickState& kick;
  ClientMetricsSnapshot& clientMetrics;
  DesktopBackendState& backend;
  WatchdogState& watchdog;
  InputRouterState& inputRouter;
  SenderState& sender;
  SessionState& clientSession;
  EncoderState& encoder;
  HostStats& stats;
  CaptureState& capture;
  CaptureResources& res;
  // Requests posted by the control / sender / reader threads for the loop to act on.
  MainLoopMailbox& mailbox;
  // Owned here (formerly a function-static inside the loop): user-feedback log rate limit.
  uint64_t lastUserFeedbackUs = 0;
  // Set by a stage that returns Flow::Return; main() returns it.
  int exitCode = 0;
};

// Per-iteration values of one tick (formerly locals declared along the loop body). Constructed
// fresh every iteration, so the defaults below are exactly the old initialisers.
struct TickContext {
  uint64_t nowUs = 0;
  uint64_t tickWaitUs = 0;
  std::shared_ptr<std::vector<uint8_t>> payload;
  uint32_t seq = 0;
  uint32_t w = 0;
  uint32_t h = 0;
  uint32_t stride = 0;
  uint64_t streamGeneration = 0;
  uint64_t captureUs = 0;
  uint64_t callbackUs = 0;
  uint64_t queuePushUs = 0;
  uint64_t callbackIntervalUs = 0;
  uint64_t captureIntervalUs = 0;
  uint64_t captureClockSkewUs = 0;
  uint64_t captureAgeAtCallbackUs = 0;
  uint64_t captureD3DWaitUs = 0;
  uint64_t captureCopyMapUs = 0;
  uint64_t captureMemcpyUs = 0;
  uint64_t captureUnmapWaitUs = 0;
  uint64_t captureUnmapUs = 0;
  uint64_t version = 0;
  int32_t nv12Slot = -1;
  uint64_t nv12Generation = 0;
  uint32_t nv12W = 0;
  uint32_t nv12H = 0;
  uint32_t queueWaitReason = 0;  // 0: normal, 1: timeout, 2: no-work
  uint64_t queueSelectStartUs = 0;
  bool servedBootstrap = false;
  bool kickForcedKey = false;    // true only when this kick must open a closed media barrier (IDR)
  uint64_t queuePopUs = 0;
  uint64_t queueSelectWaitUs = 0;
  uint64_t frameAgeAtSelectUs = 0;
  uint64_t captureToCallbackUs = 0;
  uint64_t captureToQueueUs = 0;
  uint64_t queueWaitUs = 0;
  uint64_t queueGapFrames = 0;
  uint64_t queueDepthAtPop = 0;
  uint64_t captureStampUs = 0;
  uint64_t captureStampClampUs = 0;  // how far captureStampUs was moved past a synthetic stamp (0.2.97)
  bool sendFailed = false;
};

// Helpers (former main() lambdas).
bool restart_capture_session(HostContext& hx);
void pump_cursor_forward(HostContext& hx, uint64_t nowUs);
bool reconnect_tcp_data_session(HostContext& hx, const char* reason);
bool apply_selected_window_capture(HostContext& hx, uint64_t requestedWindowId, uint64_t nowUs,
                                   uint32_t* outFlags, uint64_t* outWindowId,
                                   uint64_t* outStreamGeneration,
                                   std::string* outReason, std::string* outTitle);

// The twelve stages of one tick, in call order.
Flow stage_time_limit(HostContext& hx, TickContext& tc);    // seconds limit, barrier recovery
Flow stage_backend(HostContext& hx, TickContext& tc);       // backend request, demotion/promotion
Flow stage_stream_active(HostContext& hx, TickContext& tc); // stream active/idle transitions
Flow stage_runtime_tune(HostContext& hx, TickContext& tc);  // runtime encoder config requests
Flow stage_selection(HostContext& hx, TickContext& tc);     // monitor / capture-mode / window selection
Flow stage_geometry(HostContext& hx, TickContext& tc);      // WGC content-size settle, size change
Flow stage_watchdogs(HostContext& hx, TickContext& tc);     // callback-stall + frozen-ring watchdogs
Flow stage_pace(HostContext& hx, TickContext& tc);          // raw-mode tick pacing
Flow stage_pop_frame(HostContext& hx, TickContext& tc);     // trailing kick, static refresh, frame pop
Flow stage_gate_static(HostContext& hx, TickContext& tc);   // static-frame gating, stale guards
Flow stage_encode_send(HostContext& hx, TickContext& tc);   // raw send / H.264 encode + enqueue (dispatcher)
Flow encode_send_raw(HostContext& hx, TickContext& tc);
Flow encode_send_h264(HostContext& hx, TickContext& tc);
Flow stage_stats(HostContext& hx, TickContext& tc);         // 1s tick: stats line, drain watchdog, ABR/M9 -- MUST run first (H-10)
Flow stats_tick_h264(HostContext& hx, TickContext& tc, uint64_t t, bool statsPrintDue, double mbps,
                     const std::string& targetProcessName, uint64_t queuePushPerSec,
                     uint64_t callbackFramesPerSec, uint64_t idleHoldPerSec,
                     const CaptureCadenceGate::Counters& cadence);

}  // namespace remote60::native_poc
