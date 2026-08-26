// Host shutdown: stop the threads, detach capture, stop the readback worker and the sender, close the
// sockets, shut the encoder / Media Foundation down.
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

void shutdown_host(HostContext& hx) {
  auto& useH264 = hx.useH264;
  auto& stop = hx.stop;
  auto& token = hx.token;
  auto& windowSelectionTxn = hx.windowSelectionTxn;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  auto& res = hx.res;
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
}

}  // namespace remote60::native_poc
