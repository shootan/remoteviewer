// Stage 9: trailing kick, static refresh and frame pop.
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

Flow stage_pop_frame(HostContext& hx, TickContext& tc) {
  auto& useH264 = hx.useH264;
  auto& transport = hx.transport;
  auto& stop = hx.stop;
  auto& poppedNv12Slot = hx.poppedNv12Slot;
  auto& poppedNv12Generation = hx.poppedNv12Generation;
  auto& kick = hx.kick;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  auto& payload = tc.payload;
  auto& seq = tc.seq;
  auto& w = tc.w;
  auto& h = tc.h;
  auto& stride = tc.stride;
  auto& streamGeneration = tc.streamGeneration;
  auto& captureUs = tc.captureUs;
  auto& callbackUs = tc.callbackUs;
  auto& queuePushUs = tc.queuePushUs;
  auto& callbackIntervalUs = tc.callbackIntervalUs;
  auto& captureIntervalUs = tc.captureIntervalUs;
  auto& captureClockSkewUs = tc.captureClockSkewUs;
  auto& captureAgeAtCallbackUs = tc.captureAgeAtCallbackUs;
  auto& captureD3DWaitUs = tc.captureD3DWaitUs;
  auto& captureCopyMapUs = tc.captureCopyMapUs;
  auto& captureMemcpyUs = tc.captureMemcpyUs;
  auto& captureUnmapWaitUs = tc.captureUnmapWaitUs;
  auto& captureUnmapUs = tc.captureUnmapUs;
  auto& version = tc.version;
  auto& nv12Slot = tc.nv12Slot;
  auto& nv12Generation = tc.nv12Generation;
  auto& nv12W = tc.nv12W;
  auto& nv12H = tc.nv12H;
  auto& queueWaitReason = tc.queueWaitReason;
  auto& queueSelectStartUs = tc.queueSelectStartUs;
  auto& servedBootstrap = tc.servedBootstrap;
  auto& kickForcedKey = tc.kickForcedKey;
  auto& queueWaitUs = tc.queueWaitUs;
  auto& queueGapFrames = tc.queueGapFrames;
  auto& queueDepthAtPop = tc.queueDepthAtPop;
  auto& queuePopUs = tc.queuePopUs;
  auto& queueSelectWaitUs = tc.queueSelectWaitUs;
  auto& frameAgeAtSelectUs = tc.frameAgeAtSelectUs;
  auto& captureToCallbackUs = tc.captureToCallbackUs;
  auto& captureToQueueUs = tc.captureToQueueUs;
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
  queueSelectStartUs = qpc_now_us();
 
 
  if (kick.Due(nowUs)) {
    // A real frame already waiting in the ring is always better than a kick; fall through to the
    // normal pop (the encode below re-arms and records it). Otherwise decide whether the last real
    // input still needs flushing out of the MFT.
    bool realWaiting = false;
    {
      std::lock_guard<std::mutex> lk(res.frame.mu);
      realWaiting = (res.frame.version != stats.lastVersionSent) && res.frame.payload && !res.frame.payload->empty();
    }
    if (!realWaiting) {
      // Media barrier (UDP): a closed barrier means the epoch's first key AU has not reached the
      // wire, so a fresh/returning viewer still has no picture. TCP has no barrier (always open).
      bool barrierClosed = false;
      if (transport == VideoTransport::Udp) {
        std::lock_guard<std::mutex> lk(sender.mu);
        barrierClosed = sender.waitingForKey;
      }
      const bool needKick = kick.NeedKick(barrierClosed);
      bool rearm = false;
      if (needKick && capture.KickTryFill(clientSession, kick, payload, w, h, stride, nowUs)) {
        servedBootstrap = true;
        // A closed barrier needs a real IDR; an ordinary trailing edge on an open stream can ride
        // the held frame as-is (a P-frame is fine). Leave any pre-existing encoder.forceKeyNext untouched.
        if (barrierClosed) {
          encoder.forceKeyNext = true;
          kickForcedKey = true;
        }
        seq = 0;
        version = stats.lastVersionSent;  // no real version consumed (keeps queue bookkeeping stable)
        streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
        captureUs = nowUs;     // fresh monotonic stamps: never reuse the stale capture time
        callbackUs = nowUs;
        queuePushUs = nowUs;
        kick.MarkKickedForCurrentInput();
        // Keep kicking on a still-closed barrier: each kick feeds a forced IDR, so the held frame
        // becomes an IDR within a couple of flushes and the cancel comes when it reaches the wire.
        rearm = barrierClosed;
      }
      // Otherwise one-shot: a failed fill (locked/secure/identity mismatch) leaves the screen black
      // rather than painting a wrong or stale picture, and a satisfied trailing edge stays quiet.
      if (rearm) {
        kick.Arm(nowUs, useH264);
      } else {
        kick.Cancel();
      }
    }
  }
  // Periodic static refresh (user requirement): on a still screen duplication offers no content
  // and the trailing kick is one-shot, so NOTHING is sent and the session looks frozen (the
  // field case: a static game map, revived only by dragging it). Re-serve the cached frame at a
  // low cadence (default 1Hz, REMOTE60_NATIVE_STATIC_REFRESH_MS, 0=off) as an ordinary P-frame.
  // The cadence anchors on BOTH the last emitted AU and the last refresh ATTEMPT: the async MFT
  // may legally return no output for a submitted input, and an emitted-only clock would then
  // retry on every loop iteration -- a tight input flood, the opposite of an idle 1Hz refresh.
  // The barrier must be open (a closed barrier is the kick's job and needs an IDR) and the
  // sender queue empty (stacking a synthetic frame onto a backlog helps nobody; the queue drains
  // within a few loop ticks). kick_try_fill re-validates identity/secure/size, so a lock screen
  // or a mid-switch target stays black rather than repainting a stale picture; a failed fill
  // also stamps the attempt clock so the (uncached) secure probe is not repeated every tick.
  if (!servedBootstrap && kick.staticRefreshIntervalUs > 0 && useH264 &&
      clientSession.streamControlActive.load(std::memory_order_acquire) && kick.StaticRefreshDue(nowUs)) {
    bool refreshBlocked = false;
    if (transport == VideoTransport::Udp) {
      std::lock_guard<std::mutex> lk(sender.mu);
      refreshBlocked = sender.waitingForKey || !sender.queue.empty();
    }
    if (!refreshBlocked) {
      // Stamped on the ATTEMPT, before the encode result is known -- see the cadence note.
      kick.lastStaticRefreshAttemptUs = nowUs;
      if (capture.KickTryFill(clientSession, kick, payload, w, h, stride, nowUs)) {
        servedBootstrap = true;
        seq = 0;
        version = stats.lastVersionSent;  // no real version consumed (keeps queue bookkeeping stable)
        streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
        captureUs = nowUs;
        callbackUs = nowUs;
        queuePushUs = nowUs;
        ++kick.staticRefreshCount;
      }
    }
  }
  bool queueReady = false;
  if (!servedBootstrap) {
    std::unique_lock<std::mutex> lk(res.frame.mu);
    queueReady = res.frame.cv.wait_for(lk, std::chrono::microseconds(capture.EffectiveQueueWaitTimeoutUs(encoder)), [&] {
      return stop.load() || res.frame.version != stats.lastVersionSent;
    });
    if (!queueReady && !stop.load()) {
      queueWaitReason = 1;
      ++stats.queueWaitTimeoutCount;
      return Flow::Continue;
    }
    if (stop.load()) return Flow::Break;
    if (res.frame.version == stats.lastVersionSent || !res.frame.payload || res.frame.payload->empty()) {
      queueWaitReason = 2;
      ++stats.queueWaitNoWorkCount;
      return Flow::Continue;
    }
    version = res.frame.version;
    payload = res.frame.payload;
    seq = res.frame.seq;
    w = res.frame.width;
    h = res.frame.height;
    stride = res.frame.stride;
    streamGeneration = res.frame.streamGeneration;
    captureUs = res.frame.captureUs;
    callbackUs = res.frame.callbackUs;
    callbackIntervalUs = res.frame.callbackIntervalUs;
    captureIntervalUs = res.frame.captureIntervalUs;
    queuePushUs = res.frame.queuePushUs;
    captureAgeAtCallbackUs = res.frame.captureAgeAtCallbackUs;
    captureClockSkewUs = res.frame.captureClockSkewUs;
    captureD3DWaitUs = res.frame.captureD3DWaitUs;
    captureCopyMapUs = res.frame.captureCopyMapUs;
    captureMemcpyUs = res.frame.captureMemcpyUs;
    captureUnmapWaitUs = res.frame.captureUnmapWaitUs;
    captureUnmapUs = res.frame.captureUnmapUs;
    nv12Slot = res.frame.nv12Slot;
    nv12Generation = res.frame.nv12Generation;
    nv12W = res.frame.nv12W;
    nv12H = res.frame.nv12H;
    res.frame.nv12Slot = -1;  // claimed; this loop now owns the release
  }
  // NB: a real frame pop deliberately does NOT cancel the kick. The pending timer is (re)armed and
  // kick.lastRealInputCaptureUs recorded once the frame is actually fed to the MFT (see below), so the
  // deadline trails the LAST real input; the kick then cancels only when that input is observed
  // coming out of the encoder, not merely because a frame was popped.
  if (poppedNv12Slot >= 0) {
    // The previous iteration bailed out before encoding (gating skip, stale drop);
    // release its claimed conversion now.
    res.captureReadback.ReleaseNv12Slot(poppedNv12Slot, poppedNv12Generation);
  }
  poppedNv12Slot = nv12Slot;
  poppedNv12Generation = nv12Generation;
queuePopUs = qpc_now_us();
queueSelectWaitUs =
    (queuePopUs >= queueSelectStartUs) ? (queuePopUs - queueSelectStartUs) : 0;
frameAgeAtSelectUs =
    (callbackUs > 0 && queuePopUs >= callbackUs) ? (queuePopUs - callbackUs) : 0;
captureToCallbackUs =
    (callbackUs > 0 && captureUs > 0)
        ? (callbackUs >= captureUs ? (callbackUs - captureUs) : (captureUs - callbackUs))
        : 0;
captureToQueueUs =
    (queuePushUs > 0 && captureUs > 0)
        ? (queuePushUs >= captureUs ? (queuePushUs - captureUs) : (captureUs - queuePushUs))
        : 0;
  // Remember the size for the 1s tick, which no longer sees this tick's TickContext.
  stats.lastFrameW = w;
  stats.lastFrameH = h;
  // Real readbacks only. A trailing kick or a static-refresh frame is served from the bootstrap
  // cache and never went through D3D at all, so its stage timings are all zero -- feeding them
  // here added zeros to the numerator and ones to the denominator, and on a static desktop (where
  // the 1Hz refresh may be the ONLY thing flowing) that dragged every capture-stage average
  // toward zero exactly when someone would be reading them to ask why nothing is moving.
  // (Ledger H-13; the client-side twin is viewer ledger F-10.)
  if (!servedBootstrap) {
    ++stats.captureReadbackSamples;
    stats.captureD3DWaitSumUs += captureD3DWaitUs;
    stats.captureD3DWaitMaxUs = std::max(stats.captureD3DWaitMaxUs, captureD3DWaitUs);
    stats.captureCopyMapSumUs += captureCopyMapUs;
    stats.captureCopyMapMaxUs = std::max(stats.captureCopyMapMaxUs, captureCopyMapUs);
    stats.captureMemcpySumUs += captureMemcpyUs;
    stats.captureMemcpyMaxUs = std::max(stats.captureMemcpyMaxUs, captureMemcpyUs);
    stats.captureUnmapWaitSumUs += captureUnmapWaitUs;
    stats.captureUnmapWaitMaxUs = std::max(stats.captureUnmapWaitMaxUs, captureUnmapWaitUs);
    stats.captureUnmapSumUs += captureUnmapUs;
    stats.captureUnmapMaxUs = std::max(stats.captureUnmapMaxUs, captureUnmapUs);
  }
  queueWaitUs =
      (queuePopUs > 0 && queuePushUs > 0 && queuePopUs >= queuePushUs) ? (queuePopUs - queuePushUs) : 0;
  queueGapFrames =
      (stats.lastVersionSent > 0 && version > stats.lastVersionSent) ? (version - stats.lastVersionSent - 1) : 0;
  ++stats.queuePopCount;
  const uint64_t lastPopVersionAtRead = capture.lastPopFrameVersion.load(std::memory_order_acquire);
  queueDepthAtPop = (version > lastPopVersionAtRead) ? (version - lastPopVersionAtRead) : 0;
  update_u64_max(stats.queueDepthMax, queueDepthAtPop);
  update_u64_max(stats.queueDepthWindowMax, queueDepthAtPop);
  capture.lastPopFrameVersion.store(version, std::memory_order_release);
  return Flow::Next;
}

}  // namespace remote60::native_poc
