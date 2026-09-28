#pragma once

// Clipboard image v1 (clip-image), the pure core: limits, the DIB header check, the bulk stream
// namespace and its generation allocator. No Win32 calls, no sockets -- all unit-tested.
//
// Role:    decides what an image transfer may be before any memory is spent on it, and which UDP
//          datagrams belong to the bulk channel rather than the control channel.
// Thread:  none of its own; BulkGenAllocator is not synchronised (the host's control thread owns it).
// Callers: the clipboard image engine (host and viewer), clip_image_core_test.
//
// Plan: .claude/clip_image_plan.md (r1 + r2), agreed in clip_image_debate_2026-09-28.md.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

// ------------------------------------------------------------------ limits (plan r1 ③, r2 §9)
constexpr uint32_t kClipImageMaxSide = 8192;
constexpr uint64_t kClipImageMaxDecodedBytes = 64ull * 1024 * 1024;   // w*h*4
constexpr uint64_t kClipImageMaxPngBytes = 16ull * 1024 * 1024;
constexpr uint64_t kClipImageMaxTextBytes = 2ull * kClipboardTextMaxUtf16;  // UTF-16, 1 MiB
constexpr uint64_t kClipImagePeakBudgetBytes = 100ull * 1024 * 1024;
constexpr uint64_t kClipImageSlackBytes = 1ull * 1024 * 1024;

enum class ClipImageGateReason : uint8_t {
  Ok = 0,
  ZeroSize,
  SideTooLarge,
  DecodedTooLarge,
  PngTooLarge,
  TextTooLarge,
  PeakTooLarge,
};

struct ClipImageGate {
  ClipImageGateReason reason = ClipImageGateReason::Ok;
  uint64_t decodedBytes = 0;
  uint64_t receiverPeakBytes = 0;  // R4: package P+T, the DIBV5 HGLOBAL (D), the PNG HGLOBAL (P)
  uint64_t senderPeakBytes = 0;    // S2: the DIB snapshot (D) and the encoded PNG (P)
  bool ok() const { return reason == ClipImageGateReason::Ok; }
};

/**
 * Whether an image of w x h pixels, pngBytes compressed and textBytes of same-revision text, may be
 * transferred at all. Judged on the DECODED size, never the compressed one -- a tiny PNG of a huge
 * canvas costs its full w*h*4 when decoded (plan r2 §9). All arithmetic is 64-bit.
 */
inline ClipImageGate clip_image_gate(uint32_t w, uint32_t h, uint64_t pngBytes, uint64_t textBytes) {
  ClipImageGate g{};
  if (w == 0 || h == 0) { g.reason = ClipImageGateReason::ZeroSize; return g; }
  if (w > kClipImageMaxSide || h > kClipImageMaxSide) { g.reason = ClipImageGateReason::SideTooLarge; return g; }
  g.decodedBytes = static_cast<uint64_t>(w) * h * 4ull;
  g.receiverPeakBytes = 2 * pngBytes + textBytes + g.decodedBytes + kClipImageSlackBytes;
  g.senderPeakBytes = g.decodedBytes + pngBytes + kClipImageSlackBytes;
  if (g.decodedBytes > kClipImageMaxDecodedBytes) g.reason = ClipImageGateReason::DecodedTooLarge;
  else if (pngBytes > kClipImageMaxPngBytes) g.reason = ClipImageGateReason::PngTooLarge;
  else if (textBytes > kClipImageMaxTextBytes) g.reason = ClipImageGateReason::TextTooLarge;
  else if (g.receiverPeakBytes > kClipImagePeakBudgetBytes || g.senderPeakBytes > kClipImagePeakBudgetBytes)
    g.reason = ClipImageGateReason::PeakTooLarge;
  return g;
}

// ------------------------------------------------------------------ DIB / DIBV5 header (plan r1 ⑤, r2 §8-4)
enum class DibReason : uint8_t {
  Ok = 0,
  Truncated,          // smaller than its own header, masks, palette or pixel rows
  BadHeaderSize,      // not 40/52/56/108/124
  BadDimensions,      // width <= 0 or height == 0, or a side over the limit
  BadPlanes,
  BadBitCount,
  BadCompression,     // anything but BI_RGB, or BI_BITFIELDS at 16/32 bpp
  BadPalette,         // more colours than the bit depth can index
  UnsupportedColorSpace,  // calibrated, linked or embedded profile: refused, never recoloured
};

constexpr uint32_t kDibBiRgb = 0;
constexpr uint32_t kDibBiBitfields = 3;
constexpr uint32_t kLcsCalibratedRgb = 0;
constexpr uint32_t kLcsSrgb = 0x73524742u;           // 'sRGB'
constexpr uint32_t kLcsWindowsColorSpace = 0x57696E20u;  // 'Win '

struct DibInfo {
  DibReason reason = DibReason::Ok;
  uint32_t headerSize = 0;
  uint32_t width = 0;
  uint32_t height = 0;       // absolute
  bool topDown = false;
  uint16_t bitCount = 0;
  uint32_t compression = 0;
  uint32_t paletteEntries = 0;
  uint32_t masks[4] = {};    // R, G, B, A when BI_BITFIELDS (A only from V3+)
  uint64_t stride = 0;
  uint64_t pixelOffset = 0;  // from the start of the header
  uint64_t pixelBytes = 0;
  bool ok() const { return reason == DibReason::Ok; }
};

inline uint32_t dib_u32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline int32_t dib_i32(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }
inline uint16_t dib_u16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }

/**
 * Validates a packed DIB (CF_DIB / CF_DIBV5 memory: header, optional masks, palette, rows) of `size`
 * bytes -- the whole of what GlobalSize reported. Every size is computed in 64 bits before it is
 * compared, so a crafted header cannot wrap a multiplication into a small number.
 */
inline DibInfo validate_dib(const uint8_t* data, uint64_t size) {
  DibInfo d{};
  if (!data || size < 4) { d.reason = DibReason::Truncated; return d; }
  d.headerSize = dib_u32(data);
  if (d.headerSize != 40 && d.headerSize != 52 && d.headerSize != 56 && d.headerSize != 108 && d.headerSize != 124) {
    d.reason = DibReason::BadHeaderSize;
    return d;
  }
  if (size < d.headerSize) { d.reason = DibReason::Truncated; return d; }
  const int32_t w = dib_i32(data + 4);
  const int32_t h = dib_i32(data + 8);
  if (w <= 0 || h == 0 || h == INT32_MIN) { d.reason = DibReason::BadDimensions; return d; }
  d.width = static_cast<uint32_t>(w);
  d.height = static_cast<uint32_t>(h < 0 ? -static_cast<int64_t>(h) : h);
  d.topDown = h < 0;
  if (d.width > kClipImageMaxSide || d.height > kClipImageMaxSide) { d.reason = DibReason::BadDimensions; return d; }
  if (dib_u16(data + 12) != 1) { d.reason = DibReason::BadPlanes; return d; }
  d.bitCount = dib_u16(data + 14);
  if (d.bitCount != 1 && d.bitCount != 4 && d.bitCount != 8 && d.bitCount != 16 && d.bitCount != 24 && d.bitCount != 32) {
    d.reason = DibReason::BadBitCount;
    return d;
  }
  d.compression = dib_u32(data + 16);
  const bool bitfields = d.compression == kDibBiBitfields;
  if (!(d.compression == kDibBiRgb || (bitfields && (d.bitCount == 16 || d.bitCount == 32)))) {
    d.reason = DibReason::BadCompression;
    return d;
  }
  uint64_t offset = d.headerSize;
  if (bitfields) {
    if (d.headerSize == 40) {
      // BITMAPINFOHEADER keeps its three masks after the header.
      if (size < offset + 12) { d.reason = DibReason::Truncated; return d; }
      for (int i = 0; i < 3; ++i) d.masks[i] = dib_u32(data + 40 + 4 * i);
      offset += 12;
    } else {
      for (int i = 0; i < 3; ++i) d.masks[i] = dib_u32(data + 40 + 4 * i);
    }
  }
  if (d.headerSize >= 56) d.masks[3] = dib_u32(data + 52);
  if (d.headerSize >= 108) {
    const uint32_t cs = dib_u32(data + 56);
    if (cs != kLcsSrgb && cs != kLcsWindowsColorSpace) { d.reason = DibReason::UnsupportedColorSpace; return d; }
  }
  const uint32_t clrUsed = dib_u32(data + 32);
  if (d.bitCount <= 8) {
    const uint32_t maxColors = 1u << d.bitCount;
    d.paletteEntries = clrUsed == 0 ? maxColors : clrUsed;
    if (d.paletteEntries > maxColors) { d.reason = DibReason::BadPalette; return d; }
  } else {
    // An optional optimisation palette may precede high-colour rows; bounded like any other.
    d.paletteEntries = clrUsed;
    if (d.paletteEntries > 256) { d.reason = DibReason::BadPalette; return d; }
  }
  offset += static_cast<uint64_t>(d.paletteEntries) * 4ull;
  d.stride = ((static_cast<uint64_t>(d.width) * d.bitCount + 31ull) / 32ull) * 4ull;
  d.pixelBytes = d.stride * d.height;
  d.pixelOffset = offset;
  if (offset > size || d.pixelBytes > size - offset) { d.reason = DibReason::Truncated; return d; }
  return d;
}

// ------------------------------------------------------------------ bulk stream namespace (plan r1 ④, r2 §8-3)
// bit31 is the control-resume namespace (control_resume.hpp), bits 30..31 == 01 is bulk, and the
// plain control ids are 1/2. The low two bits carry the direction base, as for control.
constexpr uint32_t kBulkStreamTag = 0x40000000u;
constexpr uint32_t kBulkStreamTagMask = 0xC0000000u;
constexpr uint32_t kBulkStreamClientToHost = 1u;
constexpr uint32_t kBulkStreamHostToClient = 2u;
constexpr uint32_t kBulkGenBits = 28u;
constexpr uint32_t kBulkGenMask = (1u << kBulkGenBits) - 1u;

inline uint32_t bulk_stream_id(uint32_t bulkGen, uint32_t base) {
  return kBulkStreamTag | ((bulkGen & kBulkGenMask) << 2) | (base & 0x3u);
}

inline bool bulk_stream_id_is_bulk(uint32_t streamId) { return (streamId & kBulkStreamTagMask) == kBulkStreamTag; }

/**
 * Whether a datagram belongs to the bulk channel. Decided BEFORE the control channel sees it:
 * UdpControlChannel::OnPacket consumes every ControlData/Ack/Nack datagram whatever its stream id
 * (udp_control_channel.cpp:363-374), so a bulk datagram offered to it second would be swallowed.
 * Data, Ack and Nack all carry the stream id at byte 8.
 */
inline bool bulk_stream_claims(const void* data, size_t len) {
  if (!data || len < 12) return false;
  const auto* p = static_cast<const uint8_t*>(data);
  uint32_t magic = 0;
  uint16_t kind = 0;
  uint32_t streamId = 0;
  std::memcpy(&magic, p, 4);
  std::memcpy(&kind, p + 4, 2);
  std::memcpy(&streamId, p + 8, 4);
  if (magic != kMagic) return false;
  const bool controlKind = kind == static_cast<uint16_t>(UdpPacketKind::ControlData) ||
                           kind == static_cast<uint16_t>(UdpPacketKind::ControlAck) ||
                           kind == static_cast<uint16_t>(UdpPacketKind::ControlNack);
  return controlKind && bulk_stream_id_is_bulk(streamId);
}

/**
 * Hands out bulkGen values. The stream id keeps only the low 28 bits, so a value whose low 28 bits
 * match the current transfer or any of the last 64 is skipped -- a late datagram of a recent
 * transfer can then never be mistaken for the current one. Zero is never handed out.
 */
class BulkGenAllocator {
 public:
  static constexpr size_t kRecent = 64;

  uint32_t Next() {
    for (;;) {
      ++next_;
      const uint32_t low = next_ & kBulkGenMask;
      if (low == 0) continue;
      if (std::find(recent_.begin(), recent_.end(), low) != recent_.end()) continue;
      recent_.push_back(low);
      if (recent_.size() > kRecent + 1) recent_.pop_front();  // + the current one
      return next_;
    }
  }
  // Test seam: start the counter elsewhere (e.g. just below a wrap).
  void SeedForTest(uint32_t value) { next_ = value; }

 private:
  uint32_t next_ = 0;
  std::deque<uint32_t> recent_;
};

}  // namespace remote60::native_poc
