#pragma once

// One enumerated window -> one wire entry of ControlWindowListMessage.
//
// Its own function so a test can put a real title through the exact code the host sends with:
// the title is limited to 20 characters and cut at a character boundary (utf8_bounded.hpp). The
// byte cut that used to be here crashed the Android app on every window list from a PC with a long
// Hangul title.

#include <algorithm>

#include "host_window_enum.hpp"
#include "poc_protocol.hpp"
#include "utf8_bounded.hpp"

namespace remote60::native_poc {

// The user's call (2026-09-25): "the whole title need not show; limit it to about 20 characters
// from the front". Characters, not bytes -- 20 Hangul syllables are 60 bytes and fit the 96-byte
// field with room to spare, and a shortened title ends in U+2026 inside those 20.
inline constexpr size_t kWindowTitleMaxChars = 20;

/** A window title into a fixed wire field: at most kWindowTitleMaxChars, cut between characters. */
template <size_t N>
inline void fill_window_title(char (&dst)[N], std::string_view title) {
  utf8_copy_bounded(dst, N, utf8_limit_chars(title, kWindowTitleMaxChars));
}

inline void fill_window_entry(ControlWindowEntry& dst, const WindowListEntry& src) {
  dst.id = src.id;
  dst.pid = src.pid;
  dst.width = static_cast<uint32_t>(std::max<int>(0, src.width));
  dst.height = static_cast<uint32_t>(std::max<int>(0, src.height));
  if (src.minimized) dst.flags |= 0x1u;
  fill_window_title(dst.title, src.title);
}

}  // namespace remote60::native_poc
