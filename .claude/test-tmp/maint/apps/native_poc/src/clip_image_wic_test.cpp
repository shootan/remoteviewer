// clip-image: the WIC side -- DIB -> PNG -> CF_DIBV5 keeps every pixel and alpha, dimensions come
// from the PNG header before any decode, and a PNG of another size is refused.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "clip_image_core.hpp"
#include "clip_image_wic.hpp"

using namespace remote60::native_poc;

namespace {
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

uint32_t px(uint32_t x, uint32_t y) {  // BGRA with a varying alpha, including 0 and 255
  const uint8_t a = static_cast<uint8_t>((x * 37 + y * 11) & 0xFF);
  return (static_cast<uint32_t>(a) << 24) | ((x * 7 & 0xFF) << 16) | ((y * 13 & 0xFF) << 8) | ((x + y) & 0xFF);
}

// A V5 DIB, BI_BITFIELDS with an alpha mask, bottom-up (the common clipboard layout).
std::vector<uint8_t> make_v5(uint32_t w, uint32_t h) {
  std::vector<uint8_t> b(sizeof(BITMAPV5HEADER) + static_cast<size_t>(w) * h * 4);
  BITMAPV5HEADER hdr{};
  hdr.bV5Size = sizeof(hdr);
  hdr.bV5Width = static_cast<LONG>(w);
  hdr.bV5Height = static_cast<LONG>(h);
  hdr.bV5Planes = 1;
  hdr.bV5BitCount = 32;
  hdr.bV5Compression = BI_BITFIELDS;
  hdr.bV5RedMask = 0x00FF0000;
  hdr.bV5GreenMask = 0x0000FF00;
  hdr.bV5BlueMask = 0x000000FF;
  hdr.bV5AlphaMask = 0xFF000000;
  hdr.bV5CSType = LCS_sRGB;
  std::memcpy(b.data(), &hdr, sizeof(hdr));
  auto* rows = reinterpret_cast<uint32_t*>(b.data() + sizeof(hdr));
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) rows[(h - 1 - y) * w + x] = px(x, y);  // bottom-up
  return b;
}

// A 24 bpp BITMAPINFOHEADER DIB (no alpha), bottom-up, rows padded to 4 bytes.
std::vector<uint8_t> make_24(uint32_t w, uint32_t h) {
  const uint32_t stride = (w * 3 + 3) & ~3u;
  std::vector<uint8_t> b(sizeof(BITMAPINFOHEADER) + static_cast<size_t>(stride) * h);
  BITMAPINFOHEADER hdr{};
  hdr.biSize = sizeof(hdr);
  hdr.biWidth = static_cast<LONG>(w);
  hdr.biHeight = static_cast<LONG>(h);
  hdr.biPlanes = 1;
  hdr.biBitCount = 24;
  std::memcpy(b.data(), &hdr, sizeof(hdr));
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      uint8_t* p = b.data() + sizeof(hdr) + (h - 1 - y) * stride + x * 3;
      const uint32_t c = px(x, y);
      p[0] = c & 0xFF;
      p[1] = (c >> 8) & 0xFF;
      p[2] = (c >> 16) & 0xFF;
    }
  return b;
}
}  // namespace

int main() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  {
    const uint32_t W = 37, H = 23;  // odd sizes: stride and row order both matter
    auto dib = make_v5(W, H);
    std::vector<uint8_t> png;
    uint32_t w = 0, h = 0;
    const ClipWicResult enc = clip_dib_to_png(dib.data(), dib.size(), &png, &w, &h);
    check("V5 32 bpp DIB with alpha encodes to PNG", enc == ClipWicResult::Ok && w == W && h == H && png.size() > 8);
    uint32_t pw = 0, ph = 0;
    check("PNG dimensions come from its header", clip_png_dimensions(png.data(), png.size(), &pw, &ph) == ClipWicResult::Ok &&
                                                     pw == W && ph == H);
    HGLOBAL g = nullptr;
    const ClipWicResult dec = clip_png_to_dibv5(png.data(), png.size(), W, H, &g);
    check("PNG decodes into a CF_DIBV5 HGLOBAL", dec == ClipWicResult::Ok && g != nullptr);
    if (g) {
      auto* base = static_cast<const uint8_t*>(GlobalLock(g));
      const DibInfo info = validate_dib(base, GlobalSize(g));
      check("the published DIBV5 passes our own header check (top-down, alpha mask, sRGB)",
            info.ok() && info.topDown && info.bitCount == 32 && info.masks[3] == 0xFF000000u);
      const auto* rows = reinterpret_cast<const uint32_t*>(base + info.pixelOffset);
      uint32_t bad = 0, alphaZero = 0, alphaFull = 0;
      for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
          const uint32_t want = px(x, y);
          if (rows[y * W + x] != want) ++bad;
          alphaZero += (want >> 24) == 0;
          alphaFull += (want >> 24) == 255;
        }
      check("every pixel and alpha survives DIB -> PNG -> DIBV5 (" + std::to_string(W * H) + " px)", bad == 0);
      check("the pattern really exercised alpha 0 and 255", alphaZero > 0 && alphaFull > 0);
      GlobalUnlock(g);
      GlobalFree(g);
    }
    HGLOBAL wrong = nullptr;
    check("a PNG whose size differs from the offer is refused before decode",
          clip_png_to_dibv5(png.data(), png.size(), W + 1, H, &wrong) == ClipWicResult::SizeMismatch && wrong == nullptr);
  }
  {
    auto dib = make_24(31, 17);
    std::vector<uint8_t> png;
    uint32_t w = 0, h = 0;
    check("24 bpp padded DIB encodes", clip_dib_to_png(dib.data(), dib.size(), &png, &w, &h) == ClipWicResult::Ok);
    HGLOBAL g = nullptr;
    clip_png_to_dibv5(png.data(), png.size(), 31, 17, &g);
    bool ok = g != nullptr;
    if (g) {
      auto* base = static_cast<const uint8_t*>(GlobalLock(g));
      const auto* rows = reinterpret_cast<const uint32_t*>(base + sizeof(BITMAPV5HEADER));
      for (uint32_t y = 0; y < 17 && ok; ++y)
        for (uint32_t x = 0; x < 31 && ok; ++x) ok = (rows[y * 31 + x] & 0x00FFFFFF) == (px(x, y) & 0x00FFFFFF) &&
                                                    (rows[y * 31 + x] >> 24) == 0xFF;
      GlobalUnlock(g);
      GlobalFree(g);
    }
    check("24 bpp colours survive and come out opaque", ok);
  }
  {
    auto dib = make_v5(4, 4);
    BITMAPV5HEADER* hdr = reinterpret_cast<BITMAPV5HEADER*>(dib.data());
    hdr->bV5CSType = PROFILE_EMBEDDED;
    std::vector<uint8_t> png;
    uint32_t w = 0, h = 0;
    check("an embedded colour profile is refused, not recoloured",
          clip_dib_to_png(dib.data(), dib.size(), &png, &w, &h) == ClipWicResult::BadDib);
    const uint8_t junk[16] = {1, 2, 3};
    uint32_t pw = 0, ph = 0;
    check("bytes that are not a PNG are refused", clip_png_dimensions(junk, sizeof(junk), &pw, &ph) == ClipWicResult::NotAnImage);
  }
  CoUninitialize();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
