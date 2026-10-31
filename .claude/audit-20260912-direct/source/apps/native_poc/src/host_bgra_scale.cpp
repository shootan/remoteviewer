// See host_bgra_scale.hpp for the module summary. Bodies below are moved verbatim from
// native_video_host_main.cpp (host split refactor Phase 0-2); no logic change.

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "encode_resolution_ladder.hpp"
#include "host_args.hpp"
#include "host_bgra_scale.hpp"

namespace remote60::native_poc {

uint32_t clamp_even_dim(uint32_t v, uint32_t minValue, uint32_t maxValue) {
  if (v < minValue) v = minValue;
  if (v > maxValue) v = maxValue;
  if (v & 1u) {
    if (v < maxValue) {
      ++v;
    } else if (v > minValue) {
      --v;
    }
  }
  return v;
}

// Fit a source frame inside a target box without changing its aspect ratio. Encoding a
// 4:3 window into a 16:9 box (the shipped profiles) otherwise stretches the picture.
void fit_size_preserving_aspect(uint32_t srcW, uint32_t srcH, uint32_t boxW, uint32_t boxH,
                                uint32_t* outW, uint32_t* outH) {
  if (!outW || !outH) return;
  if (srcW == 0 || srcH == 0 || boxW == 0 || boxH == 0) {
    *outW = boxW;
    *outH = boxH;
    return;
  }
  const double scale = std::min({static_cast<double>(boxW) / static_cast<double>(srcW),
                                 static_cast<double>(boxH) / static_cast<double>(srcH), 1.0});
  *outW = clamp_even_dim(static_cast<uint32_t>(std::lround(srcW * scale)), 2, srcW);
  *outH = clamp_even_dim(static_cast<uint32_t>(std::lround(srcH * scale)), 2, srcH);
}

void choose_h264_encode_size(const Args& args, uint32_t captureW, uint32_t captureH,
                             uint32_t* outW, uint32_t* outH, bool* outAutoFallback720) {
  if (!outW || !outH || !outAutoFallback720) return;
  *outAutoFallback720 = false;
  uint32_t targetW = captureW;
  uint32_t targetH = captureH;
  if (args.encodeWidth > 0 && args.encodeHeight > 0) {
    // Treat the configured size as a bounding box and fit the capture inside it. Clamping
    // each axis on its own squashes the picture whenever the source is not the same aspect
    // as the profile (a 16:10 or 3:2 monitor against the shipped 1920x1080 profiles).
    const double sx = static_cast<double>(args.encodeWidth) / static_cast<double>(captureW);
    const double sy = static_cast<double>(args.encodeHeight) / static_cast<double>(captureH);
    const double scale = std::min({sx, sy, 1.0});
    if (scale > 0.0) {
      targetW = static_cast<uint32_t>(std::lround(captureW * scale));
      targetH = static_cast<uint32_t>(std::lround(captureH * scale));
    }
    targetW = clamp_even_dim(targetW, 2, captureW);
    targetH = clamp_even_dim(targetH, 2, captureH);
  } else {
    // What the bitrate can carry. The threshold used to sit at 1.5 Mbps, which only caught the
    // extremes; 3 Mbps at 1080p was left to spend a quarter of the bits per pixel and showed it
    // whenever the whole screen changed at once. See encode_resolution_ladder.hpp.
    const auto choice =
        remote60::native_poc::choose_encode_resolution(args.bitrate, captureW, captureH, false);
    if (choice.reduced) {
      targetW = choice.width;
      targetH = choice.height;
      *outAutoFallback720 = true;
    }
  }
  *outW = targetW;
  *outH = targetH;
}

void choose_abr_720_size(uint32_t captureW, uint32_t captureH, uint32_t* outW, uint32_t* outH) {
  if (!outW || !outH) return;
  uint32_t targetW = captureW;
  uint32_t targetH = captureH;
  if (captureW > 1280 || captureH > 720) {
    const double sx = 1280.0 / static_cast<double>(captureW);
    const double sy = 720.0 / static_cast<double>(captureH);
    const double scale = std::min(sx, sy);
    if (scale > 0.0 && scale < 1.0) {
      targetW = static_cast<uint32_t>(captureW * scale);
      targetH = static_cast<uint32_t>(captureH * scale);
    }
  }
  targetW = clamp_even_dim(targetW, 2, captureW);
  targetH = clamp_even_dim(targetH, 2, captureH);
  *outW = targetW;
  *outH = targetH;
}

// Fraction of the frame that differs from the previous one, in permille.
//
// Returns 0 if and only if the two frames are byte-identical, so callers can use "0" as an
// exact "nothing moved" test. Anything else reports at least 1.
//
// This deliberately does a full blockwise compare rather than sampling pixels. The previous
// version sampled a few thousand pixels and averaged their intensity delta, which reports 0
// for small localised edits: typing one character changes ~200 of 2 million pixels, so the
// average barely moves and the frame looks static. Gating on that throttles typing. memcmp is
// SIMD-optimised in the CRT and costs far less than the frame copy already being done.
uint32_t estimate_bgra_change_permille(const uint8_t* a, const uint8_t* b, size_t sizeBytes,
                                       uint32_t /*sampleTarget*/) {
  if (!a || !b || sizeBytes == 0) return 1000;
  constexpr size_t kBlockBytes = 4096;
  const size_t blockCount = (sizeBytes + kBlockBytes - 1) / kBlockBytes;
  size_t changedBlocks = 0;
  for (size_t i = 0; i < blockCount; ++i) {
    const size_t offset = i * kBlockBytes;
    const size_t len = std::min(kBlockBytes, sizeBytes - offset);
    if (std::memcmp(a + offset, b + offset, len) != 0) ++changedBlocks;
  }
  if (changedBlocks == 0) return 0;
  const uint64_t permille = (static_cast<uint64_t>(changedBlocks) * 1000ULL) / blockCount;
  return static_cast<uint32_t>(std::clamp<uint64_t>(permille, 1, 1000));
}

// Average 2x2 blocks into one pixel. Bilinear alone only samples 2 taps per axis, so a
// >2x downscale (4K->1080p, 1440p->720p) drops most source pixels and aliases hard on text.
void box_halve_bgra(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t srcStride,
                    std::vector<uint8_t>* out, uint32_t* outW, uint32_t* outH) {
  const uint32_t dstW = std::max<uint32_t>(1, srcW / 2);
  const uint32_t dstH = std::max<uint32_t>(1, srcH / 2);
  out->resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
  uint8_t* dst = out->data();
  for (uint32_t y = 0; y < dstH; ++y) {
    const uint8_t* row0 = src + static_cast<size_t>(std::min(srcH - 1, y * 2)) * srcStride;
    const uint8_t* row1 = src + static_cast<size_t>(std::min(srcH - 1, y * 2 + 1)) * srcStride;
    uint8_t* dstRow = dst + static_cast<size_t>(y) * dstW * 4;
    for (uint32_t x = 0; x < dstW; ++x) {
      const uint32_t x0 = std::min(srcW - 1, x * 2);
      const uint32_t x1 = std::min(srcW - 1, x * 2 + 1);
      const uint8_t* p00 = row0 + static_cast<size_t>(x0) * 4;
      const uint8_t* p10 = row0 + static_cast<size_t>(x1) * 4;
      const uint8_t* p01 = row1 + static_cast<size_t>(x0) * 4;
      const uint8_t* p11 = row1 + static_cast<size_t>(x1) * 4;
      uint8_t* outPx = dstRow + static_cast<size_t>(x) * 4;
      for (int c = 0; c < 4; ++c) {
        outPx[c] = static_cast<uint8_t>((p00[c] + p10[c] + p01[c] + p11[c] + 2) >> 2);
      }
    }
  }
  *outW = dstW;
  *outH = dstH;
}

bool resize_bgra_bilinear(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t srcStride,
                          uint32_t dstW, uint32_t dstH, std::vector<uint8_t>* outBgra) {
  if (!src || srcW == 0 || srcH == 0 || srcStride < (srcW * 4) || dstW == 0 || dstH == 0 || !outBgra) {
    return false;
  }

  std::vector<uint8_t> reduced;
  while (srcW >= dstW * 2 && srcH >= dstH * 2 && srcW > 1 && srcH > 1) {
    std::vector<uint8_t> next;
    uint32_t nextW = 0;
    uint32_t nextH = 0;
    box_halve_bgra(src, srcW, srcH, srcStride, &next, &nextW, &nextH);
    reduced.swap(next);
    src = reduced.data();
    srcW = nextW;
    srcH = nextH;
    srcStride = nextW * 4;
  }
  if (srcW == dstW && srcH == dstH) {
    outBgra->resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
    for (uint32_t y = 0; y < dstH; ++y) {
      std::memcpy(outBgra->data() + static_cast<size_t>(y) * dstW * 4,
                  src + static_cast<size_t>(y) * srcStride, static_cast<size_t>(dstW) * 4);
    }
    return true;
  }
  outBgra->resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
  auto* dst = outBgra->data();
  const uint64_t xScale =
      (dstW > 1) ? ((static_cast<uint64_t>(srcW - 1) << 16) / static_cast<uint64_t>(dstW - 1)) : 0;
  const uint64_t yScale =
      (dstH > 1) ? ((static_cast<uint64_t>(srcH - 1) << 16) / static_cast<uint64_t>(dstH - 1)) : 0;
  for (uint32_t y = 0; y < dstH; ++y) {
    const uint32_t srcYFixed = static_cast<uint32_t>(static_cast<uint64_t>(y) * yScale);
    const uint32_t y0 = std::min<uint32_t>(srcH - 1, srcYFixed >> 16);
    const uint32_t y1 = std::min<uint32_t>(srcH - 1, y0 + 1);
    const uint32_t wy = (srcYFixed & 0xFFFFu) >> 8;
    const uint32_t invWy = 256u - wy;
    const uint8_t* srcRow0 = src + static_cast<size_t>(y0) * srcStride;
    const uint8_t* srcRow1 = src + static_cast<size_t>(y1) * srcStride;
    uint8_t* dstRow = dst + static_cast<size_t>(y) * dstW * 4;
    for (uint32_t x = 0; x < dstW; ++x) {
      const uint32_t srcXFixed = static_cast<uint32_t>(static_cast<uint64_t>(x) * xScale);
      const uint32_t x0 = std::min<uint32_t>(srcW - 1, srcXFixed >> 16);
      const uint32_t x1 = std::min<uint32_t>(srcW - 1, x0 + 1);
      const uint32_t wx = (srcXFixed & 0xFFFFu) >> 8;
      const uint32_t invWx = 256u - wx;

      const uint8_t* p00 = srcRow0 + static_cast<size_t>(x0) * 4;
      const uint8_t* p10 = srcRow0 + static_cast<size_t>(x1) * 4;
      const uint8_t* p01 = srcRow1 + static_cast<size_t>(x0) * 4;
      const uint8_t* p11 = srcRow1 + static_cast<size_t>(x1) * 4;
      uint8_t* outPx = dstRow + static_cast<size_t>(x) * 4;
      for (int c = 0; c < 4; ++c) {
        const uint32_t top = p00[c] * invWx + p10[c] * wx;
        const uint32_t bottom = p01[c] * invWx + p11[c] * wx;
        const uint32_t blended = (top * invWy + bottom * wy + 32768u) >> 16;
        outPx[c] = static_cast<uint8_t>(blended);
      }
    }
  }
  return true;
}

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

// Grab a still preview of one window for the target picker. The capture pipeline only ever
// streams a single target, so previews for the *other* listed windows have no pixel source --
// PrintWindow renders a window into our own DC even while it is occluded, which is what makes
// a thumbnail grid possible without spinning up a capture session per window.
bool capture_window_thumbnail(HWND hwnd, uint32_t maxW, uint32_t maxH,
                              std::vector<uint8_t>* outBgra, uint32_t* outW, uint32_t* outH) {
  if (!outBgra || !outW || !outH) return false;
  outBgra->clear();
  *outW = 0;
  *outH = 0;
  if (maxW == 0 || maxH == 0) return false;

  int srcW = 0;
  int srcH = 0;
  int srcOriginX = 0;
  int srcOriginY = 0;
  const bool desktop = (hwnd == nullptr);
  if (desktop) {
    srcW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    srcH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    srcOriginX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    srcOriginY = GetSystemMetrics(SM_YVIRTUALSCREEN);
  } else {
    if (!IsWindow(hwnd) || IsIconic(hwnd)) return false;
    // PrintWindow delivers WM_PRINT with SendMessage and no timeout; one hung window (a
    // crashed-driver dialog, a stuck installer) wedges the whole control session behind it,
    // and with it every window-select and stream-state message.
    if (IsHungAppWindow(hwnd)) return false;
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return false;
    srcW = rc.right - rc.left;
    srcH = rc.bottom - rc.top;
    if (srcW <= 1 || srcH <= 1) {
      RECT wr{};
      if (!GetWindowRect(hwnd, &wr)) return false;
      srcW = wr.right - wr.left;
      srcH = wr.bottom - wr.top;
    }
  }
  if (srcW <= 1 || srcH <= 1) return false;

  // Decide the output size FIRST and let GDI do the downscale.
  //
  // This used to render at full source size into a DIB, memcpy the whole thing into a vector,
  // and then bilinear-resize on the CPU -- for the desktop preview that is a virtual-screen-sized
  // allocation and copy (tens of MB on a 4K or multi-monitor setup) per request, on the control
  // thread, to produce at most a 256x160 thumbnail. StretchBlt with HALFTONE does the same job in
  // the driver and only the thumbnail-sized bits are ever touched by the CPU. (Ledger H-21.)
  uint32_t dstW = 0;
  uint32_t dstH = 0;
  fit_size_preserving_aspect(static_cast<uint32_t>(srcW), static_cast<uint32_t>(srcH), maxW, maxH,
                             &dstW, &dstH);
  if (dstW == 0 || dstH == 0) return false;

  HDC screenDc = GetDC(nullptr);
  if (!screenDc) return false;

  // Owns an HDC + DIB section pair; the thumbnail path builds one or two of them and every
  // failure path has to unwind both, which is what a scope guard is for.
  struct DibSurface {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ oldBmp = nullptr;
    void* bits = nullptr;
    bool Create(HDC ref, int w, int h) {
      dc = CreateCompatibleDC(ref);
      if (!dc) return false;
      BITMAPINFO bmi{};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = w;
      bmi.bmiHeader.biHeight = -h;  // top-down
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;
      bmp = CreateDIBSection(ref, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
      if (!bmp || !bits) return false;
      oldBmp = SelectObject(dc, bmp);
      return true;
    }
    ~DibSurface() {
      if (dc && oldBmp) SelectObject(dc, oldBmp);
      if (bmp) DeleteObject(bmp);
      if (dc) DeleteDC(dc);
    }
  };

  bool captured = false;
  {
    DibSurface dst;
    if (dst.Create(screenDc, static_cast<int>(dstW), static_cast<int>(dstH))) {
      SetStretchBltMode(dst.dc, HALFTONE);
      SetBrushOrgEx(dst.dc, 0, 0, nullptr);
      if (desktop) {
        captured = (StretchBlt(dst.dc, 0, 0, static_cast<int>(dstW), static_cast<int>(dstH),
                               screenDc, srcOriginX, srcOriginY, srcW, srcH,
                               SRCCOPY | CAPTUREBLT) != FALSE);
      } else {
        // PrintWindow does not scale into a smaller DC, so a window still renders at full size
        // first -- but the downscale and the CPU copy are the thumbnail's, not the window's.
        DibSurface full;
        if (full.Create(screenDc, srcW, srcH)) {
          // PW_RENDERFULLCONTENT is needed for DirectComposition/UWP-backed windows; without it
          // those render blank. It is ignored on older systems.
          bool rendered = (PrintWindow(hwnd, full.dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT) != FALSE);
          if (!rendered) rendered = (PrintWindow(hwnd, full.dc, PW_RENDERFULLCONTENT) != FALSE);
          if (rendered) {
            captured = (StretchBlt(dst.dc, 0, 0, static_cast<int>(dstW), static_cast<int>(dstH),
                                   full.dc, 0, 0, srcW, srcH, SRCCOPY) != FALSE);
          }
        }
      }
      if (captured) {
        // Required before reading a DIB section's bits directly: GDI batches drawing calls per
        // thread, and without the flush the memcpy below can read pixels the BitBlt/StretchBlt
        // has not actually written yet. (Ledger H-21.)
        GdiFlush();
        const size_t stride = static_cast<size_t>(dstW) * 4u;
        outBgra->resize(stride * dstH);
        std::memcpy(outBgra->data(), dst.bits, outBgra->size());
        // PrintWindow leaves alpha at 0 for many windows; force opaque so clients can blit it.
        for (size_t i = 3; i < outBgra->size(); i += 4) (*outBgra)[i] = 0xFF;
      }
    }
  }
  ReleaseDC(nullptr, screenDc);
  if (!captured || outBgra->empty()) {
    outBgra->clear();
    return false;
  }
  *outW = dstW;
  *outH = dstH;
  return true;
}

}  // namespace remote60::native_poc
