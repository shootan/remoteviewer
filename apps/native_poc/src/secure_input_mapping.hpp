#pragma once

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

// Turns a click in the client's coordinate space into an absolute desktop point.
//
// This used to map onto GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN) -- the agent session's PRIMARY
// monitor. On a machine with more than one display that puts every click on the primary, so a UAC
// prompt that opened on a secondary monitor was unreachable: the cursor went to a proportional
// position on the wrong screen and the click landed on whatever happened to be there. Nothing
// reported it, because SetCursorPos succeeds at any valid coordinate.
//
// The target rect is now explicit. SetCursorPos takes coordinates spanning the whole virtual
// desktop, so as long as the caller says which region the client's pixels correspond to, a
// secondary monitor is no different from the primary.

struct DesktopRect {
  int32_t originX = 0;
  int32_t originY = 0;
  int32_t width = 0;
  int32_t height = 0;

  bool valid() const { return width > 0 && height > 0; }
};

struct MappedPoint {
  int32_t x = 0;
  int32_t y = 0;
};

/**
 * Maps (x, y) given in a `inputWidth` x `inputHeight` space onto `target`.
 *
 * `target` is where those pixels live in desktop coordinates -- for full-desktop capture that is
 * the virtual screen, including any negative origin a monitor left of the primary produces.
 * Out-of-range input is clamped rather than rejected: a click one pixel past the edge is a
 * rounding artefact of the client's scaling, not a reason to drop the event.
 */
inline MappedPoint map_client_point(int32_t x, int32_t y, uint32_t inputWidth,
                                    uint32_t inputHeight, const DesktopRect& target) {
  const int32_t inW = static_cast<int32_t>(std::max<uint32_t>(1, inputWidth));
  const int32_t inH = static_cast<int32_t>(std::max<uint32_t>(1, inputHeight));
  const int32_t clampedX = std::clamp<int32_t>(x, 0, inW - 1);
  const int32_t clampedY = std::clamp<int32_t>(y, 0, inH - 1);

  MappedPoint point;
  // A one-pixel-wide space maps to the origin rather than dividing by zero.
  point.x = target.originX + (inW > 1 ? static_cast<int32_t>((static_cast<int64_t>(clampedX) *
                                                              (target.width - 1)) /
                                                             (inW - 1))
                                      : 0);
  point.y = target.originY + (inH > 1 ? static_cast<int32_t>((static_cast<int64_t>(clampedY) *
                                                              (target.height - 1)) /
                                                             (inH - 1))
                                      : 0);
  return point;
}

}  // namespace remote60::native_poc
