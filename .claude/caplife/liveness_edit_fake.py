D = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/test_fake_ndk/'


def edit(path, reps):
    s = open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (a[:90], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print('ok', path)


edit(D + 'fake_ndk.hpp', [
    ('''#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
''', '''#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
'''),
    ('''  int dequeued = 0;          // pictures handed out
  int released = 0;          // releaseOutputBuffer / AtTime calls
};

std::mutex& mu();
Controls& controls();  // take mu() around reads/writes from the test
''', '''  int dequeued = 0;          // pictures handed out
  int released = 0;          // releaseOutputBuffer / AtTime calls
  // apk-decoder-liveness r1: who touched the codec, what was made and let go, and which window a codec was configured
  // with (the stand-in's window is the Surface pointer itself).
  std::set<std::thread::id> codecThreads;
  int codecsDeleted = 0;
  int windowsAcquired = 0;
  int windowsReleased = 0;
  const void* lastConfiguredWindow = nullptr;
};

std::mutex& mu();
Controls& controls();  // take mu() around reads/writes from the test

// apk-decoder-liveness r1: a slow codec, deterministically. While `held`, AMediaCodec_createDecoderByType /
// configure / start / stop (the ones whose `which` bit is set) wait -- WITHOUT mu() -- until the test lets go.
// `waiting` counts the calls blocked there right now.
enum HoldWhich : uint32_t { kHoldCreate = 1, kHoldConfigure = 2, kHoldStart = 4, kHoldStop = 8 };
struct Barrier {
  std::atomic<uint32_t> which{0};
  std::atomic<bool> held{false};
  std::atomic<int> waiting{0};
  std::atomic<int> passed{0};  // calls that waited and went on
};
Barrier& barrier();
'''),
])

edit(D + 'fake_ndk.cpp', [
    ('''Controls& controls() {
  static Controls c;
  return c;
}
}  // namespace fake_ndk
''', '''Controls& controls() {
  static Controls c;
  return c;
}
Barrier& barrier() {
  static Barrier b;
  return b;
}
namespace {
// Called with mu() NOT held.
void hold_if(uint32_t bit) {
  Barrier& b = barrier();
  if ((b.which.load() & bit) == 0 || !b.held.load()) return;
  b.waiting.fetch_add(1);
  while (b.held.load() && (b.which.load() & bit) != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  b.waiting.fetch_sub(1);
  b.passed.fetch_add(1);
}
void note_thread() { controls().codecThreads.insert(std::this_thread::get_id()); }  // with mu() held
}  // namespace
}  // namespace fake_ndk
'''),
    ('''#include <cstdio>
#include <cstring>
''', '''#include <chrono>
#include <cstdio>
#include <cstring>
'''),
    ('''ANativeWindow* ANativeWindow_fromSurface(JNIEnv*, jobject surface) { return static_cast<ANativeWindow*>(surface); }
void ANativeWindow_release(ANativeWindow*) {}''', '''ANativeWindow* ANativeWindow_fromSurface(JNIEnv*, jobject surface) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  if (surface) ++fake_ndk::controls().windowsAcquired;
  return static_cast<ANativeWindow*>(surface);
}
void ANativeWindow_release(ANativeWindow* w) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  if (w) ++fake_ndk::controls().windowsReleased;
}'''),
    ('''AMediaCodec* AMediaCodec_createDecoderByType(const char*) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  if (fake_ndk::controls().createFails) return nullptr;''', '''AMediaCodec* AMediaCodec_createDecoderByType(const char*) {
  fake_ndk::hold_if(fake_ndk::kHoldCreate);
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::note_thread();
  if (fake_ndk::controls().createFails) return nullptr;'''),
    ('''media_status_t AMediaCodec_configure(AMediaCodec*, const AMediaFormat*, ANativeWindow*, AMediaCrypto*, uint32_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  return fake_ndk::controls().configureFails ? AMEDIA_ERROR_UNKNOWN : AMEDIA_OK;
}
media_status_t AMediaCodec_start(AMediaCodec* c) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());''', '''media_status_t AMediaCodec_configure(AMediaCodec*, const AMediaFormat*, ANativeWindow* w, AMediaCrypto*, uint32_t) {
  fake_ndk::hold_if(fake_ndk::kHoldConfigure);
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::note_thread();
  if (fake_ndk::controls().configureFails) return AMEDIA_ERROR_UNKNOWN;
  fake_ndk::controls().lastConfiguredWindow = w;
  return AMEDIA_OK;
}
media_status_t AMediaCodec_start(AMediaCodec* c) {
  fake_ndk::hold_if(fake_ndk::kHoldStart);
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::note_thread();'''),
    ('''media_status_t AMediaCodec_stop(AMediaCodec* c) {
  c->started = false;
  return AMEDIA_OK;
}
media_status_t AMediaCodec_delete(AMediaCodec* c) {
  delete c;
  return AMEDIA_OK;
}
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec*, int64_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());''', '''media_status_t AMediaCodec_stop(AMediaCodec* c) {
  fake_ndk::hold_if(fake_ndk::kHoldStop);
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::note_thread();
  c->started = false;
  return AMEDIA_OK;
}
media_status_t AMediaCodec_delete(AMediaCodec* c) {
  {
    std::lock_guard<std::mutex> lk(fake_ndk::mu());
    fake_ndk::note_thread();
    ++fake_ndk::controls().codecsDeleted;
  }
  delete c;
  return AMEDIA_OK;
}
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec*, int64_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::note_thread();'''),
    ('''media_status_t AMediaCodec_queueInputBuffer(AMediaCodec* c, size_t, int64_t, size_t size, uint64_t ptsUs, uint32_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());''', '''media_status_t AMediaCodec_queueInputBuffer(AMediaCodec* c, size_t, int64_t, size_t size, uint64_t ptsUs, uint32_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::note_thread();'''),
    ('''ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec*, AMediaCodecBufferInfo* info, int64_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());''', '''ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec*, AMediaCodecBufferInfo* info, int64_t) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::note_thread();'''),
])
