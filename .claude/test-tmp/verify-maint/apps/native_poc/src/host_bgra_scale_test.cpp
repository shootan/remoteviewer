// Thumbnail capture (ledger H-21) + the pure size fit it depends on.
//
// capture_window_thumbnail talks to GDI, so this exercises the real path on the desktop of the
// machine it runs on. Two things it is meant to catch:
//  - a missing GdiFlush before reading the DIB section's bits, which shows up as a thumbnail that
//    is uniformly blank because the batched StretchBlt had not landed yet;
//  - the StretchBlt downscale silently producing the wrong geometry.
// On a session with no desktop (headless agent) it reports SKIP rather than failing.

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <vector>

#include "host_bgra_scale.hpp"

using remote60::native_poc::capture_window_thumbnail;
using remote60::native_poc::fit_size_preserving_aspect;

namespace {

int gFailures = 0;

void expect(bool ok, const char* what) {
  if (ok) return;
  std::printf("  FAIL %s\n", what);
  ++gFailures;
}

void test_fit() {
  std::printf("fit_size_preserving_aspect\n");
  uint32_t w = 0, h = 0;
  fit_size_preserving_aspect(1920, 1080, 256, 160, &w, &h);
  expect(w <= 256 && h <= 160, "fits inside the box");
  expect(w > 0 && h > 0, "non-degenerate");
  // 16:9 into a 16:10 box is width-bound.
  expect(w == 256, "16:9 into 256x160 is width-bound");

  fit_size_preserving_aspect(1000, 2000, 256, 160, &w, &h);
  expect(w <= 256 && h <= 160, "portrait fits inside the box");
  expect(h == 160, "portrait is height-bound");
}

void test_desktop_thumbnail() {
  std::printf("capture_window_thumbnail(desktop)\n");
  const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (vw <= 1 || vh <= 1) {
    std::printf("  SKIP no desktop in this session (%dx%d)\n", vw, vh);
    return;
  }

  std::vector<uint8_t> bgra;
  uint32_t w = 0, h = 0;
  const bool ok = capture_window_thumbnail(nullptr, 256, 160, &bgra, &w, &h);
  expect(ok, "desktop thumbnail captured");
  if (!ok) return;

  expect(w > 0 && h > 0, "non-zero size");
  expect(w <= 256 && h <= 160, "within the requested box");
  expect(bgra.size() == static_cast<size_t>(w) * h * 4u, "buffer matches w*h*4");

  uint32_t expectW = 0, expectH = 0;
  fit_size_preserving_aspect(static_cast<uint32_t>(vw), static_cast<uint32_t>(vh), 256, 160,
                             &expectW, &expectH);
  expect(w == expectW && h == expectH, "size matches the aspect fit of the virtual screen");

  bool alphaOpaque = true;
  for (size_t i = 3; i < bgra.size(); i += 4) {
    if (bgra[i] != 0xFF) { alphaOpaque = false; break; }
  }
  expect(alphaOpaque, "alpha forced opaque");

  // The GdiFlush regression: without it the bits can still be the untouched DIB (all zero) or a
  // partially-written surface. A real desktop is not one flat colour.
  bool varied = false;
  const uint8_t first = bgra.empty() ? 0 : bgra[0];
  for (size_t i = 0; i < bgra.size(); i += 4) {
    if (bgra[i] != first) { varied = true; break; }
  }
  expect(varied, "pixels actually written (not a uniform/unflushed surface)");
}

void test_rejects_bad_args() {
  std::printf("argument guards\n");
  std::vector<uint8_t> bgra;
  uint32_t w = 0, h = 0;
  expect(!capture_window_thumbnail(nullptr, 0, 160, &bgra, &w, &h), "zero maxW rejected");
  expect(!capture_window_thumbnail(nullptr, 256, 0, &bgra, &w, &h), "zero maxH rejected");
  expect(!capture_window_thumbnail(nullptr, 256, 160, nullptr, &w, &h), "null out rejected");
  // A window handle that is not a window must not be blitted.
  expect(!capture_window_thumbnail(reinterpret_cast<HWND>(static_cast<uintptr_t>(0xDEAD)), 256, 160,
                                   &bgra, &w, &h),
         "bogus HWND rejected");
}

}  // namespace

int main() {
  test_fit();
  test_desktop_thumbnail();
  test_rejects_bad_args();
  if (gFailures == 0) {
    std::printf("host_bgra_scale_test: PASS\n");
    return 0;
  }
  std::printf("host_bgra_scale_test: FAIL (%d)\n", gFailures);
  return 1;
}
