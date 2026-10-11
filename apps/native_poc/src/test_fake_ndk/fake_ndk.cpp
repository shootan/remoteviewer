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
  int unused = 0;
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
bool AMediaFormat_getInt32(AMediaFormat*, const char*, int32_t*) { return false; }

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
media_status_t AMediaCodec_queueInputBuffer(AMediaCodec* c, size_t, int64_t, size_t size, uint64_t, uint32_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::controls().queued.emplace_back(c->input.begin(), c->input.begin() + static_cast<std::ptrdiff_t>(size));
  return AMEDIA_OK;
}
ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec*, AMediaCodecBufferInfo*, int64_t) {
  return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
}
AMediaFormat* AMediaCodec_getOutputFormat(AMediaCodec*) { return new AMediaFormat(); }
media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec*, size_t, bool) { return AMEDIA_OK; }
media_status_t AMediaCodec_releaseOutputBufferAtTime(AMediaCodec*, size_t, int64_t) { return AMEDIA_OK; }
