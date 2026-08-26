// Stage 6: WGC content-size settle and capture size change.
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

Flow stage_geometry(HostContext& hx, TickContext& tc) {
  auto& item = hx.item;
  auto& frameGating = hx.frameGating;
  auto& watchdog = hx.watchdog;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  // WGC ContentSize settle + main-thread pool recreate. The capture callback dropped frames whose
  // ContentSize != the pool geometry and recorded the pending content size here; during an
  // interactive window drag that size churns every frame. Wait for it to hold steady for a short
  // settle window, then recreate the pool + readback at the new size on THIS (main) thread --
  // the callback thread must never recreate capture resources. restart_capture_session(hx) rebuilds
  // the WGC pool at item.Size() (the settled window size) and create_staging at the new geometry.
  if (capture.wgcContentSizeMismatchPending.load(std::memory_order_acquire) != 0) {
    const uint32_t pendW = capture.wgcPendingContentW.load(std::memory_order_acquire);
    const uint32_t pendH = capture.wgcPendingContentH.load(std::memory_order_acquire);
    uint32_t curCapW = 0;
    uint32_t curCapH = 0;
    {
      std::lock_guard<std::mutex> lk(capture.resourceMu);
      curCapW = capture.width;
      curCapH = capture.height;
    }
    if (pendW < 2 || pendH < 2 || (pendW == curCapW && pendH == curCapH)) {
      // Content settled back to the current pool geometry -- nothing to recreate.
      capture.wgcContentSizeMismatchPending.store(0, std::memory_order_release);
      capture.wgcSettleTrackW = 0;
      capture.wgcSettleTrackH = 0;
      capture.wgcSettleSinceUs = 0;
    } else if (pendW != capture.wgcSettleTrackW || pendH != capture.wgcSettleTrackH) {
      // Size still moving: (re)arm the settle timer on the newest candidate.
      capture.wgcSettleTrackW = pendW;
      capture.wgcSettleTrackH = pendH;
      capture.wgcSettleSinceUs = nowUs;
    } else if (nowUs - capture.wgcSettleSinceUs >= kWgcContentSettleUs) {
      // Stable for the settle window: recreate the pool/readback at the new size on the main thread.
      capture.wgcContentSizeMismatchPending.store(0, std::memory_order_release);
      capture.wgcSettleTrackW = 0;
      capture.wgcSettleTrackH = 0;
      capture.wgcSettleSinceUs = 0;
      watchdog.lastCaptureRestartUs = nowUs;
      capture.FlushCapturePipelineState(res, frameGating, stats, "wgc-content-size");
      if (restart_capture_session(hx)) {
        ++capture.restartCount;
        ++capture.wgcPoolRecreates;
        capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
        capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
        capture.lastCallbackUs.store(0, std::memory_order_release);
        encoder.ResetTimelineAnchors(capture);
        // Force an IDR at the (now correct) geometry. An interactive drag still lets the encode
        // size catch up on the 0.4s refit path; only the capture pool was resized here.
        encoder.forceKeyNext = true;
        uint32_t newCapW = 0;
        uint32_t newCapH = 0;
        {
          std::lock_guard<std::mutex> lk(capture.resourceMu);
          newCapW = capture.width;
          newCapH = capture.height;
        }
        std::cout << "[native-video-host] wgc-content-size pool recreated content="
                  << pendW << "x" << pendH << " capture=" << newCapW << "x" << newCapH
                  << " poolRecreates=" << capture.wgcPoolRecreates
                  << " restartCount=" << capture.restartCount << "\n";
      } else {
        std::cerr << "[native-video-host] wgc-content-size pool recreate failed content="
                  << pendW << "x" << pendH << "\n";
      }
    }
  }
  if (capture.sizeChangePending.exchange(0, std::memory_order_acq_rel) != 0) {
    watchdog.lastCaptureRestartUs = nowUs;
    capture.FlushCapturePipelineState(res, frameGating, stats, "size-change");
    if (restart_capture_session(hx)) {
      ++capture.restartCount;
      capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
      capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
      capture.lastCallbackUs.store(0, std::memory_order_release);
      encoder.ResetTimelineAnchors(capture);
      encoder.forceKeyNext = true;
      std::cout << "[native-video-host] capture session restarted reason=size-change count="
                << capture.restartCount << "\n";
    } else {
      std::cerr << "[native-video-host] capture session restart failed reason=size-change\n";
    }
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc
