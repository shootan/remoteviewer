// ka-pc V1 (Codex evidence candidate r2): what the managed e2e accepts as "the picker is visible" and "a fresh frame
// after the selection" -- judged on the window's CLIENT area and on the card the current activation's own log names,
// never on a whole-window color count (a white window with its title bar passed that: 9 colors > 4, 10-28/29).
//
// Why the capture changed (ka_pc_wgc_offscreen_probe, 2026-10-29): a fresh activation window has no swapchain present
// yet, so the viewer draws its picker with GDI into the window's DC (viewer_present.cpp, the else branch). For a
// window off every monitor -- where this test keeps it -- WGC returns GDI content as an all-white surface (probe C/C2:
// white even when repainted during the capture), while a flip-swapchain present is returned as drawn (probe A/B).
// PrintWindow(PW_CLIENTONLY | PW_RENDERFULLCONTENT) returns the GDI content of that same off-screen window (probe D).
// So: the picker is captured by PrintWindow (its client area), the video after the selection by WGC as before.
// What this does NOT show: the picture on a physical monitor (the window is never put on one -- the user's screen).
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ka_pc_e2e {

struct Image {
  int w = 0, h = 0;
  std::vector<uint32_t> px;  // 0x00RRGGBB per pixel (alpha dropped), top-down
  uint32_t at(int x, int y) const { return px[static_cast<size_t>(y) * w + x] & 0xFFFFFF; }
};

constexpr uint32_t kPickerBackground = 0x0D0F14;  // draw_overlay: draw_alpha_rect(clientRect, RGB(13, 15, 20))
constexpr uint32_t kCardFrame = 0x343A46;         // draw_card: FrameRect RGB(52, 58, 70) (not the active card)
constexpr uint32_t kCardFrameActive = 0x3B82F6;   // draw_card: RGB(59, 130, 246) (the active card)

inline bool near_color(uint32_t a, uint32_t b, int tol) {
  for (int s = 0; s < 24; s += 8) {
    const int d = static_cast<int>((a >> s) & 0xFF) - static_cast<int>((b >> s) & 0xFF);
    if (d > tol || d < -tol) return false;
  }
  return true;
}

// "The picker is visible" (empty = yes; otherwise why not): on the client-area image,
//   1. the picker's background fills most of the area outside the card (>= 50% of samples within +-6 per channel);
//   2. the desktop card the CURRENT activation logged (client pixels) lies inside the image, its frame carries the card
//      frame color on at least half of its sampled perimeter, and its inside is drawn (>= 3 distinct colors).
inline std::string picker_visible_why(const Image& img, const RECT& card) {
  if (img.w <= 0 || img.h <= 0 || img.px.size() != static_cast<size_t>(img.w) * img.h) return "no image";
  if (card.left < 0 || card.top < 0 || card.right > img.w || card.bottom > img.h || card.right - card.left < 20 ||
      card.bottom - card.top < 20)
    return "the logged desktop card is not inside the client area";
  size_t bg = 0, outside = 0;
  for (int y = 0; y < img.h; y += 4)
    for (int x = 0; x < img.w; x += 4) {
      if (x >= card.left && x < card.right && y >= card.top && y < card.bottom) continue;
      ++outside;
      if (near_color(img.at(x, y), kPickerBackground, 6)) ++bg;
    }
  if (outside == 0 || bg * 2 < outside) {
    char b[160];
    std::snprintf(b, sizeof(b), "the picker background covers %zu of %zu samples outside the card (needs half)", bg, outside);
    return b;
  }
  size_t frameHits = 0, frameSamples = 0;
  auto frame_at = [&](int x, int y) {
    ++frameSamples;
    const uint32_t v = img.at(x, y);
    if (near_color(v, kCardFrame, 6) || near_color(v, kCardFrameActive, 6)) ++frameHits;
  };
  for (int x = card.left + 4; x < card.right - 4; x += 4) {
    frame_at(x, card.top);
    frame_at(x, card.bottom - 1);
  }
  for (int y = card.top + 4; y < card.bottom - 4; y += 4) {
    frame_at(card.left, y);
    frame_at(card.right - 1, y);
  }
  if (frameHits * 2 < frameSamples) {
    char b[160];
    std::snprintf(b, sizeof(b), "the desktop card's frame color on %zu of %zu perimeter samples (needs half)", frameHits,
                  frameSamples);
    return b;
  }
  std::vector<uint32_t> inside;
  for (int y = card.top + 3; y < card.bottom - 3; y += 3)
    for (int x = card.left + 3; x < card.right - 3; x += 3) {
      const uint32_t v = img.at(x, y) & 0xF0F0F0;
      bool seen = false;
      for (uint32_t s : inside) seen = seen || s == v;
      if (!seen) inside.push_back(v);
      if (inside.size() >= 3) return {};
    }
  return "the desktop card's inside is one or two flat colors (not drawn)";
}

// "A fresh frame after the selection" (empty = yes): an image that is a picture -- >= 16 distinct coarse colors over
// its interior -- and not the picker (its background does not cover half of it).
inline std::string video_frame_why(const Image& img) {
  if (img.w <= 40 || img.h <= 80 || img.px.size() != static_cast<size_t>(img.w) * img.h) return "no image";
  std::vector<uint32_t> seen;
  size_t bg = 0, total = 0;
  for (int y = 40; y < img.h - 10; y += 5)
    for (int x = 10; x < img.w - 10; x += 5) {
      const uint32_t v = img.at(x, y);
      ++total;
      if (near_color(v, kPickerBackground, 6)) ++bg;
      const uint32_t c = v & 0xF0F0F0;
      bool hit = false;
      for (uint32_t s : seen) hit = hit || s == c;
      if (!hit && seen.size() < 64) seen.push_back(c);
    }
  if (bg * 2 >= total) return "it is the picker's background, not a picture";
  if (seen.size() < 16) return "only " + std::to_string(seen.size()) + " coarse colors over the interior (a flat image)";
  return {};
}

// The window's client area, as the window renders it, without moving or showing it: PrintWindow with
// PW_CLIENTONLY | PW_RENDERFULLCONTENT into a 32-bit DIB.
inline bool print_client(HWND hwnd, Image* out, std::string* why) {
  RECT r{};
  if (!GetClientRect(hwnd, &r) || r.right <= 0 || r.bottom <= 0) {
    *why = "no client area";
    return false;
  }
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = r.right;
  bi.bmiHeader.biHeight = -r.bottom;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  bool ok = false;
  if (bmp && bits) {
    HGDIOBJ prev = SelectObject(mem, bmp);
    ok = PrintWindow(hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT) != 0;
    GdiFlush();
    if (ok) {
      out->w = r.right;
      out->h = r.bottom;
      const uint32_t* p = static_cast<const uint32_t*>(bits);
      out->px.assign(p, p + static_cast<size_t>(r.right) * r.bottom);
      for (auto& v : out->px) v &= 0xFFFFFF;
    } else {
      *why = "PrintWindow failed (" + std::to_string(GetLastError()) + ")";
    }
    SelectObject(mem, prev);
    DeleteObject(bmp);
  } else {
    *why = "no DIB";
  }
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  return ok;
}

// A 24/32-bit BMP (as e2e_save_bmp writes them) as an Image; `crop` cuts a region (window captures: the client part).
inline bool load_bmp(const std::wstring& path, Image* out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  GetFileSizeEx(h, &size);
  std::vector<uint8_t> b(static_cast<size_t>(size.QuadPart));
  DWORD got = 0;
  const bool read = ReadFile(h, b.data(), static_cast<DWORD>(b.size()), &got, nullptr) && got == b.size();
  CloseHandle(h);
  if (!read || b.size() < 54 || b[0] != 'B' || b[1] != 'M') return false;
  const uint32_t off = *reinterpret_cast<const uint32_t*>(&b[10]);
  const int32_t w = *reinterpret_cast<const int32_t*>(&b[18]);
  const int32_t hh = *reinterpret_cast<const int32_t*>(&b[22]);
  const uint16_t bpp = *reinterpret_cast<const uint16_t*>(&b[28]);
  if (w <= 0 || hh == 0 || (bpp != 24 && bpp != 32)) return false;
  const int H = hh < 0 ? -hh : hh;
  const size_t stride = ((static_cast<size_t>(w) * bpp / 8) + 3) / 4 * 4;
  if (off + stride * H > b.size()) return false;
  out->w = w;
  out->h = H;
  out->px.resize(static_cast<size_t>(w) * H);
  for (int y = 0; y < H; ++y) {
    const uint8_t* row = &b[off + stride * static_cast<size_t>(hh < 0 ? y : H - 1 - y)];
    for (int x = 0; x < w; ++x) {
      const uint8_t* p = row + static_cast<size_t>(x) * (bpp / 8);
      out->px[static_cast<size_t>(y) * w + x] = (static_cast<uint32_t>(p[2]) << 16) | (p[1] << 8) | p[0];
    }
  }
  return true;
}

// The region (l, t, r, b) of an image.
inline Image crop(const Image& in, int l, int t, int r, int b) {
  Image o;
  if (l < 0 || t < 0 || r > in.w || b > in.h || r <= l || b <= t) return o;
  o.w = r - l;
  o.h = b - t;
  o.px.reserve(static_cast<size_t>(o.w) * o.h);
  for (int y = t; y < b; ++y)
    for (int x = l; x < r; ++x) o.px.push_back(in.at(x, y));
  return o;
}

// e2e_save_bmp's counterpart for an Image (32-bit, top-down).
inline bool save_image_bmp(const std::wstring& path, const Image& img) {
  BITMAPFILEHEADER fh{};
  BITMAPINFOHEADER ih{};
  ih.biSize = sizeof(ih);
  ih.biWidth = img.w;
  ih.biHeight = -img.h;
  ih.biPlanes = 1;
  ih.biBitCount = 32;
  const DWORD bytes = static_cast<DWORD>(img.px.size() * 4);
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(ih);
  fh.bfSize = fh.bfOffBits + bytes;
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD put = 0;
  bool ok = WriteFile(h, &fh, sizeof(fh), &put, nullptr) && WriteFile(h, &ih, sizeof(ih), &put, nullptr) &&
            WriteFile(h, img.px.data(), bytes, &put, nullptr) && put == bytes;
  CloseHandle(h);
  return ok;
}

}  // namespace ka_pc_e2e
