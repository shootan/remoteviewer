#include "android_video_decoder.hpp"

#include <android/log.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace remote60::android_direct {

namespace {

constexpr const char* kLogTag = "remote60_android_direct";
constexpr const char* kMimeTypeAvc = "video/avc";

struct NalUnitView {
  const uint8_t* data = nullptr;
  size_t size = 0;
  uint8_t type = 0;
};

void log_info(const char* message) {
  __android_log_write(ANDROID_LOG_INFO, kLogTag, message);
}

void log_error(const char* message) {
  __android_log_write(ANDROID_LOG_ERROR, kLogTag, message);
}

bool find_start_code(const std::vector<uint8_t>& data, size_t offset, size_t* outPos, size_t* outCodeSize) {
  if (!outPos || !outCodeSize) return false;
  for (size_t i = offset; i + 3 < data.size(); ++i) {
    if (data[i] != 0 || data[i + 1] != 0) continue;
    if (data[i + 2] == 1) {
      *outPos = i;
      *outCodeSize = 3;
      return true;
    }
    if (i + 4 < data.size() && data[i + 2] == 0 && data[i + 3] == 1) {
      *outPos = i;
      *outCodeSize = 4;
      return true;
    }
  }
  return false;
}

bool next_annexb_nal(const std::vector<uint8_t>& data, size_t* cursor, NalUnitView* out) {
  if (!cursor || !out) return false;
  size_t startPos = 0;
  size_t startCodeSize = 0;
  if (!find_start_code(data, *cursor, &startPos, &startCodeSize)) return false;

  size_t nextPos = data.size();
  size_t nextCodeSize = 0;
  if (find_start_code(data, startPos + startCodeSize, &nextPos, &nextCodeSize)) {
    (void)nextCodeSize;
  }

  const size_t nalStart = startPos + startCodeSize;
  if (nalStart >= data.size()) return false;
  const size_t nalSize = nextPos - nalStart;
  if (nalSize == 0) return false;

  out->data = data.data() + nalStart;
  out->size = nalSize;
  out->type = static_cast<uint8_t>(out->data[0] & 0x1Fu);
  *cursor = nextPos;
  return true;
}

std::vector<uint8_t> with_start_code(const uint8_t* data, size_t size) {
  std::vector<uint8_t> out;
  out.reserve(size + 4);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  out.push_back(1);
  out.insert(out.end(), data, data + size);
  return out;
}

}  // namespace

AndroidVideoDecoderSink::~AndroidVideoDecoderSink() {
  std::lock_guard<std::mutex> lock(mu_);
  ResetCodecLocked();
  ReleaseSurfaceLocked();
}

void AndroidVideoDecoderSink::SetSurface(JNIEnv* env, jobject surface) {
  std::lock_guard<std::mutex> lock(mu_);
  ResetCodecLocked();
  ReleaseSurfaceLocked();

  if (surface == nullptr) {
    log_info("video surface cleared");
    return;
  }

  window_ = ANativeWindow_fromSurface(env, surface);
  if (!window_) {
    log_error("failed to acquire ANativeWindow from Surface");
    return;
  }

  if (!EnsureCodecLocked(configuredWidth_, configuredHeight_)) {
    log_info("video surface set; codec waiting for keyframe/csd");
  }
}

void AndroidVideoDecoderSink::OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) {
  std::lock_guard<std::mutex> lock(mu_);

  configuredWidth_ = frame.header.width;
  configuredHeight_ = frame.header.height;
  const bool codecConfigUpdated = UpdateCodecConfigLocked(frame.payload);
  if (codecConfigUpdated && codec_) {
    ResetCodecLocked();
  }
  if (!EnsureCodecLocked(frame.header.width, frame.header.height)) {
    return;
  }
  if (!codec_) return;

  const ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec_, 1000);
  if (inputIndex < 0) {
    DrainOutputLocked();
    return;
  }

  size_t inputBufferSize = 0;
  uint8_t* inputBuffer = AMediaCodec_getInputBuffer(codec_, inputIndex, &inputBufferSize);
  if (!inputBuffer || frame.payload.size() > inputBufferSize) {
    AMediaCodec_queueInputBuffer(codec_, inputIndex, 0, 0, 0, 0);
    return;
  }

  std::memcpy(inputBuffer, frame.payload.data(), frame.payload.size());
  const int64_t ptsUs = (frame.header.captureQpcUs > 0)
      ? static_cast<int64_t>(frame.header.captureQpcUs)
      : static_cast<int64_t>(frame.header.seq);
  if (AMediaCodec_queueInputBuffer(codec_, inputIndex, 0,
                                   static_cast<size_t>(frame.payload.size()),
                                   ptsUs, 0) != AMEDIA_OK) {
    log_error("AMediaCodec_queueInputBuffer failed");
    ResetCodecLocked();
    return;
  }
  ++inputFrameCount_;
  if ((inputFrameCount_ % 30) == 1) {
    char line[128];
    std::snprintf(line, sizeof(line), "queued h264 frame count=%llu bytes=%zu key=%u",
                  static_cast<unsigned long long>(inputFrameCount_),
                  frame.payload.size(),
                  (frame.header.flags & 1u) ? 1u : 0u);
    log_info(line);
  }

  DrainOutputLocked();
}

void AndroidVideoDecoderSink::OnVideoStreamReset() {
  std::lock_guard<std::mutex> lock(mu_);
  ResetCodecLocked();
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  csd0_.clear();
  csd1_.clear();
}

bool AndroidVideoDecoderSink::EnsureCodecLocked(uint32_t width, uint32_t height) {
  if (codec_ && configuredWidth_ == width && configuredHeight_ == height) {
    return true;
  }
  if (!window_ || width == 0 || height == 0 || csd0_.empty() || csd1_.empty()) {
    return false;
  }

  ResetCodecLocked();

  codec_ = AMediaCodec_createDecoderByType(kMimeTypeAvc);
  if (!codec_) {
    log_error("AMediaCodec_createDecoderByType(video/avc) failed");
    return false;
  }

  AMediaFormat* format = AMediaFormat_new();
  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, kMimeTypeAvc);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, static_cast<int32_t>(width));
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, static_cast<int32_t>(height));
  AMediaFormat_setBuffer(format, AMEDIAFORMAT_KEY_CSD_0, csd0_.data(), csd0_.size());
  AMediaFormat_setBuffer(format, AMEDIAFORMAT_KEY_CSD_1, csd1_.data(), csd1_.size());

  media_status_t status = AMediaCodec_configure(codec_, format, window_, nullptr, 0);
  AMediaFormat_delete(format);
  if (status != AMEDIA_OK) {
    log_error("AMediaCodec_configure failed");
    ResetCodecLocked();
    return false;
  }
  if (AMediaCodec_start(codec_) != AMEDIA_OK) {
    log_error("AMediaCodec_start failed");
    ResetCodecLocked();
    return false;
  }

  configuredWidth_ = width;
  configuredHeight_ = height;
  char line[128];
  std::snprintf(line, sizeof(line), "MediaCodec started width=%u height=%u csd0=%zu csd1=%zu",
                width, height, csd0_.size(), csd1_.size());
  log_info(line);
  return true;
}

void AndroidVideoDecoderSink::ResetCodecLocked() {
  if (codec_) {
    AMediaCodec_stop(codec_);
    AMediaCodec_delete(codec_);
    codec_ = nullptr;
  }
}

void AndroidVideoDecoderSink::ReleaseSurfaceLocked() {
  if (window_) {
    ANativeWindow_release(window_);
    window_ = nullptr;
  }
}

bool AndroidVideoDecoderSink::UpdateCodecConfigLocked(const std::vector<uint8_t>& annexb) {
  size_t cursor = 0;
  std::vector<uint8_t> sps;
  std::vector<uint8_t> pps;
  NalUnitView nal{};
  while (next_annexb_nal(annexb, &cursor, &nal)) {
    if (nal.type == 7 && sps.empty()) {
      sps = with_start_code(nal.data, nal.size);
    } else if (nal.type == 8 && pps.empty()) {
      pps = with_start_code(nal.data, nal.size);
    }
    if (!sps.empty() && !pps.empty()) break;
  }

  bool updated = false;
  if (!sps.empty() && sps != csd0_) {
    csd0_ = std::move(sps);
    updated = true;
  }
  if (!pps.empty() && pps != csd1_) {
    csd1_ = std::move(pps);
    updated = true;
  }
  if (updated) {
    char line[128];
    std::snprintf(line, sizeof(line), "updated codec config csd0=%zu csd1=%zu", csd0_.size(), csd1_.size());
    log_info(line);
  }
  return updated;
}

void AndroidVideoDecoderSink::DrainOutputLocked() {
  if (!codec_) return;

  AMediaCodecBufferInfo info{};
  for (;;) {
    const ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(codec_, &info, 0);
    if (outputIndex >= 0) {
      AMediaCodec_releaseOutputBuffer(codec_, outputIndex, true);
      ++outputFrameCount_;
      if ((outputFrameCount_ % 30) == 1) {
        char line[128];
        std::snprintf(line, sizeof(line), "released output frame count=%llu flags=%u",
                      static_cast<unsigned long long>(outputFrameCount_), info.flags);
        log_info(line);
      }
      continue;
    }
    if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ||
        outputIndex == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      continue;
    }
    break;
  }
}

}  // namespace remote60::android_direct
