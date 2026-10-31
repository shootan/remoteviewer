#pragma once

// UTF-8 that stays UTF-8 when it is cut, and when it crosses into Java.
//
// Role:    pure, platform-free helpers shared by the host (fixed-size wire fields) and the Android
//          JNI bridge (strings to and from Java).
// Why:     the host copied window titles into `char title[96]` with snprintf, which cuts at a BYTE.
//          A 152-byte Chrome title with Hangul (3 bytes per syllable) was cut at 95 bytes, leaving
//          two bytes of a three-byte character. The Android bridge handed that to NewStringUTF,
//          which aborts the process on malformed input -- the phone app died every time it received
//          the window list from this PC (2026-09-25). Both halves are fixed: the host never cuts
//          inside a character, and the bridge no longer trusts what it is given.
//
// Validity is strict UTF-8 (RFC 3629): no overlong forms, no surrogates (U+D800..U+DFFF), nothing
// above U+10FFFF. That is stricter than what NewStringUTF accepts on some runtimes and looser than
// "modified UTF-8" in one respect -- 4-byte characters are valid here -- which is why the bridge
// converts to UTF-16 itself rather than handing UTF-8 to the JVM.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace remote60::native_poc {

/**
 * Length (1..4) of the valid UTF-8 sequence starting at `p`, or 0 if the bytes there are not one.
 * `n` is how many bytes are available from `p`; a sequence that would run past it is invalid.
 */
inline size_t utf8_sequence_length(const unsigned char* p, size_t n) {
  if (n == 0) return 0;
  const unsigned char b0 = p[0];
  if (b0 < 0x80) return 1;
  size_t len = 0;
  uint32_t min = 0;
  uint32_t cp = 0;
  if ((b0 & 0xE0) == 0xC0) {
    len = 2; min = 0x80; cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    len = 3; min = 0x800; cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    len = 4; min = 0x10000; cp = b0 & 0x07;
  } else {
    return 0;  // a continuation byte, or 0xF8..0xFF
  }
  if (len > n) return 0;
  for (size_t i = 1; i < len; ++i) {
    if ((p[i] & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  if (cp < min) return 0;                       // overlong
  if (cp >= 0xD800 && cp <= 0xDFFF) return 0;   // a surrogate is not a character
  if (cp > 0x10FFFF) return 0;
  return len;
}

/** Decodes the valid sequence of `len` bytes at `p` (as returned by utf8_sequence_length). */
inline uint32_t utf8_decode(const unsigned char* p, size_t len) {
  if (len == 1) return p[0];
  uint32_t cp = p[0] & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
  for (size_t i = 1; i < len; ++i) cp = (cp << 6) | (p[i] & 0x3F);
  return cp;
}

/** True when every byte of `s` belongs to a valid UTF-8 sequence. */
inline bool utf8_is_valid(std::string_view s) {
  const auto* p = reinterpret_cast<const unsigned char*>(s.data());
  size_t i = 0;
  while (i < s.size()) {
    const size_t len = utf8_sequence_length(p + i, s.size() - i);
    if (len == 0) return false;
    i += len;
  }
  return true;
}

/**
 * Copies `src` into the fixed field `dst[dstSize]` as valid UTF-8, always NUL-terminated.
 *
 * Cut only between characters: a character that does not fit in the remaining dstSize-1 bytes is
 * dropped whole, never split. A byte that is not part of a valid sequence in `src` becomes '?'
 * (one byte, so the bound stays simple and the damage stays visible). Stops at an embedded NUL,
 * as "%s" did. The rest of the field is zero-filled, so nothing stale follows the terminator on
 * the wire. Returns the number of bytes written before the NUL.
 */
inline size_t utf8_copy_bounded(char* dst, size_t dstSize, std::string_view src) {
  if (!dst || dstSize == 0) return 0;
  const auto* p = reinterpret_cast<const unsigned char*>(src.data());
  const size_t cap = dstSize - 1;
  size_t in = 0;
  size_t out = 0;
  while (in < src.size() && p[in] != 0) {
    size_t len = utf8_sequence_length(p + in, src.size() - in);
    if (len == 0) {
      if (out + 1 > cap) break;
      dst[out++] = '?';
      ++in;
      continue;
    }
    if (out + len > cap) break;
    for (size_t k = 0; k < len; ++k) dst[out++] = static_cast<char>(p[in + k]);
    in += len;
  }
  for (size_t k = out; k < dstSize; ++k) dst[k] = '\0';
  return out;
}

/** U+2026 HORIZONTAL ELLIPSIS, the mark a shortened title ends with. */
inline constexpr std::string_view kUtf8Ellipsis = "\xE2\x80\xA6";

/**
 * At most `maxChars` characters (code points) of `src`, as valid UTF-8. If `src` has more, the
 * result is its first maxChars-1 characters followed by U+2026, so it is still maxChars long.
 * Bytes that are not valid UTF-8 become '?' and count as one character each; an embedded NUL
 * ends the text. Used for window titles on the wire: the user asked for "the first 20 or so
 * characters", and whole characters are what a person counts.
 */
inline std::string utf8_limit_chars(std::string_view src, size_t maxChars) {
  std::string out;
  const auto* p = reinterpret_cast<const unsigned char*>(src.data());
  size_t in = 0;
  size_t chars = 0;
  size_t endOfKept = 0;  // byte length of the first maxChars-1 characters
  while (in < src.size() && p[in] != 0) {
    if (chars == maxChars) {
      // There is more than fits: keep maxChars-1 characters and the ellipsis.
      out.resize(endOfKept);
      if (maxChars > 0) out.append(kUtf8Ellipsis);
      return out;
    }
    const size_t len = utf8_sequence_length(p + in, src.size() - in);
    if (len == 0) {
      out.push_back('?');
      ++in;
    } else {
      out.append(reinterpret_cast<const char*>(p + in), len);
      in += len;
    }
    ++chars;
    if (chars + 1 == maxChars) endOfKept = out.size();
  }
  return out;
}

/** Reads a fixed field that may not be NUL-terminated: at most `size` bytes, up to the first NUL. */
inline std::string_view fixed_field_view(const char* field, size_t size) {
  size_t n = 0;
  while (n < size && field[n] != '\0') ++n;
  return std::string_view(field, n);
}

/**
 * UTF-8 to UTF-16, never failing: each byte that is not part of a valid sequence becomes U+FFFD,
 * characters above U+FFFF become surrogate pairs, and an embedded NUL is kept as U+0000 (UTF-16
 * carries it; modified UTF-8 would not have).
 */
inline std::u16string utf8_to_utf16_lossy(std::string_view s) {
  std::u16string out;
  out.reserve(s.size());
  const auto* p = reinterpret_cast<const unsigned char*>(s.data());
  size_t i = 0;
  while (i < s.size()) {
    const size_t len = utf8_sequence_length(p + i, s.size() - i);
    if (len == 0) {
      out.push_back(u'�');
      ++i;
      continue;
    }
    const uint32_t cp = utf8_decode(p + i, len);
    if (cp >= 0x10000) {
      const uint32_t v = cp - 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
    i += len;
  }
  return out;
}

/** UTF-16 to UTF-8, never failing: an unpaired surrogate becomes U+FFFD. */
inline std::string utf16_to_utf8_lossy(const char16_t* s, size_t n) {
  std::string out;
  out.reserve(n);
  const auto put = [&out](uint32_t cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  };
  for (size_t i = 0; i < n; ++i) {
    const uint32_t u = s[i];
    if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      put(0x10000 + ((u - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00));
      ++i;
    } else if (u >= 0xD800 && u <= 0xDFFF) {
      put(0xFFFD);
    } else {
      put(u);
    }
  }
  return out;
}

}  // namespace remote60::native_poc
