// Stage 7: callback-stall and frozen-ring capture watchdogs.
//
// Host split refactor Phase 3.5: moved verbatim out of host_main_loop.cpp so each stage reads on its
// own; see host_main_loop.hpp for the loop, HostContext / TickContext and Flow.


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
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture_backend_dxgi.hpp"
#include "d3d_capture_readback.hpp"
#include "encode_resolution_ladder.hpp"
#include "gdi_capture_process.hpp"
#include "host_abr.hpp"
#include "host_args.hpp"
#include "host_backend_policy.hpp"
#include "host_bgra_scale.hpp"
#include "host_bottleneck.hpp"
#include "host_capture_device.hpp"
#include "host_capture_session.hpp"
#include "host_client_metrics.hpp"
#include "host_control_session.hpp"
#include "host_encoded_sender.hpp"
#include "host_encoder_manager.hpp"
#include "host_frame_gate.hpp"
#include "host_frame_state.hpp"
#include "host_gpu_scaler.hpp"
#include "host_input_inject.hpp"
#include "host_input_router.hpp"
#include "host_kick.hpp"
#include "host_log.hpp"
#include "host_main_loop.hpp"
#include "host_net_io.hpp"
#include "host_session.hpp"
#include "host_stats.hpp"
#include "host_string_util.hpp"
#include "host_watchdog.hpp"
#include "host_window_enum.hpp"
#include "mf_h264_codec.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::host::DesktopCaptureBackend;
using remote60::host::DxgiDesktopCaptureConfig;
using remote60::host::DxgiDesktopCaptureSession;

namespace remote60::native_poc {

Flow stage_watchdogs(HostContext& hx, TickContext& tc) {
  auto& backend = hx.backend;
  auto& watchdog = hx.watchdog;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  if (capture.sessionReady.load(std::memory_order_acquire) &&
      clientSession.streamControlActive.load(std::memory_order_acquire) &&
      !capture.windowModeActive.load(std::memory_order_acquire) &&
      backend.active == DesktopCaptureBackend::Gdi) {
    // GDI is clocked and must publish continuously. WGC/DXGI are change-driven and can
    // legitimately stay silent on a static desktop, so callback silence is not a stall for
    // those backends and must never trigger a restart loop.
    const uint64_t lastCbUs = capture.lastCallbackUs.load(std::memory_order_acquire);
    const uint64_t sessionStartUs = capture.sessionStartedUs;
    const uint64_t stallBaseUs = (lastCbUs > 0) ? lastCbUs : sessionStartUs;
    const bool restartCooldownDone =
        (watchdog.lastCaptureRestartUs == 0 ||
         nowUs >= (watchdog.lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
    if (stallBaseUs > 0 && nowUs >= (stallBaseUs + kCaptureCallbackStallRestartUs) &&
        restartCooldownDone) {
      const uint64_t stallUs = nowUs - stallBaseUs;
      watchdog.lastCaptureRestartUs = nowUs;
      const bool restarted = restart_capture_session(hx);
      if (restarted) {
        ++capture.restartCount;
        capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
        capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
        capture.lastCallbackUs.store(0, std::memory_order_release);
        encoder.ResetTimelineAnchors(capture);
        encoder.forceKeyNext = true;
        ++watchdog.deadRestartCount;
        std::cout << "[native-video-host] capture session restarted count=" << capture.restartCount
                  << " captureDeadRestartCount=" << watchdog.deadRestartCount
                  << " stallUs=" << stallUs
                  << " lastCallbackUs=" << lastCbUs
                  << "\n";
      } else {
        std::cerr << "[native-video-host] capture session restart failed stallUs=" << stallUs
                  << "\n";
      }
    }
  }
  // DXGI/WGC frozen-ring self-heal. The callback-stall watchdog above is GDI-only on purpose:
  // change-driven backends are silent on a static desktop, so silence there is not a stall. A
  // ring that has frozen under GPU contention is a different thing and it has a distinct signal
  // -- its oldest submit sits in GpuPending because the completion query never fires, while an
  // idle ring holds nothing pending at all. Restart on that age, over two consecutive polls so a
  // single slow readback does not trip it. This is the "it went dark and reconnecting shows
  // nothing" report from a host pinned by a GPU-heavy game; before this, DXGI/WGC had no path
  // back short of the user restarting the host.
  if (capture.sessionReady.load(std::memory_order_acquire) &&
      clientSession.streamControlActive.load(std::memory_order_acquire) &&
      !capture.windowModeActive.load(std::memory_order_acquire) &&
      backend.active != DesktopCaptureBackend::Gdi) {
    const uint64_t oldestPendingUs = res.captureReadback.OldestGpuPendingAgeUs();
    watchdog.oldestGpuPendingPeakUs = std::max(watchdog.oldestGpuPendingPeakUs, oldestPendingUs);
    // Same loop-rate sample feeds the readback-drain watchdog's per-1s-window peak; unlike the
    // frozen-ring peak above (reset per print interval) this one is reset every stats tick.
    watchdog.drainOldestPendingPeakUs = std::max(watchdog.drainOldestPendingPeakUs, oldestPendingUs);
    watchdog.gpuPendingCountPeak = std::max(watchdog.gpuPendingCountPeak, res.captureReadback.GpuPendingCount());
    if (oldestPendingUs >= kCaptureFrozenWarnUs) {
      watchdog.readbackSlowWindowPeakUs = std::max(watchdog.readbackSlowWindowPeakUs, oldestPendingUs);
    }
    // Advance the 1s window on the boundary regardless of whether it logs, so a peak from an
    // earlier slow episode never bleeds into a later warn (Codex 2026-08-25).
    if (watchdog.readbackSlowLastLogUs == 0) watchdog.readbackSlowLastLogUs = nowUs;
    if (nowUs - watchdog.readbackSlowLastLogUs >= 1'000'000) {
      if (watchdog.readbackSlowWindowPeakUs >= kCaptureFrozenWarnUs) {
        std::cout << "[native-video-host] capture readback slow oldestPendingUs=" << oldestPendingUs
                  << " peakUs=" << watchdog.readbackSlowWindowPeakUs << "\n";
      }
      watchdog.readbackSlowLastLogUs = nowUs;
      watchdog.readbackSlowWindowPeakUs = 0;
    }
    if (oldestPendingUs >= kCaptureFrozenRestartUs) {
      ++watchdog.frozenPollStreak;
    } else {
      watchdog.frozenPollStreak = 0;
    }
    const bool restartCooldownDone =
        (watchdog.lastCaptureRestartUs == 0 ||
         nowUs >= (watchdog.lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
    if (watchdog.frozenPollStreak >= kCaptureFrozenPollStreakMin && restartCooldownDone) {
      watchdog.frozenPollStreak = 0;
      // First freeze: a same-device capture restart clears a wedged duplication/WGC session.
      // A refreeze inside the window means the device itself is stuck -- restarting capture on
      // the same device will not clear it -- so exit and let the supervisor rebuild the process
      // with a fresh D3D device. main() runs under that supervisor; a non-zero return long after
      // startup reads to it as a restartable exit (ranMs >= 15s, so not counted as a crash-loop),
      // and it relaunches without the startup backoff.
      const bool refroze =
          watchdog.lastFrozenRestartUs != 0 &&
          nowUs < (watchdog.lastFrozenRestartUs + kCaptureFrozenEscalationWindowUs);
      watchdog.lastFrozenRestartUs = nowUs;
      if (refroze) {
        std::cerr << "[native-video-host] capture ring refroze within "
                  << (kCaptureFrozenEscalationWindowUs / 1000000)
                  << "s oldestPendingUs=" << oldestPendingUs
                  << "; exiting for a full process restart\n";
        // A machine-readable twin of the line above: an in-process escalation counter would be
        // pointless (the process exits before another stats print), so this single record carries
        // the last state the frozen ring reached and pairs with host_app.log's exit-code-3 line
        // to reconstruct a cross-process recovery across the restart. (Codex.)
        const uint64_t refreezeLastPubUs = capture.lastPublishUs.load(std::memory_order_acquire);
        const uint64_t refreezeLastPubAgeUs =
            (refreezeLastPubUs > 0 && nowUs > refreezeLastPubUs) ? nowUs - refreezeLastPubUs : 0;
        std::cout << "[native-video-host] capture-recovery reason=frozen-ring-refreeze"
                  << " action=process-restart exitCode=3"
                  << " oldestPendingUs=" << oldestPendingUs
                  << " gpuPendingCount=" << res.captureReadback.GpuPendingCount()
                  << " frozenRingRestarts=" << watchdog.frozenRingRestartCount
                  << " captureRestarts=" << capture.restartCount
                  << " lastPublishAgeUs=" << refreezeLastPubAgeUs
                  << " backend=" << desktop_capture_backend_name(backend.active)
                  << " windowSec=" << (kCaptureFrozenEscalationWindowUs / 1000000)
                  << "\n";
        std::cout.flush();
        std::cerr.flush();
        { hx.exitCode = 3; return Flow::Return; }
      }
      watchdog.lastCaptureRestartUs = nowUs;
      const bool restarted = restart_capture_session(hx);
      if (restarted) {
        ++capture.restartCount;
        capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(),
                                   std::memory_order_release);
        capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
        capture.lastCallbackUs.store(0, std::memory_order_release);
        encoder.ResetTimelineAnchors(capture);
        encoder.forceKeyNext = true;
        ++watchdog.deadRestartCount;
        ++watchdog.frozenRingRestartCount;
        std::cout << "[native-video-host] capture session restarted reason=frozen-ring count="
                  << capture.restartCount << " captureDeadRestartCount=" << watchdog.deadRestartCount
                  << " frozenRingRestarts=" << watchdog.frozenRingRestartCount
                  << " oldestPendingUs=" << oldestPendingUs << "\n";
      } else {
        std::cerr << "[native-video-host] frozen-ring restart failed oldestPendingUs="
                  << oldestPendingUs << "\n";
      }
    }
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc
