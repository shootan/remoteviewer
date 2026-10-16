// TEST ONLY: see fake_ndk.hpp.
#include "fake_ndk.hpp"

#include <android/log.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <cstdio>
#include <cstring>

namespace fake_ndk {
std::mutex& mu() {
  static std::mutex m;
  return m;
}
Controls& controls() {
  static Controls c;
  return c;
}
}  // namespace fake_ndk

struct AMediaFormat {
  int32_t width = 0;   // set only on a format returned by getOutputFormat
  int32_t height = 0;
  bool cropRect = false, cropKeys = false;
  int32_t cropL = 0, cropT = 0, cropR = 0, cropB = 0;
};
struct AMediaCodec {
  bool started = false;
  std::vector<uint8_t> input = std::vector<uint8_t>(1 << 20);
};

const char* AMEDIAFORMAT_KEY_MIME = "mime";
const char* AMEDIAFORMAT_KEY_WIDTH = "width";
const char* AMEDIAFORMAT_KEY_HEIGHT = "height";
const char* AMEDIAFORMAT_KEY_CSD_0 = "csd-0";
const char* AMEDIAFORMAT_KEY_CSD_1 = "csd-1";
const char* AMEDIAFORMAT_KEY_PRIORITY = "priority";
const char* AMEDIAFORMAT_KEY_DISPLAY_CROP = "crop";

int __android_log_write(int prio, const char* tag, const char* text) {
  std::printf("      [decoder %s] %s\n", prio >= ANDROID_LOG_ERROR ? "E" : "I", text ? text : "");
  (void)tag;
  return 0;
}

ANativeWindow* ANativeWindow_fromSurface(JNIEnv*, jobject surface) { return static_cast<ANativeWindow*>(surface); }
void ANativeWindow_release(ANativeWindow*) {}

AMediaFormat* AMediaFormat_new() { return new AMediaFormat(); }
media_status_t AMediaFormat_delete(AMediaFormat* f) {
  delete f;
  return AMEDIA_OK;
}
void AMediaFormat_setString(AMediaFormat*, const char*, const char*) {}
void AMediaFormat_setInt32(AMediaFormat*, const char*, int32_t) {}
void AMediaFormat_setBuffer(AMediaFormat*, const char*, const void*, size_t) {}
bool AMediaFormat_getInt32(AMediaFormat* f, const char* name, int32_t* out) {
  if (!f || !out) return false;
  if (name == AMEDIAFORMAT_KEY_WIDTH && f->width > 0) {
    *out = f->width;
    return true;
  }
  if (name == AMEDIAFORMAT_KEY_HEIGHT && f->height > 0) {
    *out = f->height;
    return true;
  }
  if (f->cropKeys) {
    if (std::strcmp(name, "crop-left") == 0) return (*out = f->cropL), true;
    if (std::strcmp(name, "crop-top") == 0) return (*out = f->cropT), true;
    if (std::strcmp(name, "crop-right") == 0) return (*out = f->cropR), true;
    if (std::strcmp(name, "crop-bottom") == 0) return (*out = f->cropB), true;
  }
  return false;
}
bool AMediaFormat_getRect(AMediaFormat* f, const char* name, int32_t* l, int32_t* t, int32_t* r, int32_t* b) {
  if (!f || !f->cropRect || std::strcmp(name, AMEDIAFORMAT_KEY_DISPLAY_CROP) != 0) return false;
  *l = f->cropL, *t = f->cropT, *r = f->cropR, *b = f->cropB;
  return true;
}

AMediaCodec* AMediaCodec_createDecoderByType(const char*) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  if (fake_ndk::controls().createFails) return nullptr;
  ++fake_ndk::controls().codecsCreated;
  return new AMediaCodec();
}
media_status_t AMediaCodec_configure(AMediaCodec*, const AMediaFormat*, ANativeWindow*, AMediaCrypto*, uint32_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  return fake_ndk::controls().configureFails ? AMEDIA_ERROR_UNKNOWN : AMEDIA_OK;
}
media_status_t AMediaCodec_start(AMediaCodec* c) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  if (fake_ndk::controls().startFails) return AMEDIA_ERROR_UNKNOWN;
  c->started = true;
  ++fake_ndk::controls().codecsStarted;
  return AMEDIA_OK;
}
media_status_t AMediaCodec_stop(AMediaCodec* c) {
  c->started = false;
  return AMEDIA_OK;
}
media_status_t AMediaCodec_delete(AMediaCodec* c) {
  delete c;
  return AMEDIA_OK;
}
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec*, int64_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  return fake_ndk::controls().inputAvailable ? 0 : AMEDIACODEC_INFO_TRY_AGAIN_LATER;
}
uint8_t* AMediaCodec_getInputBuffer(AMediaCodec* c, size_t, size_t* outSize) {
  *outSize = c->input.size();
  return c->input.data();
}
media_status_t AMediaCodec_queueInputBuffer(AMediaCodec* c, size_t, int64_t, size_t size, uint64_t ptsUs, uint32_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::controls().queued.emplace_back(c->input.begin(), c->input.begin() + static_cast<std::ptrdiff_t>(size));
  fake_ndk::controls().queuedPts.push_back(static_cast<int64_t>(ptsUs));
  return AMEDIA_OK;
}
ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec*, AMediaCodecBufferInfo* info, int64_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  auto& c = fake_ndk::controls();
  if (c.outputs.empty()) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
  const fake_ndk::Controls::Output o = c.outputs.front();
  c.outputs.pop_front();
  if (o.formatChange) {
    c.outputWidth = o.width;
    c.outputHeight = o.height;
    c.outputCropRect = o.cropRect;
    c.outputCropKeys = o.cropKeys;
    c.outputCropL = o.cropL, c.outputCropT = o.cropT, c.outputCropR = o.cropR, c.outputCropB = o.cropB;
    return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
  }
  if (info) {
    info->offset = 0;
    info->size = o.size;
    info->presentationTimeUs = o.presentationTimeUs;
    info->flags = o.flags;
  }
  ++c.dequeued;
  return 0;
}
AMediaFormat* AMediaCodec_getOutputFormat(AMediaCodec*) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  auto* f = new AMediaFormat();
  const auto& c = fake_ndk::controls();
  f->width = c.outputWidth;
  f->height = c.outputHeight;
  f->cropRect = c.outputCropRect;
  f->cropKeys = c.outputCropKeys;
  f->cropL = c.outputCropL, f->cropT = c.outputCropT, f->cropR = c.outputCropR, f->cropB = c.outputCropB;
  return f;
}
media_status_t AMediaCodec_getName(AMediaCodec*, char** outName) {
  static char name[] = "fake.h264.decoder";
  *outName = name;
  return AMEDIA_OK;
}
void AMediaCodec_releaseName(AMediaCodec*, char*) {}
media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec*, size_t, bool) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  ++fake_ndk::controls().released;
  return AMEDIA_OK;
}
media_status_t AMediaCodec_releaseOutputBufferAtTime(AMediaCodec*, size_t, int64_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  ++fake_ndk::controls().released;
  return AMEDIA_OK;
}
