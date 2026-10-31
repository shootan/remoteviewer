D = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/main/cpp/'


def edit(path, reps):
    s = open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (path, a[:80], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print('ok', path)


edit(D + 'android_video_decoder.hpp', [
    ('#include <atomic>\n#include <condition_variable>\n', '#include <atomic>\n#include <chrono>\n#include <condition_variable>\n'),
    ('''#include "client_decode_snapshot.hpp"
''', '''#include "client_decode_snapshot.hpp"
#include "client_liveness_trace.hpp"
'''),
    ('''  std::string DebugStatus();
  uint64_t VideoSizePacked();''', '''  std::string DebugStatus();
  // apk-decoder-liveness r2 (test builds, GNLINK_LIVENESS_TRACE): the worker's last pass and the codec call it is in
  // right now (with mu_ released), and its tid -- lock-free. Empty without the flag.
  std::string LivenessTraceLine() const;
  uint64_t VideoSizePacked();'''),
    ('''    if (workerLock_ == nullptr || std::this_thread::get_id() != workerId_) return f();
    workerLock_->unlock();
    struct Relock {
      std::unique_lock<std::mutex>* l;
      ~Relock() { l->lock(); }
    } relock{workerLock_};
    return f();''', '''    if (workerLock_ == nullptr || std::this_thread::get_id() != workerId_) return f();
    REMOTE60_LT(ltCodecCallSinceUs_.store(LtNowUs(), std::memory_order_relaxed));
    workerLock_->unlock();
    struct Relock {
      std::unique_lock<std::mutex>* l;
      std::atomic<uint64_t>* since;
      ~Relock() {
        REMOTE60_LT(since->store(0, std::memory_order_relaxed));
        l->lock();
      }
    } relock{workerLock_, &ltCodecCallSinceUs_};
    return f();'''),
    ('''  const char* codecState_ = "none";
  uint64_t codecFailures_ = 0;
  uint64_t codecVoidStarts_ = 0;''', '''  const char* codecState_ = "none";
  uint64_t codecFailures_ = 0;
  uint64_t codecVoidStarts_ = 0;
  // apk-decoder-liveness r2 trace (written only with GNLINK_LIVENESS_TRACE).
  static uint64_t LtNowUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
  }
  std::atomic<uint64_t> ltCodecCallSinceUs_{0};
  std::atomic<uint64_t> ltWorkerPassUs_{0};
  std::atomic<uint64_t> ltWorkerTid_{0};
  std::atomic<uint64_t> ltInbox_{0};'''),
])

edit(D + 'android_video_decoder.cpp', [
    ('''  std::unique_lock<std::mutex> lk(mu_);
  workerId_ = std::this_thread::get_id();
  workerLock_ = &lk;''', '''  std::unique_lock<std::mutex> lk(mu_);
  workerId_ = std::this_thread::get_id();
  workerLock_ = &lk;
  REMOTE60_LT(ltWorkerTid_.store(remote60::native_poc::liveness_tid(), std::memory_order_relaxed));'''),
    ('''    PublishDecodeStateLocked();
    ++workerPasses_;
    doneCv_.notify_all();''', '''    PublishDecodeStateLocked();
    ++workerPasses_;
    REMOTE60_LT({
      ltWorkerPassUs_.store(LtNowUs(), std::memory_order_relaxed);
      ltInbox_.store(inbox_.size(), std::memory_order_relaxed);
    });
    doneCv_.notify_all();'''),
    ('''uint64_t AndroidVideoDecoderSink::VideoSizePacked() {''', '''std::string AndroidVideoDecoderSink::LivenessTraceLine() const {
#ifdef GNLINK_LIVENESS_TRACE
  const uint64_t now = LtNowUs();
  auto age = [now](uint64_t t) -> long long {
    return t == 0 ? -1 : static_cast<long long>((now >= t ? now - t : 0) / 1000);
  };
  char line[160];
  std::snprintf(line, sizeof(line), "sink pass=%lldms codecCall=%lldms tid=%llu inbox=%llu",
                age(ltWorkerPassUs_.load(std::memory_order_relaxed)),
                age(ltCodecCallSinceUs_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltWorkerTid_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInbox_.load(std::memory_order_relaxed)));
  return line;
#else
  return std::string();
#endif
}

uint64_t AndroidVideoDecoderSink::VideoSizePacked() {'''),
])
