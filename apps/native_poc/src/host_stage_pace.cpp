// Stage 8: raw-mode tick pacing.
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

Flow stage_pace(HostContext& hx, TickContext& tc) {
  auto& paceByTick = hx.paceByTick;
  auto& nextTickUs = hx.nextTickUs;
  auto& sender = hx.sender;
  auto& encoder = hx.encoder;
  auto& nowUs = tc.nowUs;
  auto& tickWaitUs = tc.tickWaitUs;
  if (paceByTick) {
    if (nowUs < nextTickUs) {
      const uint64_t paceWaitStartUs = qpc_now_us();
      // Reuse the high-resolution sender timer. sleep_for commonly overshoots a 60 Hz
      // deadline by 1-3ms on Windows; resetting the clock to that late wakeup on every
      // frame turned a requested 60fps into a stable 48-54fps.
      udp_pace_wait_until(nextTickUs);
      const uint64_t paceWaitDoneUs = qpc_now_us();
      tickWaitUs = (paceWaitDoneUs >= paceWaitStartUs) ? (paceWaitDoneUs - paceWaitStartUs) : 0;
      return Flow::Continue;
    }
    // Preserve the target phase after a normal sub-frame timer overshoot. Re-anchor only
    // when processing actually missed a whole frame, avoiding both drift and catch-up bursts.
    if (nowUs > nextTickUs + encoder.activePacingFrameIntervalUs) {
      nextTickUs = nowUs;
    }
    nextTickUs += encoder.activePacingFrameIntervalUs;
  }

  return Flow::Next;
}

}  // namespace remote60::native_poc
