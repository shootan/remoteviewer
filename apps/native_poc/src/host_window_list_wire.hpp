#pragma once

// One enumerated window -> one wire entry of ControlWindowListMessage.
//
// Its own function so a test can put a real title through the exact code the host sends with:
// the title is cut at a character boundary (utf8_bounded.hpp), never inside one. The byte cut that
// used to be here crashed the Android app on every window list from a PC with a long Hangul title.

#include <algorithm>

#include "host_window_enum.hpp"
#include "poc_protocol.hpp"
#include "utf8_bounded.hpp"

namespace remote60::native_poc {

inline void fill_window_entry(ControlWindowEntry& dst, const WindowListEntry& src) {
  dst.id = src.id;
  dst.pid = src.pid;
  dst.width = static_cast<uint32_t>(std::max<int>(0, src.width));
  dst.height = static_cast<uint32_t>(std::max<int>(0, src.height));
  if (src.minimized) dst.flags |= 0x1u;
  utf8_copy_bounded(dst.title, sizeof(dst.title), src.title);
}

}  // namespace remote60::native_poc
