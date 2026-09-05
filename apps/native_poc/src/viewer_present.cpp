// See viewer_present.hpp. Body is the WM_PAINT case of WndProc (native_video_client_main.cpp ->
// viewer_window_proc.cpp), verbatim (viewer split refactor Phase 2-8).

#include "viewer_present.hpp"

#include <iostream>
#include <sstream>

#include "viewer_gdi_util.hpp"
#include "viewer_state.hpp"
#include "viewer_layout.hpp"
#include "viewer_log.hpp"
#include "viewer_nv12_renderer.hpp"
#include "viewer_overlay_draw.hpp"

namespace remote60::native_poc::viewer {

void request_video_paint(ViewerState& ctx, HWND hwnd) {
  if (!hwnd) return;
  // The picker draws itself and invalidates on its own transitions; repainting the whole card
  // grid at video cadence would be pure cost.
  if (ctx.picker.visible.load(std::memory_order_relaxed)) return;
  if (ctx.frameBuf.paintQueued.exchange(true)) {
    ++ctx.frameBuf.paintCoalescedCount;
    return;
  }
  if (!InvalidateRect(hwnd, nullptr, FALSE)) {
    // The request never reached the OS, so nothing will ever clear the latch. Put it back or the
    // viewer stops painting for good.
    ++ctx.frameBuf.invalidateFailCount;
    ctx.frameBuf.paintQueued.store(false, std::memory_order_release);
  }
}

void poll_video_paint_liveness(ViewerState& ctx, HWND hwnd) {
  if (!hwnd) return;
  if (ctx.picker.visible.load(std::memory_order_relaxed)) return;
  uint64_t latestVersion = 0;
  {
    std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
    latestVersion = ctx.frameBuf.frame.version;
  }
  if (latestVersion == ctx.frameBuf.lastPresentedVersion.load(std::memory_order_relaxed)) return;
  // A newer frame exists and has not been presented. If a paint is genuinely pending the update
  // region is non-empty and this does nothing; if the latch and the OS have drifted apart, this
  // is what turns a permanent freeze into one dropped tick. Correctness still lives in
  // request_video_paint -- this only catches the case where that request was lost.
  const uint64_t nowUs = qpc_now_us();
  const uint64_t lastEnterUs = ctx.frameBuf.lastPaintEnterUs.load(std::memory_order_relaxed);
  if (lastEnterUs != 0 && nowUs - lastEnterUs < kPaintLivenessGraceUs) return;
  RECT updateRect{};
  if (GetUpdateRect(hwnd, &updateRect, FALSE)) return;  // a paint really is pending
  ++ctx.frameBuf.paintSelfHealCount;
  ctx.frameBuf.paintQueued.store(false, std::memory_order_release);
  request_video_paint(ctx, hwnd);
}

LRESULT paint_video_frame(ViewerState& ctx, HWND hwnd) {
  PAINTSTRUCT ps{};
  HDC hdc = BeginPaint(hwnd, &ps);
  // AFTER BeginPaint, never before. BeginPaint validates (clears) the window's update region, so
  // clearing the latch first opened a window where a producer's InvalidateRect was swallowed by
  // this very BeginPaint: the latch stayed set, no invalid region remained, and no WM_PAINT could
  // ever be generated again. Clearing here means any request that lands after this point creates
  // a fresh region that this paint cannot swallow. (Viewer ledger F-20.)
  ctx.frameBuf.paintQueued.store(false, std::memory_order_release);
  ++ctx.frameBuf.paintEnterCount;
  const uint64_t paintStartUs = qpc_now_us();
  ctx.frameBuf.lastPaintEnterUs.store(paintStartUs, std::memory_order_relaxed);
  if (!hdc) {
    // Nothing to draw into. EndPaint still has to run so the region stays validated, and the next
    // frame will ask again through the (now cleared) latch.
    ++ctx.frameBuf.beginPaintFailCount;
    EndPaint(hwnd, &ps);
    return 0;
  }
  const ClientLayout layout = compute_client_layout(ctx, hwnd);
  const RECT& videoRect = layout.videoRect;
  const RECT contentRect = resolve_video_content_rect(ctx, hwnd, videoRect);
  const bool pickerVisible = ctx.picker.visible.load(std::memory_order_relaxed);

  std::shared_ptr<std::vector<uint8_t>> local;
  Microsoft::WRL::ComPtr<IMFSample> localSurfaceSample;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> localSurfaceTexture;
  uint32_t localSurfaceSubresource = 0;
  SharedFrame::PixelFormat localFormat = SharedFrame::PixelFormat::Unknown;
  uint32_t w = 0, h = 0;
  uint32_t codedW = 0, codedH = 0;
  uint32_t visL = 0, visT = 0;
  uint32_t seq = 0;
  bool frameKey = false;
  bool frameSynthetic = false;
  uint64_t frameStreamGeneration = 0;
  uint64_t captureUs = 0;
  uint64_t encodeStartUs = 0;
  uint64_t encodeEndUs = 0;
  uint64_t sendUs = 0;
  uint64_t recvUs = 0;
  uint64_t decodeStartUs = 0;
  uint64_t decodeEndUs = 0;
  uint64_t queueSetUs = 0;
  uint64_t decodeToQueueUs = 0;
  uint64_t frameVersion = 0;
  uint64_t presentAtUs = 0;
  {
    std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
    presentAtUs = ctx.frameBuf.frame.presentAtUs;
    if ((ctx.frameBuf.frame.bytes && !ctx.frameBuf.frame.bytes->empty()) || ctx.frameBuf.frame.surfaceTexture) {
      local = ctx.frameBuf.frame.bytes;
      localSurfaceSample = ctx.frameBuf.frame.surfaceSample;
      localSurfaceTexture = ctx.frameBuf.frame.surfaceTexture;
      localSurfaceSubresource = ctx.frameBuf.frame.surfaceSubresource;
      localFormat = ctx.frameBuf.frame.format;
      w = ctx.frameBuf.frame.width;
      h = ctx.frameBuf.frame.height;
      codedW = (ctx.frameBuf.frame.codedWidth > 0) ? ctx.frameBuf.frame.codedWidth : ctx.frameBuf.frame.width;
      codedH = (ctx.frameBuf.frame.codedHeight > 0) ? ctx.frameBuf.frame.codedHeight : ctx.frameBuf.frame.height;
      visL = ctx.frameBuf.frame.visibleLeft;
      visT = ctx.frameBuf.frame.visibleTop;
      seq = ctx.frameBuf.frame.seq;
      frameKey = ctx.frameBuf.frame.key;
      frameSynthetic = ctx.frameBuf.frame.synthetic;
      frameStreamGeneration = ctx.frameBuf.frame.streamGeneration;
      captureUs = ctx.frameBuf.frame.captureUs;
      encodeStartUs = ctx.frameBuf.frame.encodeStartUs;
      encodeEndUs = ctx.frameBuf.frame.encodeEndUs;
      sendUs = ctx.frameBuf.frame.sendUs;
      recvUs = ctx.frameBuf.frame.recvUs;
      decodeStartUs = ctx.frameBuf.frame.decodeStartUs;
      decodeEndUs = ctx.frameBuf.frame.decodeEndUs;
      queueSetUs = ctx.frameBuf.frame.queueSetUs;
      decodeToQueueUs = ctx.frameBuf.frame.decodeToQueueUs;
      frameVersion = ctx.frameBuf.frame.version;
    }
  }
  bool presented = false;
  Nv12RenderTelemetry renderTelemetry{};
  const char* renderPath = "none";
  const char* fallbackReason = "none";
  // Paced playout (F-11): the frame is decoded but its slot on the cadence has not come. Leave
  // what is on screen, re-arm for the remainder, and let the timer bring us back through
  // request_video_paint. The version recheck at the bottom is skipped on purpose -- a newer frame
  // will re-stamp presentAtUs and request its own paint.
  if (!pickerVisible && presentAtUs > 0 && frameVersion != ctx.frameBuf.lastPresentedVersion.load(std::memory_order_relaxed)) {
    const uint64_t nowUs = qpc_now_us();
    if (nowUs < presentAtUs) {
      const uint64_t waitMs = (presentAtUs - nowUs + 999) / 1000;
      SetTimer(hwnd, kPacedPresentTimerId, static_cast<UINT>(std::clamp<uint64_t>(waitMs, 1, 250)), nullptr);
      ++ctx.frameBuf.pacedHoldCount;
      EndPaint(hwnd, &ps);
      return 0;
    }
  }
  if (!pickerVisible && (local || localSurfaceTexture) && w > 0 && h > 0) {
    if (localFormat == SharedFrame::PixelFormat::Nv12) {
      if (!ctx.ui.nv12Renderer.ready) {
        if (!ctx.ui.nv12Renderer.init(hwnd)) {
          ++ctx.present.d3dPresentFailCount;
          ++ctx.present.fallbackInitFailCount;
          fallbackReason = "d3d_init_fail";
        }
      }
      if (ctx.ui.nv12Renderer.ready) {
        if (localSurfaceTexture) {
          presented = ctx.ui.nv12Renderer.render_surface(
              hwnd, contentRect, localSurfaceTexture.Get(), localSurfaceSubresource,
              codedW, codedH, visL, visT, w, h, &renderTelemetry);
        } else {
          presented = ctx.ui.nv12Renderer.render(hwnd, contentRect, local->data(), codedW, codedH,
                                           visL, visT, w, h, &renderTelemetry);
        }
        if (presented) {
          ++ctx.present.d3dPresentSuccessCount;
          renderPath = localSurfaceTexture ? "d3d_nv12_surface" : "d3d_nv12";
        } else {
          ++ctx.present.d3dPresentFailCount;
          ++ctx.present.fallbackRenderFailCount;
          fallbackReason = renderTelemetry.failStage;
        }
      }
      if (!presented && local) {
        std::vector<uint8_t> bgra;
        if (nv12_to_bgra(local->data(), codedW, codedH, &bgra) && !bgra.empty()) {
          // The DIB carries the coded plane; the source rect and a row-offset base
          // pointer select only the visible picture out of it.
          BITMAPINFO bmi{};
          bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
          bmi.bmiHeader.biWidth = static_cast<LONG>(codedW);
          bmi.bmiHeader.biHeight = -static_cast<LONG>(h);
          bmi.bmiHeader.biPlanes = 1;
          bmi.bmiHeader.biBitCount = 32;
          bmi.bmiHeader.biCompression = BI_RGB;
          SetStretchBltMode(hdc, COLORONCOLOR);
          FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
          StretchDIBits(hdc, contentRect.left, contentRect.top,
                        contentRect.right - contentRect.left, contentRect.bottom - contentRect.top,
                        static_cast<int>(visL), 0, static_cast<int>(w), static_cast<int>(h),
                        bgra.data() + static_cast<size_t>(visT) * codedW * 4, &bmi,
                        DIB_RGB_COLORS, SRCCOPY);
          presented = true;
          ++ctx.present.gdiFallbackPresentedCount;
          renderPath = "gdi_nv12_fallback";
        } else {
          ++ctx.present.fallbackNv12ConvertFailCount;
          fallbackReason = "nv12_to_bgra_fail";
        }
      }
    } else if (localFormat == SharedFrame::PixelFormat::Bgra32) {
      BITMAPINFO bmi{};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = static_cast<LONG>(w);
      bmi.bmiHeader.biHeight = -static_cast<LONG>(h);  // top-down
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;
      SetStretchBltMode(hdc, COLORONCOLOR);
      FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
      StretchDIBits(hdc, contentRect.left, contentRect.top,
                    contentRect.right - contentRect.left, contentRect.bottom - contentRect.top,
                    0, 0, static_cast<int>(w), static_cast<int>(h),
                    local->data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
      presented = true;
      renderPath = "gdi_bgra";
    }
  }
  if (presented) {
    ctx.present.hasPresentedAtLeastOneFrame = true;
    ctx.frameBuf.lastPresentedVersion.store(frameVersion, std::memory_order_relaxed);
    // A synthetic (kick / refresh) frame carries the kick time, not a capture time: it must not move
    // the present anchor, or the next real frame a slow host readback delivers late reads as "behind
    // presented" and is dropped with a decoder reset + IDR request (0.2.97).
    if (!frameSynthetic) {
      ctx.frameBuf.lastPresentedCaptureUs.store(captureUs, std::memory_order_relaxed);
    }
    const uint64_t presentUs = qpc_now_us();
    const uint64_t presentGapUs = (ctx.present.lastPresentUs > 0) ? (presentUs - ctx.present.lastPresentUs) : 0;
    const uint64_t queueToPaintUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
    const uint64_t queueToPresentUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
    const uint32_t traceEvery = ctx.present.traceEvery.load();
    const uint32_t traceMax = ctx.present.traceMax.load();
    if (traceEvery > 0 && (seq % traceEvery) == 0 &&
        (traceMax == 0 || ctx.present.tracePresentPrinted.load() < traceMax)) {
      const auto nowPrinted = ctx.present.tracePresentPrinted.fetch_add(1) + 1;
      if (traceMax == 0 || nowPrinted <= traceMax) {
        const uint64_t netUs = (recvUs >= sendUs) ? (recvUs - sendUs) : 0;
        const uint64_t c2eUs = (encodeStartUs >= captureUs) ? (encodeStartUs - captureUs) : 0;
        const uint64_t encUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
        const uint64_t e2sUs = (sendUs >= encodeEndUs) ? (sendUs - encodeEndUs) : 0;
        const uint64_t r2dUs = (decodeStartUs >= recvUs) ? (decodeStartUs - recvUs) : 0;
        const uint64_t decUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
        const uint64_t d2pUs = (presentUs >= decodeEndUs) ? (presentUs - decodeEndUs) : 0;
        const uint64_t renderUs = (presentUs >= recvUs) ? (presentUs - recvUs) : 0;
        const uint64_t queueWaitUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
        const uint64_t paintUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
        const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
        std::ostringstream oss;
        oss << "[native-video-client][trace_present] seq=" << seq
            << " captureUs=" << captureUs
            << " encodeStartUs=" << encodeStartUs
            << " encodeEndUs=" << encodeEndUs
            << " sendUs=" << sendUs
            << " recvUs=" << recvUs
            << " decodeStartUs=" << decodeStartUs
            << " decodeEndUs=" << decodeEndUs
            << " presentUs=" << presentUs
            << " c2eUs=" << c2eUs
            << " encUs=" << encUs
            << " e2sUs=" << e2sUs
            << " netUs=" << netUs
            << " r2dUs=" << r2dUs
            << " decUs=" << decUs
            << " d2pUs=" << d2pUs
            << " decodeToQueueUs=" << decodeToQueueUs
            << " queueWaitUs=" << queueWaitUs
            << " paintUs=" << paintUs
            << " uploadYUs=" << renderTelemetry.uploadYUs
            << " uploadUVUs=" << renderTelemetry.uploadUVUs
            << " drawUs=" << renderTelemetry.drawUs
            << " presentBlockUs=" << renderTelemetry.presentBlockUs
            << " renderUs=" << renderUs
            << " totalUs=" << totalUs
            << " renderPath=" << renderPath
            << " fallbackReason=" << fallbackReason;
        log_client_line(ctx, oss.str());
      }
    }
    // Emitted for every present, not only the ones that crossed a warning threshold.
    // Smoothness is a property of the whole interval distribution: a stream can average a
    // clean 30fps while alternating 16ms and 50ms gaps, which is exactly what a viewer
    // reports as stutter. Gating this behind the warning thresholds left the aggregate
    // reading zero through visibly uneven playback, so there was nothing to optimise
    // against.
    if (ctx.present.lastPresentUs > 0) {
      std::ostringstream gapLine;
      gapLine << "[native-video-client][present] seq=" << seq
              << " frameGapUs=" << presentGapUs;
      log_client_line(ctx, gapLine.str());
    }
    const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
    // GNLink stream telemetry (diagnostics only): one line per presented keyframe, plus any
    // non-key frame whose present interval jumped past 1.5x the expected cadence -- the client
    // side of a periodic stutter. Joins the host 'wire seq=' log by seq+gen; steady play stays
    // quiet. This only observes the timestamps the present path already produced.
    {
      const uint32_t expIntervalUs = ctx.present.presentFrameIntervalUs.load(std::memory_order_relaxed);
      const uint64_t anomalyGapUs =
          (expIntervalUs > 0) ? (static_cast<uint64_t>(expIntervalUs) * 3ULL) / 2ULL : 25000ULL;
      const bool presentAnomaly = (ctx.present.lastPresentUs > 0 && presentGapUs > anomalyGapUs);
      if (frameKey || presentAnomaly) {
        uint64_t presentBacklog = 0;
        {
          std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
          presentBacklog = (ctx.frameBuf.frame.version >= frameVersion) ? (ctx.frameBuf.frame.version - frameVersion) : 0;
        }
        std::ostringstream telem;
        telem << "[native-video-client][telemetry] stage=present"
              << " seq=" << seq
              << " gen=" << frameStreamGeneration
              << " key=" << (frameKey ? 1 : 0)
              << " presentUs=" << presentUs
              << " presentedIntervalUs=" << presentGapUs
              << " presentBacklog=" << presentBacklog
              << " paintUs=" << queueToPresentUs
              << " totalUs=" << totalUs;
        log_client_line(ctx, telem.str());
      }
    }
    if ((totalUs >= kUserFeedbackLagWarnUs || (presentGapUs >= kUserFeedbackGapWarnUs && ctx.present.lastPresentUs > 0)) &&
        (presentUs >= ctx.present.lastUserFeedbackUs + kUserFeedbackMinIntervalUs || ctx.present.lastUserFeedbackUs == 0)) {
      const uint64_t overwriteCountNow = ctx.frameBuf.overwriteBeforePresentCount.load(std::memory_order_relaxed);
      const uint64_t overwriteDelta = (overwriteCountNow >= ctx.present.lastUserFeedbackOverwrite)
                                         ? (overwriteCountNow - ctx.present.lastUserFeedbackOverwrite)
                                         : 0;
      const uint64_t d3dSuccess = ctx.present.d3dPresentSuccessCount.load(std::memory_order_relaxed);
      const uint64_t d3dFail = ctx.present.d3dPresentFailCount.load(std::memory_order_relaxed);
      const uint64_t gdiFallback = ctx.present.gdiFallbackPresentedCount.load(std::memory_order_relaxed);
      const uint64_t paintCoalesced = ctx.frameBuf.paintCoalescedCount.load(std::memory_order_relaxed);
      const uint64_t queueWaitUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
      const uint64_t paintUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
      const uint64_t netUs = (recvUs >= sendUs) ? (recvUs - sendUs) : 0;
      const uint64_t c2eUs = (encodeStartUs >= captureUs) ? (encodeStartUs - captureUs) : 0;
      const uint64_t encUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
      const uint64_t e2sUs = (sendUs >= encodeEndUs) ? (sendUs - encodeEndUs) : 0;
      const uint64_t r2dUs = (decodeStartUs >= recvUs) ? (decodeStartUs - recvUs) : 0;
      const uint64_t decUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
      const uint64_t d2pUs = (presentUs >= decodeEndUs) ? (presentUs - decodeEndUs) : 0;
      std::ostringstream oss;
      oss << "[native-video-client][user-feedback] seq=" << seq
          << " totalUs=" << totalUs
          << " capGapUs=" << presentGapUs
          << " queueToPaintUs=" << queueToPaintUs
          << " queueToPresentUs=" << queueToPresentUs
          << " d3dPresentSuccess=" << d3dSuccess
          << " d3dPresentFail=" << d3dFail
          << " gdiFallback=" << gdiFallback
          << " paintCoalesced=" << paintCoalesced
          << " overwriteDelta=" << overwriteDelta
          << " c2eUs=" << c2eUs
          << " encUs=" << encUs
          << " e2sUs=" << e2sUs
          << " netUs=" << netUs
          << " r2dUs=" << r2dUs
          << " decUs=" << decUs
          << " d2pUs=" << d2pUs
          << " decodeToQueueUs=" << decodeToQueueUs
          << " queueWaitUs=" << queueWaitUs
          << " paintUs=" << paintUs
          << " presentBlockUs=" << renderTelemetry.presentBlockUs
          << " renderPath=" << renderPath
          << " fallbackReason=" << fallbackReason;
      log_client_line(ctx, oss.str());
      ctx.present.lastUserFeedbackUs = presentUs;
      ctx.present.lastUserFeedbackOverwrite = overwriteCountNow;
    }
    ctx.present.lastPresentUs = presentUs;
  } else if (pickerVisible || !ctx.present.hasPresentedAtLeastOneFrame) {
    // Before first successful frame, keep a deterministic background.
    FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  }
  draw_overlay(ctx, hdc);
  EndPaint(hwnd, &ps);
  uint64_t latestVersion = 0;
  {
    std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
    latestVersion = ctx.frameBuf.frame.version;
  }
  if (latestVersion != frameVersion) request_video_paint(ctx, hwnd);
  return 0;
}

}  // namespace remote60::native_poc::viewer
