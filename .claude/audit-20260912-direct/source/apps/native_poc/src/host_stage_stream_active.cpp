// Stage 3: stream active/idle transitions (idle detach, reattach).
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

Flow stage_stream_active(HostContext& hx, TickContext& tc) {
  auto& useH264 = hx.useH264;
  auto& streamActiveApplied = hx.streamActiveApplied;
  auto& powerKeepalive = hx.powerKeepalive;
  auto& token = hx.token;
  auto& frameGating = hx.frameGating;
  auto& kick = hx.kick;
  auto& backend = hx.backend;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  const bool streamActive = clientSession.streamControlActive.load(std::memory_order_acquire);
  if (!streamActive) {
    if (streamActiveApplied) {
      capture.FlushCapturePipelineState(res, frameGating, stats, "stream-inactive");
      streamActiveApplied = false;
      powerKeepalive.SetStreaming(false);
      capture.idleDetachAtUs = qpc_now_us() + kCaptureIdleDetachDelayUs;
      std::cout << "[native-video-host] stream inactive\n";
    }
    if (!capture.idleDetached && qpc_now_us() >= capture.idleDetachAtUs) {
      capture.DetachCaptureSession(res, token);
      // Stale by construction: whatever forced a fallback while nobody was watching is
      // re-evaluated from scratch when the reattach picks its backend.
      capture.dxgiFallbackRequested.store(false, std::memory_order_release);
      capture.gdiFallbackRequested.store(false, std::memory_order_release);
      capture.idleDetached = true;
      std::cout << "[native-video-host] capture detached (idle)\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return Flow::Continue;
  }
  if (!streamActiveApplied) {
    if (capture.idleDetached) {
      // Reattach before declaring the stream applied, and only declare it on success:
      // marking it applied with no capture running would serve black frames with no path
      // back. Failure retries with backoff -- the desktop may still be mid-transition
      // (RDP disconnecting, a secure desktop closing) when the client returns.
      const uint64_t nowUs = qpc_now_us();
      if (nowUs < capture.reattachRetryAtUs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return Flow::Continue;
      }
      if (!capture.windowModeActive.load(std::memory_order_acquire)) {
        // Fresh resolution, not the backend the last session was demoted to. This is also
        // what frees a host parked on WGC by an RDP visit: the desktop is back on the real
        // adapter by now, and starting from the requested backend finds it.
        backend.active = backend.requested;
        // Reattach bypasses the climb-back secure gate, so honour the same rule here: if the
        // requested backend is DXGI but the desktop is currently secure (UAC/lock), attaching
        // DXGI would just take an immediate E_ACCESSDENIED and demote. Start on WGC instead so
        // the returning viewer gets a picture now, and let the climb-back promote to DXGI once
        // the default desktop settles. A requested WGC/GDI backend is respected as-is.
        if (backend.requested == DesktopCaptureBackend::Dxgi &&
            !interactive_desktop_is_default_uncached()) {
          backend.active = DesktopCaptureBackend::Wgc;
        }
      }
      if (!restart_capture_session(hx)) {
        capture.reattachRetryDelayUs =
            std::min<uint64_t>(capture.reattachRetryDelayUs * 2, kCaptureReattachRetryMaxUs);
        capture.reattachRetryAtUs = nowUs + capture.reattachRetryDelayUs;
        std::cerr << "[native-video-host] capture reattach failed; retrying in "
                  << (capture.reattachRetryDelayUs / 1000) << "ms\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return Flow::Continue;
      }
      capture.idleDetached = false;
      capture.reattachRetryAtUs = 0;
      capture.reattachRetryDelayUs = kCaptureReattachRetryMinUs;
      ++capture.restartCount;
      capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
      capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
      capture.lastCallbackUs.store(0, std::memory_order_release);
      encoder.ResetTimelineAnchors(capture);
      capture.FlushCapturePipelineState(res, frameGating, stats, "capture-reattached");
      std::cout << "[native-video-host] capture reattached backend="
                << desktop_capture_backend_name(backend.active) << "\n";
    }
    streamActiveApplied = true;
    encoder.forceKeyNext = true;
    // A returning viewer on a still desktop needs a picture too; arm the trailing-edge kick for
    // the current epoch (coalesces with any arm from the epoch/generation edges above). This also
    // covers the stream-inactive->active edge and a capture reattach, which both land here.
    kick.Arm(qpc_now_us(), useH264);
    powerKeepalive.SetStreaming(true, true);
    // A stream-inactive->active edge starts a fresh streaming episode; drop any no-output streak
    // left from before so the inactive gap is not mistaken for encoder starvation.
    encoder.ResetStarvationEpisode();
    std::cout << "[native-video-host] stream active; forcing keyframe\n";
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc
