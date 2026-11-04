#pragma once

// The decoder's two copies of a decoded Media Foundation sample into a byte vector (mf_h264_codec.cpp
// sample_to_bytes / sample_to_nv12_bytes_from_2d_buffer), moved here unchanged except for one thing
// (lk-abcd-rel B2):
//
// An allocation that cannot be had is a DROPPED FRAME, not a terminated viewer. Both copies allocate the output
// vector while the media buffer is locked; a std::bad_alloc there unwound out of the decoder, through the video
// receiver's thread, and -- nothing on that thread catching it -- std::terminate ended the whole process
// (2026-11-04, e2e still-render: fast-fail 7 from abort <- terminate <- operator new <- sample_to_bytes <-
// H264Decoder::decode_access_unit <- VideoReceiver::process_h264_frame <- run_udp; minidump + a /MAP of a rebuild of
// the same source). Now the copy says false, the buffer is unlocked, and the callers' existing "no usable output"
// paths run (the frame is not shown; the next one is decoded). Each failure is counted (AllocationFailures).
//
// Also: a contiguous buffer whose current length exceeds its maximum is refused instead of read past.
//
// Header-only so the twenty targets that compile mf_h264_codec.cpp need no new source, and the unit test
// (mf_sample_copy_test.cpp) runs this exact code on real Media Foundation buffers.

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace remote60::native_poc::mf_copy {

inline std::atomic<uint64_t>& AllocationFailures() {
  static std::atomic<uint64_t> n{0};
  return n;
}

// The sample's contiguous bytes. False (out empty) when there are none or they cannot be copied.
inline bool SampleToBytes(IMFSample* sample, std::vector<uint8_t>* out) noexcept {
  if (!sample || !out) return false;
  Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) return false;
  BYTE* src = nullptr;
  DWORD maxLen = 0;
  DWORD curLen = 0;
  if (FAILED(buffer->Lock(&src, &maxLen, &curLen)) || !src) return false;
  bool ok = false;
  if (curLen <= maxLen) {
    try {
      out->assign(src, src + curLen);
      ok = !out->empty();
    } catch (...) {  // std::bad_alloc (or length_error): this frame is dropped
      AllocationFailures().fetch_add(1, std::memory_order_relaxed);
    }
  }
  buffer->Unlock();
  if (!ok) out->clear();
  return ok;
}

// The sample as tight NV12 (width x height luma, then interleaved chroma) from its first 2D buffer.
inline bool SampleToNv12From2DBuffer(IMFSample* sample, uint32_t width, uint32_t height,
                                     std::vector<uint8_t>* out) noexcept {
  if (!sample || !out || width == 0 || height == 0) return false;

  DWORD bufferCount = 0;
  if (FAILED(sample->GetBufferCount(&bufferCount)) || bufferCount == 0) return false;

  for (DWORD i = 0; i < bufferCount; ++i) {
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->GetBufferByIndex(i, &buffer)) || !buffer) continue;

    Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2d;
    if (FAILED(buffer.As(&buffer2d)) || !buffer2d) continue;

    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    if (FAILED(buffer2d->Lock2D(&scan0, &pitch)) || !scan0 || pitch == 0) continue;

    const LONG absPitch = (pitch >= 0) ? pitch : -pitch;
    const size_t yPlaneBytes = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uvPlaneBytes =
        static_cast<size_t>(width) * static_cast<size_t>((height + 1) / 2);
    try {
      out->assign(yPlaneBytes + uvPlaneBytes, 0);
    } catch (...) {  // lk-abcd-rel B2: as in SampleToBytes -- dropped, unlocked, not thrown
      AllocationFailures().fetch_add(1, std::memory_order_relaxed);
      buffer2d->Unlock2D();
      out->clear();
      return false;
    }
    auto* dstY = out->data();
    auto* dstUV = out->data() + yPlaneBytes;

    const BYTE* yBase = (pitch >= 0)
                            ? scan0
                            : (scan0 + static_cast<ptrdiff_t>(height - 1) * absPitch);
    for (uint32_t y = 0; y < height; ++y) {
      const BYTE* srcRow =
          (pitch >= 0) ? (yBase + static_cast<ptrdiff_t>(y) * absPitch)
                       : (yBase - static_cast<ptrdiff_t>(y) * absPitch);
      std::memcpy(dstY + static_cast<size_t>(y) * width, srcRow, width);
    }

    const uint32_t uvRows = (height + 1) / 2;
    const BYTE* uvBase = (pitch >= 0)
                             ? (yBase + static_cast<ptrdiff_t>(height) * absPitch)
                             : (yBase - static_cast<ptrdiff_t>(height) * absPitch);
    for (uint32_t y = 0; y < uvRows; ++y) {
      const BYTE* srcRow =
          (pitch >= 0) ? (uvBase + static_cast<ptrdiff_t>(y) * absPitch)
                       : (uvBase - static_cast<ptrdiff_t>(y) * absPitch);
      std::memcpy(dstUV + static_cast<size_t>(y) * width, srcRow, width);
    }

    buffer2d->Unlock2D();
    return true;
  }

  return false;
}

}  // namespace remote60::native_poc::mf_copy
