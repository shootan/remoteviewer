// Stage 10: static-frame gating and stale-frame guards.
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

Flow stage_gate_static(HostContext& hx, TickContext& tc) {
  auto& useH264 = hx.useH264;
  auto& guardStalePreEncode = hx.guardStalePreEncode;
  auto& paceByTick = hx.paceByTick;
  auto& frameGating = hx.frameGating;
  auto& clientMetrics = hx.clientMetrics;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& kick = hx.kick;
  auto& payload = tc.payload;
  auto& w = tc.w;
  auto& h = tc.h;
  auto& stride = tc.stride;
  auto& captureUs = tc.captureUs;
  auto& callbackUs = tc.callbackUs;
  auto& version = tc.version;
  auto& servedBootstrap = tc.servedBootstrap;
  auto& captureStampUs = tc.captureStampUs;
  auto& queuePopUs = tc.queuePopUs;
  auto& frameAgeAtSelectUs = tc.frameAgeAtSelectUs;
  if (!servedBootstrap && frameGating.enabled && useH264 && payload && !payload->empty()) {
    if (frameGating.refPayload && !frameGating.refPayload->empty() &&
        frameGating.refW == w && frameGating.refH == h && frameGating.refStride == stride) {
      frameGating.RecordChange(estimate_bgra_change_permille(
          payload->data(), frameGating.refPayload->data(), payload->size(), frameGating.sampleTarget));
    } else {
      frameGating.RecordReferenceMiss();
    }

    if (frameGating.UpdateMode()) {
      std::cout << "[native-video-host] frame-gating mode="
                << (frameGating.staticMode ? "static" : "motion")
                << " changePm=" << frameGating.changePermilleLast
                << " staticStreak=" << frameGating.staticStreak
                << " motionStreak=" << frameGating.motionStreak
                << "\n";
    }

    // encoder.forceKeyNext, not a mailbox peek. The mailbox is drained in stage_time_limit --
    // nine stages earlier in the same tick -- so peeking here read false essentially always, and
    // the gate could throttle the very frame that was supposed to carry the forced IDR. The
    // authoritative "the next accepted input must be a key" state is main-owned and already set
    // by then. (Ledger H-26a; the old requestedKeyFrame atomic worked only because it was
    // consumed AFTER this gate.)
    const bool keyReqPending = encoder.forceKeyNext;
    // The static interval throttles idle scenes; it must never hold back a frame that
    // actually changed, or the first interaction after idle arrives late.
    // In paced motion mode the main tick already enforces encoder.activeFrameIntervalUs. Applying
    // the same interval here a second time makes a slightly-early capture timestamp skip
    // the entire tick (measured 1-6 lost frames/s at 60fps). Keep this limiter only for
    // static throttling or the explicitly unpaced throughput path.
    if (frameGating.ShouldSkip(queuePopUs, keyReqPending, encoder.activeFrameIntervalUs, paceByTick)) {
      ++frameGating.skipCount;
      if (frameGating.staticMode) ++frameGating.staticSkipCount;
      stats.lastVersionSent = version;
      return Flow::Continue;
    }
  }
  if (useH264 && guardStalePreEncode && frameAgeAtSelectUs > kMaxPreEncodeFrameAgeUs) {
    ++stats.stalePreEncodeDropCount;
    return Flow::Continue;
  }
  if (!servedBootstrap) {
    if (stats.lastVersionSent > 0 && version > stats.lastVersionSent + 1) {
      stats.skippedByOverwrite += (version - stats.lastVersionSent - 1);
    }
    stats.lastVersionSent = version;
  }
  captureStampUs = (callbackUs > 0) ? callbackUs : captureUs;
  // Monotonic guard (0.2.97): a real frame that a slow readback published late must not carry a
  // stamp older than the kick/refresh already handed to the encoder -- see KickState::ClampRealStamp.
  // Only the encoder/wire stamp moves; captureUs/callbackUs (telemetry) keep the true capture time.
  if (!servedBootstrap) {
    const uint64_t clampedStampUs = KickState::ClampRealStamp(captureStampUs, kick.lastEncoderStampUs);
    if (clampedStampUs != captureStampUs) {
      const uint64_t behindUs = clampedStampUs - captureStampUs;
      tc.captureStampClampUs = behindUs;
      ++kick.stampClampCount;
      kick.stampClampMaxUs = std::max(kick.stampClampMaxUs, behindUs);
      const uint64_t nowUs = qpc_now_us();
      if (kick.stampClampLastLogUs == 0 || nowUs >= kick.stampClampLastLogUs + 1'000'000) {
        kick.stampClampLastLogUs = nowUs;
        std::cout << "[native-video-host] capture-stamp clamped behind-synthetic behindUs=" << behindUs
                  << " count=" << kick.stampClampCount << " maxUs=" << kick.stampClampMaxUs
                  << " readbackWaitUs=" << tc.captureUnmapWaitUs << "\n";
      }
      captureStampUs = clampedStampUs;
    }
  }

 
 
  return Flow::Next;
}

}  // namespace remote60::native_poc
