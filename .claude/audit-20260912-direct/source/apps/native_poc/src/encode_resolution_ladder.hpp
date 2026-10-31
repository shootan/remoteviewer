#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace remote60::native_poc {

// How much picture a bitrate can actually carry.
//
// A bitrate is a budget for the whole frame, so the same 3 Mbps buys four times as much per pixel
// at 720p as at 1080p. On a static desktop that difference is invisible -- there is almost nothing
// to encode either way. It shows up when the whole screen changes at once: a game menu opening,
// a window maximising. The encoder cannot spend what it does not have, so it predicts the new
// frame from the old one as best it can, and for a moment edges and patterns sit slightly wrong
// before the next frames correct them. Measured here: at 3 Mbps and 1080p, once every 5 to 20
// seconds of ordinary use; at 8 Mbps, gone.
//
// Raising the bitrate is not always available -- mobile data is metered, and the user picking
// 3 Mbps means 3 Mbps. Lowering the resolution buys the same headroom for free, and on a phone
// screen 720p costs little that can be seen.
//
// Hysteresis, because the two thresholds are not the same number: a session hovering between
// them would otherwise reinitialise the encoder every time the client nudged its bitrate, and
// each of those is a visible hitch.

constexpr uint32_t kLadderFullResMinBitrate = 5000000;   // at or above: whatever the source is
constexpr uint32_t kLadderReducedMaxBitrate = 4000000;   // at or below: cap at the budget below
// Expressed as an area rather than a width and a height, because what the encoder is short of is
// bits per pixel. A 1024x768 source is taller than 720 but has fewer pixels than 720p, and fitting
// it into a 1280x720 box would shrink it for no gain.
constexpr uint32_t kLadderReducedPixels = 1280u * 720u;

struct EncodeResolutionChoice {
  uint32_t width = 0;
  uint32_t height = 0;
  bool reduced = false;   // true when the cap, not the source, decided the size
};

/**
 * The encode size for a bitrate: the source, scaled down only far enough to fit the pixel budget.
 *
 * Aspect ratio is preserved, so a 16:10 monitor stays 16:10 rather than being letterboxed into a
 * 16:9 box. Never upscales, and never shrinks a source that is already within budget.
 *
 * `currentlyReduced` is the answer given last time; between the two thresholds it is returned
 * unchanged, which is what keeps a wavering bitrate from restarting the encoder repeatedly.
 */
inline EncodeResolutionChoice choose_encode_resolution(uint32_t bitrate, uint32_t captureW,
                                                       uint32_t captureH, bool currentlyReduced) {
  EncodeResolutionChoice out{captureW, captureH, false};
  if (captureW == 0 || captureH == 0) return out;

  bool reduce = currentlyReduced;
  if (bitrate >= kLadderFullResMinBitrate) reduce = false;
  else if (bitrate <= kLadderReducedMaxBitrate) reduce = true;
  if (!reduce) return out;

  const double pixels = static_cast<double>(captureW) * static_cast<double>(captureH);
  if (pixels <= static_cast<double>(kLadderReducedPixels)) return out;
  // Area scales with the square of the linear factor, so this is the square root.
  const double scale = std::sqrt(static_cast<double>(kLadderReducedPixels) / pixels);

  auto even = [](double v, uint32_t upper) {
    uint32_t n = static_cast<uint32_t>(v + 0.5);
    n -= (n % 2);
    return std::clamp<uint32_t>(n, 2u, upper);
  };
  out.width = even(captureW * scale, captureW);
  out.height = even(captureH * scale, captureH);
  out.reduced = true;
  return out;
}

/**
 * The encode size an ABR profile should apply, derived when the profile is entered.
 *
 * Derived, never stored: the profiles used to carry sizes frozen at encoder initialization,
 * and a host initialized at 3 Mbps froze ~720p into every profile including high. Each later
 * recovery then restored the bitrate and re-applied the frozen size -- "profile=high
 * encode=1256x706 bitrate=12000000" in a live log, which the user experiences as raising the
 * bitrate and still reading smudged text.
 *
 * `profile`: 0 high, 1 mid, 2 low. The low profile keeps a hard 1280x720 box on top of the
 * ladder: its purpose is decode load on a client that is already struggling, which the
 * bitrate alone does not cap.
 */
inline EncodeResolutionChoice choose_abr_profile_size(int profile, uint32_t bitrate,
                                                      uint32_t captureW, uint32_t captureH,
                                                      bool currentlyReduced) {
  EncodeResolutionChoice out =
      choose_encode_resolution(bitrate, captureW, captureH, currentlyReduced);
  if (profile != 2) return out;
  if (out.width > 1280 || out.height > 720) {
    const double sx = 1280.0 / static_cast<double>(out.width);
    const double sy = 720.0 / static_cast<double>(out.height);
    const double scale = std::min(sx, sy);
    if (scale > 0.0 && scale < 1.0) {
      out.width = static_cast<uint32_t>(out.width * scale);
      out.height = static_cast<uint32_t>(out.height * scale);
    }
  }
  return out;
}

}  // namespace remote60::native_poc
