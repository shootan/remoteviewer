// Stage 1: seconds limit and sender barrier recovery.
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

Flow stage_time_limit(HostContext& hx, TickContext& tc) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& transport = hx.transport;
  auto& startUs = hx.startUs;
  auto& kick = hx.kick;
  auto& watchdog = hx.watchdog;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  auto& nowUs = tc.nowUs;
  watchdog.MarkMainProgress(MainLoopPhase::Loop);
  nowUs = qpc_now_us();
 
  if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
    return Flow::Break;
  }
  if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
    return Flow::Break;
  }
  sender.PumpUdpHello(transport, encoder);
  pump_cursor_forward(hx, nowUs);
  // Arm the trailing-edge kick on a fresh viewer/decoder (bumped session epoch) and on a capture
  // identity change (a new stream generation -- window select, reattach, backend change). The kick
  // fires 150ms later only if no real frame has arrived and been flushed out; a real callback
  // fills the cold cache first, so even the first kick has pixels to resubmit.
  {
    const uint64_t curEpoch = clientSession.epoch.load(std::memory_order_acquire);
    if (curEpoch != kick.lastSeenBootstrapEpoch) {
      kick.lastSeenBootstrapEpoch = curEpoch;
      kick.Arm(nowUs, useH264);
    }
    const uint64_t curGen = capture.streamGenerationState.load(std::memory_order_acquire);
    if (curGen != kick.lastSeenStreamGeneration) {
      kick.lastSeenStreamGeneration = curGen;
      kick.Arm(nowUs, useH264);
    }
  }
  // Barrier recovery: the sender thread re-armed sender.waitingForKey after a same-epoch send
  // failure and cannot itself produce an IDR (encoder.forceKeyNext is main-thread-owned, and on a static
  // desktop no new frame arrives to carry sender.requestKey). Consume the flag here, before the
  // frame wait, and both force the next encode to be a key AND arm the trailing kick so the kick
  // resubmits the cached raw frame when the screen is not changing -- otherwise a re-armed barrier
  // on a still desktop would never open.
  if (sender.recoveryPending.exchange(false, std::memory_order_acq_rel)) {
    encoder.forceKeyNext = true;
    kick.Arm(nowUs, useH264);
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc
