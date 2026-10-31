// Stage 11 (raw path): copy the popped frame into a raw frame message and send it over TCP.
//
// Host split refactor Phase 3.5b: the raw arm of the former stage_encode_send if/else, verbatim;
// see host_main_loop.hpp for the loop, HostContext / TickContext and Flow.

//


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

Flow encode_send_raw(HostContext& hx, TickContext& tc) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& transport = hx.transport;
  auto& frameGating = hx.frameGating;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& lastUserFeedbackUs = hx.lastUserFeedbackUs;
  auto& tickWaitUs = tc.tickWaitUs;
  auto& payload = tc.payload;
  auto& seq = tc.seq;
  auto& w = tc.w;
  auto& h = tc.h;
  auto& stride = tc.stride;
  auto& streamGeneration = tc.streamGeneration;
  auto& captureUs = tc.captureUs;
  auto& callbackUs = tc.callbackUs;
  auto& callbackIntervalUs = tc.callbackIntervalUs;
  auto& captureIntervalUs = tc.captureIntervalUs;
  auto& captureClockSkewUs = tc.captureClockSkewUs;
  auto& captureD3DWaitUs = tc.captureD3DWaitUs;
  auto& captureCopyMapUs = tc.captureCopyMapUs;
  auto& captureMemcpyUs = tc.captureMemcpyUs;
  auto& captureUnmapWaitUs = tc.captureUnmapWaitUs;
  auto& captureUnmapUs = tc.captureUnmapUs;
  auto& queueWaitReason = tc.queueWaitReason;
  auto& queueWaitUs = tc.queueWaitUs;
  auto& queueGapFrames = tc.queueGapFrames;
  auto& queueDepthAtPop = tc.queueDepthAtPop;
  auto& captureStampUs = tc.captureStampUs;
  auto& queuePopUs = tc.queuePopUs;
  auto& queueSelectWaitUs = tc.queueSelectWaitUs;
  auto& frameAgeAtSelectUs = tc.frameAgeAtSelectUs;
  auto& captureToCallbackUs = tc.captureToCallbackUs;
  auto& captureToQueueUs = tc.captureToQueueUs;
  RawFrameHeader hdr{};
  hdr.header.magic = remote60::native_poc::kMagic;
  hdr.header.type = static_cast<uint16_t>(MessageType::RawFrameBgra);
  hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
  hdr.seq = seq;
  hdr.width = w;
  hdr.height = h;
  hdr.stride = stride;
  hdr.payloadSize = static_cast<uint32_t>(payload->size());
  hdr.streamGeneration = streamGeneration;
  hdr.captureQpcUs = captureStampUs;
  hdr.encodeStartQpcUs = captureStampUs;
  hdr.encodeEndQpcUs = captureStampUs;
  SendPathStats sendPathStats{};
  const uint64_t sendStartUs = qpc_now_us();
  const uint64_t sendIntervalUs =
      (stats.lastSendStartUs > 0 && sendStartUs >= stats.lastSendStartUs) ? (sendStartUs - stats.lastSendStartUs) : 0;
  const uint64_t sendIntervalErrUs =
      (encoder.activeFrameIntervalUs > 0 && sendIntervalUs > 0)
          ? ((sendIntervalUs >= encoder.activeFrameIntervalUs) ? (sendIntervalUs - encoder.activeFrameIntervalUs)
                                                       : (encoder.activeFrameIntervalUs - sendIntervalUs))
          : 0;
  const uint64_t queueToSendUs = (sendStartUs >= queuePopUs) ? (sendStartUs - queuePopUs) : 0;
  const uint64_t sendWaitUs = queueToSendUs;
  const uint64_t callbackToSendStartUs = (sendStartUs >= callbackUs) ? (sendStartUs - callbackUs) : 0;
  hdr.sendQpcUs = sendStartUs;
  const bool sentOk =
      (transport == VideoTransport::Tcp) &&
      send_all_timed(clientSession.clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                     &sendPathStats.headerCallCount) &&
      send_all_timed(clientSession.clientSock, payload->data(), payload->size(), &sendPathStats.payloadUs,
                     &sendPathStats.payloadCallCount);
  const uint64_t sendDoneUs = qpc_now_us();
  const uint64_t sendDurUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
  const uint64_t sendCallCount = sendPathStats.headerCallCount + sendPathStats.payloadCallCount;
  if (sentOk) {
    stats.lastSendStartUs = sendStartUs;
    capture.LogFirstSentGeneration(res, stats, "raw", streamGeneration, sendStartUs, hdr.captureQpcUs, hdr.width, hdr.height);
    if (frameGating.enabled && useH264 && payload && !payload->empty()) {
      frameGating.lastSentUs = sendStartUs;
      frameGating.refPayload = payload;
      frameGating.refW = w;
      frameGating.refH = h;
      frameGating.refStride = stride;
    }
  }

  if (!sentOk) {
    if (reconnect_tcp_data_session(hx, "raw_send_fail")) {
      return Flow::Continue;
    }
    std::cout << "[native-video-host] client disconnected\n";
    return Flow::Break;
  }
  ++sender.sentFrames;
  sender.sentBytes += payload->size();
    if (args.traceEvery > 0 && (seq % args.traceEvery) == 0 &&
        (args.traceMax == 0 || stats.tracePrinted < args.traceMax)) {
    ++stats.tracePrinted;
    const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
    const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
    const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
    const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
        queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
      std::cout << "[native-video-host][trace] seq=" << seq
                << " captureUs=" << hdr.captureQpcUs
                << " encodeStartUs=" << hdr.encodeStartQpcUs
                << " encodeEndUs=" << hdr.encodeEndQpcUs
                << " sendUs=" << hdr.sendQpcUs
                << " bottleneckStageCode=" << bottleneck.code
                << " bottleneckStageUs=" << bottleneck.us
                << " bottleneckStageName=" << bottleneck.name
                << " c2eUs=" << c2eUs
                << " captureToCallbackUs=" << captureToCallbackUs
                << " callbackIntervalUs=" << callbackIntervalUs
                << " captureIntervalUs=" << captureIntervalUs
                << " captureClockSkewUs=" << captureClockSkewUs
                << " captureD3DWaitUs=" << captureD3DWaitUs
                << " captureCopyMapUs=" << captureCopyMapUs
                << " captureMemcpyUs=" << captureMemcpyUs
                << " captureUnmapWaitUs=" << captureUnmapWaitUs
                << " captureUnmapUs=" << captureUnmapUs
                << " selectWaitUs=" << frameAgeAtSelectUs
                << " queueSelectWaitUs=" << queueSelectWaitUs
               << " queueGapFrames=" << queueGapFrames
               << " queueDepth=" << queueDepthAtPop
               << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
               << " captureToQueueUs=" << captureToQueueUs
               << " queueWaitUs=" << queueWaitUs
               << " queueWaitReason=" << queueWaitReason
               << " queueToSendUs=" << queueToSendUs
               << " sendWaitUs=" << sendWaitUs
               << " sendIntervalUs=" << sendIntervalUs
               << " sendIntervalErrUs=" << sendIntervalErrUs
               << " tickWaitUs=" << tickWaitUs
               << " sendCallCount=" << sendCallCount
               << " sendHeaderUs=" << sendPathStats.headerUs
               << " sendPayloadUs=" << sendPathStats.payloadUs
               << " sendHeaderCallCount=" << sendPathStats.headerCallCount
               << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
               << " sendChunkCount=" << sendPathStats.payloadChunkCount
               << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
               << " sendStartUs=" << sendStartUs
              << " sendDoneUs=" << sendDoneUs
              << " sendDurUs=" << sendDurUs
              << " encUs=" << encUs
              << " e2sUs=" << e2sUs
              << " payloadBytes=" << hdr.payloadSize
              << "\n";
  }
  const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
  const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
  const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
  const uint64_t pipeUs = (hdr.sendQpcUs >= hdr.captureQpcUs) ? (hdr.sendQpcUs - hdr.captureQpcUs) : 0;
  const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
      queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
  if (pipeUs >= kHostUserFeedbackWarnUs &&
      (hdr.sendQpcUs >= lastUserFeedbackUs + kHostUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
    std::cout << "[native-video-host][user-feedback] seq=" << seq
              << " codec=" << "raw"
              << " pipeUs=" << pipeUs
              << " bottleneckStageCode=" << bottleneck.code
              << " bottleneckStageUs=" << bottleneck.us
              << " bottleneckStageName=" << bottleneck.name
              << " captureToCallbackUs=" << captureToCallbackUs
              << " callbackIntervalUs=" << callbackIntervalUs
              << " captureIntervalUs=" << captureIntervalUs
              << " captureClockSkewUs=" << captureClockSkewUs
              << " captureD3DWaitUs=" << captureD3DWaitUs
              << " captureCopyMapUs=" << captureCopyMapUs
              << " captureMemcpyUs=" << captureMemcpyUs
              << " captureUnmapWaitUs=" << captureUnmapWaitUs
              << " captureUnmapUs=" << captureUnmapUs
              << " selectWaitUs=" << frameAgeAtSelectUs
              << " queueSelectWaitUs=" << queueSelectWaitUs
              << " captureToQueueUs=" << captureToQueueUs
               << " queueWaitUs=" << queueWaitUs
               << " queueWaitReason=" << queueWaitReason
                << " queueGapFrames=" << queueGapFrames
                << " queueDepth=" << queueDepthAtPop
                << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
                << " queueToSendUs=" << queueToSendUs
                << " sendIntervalUs=" << sendIntervalUs
                << " sendIntervalErrUs=" << sendIntervalErrUs
                 << " captureClockSkewUs=" << captureClockSkewUs
                 << " sendWaitUs=" << sendWaitUs
               << " tickWaitUs=" << tickWaitUs
               << " sendCallCount=" << sendCallCount
               << " sendHeaderUs=" << sendPathStats.headerUs
               << " sendPayloadUs=" << sendPathStats.payloadUs
               << " sendHeaderCallCount=" << sendPathStats.headerCallCount
               << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
               << " sendChunkCount=" << sendPathStats.payloadChunkCount
               << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
               << " c2eUs=" << c2eUs
              << " cb2eUs=" << callbackToSendStartUs
              << " encUs=" << encUs
              << " e2sUs=" << e2sUs
              << " sendStartUs=" << sendStartUs
              << " sendDoneUs=" << sendDoneUs
              << " sendDurUs=" << sendDurUs
              << "\n";
    lastUserFeedbackUs = hdr.sendQpcUs;
  }
  return Flow::Next;
}

// Stage 11 dispatcher: raw or H.264 path.
Flow stage_encode_send(HostContext& hx, TickContext& tc) {
  if (hx.useRaw) return encode_send_raw(hx, tc);
  return encode_send_h264(hx, tc);
}

}  // namespace remote60::native_poc
