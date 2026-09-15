#pragma once

// The shared-memory layout a one-shot thumbnail helper writes and the host reads.
//
// Role:    describe the single block GNLinkCapture fills in --thumbnail mode.
// Thread:  plain POD; the handshake is the done event, not this header.
// Callers: gdi_capture_worker_main.cpp (writes), host_thumbnail_helper.cpp (reads).
//
// Fixed size on purpose. The block is allocated for the largest thumbnail the protocol allows
// (kWindowThumbnailMaxWidth x kWindowThumbnailMaxHeight), so the helper can never ask the host to
// grow anything and the host never has to trust a size the helper reports before checking it.
//
// The helper is not trusted about what it wrote: `ok` says whether pixels follow, and the host
// re-derives what the byte count should be from width and height rather than believing
// `byteCount`. A helper that is killed mid-write leaves whatever it left, and the done event will
// not have been set -- which is the only reason the host reads this at all.

#include <cstdint>

#include "poc_protocol.hpp"

namespace remote60::native_poc::thumbnail_ipc {

constexpr uint32_t kMagic = 0x544D4E54u;  // "TNMT"
constexpr uint32_t kVersion = 1;

struct Header {
  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t ok = 0;  // 1: width/height/byteCount describe pixels that follow this header
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint32_t byteCount = 0;
  uint32_t reserved = 0;
};

// Enough for any thumbnail the control protocol will carry, and the same number on both sides so
// a mismatch is a build error rather than a truncated read.
constexpr uint32_t kMaxPixelBytes = kWindowThumbnailMaxPayloadBytes;
constexpr uint32_t kBlockBytes = static_cast<uint32_t>(sizeof(Header)) + kMaxPixelBytes;

}  // namespace remote60::native_poc::thumbnail_ipc
