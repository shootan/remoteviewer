// Host startup 5/5: DXGI capture-worker wedge watchdog, readback pipeline (publish callback + staging),
// capture session start + timing/stats anchors + sender thread, main-loop liveness watchdog.
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

void startup_start_dxgi_watchdog(HostContext& hx, std::atomic<bool>& dxgiWatchdogStop, std::thread& dxgiWorkerWatchdog) {
  auto& res = hx.res;
  // Independent DXGI capture-worker wedge watchdog. Kept OUT of the main-loop watchdog because the
  // field failure (15:05, 2026-08-25) was the worker hung inside a DXGI call while a user "select"
  // parked main in restart_capture_session -> Stop().join() waiting on that same worker -- the main
  // tick was blocked too, so only an independent thread can break it. Shares no lock/GPU with
  // capture; reads only the worker's atomic heartbeat (backend steady clock) and TerminateProcess
  // (44)s a worker stuck > 5s so the supervisor rebuilds the process with a fresh D3D device.
  // Joined (never detached) before dxgiCaptureSession is destroyed -- it references the session's
  // progress block. (Codex-reviewed 2026-08-25.)
  dxgiWorkerWatchdog = std::thread([&dxgiCaptureSession = res.dxgiCaptureSession, &dxgiWatchdogStop]() {
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
}

int startup_create_readback(HostContext& hx) {
  auto& useH264 = hx.useH264;
  auto& watchdog = hx.watchdog;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
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
  return 0;
}

int startup_start_capture(HostContext& hx) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& transport = hx.transport;
  auto& stop = hx.stop;
  auto& startUs = hx.startUs;
  auto& nextTickUs = hx.nextTickUs;
  auto& captureWindowRebindIntervalUs = hx.captureWindowRebindIntervalUs;
  auto& nextCaptureWindowCheckUs = hx.nextCaptureWindowCheckUs;
  auto& powerKeepalive = hx.powerKeepalive;
  auto& kick = hx.kick;
  auto& watchdog = hx.watchdog;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  // Liveness state for the main-loop watchdog (declared before restart_capture_session so it can
  // flag its own slow phase). watchdog.mainLoopProgressUs is bumped each loop iteration; the watchdog reads
  // it plus the current phase and never touches a lock or the GPU.
  watchdog.mainLoopPhase = static_cast<uint32_t>(MainLoopPhase::Startup);
  watchdog.mainLoopProgressUs = qpc_now_us();
  if (!restart_capture_session(hx)) {
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


  sender.StartThread(transport, useH264, args, clientSession, hx.mailbox);


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
  return 0;
}

void startup_start_main_loop_watchdog(HostContext& hx, MainLoopWatchdogThread& mainLoopWatchdog) {
  auto& stop = hx.stop;
  auto& watchdog = hx.watchdog;
  // Dedicated liveness watchdog. It shares no lock or GPU with the capture/encode/send threads, so
  // it stays responsive when they wedge inside a driver/MFT call (the failure seen in the field:
  // the whole main loop stopped, control threads kept running, and nothing recovered because the
  // in-loop self-heal never ran and the supervisor only relaunches on a crash). It never calls into
  // D3D (a device-wide hang could block that too) -- it only reads the atomics the main loop last
  // stored, writes one raw record via WriteFile (not iostream, whose lock a hung main may hold), and
  // TerminateProcess()es so the supervisor relaunches a fresh child. ExitProcess/normal return are
  // avoided: they run DLL detach / join the hung threads and would re-hang.
  // Owned, not detached: the joiner in main() stops and joins it before the state below goes out
  // of scope (ledger H-06). &stop / &watchdog are main() locals that outlive this object.
  mainLoopWatchdog.thread = std::thread([&mainLoopWatchdog, &stop, &watchdog]() {
    constexpr uint64_t kHangNormalUs = 10'000'000;   // Loop / EncodeCall
    constexpr uint64_t kHangSlowUs = 20'000'000;     // CaptureRestart / Startup (legit slow)
    constexpr uint64_t kStartupGraceUs = 30'000'000;  // device/encoder bring-up before arming
    const uint64_t watchdogStartUs = qpc_now_us();
    while (!stop.load(std::memory_order_acquire)) {
      // Same 1s cadence as the old sleep, but interruptible so shutdown does not wait it out.
      if (!mainLoopWatchdog.WaitOrStop(std::chrono::milliseconds(1000), stop)) break;
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
}

}  // namespace remote60::native_poc
