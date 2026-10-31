#pragma once

// Clipboard image v1: the WIC side (Windows only).
//
// Role:    turns what a clipboard holds into the PNG that travels, and the PNG that arrived into
//          the CF_DIBV5 memory that gets published -- with the buffer discipline of plan r2 §9:
//            * a DIB snapshot is read through a stream over the snapshot itself (a 14-byte file
//              header in front, nothing copied), so encoding costs the PNG and no second canvas;
//            * a PNG is sized from its IHDR before anything is decoded (the decoded-size gate);
//            * the decode writes straight into the HGLOBAL that SetClipboardData will own.
// Thread:  any thread with COM initialised (the callers' worker threads); no clipboard access here.
// Callers: the clipboard image engine (host and viewer), clip_image_wic_test.

#include <windows.h>

#include <cstdint>
#include <vector>

namespace remote60::native_poc {

enum class ClipWicResult : uint8_t {
  Ok = 0,
  ComFailed,      // WIC factory / codec creation failed
  NotAnImage,     // the bytes are not a PNG (or BMP) the codec accepts
  BadDib,         // validate_dib refused the header
  GateRefused,    // clip_image_gate refused the dimensions or the peak
  SizeMismatch,   // decoded size differs from what the offer said
  NoMemory,
};

/** Width and height from a PNG's header, without decoding its pixels. */
ClipWicResult clip_png_dimensions(const uint8_t* png, size_t bytes, uint32_t* width, uint32_t* height);

/**
 * Encodes a packed DIB (CF_DIB / CF_DIBV5 memory, validated here) as PNG. Alpha is kept when the
 * DIB carries it (V5 alpha mask / BI_BITFIELDS with an alpha mask); a 32 bpp BI_RGB DIB is read as
 * opaque BGR, as Windows itself treats it.
 */
ClipWicResult clip_dib_to_png(const uint8_t* dib, size_t bytes, std::vector<uint8_t>* png, uint32_t* width,
                              uint32_t* height);

/**
 * Decodes a PNG into a freshly GlobalAlloc'd packed CF_DIBV5 (BITMAPV5HEADER, BI_BITFIELDS, 32 bpp,
 * straight alpha, top-down rows). The caller passes the dimensions the offer declared; a PNG of any
 * other size is refused before its pixels are decoded. On success *out is owned by the caller
 * until SetClipboardData takes it.
 */
ClipWicResult clip_png_to_dibv5(const uint8_t* png, size_t bytes, uint32_t expectWidth, uint32_t expectHeight,
                                HGLOBAL* out);

}  // namespace remote60::native_poc
