// Pins the bitrate-to-resolution ladder.
//
// The rule exists because of a measured artifact: at 3 Mbps and 1080p, a sudden full-screen change
// left the picture visibly wrong for a moment, once every 5-20 seconds. What is checked here is
// mostly the boundaries -- that the bitrates the app actually sends land where they should, that
// a wavering one does not oscillate, and that nothing is ever upscaled.

#include <cstdio>

#include "encode_resolution_ladder.hpp"

using remote60::native_poc::choose_encode_resolution;

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const char* detail = "") {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, *detail ? "  " : "", detail);
  if (!cond) ++gFailures;
}

}  // namespace

int main() {
  char buf[128];

  // The two the client actually sends.
  auto mobile = choose_encode_resolution(3000000, 1920, 1080, false);
  std::snprintf(buf, sizeof(buf), "%ux%u reduced=%d", mobile.width, mobile.height, mobile.reduced);
  check("3 Mbps drops 1080p to 720p", mobile.reduced && mobile.width == 1280 && mobile.height == 720,
        buf);

  auto home = choose_encode_resolution(6000000, 1920, 1080, true);
  std::snprintf(buf, sizeof(buf), "%ux%u reduced=%d", home.width, home.height, home.reduced);
  check("6 Mbps keeps the source resolution", !home.reduced && home.width == 1920, buf);

  // Between the thresholds the previous answer stands, so an encoder restart needs a real move.
  auto drifting_down = choose_encode_resolution(4500000, 1920, 1080, true);
  check("a bitrate between the thresholds keeps 720p once reduced", drifting_down.reduced);
  auto drifting_up = choose_encode_resolution(4500000, 1920, 1080, false);
  check("and keeps full resolution once full", !drifting_up.reduced);

  // Crossing a threshold outright must still move.
  check("dropping below the lower threshold reduces even from full",
        choose_encode_resolution(3000000, 1920, 1080, false).reduced);
  check("rising above the upper threshold restores even from reduced",
        !choose_encode_resolution(8000000, 1920, 1080, true).reduced);

  // A source already at or under the cap has nothing to give up.
  auto small = choose_encode_resolution(1000000, 1280, 720, false);
  std::snprintf(buf, sizeof(buf), "%ux%u reduced=%d", small.width, small.height, small.reduced);
  check("a 720p source is left alone at any bitrate",
        !small.reduced && small.width == 1280 && small.height == 720, buf);
  auto tiny = choose_encode_resolution(1000000, 1024, 768, false);
  std::snprintf(buf, sizeof(buf), "%ux%u", tiny.width, tiny.height);
  check("and a smaller one is never upscaled", tiny.width == 1024 && tiny.height == 768, buf);

  // The budget is an area, so a 16:10 source keeps its shape and lands on the same pixel count
  // rather than being letterboxed into a 16:9 box.
  auto wide = choose_encode_resolution(3000000, 1920, 1200, false);
  std::snprintf(buf, sizeof(buf), "%ux%u ratio=%.3f", wide.width, wide.height,
                static_cast<double>(wide.width) / wide.height);
  const double wideRatio = static_cast<double>(wide.width) / wide.height;
  check("a 16:10 source keeps its aspect ratio",
        wideRatio > 1.59 && wideRatio < 1.61 &&
            wide.width * wide.height <= 1280 * 720 &&
            wide.width * wide.height > (1280 * 720 * 9) / 10,
        buf);

  // Encoders reject odd dimensions.
  auto odd = choose_encode_resolution(3000000, 1919, 1079, false);
  std::snprintf(buf, sizeof(buf), "%ux%u", odd.width, odd.height);
  check("both dimensions come out even", (odd.width % 2) == 0 && (odd.height % 2) == 0, buf);

  auto zero = choose_encode_resolution(3000000, 0, 0, false);
  check("a source of no size is returned untouched", zero.width == 0 && !zero.reduced);

  std::printf(gFailures == 0 ? "\nencode_resolution_ladder_test: PASS\n"
                             : "\nencode_resolution_ladder_test: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
