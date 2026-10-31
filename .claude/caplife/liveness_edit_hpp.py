import sys
P = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/main/cpp/android_video_decoder.hpp'


def edit(path, reps):
    s = open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (a[:80], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print('ok', path)


edit(P, [
    ('''#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
''', '''#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
'''),
    ('''class AndroidVideoDecoderSink : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  AndroidVideoDecoderSink() = default;
  ~AndroidVideoDecoderSink() override;

  void SetSurface(JNIEnv* env, jobject surface);''',
     '''// apk-decoder-liveness r1 (Codex codex_apk_decoder_liveness_design_r1.md): the codec belongs to ONE thread of this
// sink, the codec worker. Every AMediaCodec_* call and every ANativeWindow release happens there, and with mu_
// RELEASED (CodecCall). mu_ is the sink's short state lock: the selection gate, the binding, the counts, the
// bounded frame inbox and the epochs -- nobody holds it across a codec call, a wait or a join. So the receive
// thread (which also carries the UDP control channel), the session's control thread and the UI hand frames,
// answers and requests over and return at once; none of them waits for a codec being created, started, stopped or
// fed. A codec result is applied only if no reset was asked for while it was made (codecResetSeq_).
// The one wait left is the UI's detach (SetSurface(null)): the app relies on the codec having let go of the
// surface when it returns (MainActivity DetachDecoder) -- the UI waits for that, never RX or control.
class AndroidVideoDecoderSink : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  AndroidVideoDecoderSink() = default;
  ~AndroidVideoDecoderSink() override;

  void SetSurface(JNIEnv* env, jobject surface);
#ifdef REMOTE60_DECODER_TEST_HOOKS
  // TEST BUILD ONLY (the Windows tests against the stand-in NDK; never defined for the APK): true once the worker
  // has taken every frame and request made before this call and finished a whole pass after it.
  bool WaitIdleForTest(int timeoutMs);
#endif'''),
    ('''  void UpdateOutputFormatLocked();
  void DrainOutputLocked();
  void ReleaseHeldOutputLocked(uint64_t nowUs);
  void StartOutputPumpLocked();
  void StopOutputPump();
  void OutputPumpMain();

  std::mutex mu_;''',
     '''  void UpdateOutputFormatLocked();
  void DrainOutputLocked();
  void ReleaseHeldOutputLocked(uint64_t nowUs);
  // apk-decoder-liveness r1: the codec worker (see the class comment).
  struct InboxFrame {
    remote60::native_poc::UdpH264AssembledFrame frame;
    bool resetPts = false;  // the receive side saw the stream generation change (the old ResetPtsStateLocked rule)
  };
  void EnsureWorkerLocked();
  void WorkerMain();
  void RequestCodecResetLocked();
  void ClearInboxLocked();
  void ApplySurfaceLocked();
  void ProcessFrameLocked(InboxFrame&& item);
  bool CodecResetPendingLocked() const { return codecResetApplied_ != codecResetSeq_; }
  // Runs f -- codec / window work -- with mu_ released when called on the worker; inline otherwise (the
  // destructor, after the worker has ended).
  template <class F>
  decltype(auto) CodecCall(F&& f) {
    if (workerLock_ == nullptr || std::this_thread::get_id() != workerId_) return f();
    workerLock_->unlock();
    struct Relock {
      std::unique_lock<std::mutex>* l;
      ~Relock() { l->lock(); }
    } relock{workerLock_};
    return f();
  }

  std::mutex mu_;'''),
    ('''  // Hands decoded frames to the display on their own schedule. Draining only when the next
  // frame arrives meant a frame scheduled 80 ms out sat in the codec until something else
  // showed up -- so the moment arrivals bunched, which is exactly when pacing matters, the
  // schedule was missed and the frame went straight to the screen.
  std::thread outputPumpThread_;
  std::atomic<bool> outputPumpStop_{false};''',
     '''  // Hands decoded frames to the display on their own schedule. Draining only when the next
  // frame arrives meant a frame scheduled 80 ms out sat in the codec until something else
  // showed up -- so the moment arrivals bunched, which is exactly when pacing matters, the
  // schedule was missed and the frame went straight to the screen.
  // apk-decoder-liveness r1: that pump is now the codec worker -- it also takes the frames and the requests.
  std::thread worker_;
  bool workerStop_ = false;                          // under mu_
  std::thread::id workerId_;                         // set by the worker under mu_
  std::unique_lock<std::mutex>* workerLock_ = nullptr;  // the worker's own lock on mu_, while it runs
  std::condition_variable workCv_;                   // frames / requests for the worker
  std::condition_variable doneCv_;                   // the worker finished a pass / a detach (UI, tests)
  uint64_t workerPasses_ = 0;                        // under mu_
  // A request to drop the codec (surface, binding, selection, stream reset, discontinuity) bumps codecResetSeq_;
  // the worker applies it and records codecResetApplied_. A codec made or fed while one was pending is void.
  uint64_t codecResetSeq_ = 0;
  uint64_t codecResetApplied_ = 0;
  // The Surface the UI asked for (its window reference handed over) and the one the worker applied.
  ANativeWindow* requestedWindow_ = nullptr;         // under mu_; owned until the worker takes it
  bool surfaceRequested_ = false;                    // under mu_: the UI's intent (a surface is set)
  uint64_t surfaceSeq_ = 0;
  uint64_t surfaceApplied_ = 0;
  uint64_t surfaceReleasedSeq_ = 0;                  // the codec stopped and the old window let go, up to this request
  std::vector<ANativeWindow*> windowsToRelease_;     // released by the worker, outside mu_
  // Frames from the receive thread, at most kInboxMaxFrames / kInboxMaxBytes; the oldest goes when full (an IDR
  // that goes is lost: KeyLost, a key asked for; the deltas after a gap are not fed before the next IDR).
  static constexpr size_t kInboxMaxFrames = 8;
  static constexpr size_t kInboxMaxBytes = 4u << 20;
  std::deque<InboxFrame> inbox_;
  size_t inboxBytes_ = 0;
  uint64_t inboxDrops_ = 0;
  uint64_t inboxGapDrops_ = 0;
  bool inboxGap_ = false;
  // What the codec itself is doing, for video_debug: none / creating / started / failed, and how often a codec
  // came back after a reset was asked for (discarded, never used).
  const char* codecState_ = "none";
  uint64_t codecFailures_ = 0;
  uint64_t codecVoidStarts_ = 0;'''),
])
