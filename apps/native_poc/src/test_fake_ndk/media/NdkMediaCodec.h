#pragma once
// TEST ONLY: see test_fake_ndk/fake_ndk.hpp.
#include <BaseTsd.h>
#include <cstddef>
#include <cstdint>
#include "NdkMediaFormat.h"
typedef SSIZE_T ssize_t;
struct AMediaCodec;
struct ANativeWindow;
struct AMediaCrypto;
struct AMediaCodecBufferInfo {
  int32_t offset;
  int32_t size;
  int64_t presentationTimeUs;
  uint32_t flags;
};
enum {
  AMEDIACODEC_INFO_TRY_AGAIN_LATER = -1,
  AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED = -2,
  AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED = -3,
};
AMediaCodec* AMediaCodec_createDecoderByType(const char* mime);
media_status_t AMediaCodec_configure(AMediaCodec* codec, const AMediaFormat* format, ANativeWindow* surface,
                                     AMediaCrypto* crypto, uint32_t flags);
media_status_t AMediaCodec_start(AMediaCodec* codec);
media_status_t AMediaCodec_stop(AMediaCodec* codec);
media_status_t AMediaCodec_delete(AMediaCodec* codec);
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec* codec, int64_t timeoutUs);
uint8_t* AMediaCodec_getInputBuffer(AMediaCodec* codec, size_t idx, size_t* outSize);
media_status_t AMediaCodec_queueInputBuffer(AMediaCodec* codec, size_t idx, int64_t offset, size_t size, uint64_t time,
                                            uint32_t flags);
ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec* codec, AMediaCodecBufferInfo* info, int64_t timeoutUs);
AMediaFormat* AMediaCodec_getOutputFormat(AMediaCodec* codec);
media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec* codec, size_t idx, bool render);
media_status_t AMediaCodec_releaseOutputBufferAtTime(AMediaCodec* codec, size_t idx, int64_t timestampNs);
