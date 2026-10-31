D = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/main/cpp/'
CR = chr(13)


def edit(path, reps):
    s = open(path, encoding='utf-8', newline='').read()
    crlf = (CR + '\n') in s
    if crlf:
        s = s.replace(CR + '\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (path, a[:80], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', CR + '\n')
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print('ok', path)


edit(D + 'android_video_decoder.hpp', [
    ('''// The one wait left is the UI's detach (SetSurface(null)): the app relies on the codec having let go of the
// surface when it returns (MainActivity DetachDecoder) -- the UI waits for that, never RX or control.''',
     '''// The one wait left is the UI's detach (SetSurface(null)): the app connects another producer to that surface only once
// the codec has let go of it. r3 (Codex design r3 D1): that wait is BOUNDED -- at most kDetachDeadlineMs for one
// handoff, never stacked: a detach that does not finish in time returns its request as still pending (negative), the
// app leaves the surface BLOCKED and releases nothing of it, and learns of the late completion from
// SurfaceReleasedSeq(). While a detach is pending no new surface is taken (SetSurface(non-null) answers 0).'''),
    ('''  void SetSurface(JNIEnv* env, jobject surface);
#ifdef REMOTE60_DECODER_TEST_HOOKS''', '''  // r3 D1. surface != null: the request's seq (> 0), or 0 when refused (a detach is still pending, or too many
  // windows are still to be let go). surface == null: the request's seq when the codec stopped and the old window was
  // let go within the deadline; -seq when not (pending: SurfaceReleasedSeq() reaches seq once it is done).
  int64_t SetSurface(JNIEnv* env, jobject surface);
  static constexpr int kDetachDeadlineMs = 2000;
  // Every surface request up to this seq is done: the codec of the old surface stopped and deleted, its window let
  // go. Lock-free (the UI polls it while a detach is pending).
  uint64_t SurfaceReleasedSeq() const { return surfaceReleasedAtomic_.load(std::memory_order_acquire); }
#ifdef REMOTE60_DECODER_TEST_HOOKS'''),
    ('''  uint64_t surfaceReleasedSeq_ = 0;                  // the codec stopped and the old window let go, up to this request''',
     '''  uint64_t surfaceReleasedSeq_ = 0;                  // the codec stopped and the old window let go, up to this request
  std::atomic<uint64_t> surfaceReleasedAtomic_{0};   // the same, for lock-free readers
  uint64_t lastDetachSeq_ = 0;                       // the latest SetSurface(null) (under mu_)
  void MarkSurfaceReleasedLocked(uint64_t seq) {
    surfaceReleasedSeq_ = seq;
    surfaceReleasedAtomic_.store(seq, std::memory_order_release);
  }'''),
])

edit(D + 'android_video_decoder.cpp', [
    ('''      if (surfaceReleasedSeq_ != surfaceApplied_) {
        surfaceReleasedSeq_ = surfaceApplied_;
        doneCv_.notify_all();
      }
      continue;
    }
    if (surfaceReleasedSeq_ != surfaceApplied_) {
      surfaceReleasedSeq_ = surfaceApplied_;
      doneCv_.notify_all();
    }''', '''      if (surfaceReleasedSeq_ != surfaceApplied_) {
        MarkSurfaceReleasedLocked(surfaceApplied_);
        doneCv_.notify_all();
      }
      continue;
    }
    if (surfaceReleasedSeq_ != surfaceApplied_) {
      MarkSurfaceReleasedLocked(surfaceApplied_);
      doneCv_.notify_all();
    }'''),
    ('''// apk-decoder-liveness r1: the UI hands the Surface's window over and returns -- the codec for it is made by the
// worker. A detach (null) waits for the worker to have stopped the codec and let go of the old window: the app
// connects another producer to that surface right after (MainActivity DetachDecoder). Only the UI waits here.
void AndroidVideoDecoderSink::SetSurface(JNIEnv* env, jobject surface) {
  ANativeWindow* window = nullptr;
  if (surface != nullptr) {
    window = ANativeWindow_fromSurface(env, surface);
    if (!window) log_error("failed to acquire ANativeWindow from Surface");
  }
  std::unique_lock<std::mutex> lk(mu_);
  if (requestedWindow_) windowsToRelease_.push_back(requestedWindow_);  // asked for, never applied''',
     '''// apk-decoder-liveness r1: the UI hands the Surface's window over and returns -- the codec for it is made by the
// worker. r3 D1: a detach (null) waits for the worker to have stopped the codec and let go of the old window -- at
// most kDetachDeadlineMs, and not at all when an earlier detach is still pending (never stacked). Only the UI waits.
int64_t AndroidVideoDecoderSink::SetSurface(JNIEnv* env, jobject surface) {
  constexpr size_t kMaxWindowsToRelease = 4;
  ANativeWindow* window = nullptr;
  if (surface != nullptr) {
    window = ANativeWindow_fromSurface(env, surface);
    if (!window) log_error("failed to acquire ANativeWindow from Surface");
  }
  std::unique_lock<std::mutex> lk(mu_);
  if (window && (surfaceReleasedSeq_ < lastDetachSeq_ || windowsToRelease_.size() >= kMaxWindowsToRelease)) {
    // A producer may still be on the old surface (or the worker is behind on letting windows go): no new surface is
    // taken. The app keeps that surface BLOCKED; this reference was never handed over -- give it back here.
    char line[160];
    std::snprintf(line, sizeof(line), "video surface refused: detach pending (released=%llu detach=%llu) toRelease=%zu",
                  static_cast<unsigned long long>(surfaceReleasedSeq_), static_cast<unsigned long long>(lastDetachSeq_),
                  windowsToRelease_.size());
    lk.unlock();
    log_info(line);
    ANativeWindow_release(window);
    return 0;
  }
  const bool earlierDetachPending = surfaceReleasedSeq_ < lastDetachSeq_;
  if (requestedWindow_) windowsToRelease_.push_back(requestedWindow_);  // asked for, never applied'''),
    ('''  const uint64_t seq = ++surfaceSeq_;
  ++codecResetSeq_;
  EnsureWorkerLocked();''', '''  const uint64_t seq = ++surfaceSeq_;
  if (!window) lastDetachSeq_ = seq;
  ++codecResetSeq_;
  EnsureWorkerLocked();'''),
    ('''  PublishDecodeStateLocked();
  workCv_.notify_all();
  if (window) return;
  const auto t0 = std::chrono::steady_clock::now();
  doneCv_.wait(lk, [&] { return surfaceReleasedSeq_ >= seq || workerStop_; });
  const auto waitedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  if (waitedMs >= 50) {
    char line[128];
    std::snprintf(line, sizeof(line), "video surface detach waited ms=%lld (the UI only)",
                  static_cast<long long>(waitedMs));
    log_info(line);
  }
}''', '''  PublishDecodeStateLocked();
  workCv_.notify_all();
  if (window) return static_cast<int64_t>(seq);
  if (earlierDetachPending) {
    lk.unlock();
    log_info("video surface cleared: an earlier detach is still pending -- not waited for again");
    return -static_cast<int64_t>(seq);
  }
  const auto t0 = std::chrono::steady_clock::now();
  const bool done = doneCv_.wait_for(lk, std::chrono::milliseconds(kDetachDeadlineMs),
                                     [&] { return surfaceReleasedSeq_ >= seq || workerStop_; }) &&
                    surfaceReleasedSeq_ >= seq;
  lk.unlock();
  const auto waitedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  if (!done || waitedMs >= 50) {
    char line[160];
    std::snprintf(line, sizeof(line), "video surface detach %s seq=%llu waited ms=%lld (the UI only)",
                  done ? "done" : "PENDING past the deadline", static_cast<unsigned long long>(seq),
                  static_cast<long long>(waitedMs));
    log_info(line);
  }
  return done ? static_cast<int64_t>(seq) : -static_cast<int64_t>(seq);
}'''),
])

edit(D + 'native_bridge.cpp', [
    ('''extern "C" JNIEXPORT void JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSetSurface(
    JNIEnv* env, jobject /* this */, jobject surface) {
  g_video_decoder_sink.SetSurface(env, surface);
}
''', '''// apk-decoder-liveness r3 D1: see AndroidVideoDecoderSink::SetSurface -- > 0 done / taken, 0 refused, < 0 a detach
// still pending past its deadline (nativeSurfaceReleasedSeq reaches its seq once done).
extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSetSurface(
    JNIEnv* env, jobject /* this */, jobject surface) {
  return static_cast<jlong>(g_video_decoder_sink.SetSurface(env, surface));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeSurfaceReleasedSeq(
    JNIEnv* /* env */, jobject /* this */) {
  return static_cast<jlong>(g_video_decoder_sink.SurfaceReleasedSeq());
}
'''),
])
