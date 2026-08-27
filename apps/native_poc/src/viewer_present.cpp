// See viewer_present.hpp. Body is the WM_PAINT case of WndProc (native_video_client_main.cpp ->
// viewer_window_proc.cpp), verbatim (viewer split refactor Phase 2-8).

#include "viewer_present.hpp"

#include <iostream>
#include <sstream>

#include "viewer_gdi_util.hpp"
#include "viewer_globals.hpp"
#include "viewer_layout.hpp"
#include "viewer_log.hpp"
#include "viewer_nv12_renderer.hpp"
#include "viewer_overlay_draw.hpp"

namespace remote60::native_poc::viewer {

LRESULT paint_video_frame(HWND hwnd) {
  gFrameBuf.paintQueued = false;
  PAINTSTRUCT ps{};
  HDC hdc = BeginPaint(hwnd, &ps);
  const uint64_t paintStartUs = qpc_now_us();
  const ClientLayout layout = compute_client_layout(hwnd);
  const RECT& videoRect = layout.videoRect;
  const RECT contentRect = resolve_video_content_rect(hwnd, videoRect);
  const bool pickerVisible = gPicker.visible.load(std::memory_order_relaxed);

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
  {
    std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
    if ((gFrameBuf.frame.bytes && !gFrameBuf.frame.bytes->empty()) || gFrameBuf.frame.surfaceTexture) {
      local = gFrameBuf.frame.bytes;
      localSurfaceSample = gFrameBuf.frame.surfaceSample;
      localSurfaceTexture = gFrameBuf.frame.surfaceTexture;
      localSurfaceSubresource = gFrameBuf.frame.surfaceSubresource;
      localFormat = gFrameBuf.frame.format;
      w = gFrameBuf.frame.width;
      h = gFrameBuf.frame.height;
      codedW = (gFrameBuf.frame.codedWidth > 0) ? gFrameBuf.frame.codedWidth : gFrameBuf.frame.width;
      codedH = (gFrameBuf.frame.codedHeight > 0) ? gFrameBuf.frame.codedHeight : gFrameBuf.frame.height;
      visL = gFrameBuf.frame.visibleLeft;
      visT = gFrameBuf.frame.visibleTop;
      seq = gFrameBuf.frame.seq;
      frameKey = gFrameBuf.frame.key;
      frameStreamGeneration = gFrameBuf.frame.streamGeneration;
      captureUs = gFrameBuf.frame.captureUs;
      encodeStartUs = gFrameBuf.frame.encodeStartUs;
      encodeEndUs = gFrameBuf.frame.encodeEndUs;
      sendUs = gFrameBuf.frame.sendUs;
      recvUs = gFrameBuf.frame.recvUs;
      decodeStartUs = gFrameBuf.frame.decodeStartUs;
      decodeEndUs = gFrameBuf.frame.decodeEndUs;
      queueSetUs = gFrameBuf.frame.queueSetUs;
      decodeToQueueUs = gFrameBuf.frame.decodeToQueueUs;
      frameVersion = gFrameBuf.frame.version;
    }
  }
  bool presented = false;
  Nv12RenderTelemetry renderTelemetry{};
  const char* renderPath = "none";
  const char* fallbackReason = "none";
  if (!pickerVisible && (local || localSurfaceTexture) && w > 0 && h > 0) {
    if (localFormat == SharedFrame::PixelFormat::Nv12) {
      if (!gUi.nv12Renderer.ready) {
        if (!gUi.nv12Renderer.init(hwnd)) {
          ++gPresent.d3dPresentFailCount;
          ++gPresent.fallbackInitFailCount;
          fallbackReason = "d3d_init_fail";
        }
      }
      if (gUi.nv12Renderer.ready) {
        if (localSurfaceTexture) {
          presented = gUi.nv12Renderer.render_surface(
              hwnd, contentRect, localSurfaceTexture.Get(), localSurfaceSubresource,
              codedW, codedH, visL, visT, w, h, &renderTelemetry);
        } else {
          presented = gUi.nv12Renderer.render(hwnd, contentRect, local->data(), codedW, codedH,
                                           visL, visT, w, h, &renderTelemetry);
        }
        if (presented) {
          ++gPresent.d3dPresentSuccessCount;
          renderPath = localSurfaceTexture ? "d3d_nv12_surface" : "d3d_nv12";
        } else {
          ++gPresent.d3dPresentFailCount;
          ++gPresent.fallbackRenderFailCount;
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
          ++gPresent.gdiFallbackPresentedCount;
          renderPath = "gdi_nv12_fallback";
        } else {
          ++gPresent.fallbackNv12ConvertFailCount;
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
    gPresent.hasPresentedAtLeastOneFrame = true;
    gFrameBuf.lastPresentedVersion.store(frameVersion, std::memory_order_relaxed);
    gFrameBuf.lastPresentedCaptureUs.store(captureUs, std::memory_order_relaxed);
    const uint64_t presentUs = qpc_now_us();
    const uint64_t presentGapUs = (gPresent.lastPresentUs > 0) ? (presentUs - gPresent.lastPresentUs) : 0;
    const uint64_t queueToPaintUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
    const uint64_t queueToPresentUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
    const uint32_t traceEvery = gPresent.traceEvery.load();
    const uint32_t traceMax = gPresent.traceMax.load();
    if (traceEvery > 0 && (seq % traceEvery) == 0 &&
        (traceMax == 0 || gPresent.tracePresentPrinted.load() < traceMax)) {
      const auto nowPrinted = gPresent.tracePresentPrinted.fetch_add(1) + 1;
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
        log_client_line(oss.str());
      }
    }
    // Emitted for every present, not only the ones that crossed a warning threshold.
    // Smoothness is a property of the whole interval distribution: a stream can average a
    // clean 30fps while alternating 16ms and 50ms gaps, which is exactly what a viewer
    // reports as stutter. Gating this behind the warning thresholds left the aggregate
    // reading zero through visibly uneven playback, so there was nothing to optimise
    // against.
    if (gPresent.lastPresentUs > 0) {
      std::ostringstream gapLine;
      gapLine << "[native-video-client][present] seq=" << seq
              << " frameGapUs=" << presentGapUs;
      log_client_line(gapLine.str());
    }
    const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
    // GNLink stream telemetry (diagnostics only): one line per presented keyframe, plus any
    // non-key frame whose present interval jumped past 1.5x the expected cadence -- the client
    // side of a periodic stutter. Joins the host 'wire seq=' log by seq+gen; steady play stays
    // quiet. This only observes the timestamps the present path already produced.
    {
      const uint32_t expIntervalUs = gPresent.presentFrameIntervalUs.load(std::memory_order_relaxed);
      const uint64_t anomalyGapUs =
          (expIntervalUs > 0) ? (static_cast<uint64_t>(expIntervalUs) * 3ULL) / 2ULL : 25000ULL;
      const bool presentAnomaly = (gPresent.lastPresentUs > 0 && presentGapUs > anomalyGapUs);
      if (frameKey || presentAnomaly) {
        uint64_t presentBacklog = 0;
        {
          std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
          presentBacklog = (gFrameBuf.frame.version >= frameVersion) ? (gFrameBuf.frame.version - frameVersion) : 0;
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
        log_client_line(telem.str());
      }
    }
    if ((totalUs >= kUserFeedbackLagWarnUs || (presentGapUs >= kUserFeedbackGapWarnUs && gPresent.lastPresentUs > 0)) &&
        (presentUs >= gPresent.lastUserFeedbackUs + kUserFeedbackMinIntervalUs || gPresent.lastUserFeedbackUs == 0)) {
      const uint64_t overwriteCountNow = gFrameBuf.overwriteBeforePresentCount.load(std::memory_order_relaxed);
      const uint64_t overwriteDelta = (overwriteCountNow >= gPresent.lastUserFeedbackOverwrite)
                                         ? (overwriteCountNow - gPresent.lastUserFeedbackOverwrite)
                                         : 0;
      const uint64_t d3dSuccess = gPresent.d3dPresentSuccessCount.load(std::memory_order_relaxed);
      const uint64_t d3dFail = gPresent.d3dPresentFailCount.load(std::memory_order_relaxed);
      const uint64_t gdiFallback = gPresent.gdiFallbackPresentedCount.load(std::memory_order_relaxed);
      const uint64_t paintCoalesced = gFrameBuf.paintCoalescedCount.load(std::memory_order_relaxed);
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
      log_client_line(oss.str());
      gPresent.lastUserFeedbackUs = presentUs;
      gPresent.lastUserFeedbackOverwrite = overwriteCountNow;
    }
    gPresent.lastPresentUs = presentUs;
  } else if (pickerVisible || !gPresent.hasPresentedAtLeastOneFrame) {
    // Before first successful frame, keep a deterministic background.
    FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  }
  draw_overlay(hdc);
  EndPaint(hwnd, &ps);
  uint64_t latestVersion = 0;
  {
    std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
    latestVersion = gFrameBuf.frame.version;
  }
  if (!pickerVisible && latestVersion != frameVersion) {
    if (!gFrameBuf.paintQueued.exchange(true)) {
      InvalidateRect(hwnd, nullptr, FALSE);
    } else {
      ++gFrameBuf.paintCoalescedCount;
    }
  }
  return 0;
}

}  // namespace remote60::native_poc::viewer
