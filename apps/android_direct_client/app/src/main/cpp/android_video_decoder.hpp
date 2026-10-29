#pragma once

#include <jni.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "client_decode_snapshot.hpp"
#include "client_liveness_trace.hpp"
#include "native_video_client_session.hpp"
#include "decoder_pending_slot.hpp"
#include "selection_ack_gate.hpp"
#include "video_playout_clock.hpp"

struct ANativeWindow;
struct AMediaCodec;

namespace remote60::android_direct {

// apk-decoder-liveness r1 (Codex codex_apk_decoder_liveness_design_r1.md): the codec belongs to ONE thread of this
// sink, the codec worker. Every AMediaCodec_* call and every ANativeWindow release happens there, and with mu_
// RELEASED (CodecCall). mu_ is the sink's short state lock: the selection gate, the binding, the counts, the
// bounded frame inbox and the epochs -- nobody holds it across a codec call, a wait or a join. So the receive
// thread (which also carries the UDP control channel), the session's control thread and the UI hand frames,
// answers and requests over and return at once; none of them waits for a codec being created, started, stopped or
// fed. A codec result is applied only if no reset was asked for while it was made (codecResetSeq_).
// The one wait left is the UI's detach (SetSurface(null)): the app connects another producer to that surface only once
// the codec has let go of it. r3 (Codex design r3 D1): that wait is BOUNDED -- at most kDetachDeadlineMs for one
// handoff, never stacked: a detach that does not finish in time returns its request as still pending (negative), the
// app leaves the surface BLOCKED and releases nothing of it, and learns of the late completion from
// SurfaceReleasedSeq(). While a detach is pending no new surface is taken (SetSurface(non-null) answers 0).
class AndroidVideoDecoderSink : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  AndroidVideoDecoderSink() = default;
  ~AndroidVideoDecoderSink() override;

  // r3 D1. surface != null: the request's seq (> 0), or 0 when refused (a detach is still pending, too many
  // windows are still to be let go, or -- r4 -- no ANativeWindow could be had for it: nothing of the current surface,
  // codec or detach changes). surface == null: the request's seq when the codec stopped and the old window was
  // let go within the deadline; -seq when not (pending: SurfaceReleasedSeq() reaches seq once it is done).
  // A surface whose detach went past the deadline stays BLOCKED until it is replaced: its late completion only
  // releases it -- it never attaches anything, and the same blocked surface is not taken back by a later bind. A new
  // surface that came while the detach was pending is attached at its own next bind, after the completion.
  int64_t SetSurface(JNIEnv* env, jobject surface);
  static constexpr int kDetachDeadlineMs = 2000;
  // Every surface request up to this seq is done: the codec of the old surface stopped and deleted, its window let
  // go. Lock-free (the UI polls it while a detach is pending).
  uint64_t SurfaceReleasedSeq() const { return surfaceReleasedAtomic_.load(std::memory_order_acquire); }
#ifdef REMOTE60_DECODER_TEST_HOOKS
  // TEST BUILD ONLY (the Windows tests against the stand-in NDK; never defined for the APK): true once the worker
  // has taken every frame and request made before this call and finished a whole pass after it.
  bool WaitIdleForTest(int timeoutMs);
#endif
  /** One call per frame the view latched. Lock-free: this runs on the UI thread every vsync
   *  and must never contend with the decoder. */
  void NotifyFrameDisplayed() {
    displayedFrameCount_.fetch_add(1, std::memory_order_relaxed);
  }
  /** sh-filter r1 (S4): the CLOCK_MONOTONIC ns at which the frame whose output buffer carries this
   *  timestamp was handed to the output Surface; 0 when it is not among the last kHandoffRing. The
   *  timestamp is a KEY (the same one the TextureView's SurfaceTexture reports at latch, through the
   *  direct path or the sharpening pass), not a clock. Lock-free reads, for the UI thread. */
  int64_t HandoffNsForTimestamp(int64_t timestampNs) const {
    HandoffInfo info;
    return HandoffInfoForTimestamp(timestampNs, &info) ? info.handoffNs : 0;
  }
  /** sh-filter r2 (H2/H3/H5.3): everything known about the frame released with this timestamp -- when it
   *  was handed over, the stream (selection) generation and codec instance it came from, and its VISIBLE
   *  size (after crop). false when it is not among the last kHandoffRing. Lock-free (any thread). */
  struct HandoffInfo {
    int64_t handoffNs = 0;
    uint32_t streamGeneration = 0;
    uint32_t codecEpoch = 0;
    uint32_t visibleWidth = 0;
    uint32_t visibleHeight = 0;
  };
  bool HandoffInfoForTimestamp(int64_t timestampNs, HandoffInfo* out) const {
    if (timestampNs == 0) return false;
    for (size_t i = 0; i < kHandoffRing; ++i) {
      if (handoffKey_[i].load(std::memory_order_acquire) != timestampNs) continue;
      HandoffInfo info;
      info.handoffNs = handoffNs_[i].load(std::memory_order_acquire);
      const uint64_t meta = handoffMeta_[i].load(std::memory_order_acquire);
      const uint64_t size = handoffSize_[i].load(std::memory_order_acquire);
      // The slot is rewritten key=0 first and key last: the same key after the reads means a consistent set.
      if (handoffKey_[i].load(std::memory_order_acquire) != timestampNs) continue;
      info.streamGeneration = static_cast<uint32_t>(meta >> 32);
      info.codecEpoch = static_cast<uint32_t>(meta & 0xffffffffu);
      info.visibleWidth = static_cast<uint32_t>(size >> 32);
      info.visibleHeight = static_cast<uint32_t>(size & 0xffffffffu);
      *out = info;
      return true;
    }
    return false;
  }
  /** sh-filter r2 (H2): codec instances started so far; a frame of a codec started after a given moment has
   *  a larger epoch. Lock-free. */
  uint32_t CodecEpoch() const { return codecEpoch_.load(std::memory_order_acquire); }
  void SetTargetFps(uint32_t fps);
  // keepalive-multi r1: one decoder and one Surface, used by one PC connection at a time -- its BINDING (a
  // process-unique token for: entry handle, connection generation, activation, Surface). Each PC's controller talks
  // to this sink through its own adapter carrying the token it was bound with; every call below checks that token
  // against the current binding INSIDE this sink's lock, before it touches anything (no check-then-call gap). A
  // parked or closed PC's late callback -- a reset, a frame, a selection answer, a stats drain -- does nothing.
  // kAnyBinding: the direct (unbound) caller of the virtual methods (tests, single-session use) -- always the owner.
  static constexpr uint64_t kAnyBinding = ~0ull;
  // A new binding: the codec, the selection gate and every count start over, for this token only. 0 = nobody.
  void Bind(uint64_t token);
  // Ends `token`'s binding if it is still the current one (true); a stale token changes nothing.
  bool Unbind(uint64_t token);
  uint64_t Binding();

  void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) override {
    OnEncodedH264FrameAs(kAnyBinding, std::move(frame));
  }
  void OnVideoStreamReset() override { OnVideoStreamResetAs(kAnyBinding); }
  void OnVideoDiscontinuity() override { OnVideoDiscontinuityAs(kAnyBinding); }
  bool ConsumeDecoderKeyframeRequest() override { return ConsumeDecoderKeyframeRequestAs(kAnyBinding); }
  bool DrainPresentationStats(remote60::native_poc::ClientPresentationStats* out) override {
    return DrainPresentationStatsAs(kAnyBinding, out);
  }
  bool DrainDecodeStats(remote60::native_poc::ClientDecodeStats* out) override {
    return DrainDecodeStatsAs(kAnyBinding, out);
  }
  uint64_t DecodeGeneration() override { return DecodeGenerationAs(kAnyBinding); }
  void OnDecodeEpisodeStart() override { OnDecodeEpisodeStartAs(kAnyBinding); }
  void OnWindowSelectionControlResult(const remote60::native_poc::ControlWindowSelectedMessage& msg) override {
    OnWindowSelectionControlResultAs(kAnyBinding, msg);
  }
  void PrepareForWindowSelection(uint64_t selectionGeneration) {
    PrepareForWindowSelectionAs(kAnyBinding, selectionGeneration);
  }
  // t-970r4zgo: the selection a select request is queued under, and its answer for that selection.
  uint64_t CurrentSelectionTag() override { return CurrentSelectionTagAs(kAnyBinding); }
  uint64_t KeyframeOwedFor() override { return KeyframeOwedForAs(kAnyBinding); }
  void OnWindowSelectionControlResultFor(const remote60::native_poc::ControlWindowSelectedMessage& msg,
                                         uint64_t requestTag) override {
    OnWindowSelectionControlResultForAs(kAnyBinding, msg, requestTag);
  }
  void AbortWindowSelection() { AbortWindowSelectionAs(kAnyBinding); }
  // apk-0232 r1/r3: a desktop pick at a host at the logon screen (no answer comes): the armed selection continues as a
  // local one, bound by its own first IDR that reaches the codec (SelectionAckGate::BeginLocal); its completion is
  // judged and recorded under the state Prepare/Abort take. keepalive-r2 merge: owner-checked (*As) as below.
  bool BeginLocalSelectionFor(uint64_t tag) override { return BeginLocalSelectionForAs(kAnyBinding, tag); }
  uint64_t LocalSelectionBoundFor(uint64_t tag) override { return LocalSelectionBoundForAs(kAnyBinding, tag); }
  void RefuseSelectionFor(uint64_t tag) override { RefuseSelectionForAs(kAnyBinding, tag); }
  LocalCommit CommitLocalSelectionFor(uint64_t tag, const std::function<void(uint64_t generation)>& commit) override {
    return CommitLocalSelectionForAs(kAnyBinding, tag, commit);
  }

  // The same, for `token` only (a non-owner gets the empty answer: no frame taken, nothing reset, false / 0).
  void OnEncodedH264FrameAs(uint64_t token, remote60::native_poc::UdpH264AssembledFrame&& frame);
  void OnVideoStreamResetAs(uint64_t token);
  void OnVideoDiscontinuityAs(uint64_t token);
  bool ConsumeDecoderKeyframeRequestAs(uint64_t token);
  bool DrainPresentationStatsAs(uint64_t token, remote60::native_poc::ClientPresentationStats* out);
  bool DrainDecodeStatsAs(uint64_t token, remote60::native_poc::ClientDecodeStats* out);
  uint64_t DecodeGenerationAs(uint64_t token);
  void OnDecodeEpisodeStartAs(uint64_t token);
  void OnWindowSelectionControlResultAs(uint64_t token, const remote60::native_poc::ControlWindowSelectedMessage& msg);
  bool PrepareForWindowSelectionAs(uint64_t token, uint64_t selectionGeneration);
  uint64_t CurrentSelectionTagAs(uint64_t token);
  uint64_t KeyframeOwedForAs(uint64_t token);
  void OnWindowSelectionControlResultForAs(uint64_t token, const remote60::native_poc::ControlWindowSelectedMessage& msg,
                                           uint64_t requestTag);
  bool AbortWindowSelectionAs(uint64_t token);
  bool BeginLocalSelectionForAs(uint64_t token, uint64_t tag);
  uint64_t LocalSelectionBoundForAs(uint64_t token, uint64_t tag);
  void RefuseSelectionForAs(uint64_t token, uint64_t tag);
  LocalCommit CommitLocalSelectionForAs(uint64_t token, uint64_t tag, const std::function<void(uint64_t generation)>& commit);
  std::string DebugStatus();
  // apk-decoder-liveness r2 (test builds, GNLINK_LIVENESS_TRACE): the worker's last pass and the codec call it is in
  // right now (with mu_ released), and its tid -- lock-free. Empty without the flag.
  std::string LivenessTraceLine() const;
  uint64_t VideoSizePacked();
  uint64_t ReadySelectionGeneration();
  uint64_t LastOutputPresentationUs();

 private:
  bool OwnerLocked(uint64_t token) const { return token == kAnyBinding || token == binding_; }
  void ResetForBindingLocked();
  void PumpCodecLocked();
  bool TryQueueFrameLocked(remote60::native_poc::UdpH264AssembledFrame& frame);
  bool EnsureCodecLocked(uint32_t width, uint32_t height, bool keyFrame = false);
  uint64_t ComputeQueuedPtsUsLocked(const remote60::native_poc::UdpH264AssembledFrame& frame);
  void ResetPtsStateLocked();
  void ResetCodecLocked();
  void ReleaseSurfaceLocked();
  bool UpdateCodecConfigLocked(const std::vector<uint8_t>& annexb);
  void UpdateOutputFormatLocked();
  void DrainOutputLocked();
  void ReleaseHeldOutputLocked(uint64_t nowUs);
  // apk-decoder-liveness r1: the codec worker (see the class comment).
  struct InboxFrame {
    remote60::native_poc::UdpH264AssembledFrame frame;
    bool resetPts = false;  // the receive side saw the stream generation change (the old ResetPtsStateLocked rule)
    uint64_t enqueueUs = 0;  // apk-decoder-inbox r1: set only in test builds (GNLINK_LIVENESS_TRACE)
  };
  void EnsureWorkerLocked();
  void WorkerMain();
  void RequestCodecResetLocked();
  void ClearInboxLocked();
  void ApplySurfaceLocked();
  void ProcessFrameLocked(InboxFrame&& item);
  bool CodecResetPendingLocked() const { return codecResetApplied_ != codecResetSeq_; }
  // Runs f -- codec / window work -- with mu_ released when called on the worker; inline otherwise (the
  // destructor, after the worker has ended). apk-decoder-inbox r1: `kind` says which codec API it is, for the
  // test-build latency per kind (count / total / max of the call itself, mu_ released).
  enum ApiKind : int {
    kApiOther = 0, kApiCreate, kApiStopDelete, kApiName, kApiDequeueIn, kApiQueueIn, kApiFormat, kApiRelease,
    kApiDequeueOut, kApiWindowRelease, kApiKinds
  };
  template <class F>
  decltype(auto) CodecCall(F&& f) {
    return CodecCall(kApiOther, std::forward<F>(f));
  }
  template <class F>
  decltype(auto) CodecCall(int kind, F&& f) {
    if (workerLock_ == nullptr || std::this_thread::get_id() != workerId_) return f();
    REMOTE60_LT(ltCodecCallSinceUs_.store(LtNowUs(), std::memory_order_relaxed));
    workerLock_->unlock();
    struct Relock {
      std::unique_lock<std::mutex>* l;
      std::atomic<uint64_t>* since;
      AndroidVideoDecoderSink* s;
      int kind;
      ~Relock() {
        REMOTE60_LT({
          const uint64_t t0 = since->load(std::memory_order_relaxed);
          const uint64_t d = t0 ? LtNowUs() - t0 : 0;
          s->ltApiCount_[kind].fetch_add(1, std::memory_order_relaxed);
          s->ltApiTotalUs_[kind].fetch_add(d, std::memory_order_relaxed);
          LtMax(s->ltApiMaxUs_[kind], d);
          since->store(0, std::memory_order_relaxed);
        });
        l->lock();
      }
    } relock{workerLock_, &ltCodecCallSinceUs_, this, kind};
    return f();
  }

  std::mutex mu_;
  uint64_t binding_ = 0;  // keepalive-multi r1: under mu_
  // mobile-abr r1: every section under mu_ ends by publishing what the session's control thread reads
  // WITHOUT mu_ (DrainDecodeStats) -- that thread must never wait on a lock the codec work holds.
  void PublishDecodeStateLocked();
  class LockedState {
   public:
    explicit LockedState(AndroidVideoDecoderSink* s) : s_(s), lock_(s->mu_) {}
    ~LockedState() { s_->PublishDecodeStateLocked(); }  // runs before lock_ is released
    LockedState(const LockedState&) = delete;
    LockedState& operator=(const LockedState&) = delete;

   private:
    AndroidVideoDecoderSink* s_;
    std::lock_guard<std::mutex> lock_;
  };
  // r3 R2: (valid, count, size, generation) as one consistent copy (client_decode_snapshot.hpp). The
  // generation bumps whenever validity / the surface / the selection changes.
  remote60::native_poc::ClientDecodeSnapshot decodeSnapshot_;
  uint64_t decodeGeneration_ = 0;     // under mu_
  uint64_t decodeKeyHash_ = 0;        // under mu_: what the generation last changed on
  bool decodeKeySet_ = false;
  // r5 (Codex F1 contract): the startup grace of an active transition. A grace begins at an explicit
  // episode start (connection, stream resumed: always) or a transition inside active viewing (selection
  // prepared, surface back, a switch abandoned while active: only if a picture has been counted since the
  // last grace -- so prepare / abort / retry / codec retries / loss resets cannot stretch it). It ends at
  // the first counted picture, or by itself at the deadline, which the reader observes on its own clock
  // (no frame, poll or lock needed).
  // r6 (Codex S1): where the valid measurement now in progress began (client_decode_snapshot.hpp) -- a
  // new generation, a grace's deadline, or the picture that ended a grace. The drain reports the count
  // and the time since then together, and keeps no state of its own.
  static constexpr uint64_t kStartupGraceUs = 2000000ULL;
  bool graceActive_ = false;
  bool graceEligible_ = true;
  uint64_t graceDeadlineUs_ = 0;
  remote60::native_poc::ClientDecodeSpanStart span_;  // under mu_
  void StartGraceLocked(bool explicitEpisode);
  ANativeWindow* window_ = nullptr;
  AMediaCodec* codec_ = nullptr;
  uint32_t configuredWidth_ = 0;
  uint32_t configuredHeight_ = 0;
  uint32_t outputWidth_ = 0;
  uint32_t outputHeight_ = 0;
  uint64_t inputFrameCount_ = 0;
  uint64_t outputFrameCount_ = 0;
  // mobile-abr r1 (Codex contract section 3): valid decoded pictures, counted where the decoder hands
  // one out (dequeueOutputBuffer >= 0; not a codec-config buffer, not an end-of-stream buffer without a
  // picture) -- not when it is released to the display later. Never reset: outputFrameCount_ is reset
  // with the codec.
  // r3 R1: the selection's bootstrap IDR re-queued into a codec that had not answered yet is the same
  // picture again, but the playout clock gives each queue a NEW PTS. The PTS of every replay queue that
  // succeeded is remembered here (a few at most -- kBootstrapReplayMaxCount per selection) and its output
  // is counted apart, in replayOutputs_. Forgotten with the codec.
  uint64_t decodedTotal_ = 0;
  uint64_t replayOutputs_ = 0;
  bool queueingReplay_ = false;
  static constexpr size_t kReplayPtsSlots = 8;
  int64_t replayPts_[kReplayPtsSlots] = {};
  size_t replayPtsCount_ = 0;
  void RememberReplayPtsLocked(int64_t ptsUs);
  bool TakeReplayPtsLocked(int64_t ptsUs);
  // Which selection is armed, whether its answer is still awaited and which stream generation it
  // answered with (selection_ack_gate.hpp; shared with the Windows tests).
  remote60::native_poc::SelectionAckGate gate_;
  uint64_t readySelectionGeneration_ = 0;
  uint64_t latestInputStreamGeneration_ = 0;
  uint64_t latestOutputStreamGeneration_ = 0;
  uint64_t lastOutputPresentationUs_ = 0;
  // Decides when each frame reaches the screen. Shared with the desktop viewer and unit
  // tested against recorded arrival patterns, because it is control logic whose failure mode
  // is "looks slightly wrong on a device you are not holding".
  remote60::native_poc::VideoPlayoutClock playoutClock_;
  // Hands decoded frames to the display on their own schedule. Draining only when the next
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
  std::atomic<uint64_t> surfaceReleasedAtomic_{0};   // the same, for lock-free readers
  uint64_t lastDetachSeq_ = 0;                       // the latest SetSurface(null) (under mu_)
  void MarkSurfaceReleasedLocked(uint64_t seq) {
    surfaceReleasedSeq_ = seq;
    surfaceReleasedAtomic_.store(seq, std::memory_order_release);
  }
  std::vector<ANativeWindow*> windowsToRelease_;     // released by the worker, outside mu_
  // Frames from the receive thread, at most kInboxMaxFrames / kInboxMaxBytes; the oldest goes when full (an IDR
  // that goes is lost: KeyLost, a key asked for; the deltas after a gap are not fed before the next IDR).
  // r4 N2: both bounds hold always -- a single frame over kInboxMaxBytes is refused at the door (lost the same way).
  // The worker holds at most four more frames outside the inbox, each within the same cap: the one being processed,
  // the one held for an input buffer (pendingFrame_), the selection's bootstrap IDR (bootstrapFrame_) and its replay
  // copy during a replay -- at most kInboxMaxBytes + 4 x kInboxMaxBytes = 20 MB of frame PAYLOAD in the sink. r5
  // (Codex src r4): a logical payload budget, not a bound on the process's memory -- vector capacity, allocator
  // overhead, the assembler's and the codec's own buffers are outside it.
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
  uint64_t codecVoidStarts_ = 0;
  // r4 N3 (Codex src r1): a codec that could not be made is tried again by the worker itself -- a host that sends
  // nothing more (its IDRs spent on the failed attempts, the session's owed asks used up) would otherwise leave the
  // decoder without a codec for good. Every kCodecRetryIntervalUs, at most kCodecAutoRetryMax times per surface /
  // selection / binding; a codec made this way re-arms the owed IDR (SelectionAckGate::Rearm: a NEW token, the
  // session's same bounded asks once more -- the rule a new surface already had). After the last try the state is
  // failed_terminal and logged once: from there only a new surface, a new selection, a new binding or a stream
  // reset brings a codec (r5: an arriving frame does not try either). Nothing here asks the host for anything by
  // itself.
  static constexpr uint64_t kCodecRetryIntervalUs = 1000000ULL;
  static constexpr uint32_t kCodecAutoRetryMax = 20;
  // r5 (Codex src r4): ONE budget for every attempt after a failure -- a frame's and the timer's alike
  // (EnsureCodecLocked); before the deadline only an IDR may try, never a delta or the timer.
  uint64_t codecRetryAtUs_ = 0;    // 0: no failure pending
  uint32_t codecAutoRetries_ = 0;  // tries made after the failure, by any path
  bool codecTerminal_ = false;     // the budget is spent: no try until ResetCodecRetryLocked
  void ResetCodecRetryLocked() {
    codecRetryAtUs_ = 0;
    codecAutoRetries_ = 0;
    codecTerminal_ = false;
  }
  void RetryCodecLocked();
  // r4 N1 / N2 diagnostics: exchanges voided by a selection / codec change, frames refused for being over the cap.
  uint64_t queueVoided_ = 0;
  uint64_t inboxOversizeDrops_ = 0;
  // apk-decoder-liveness r2 trace (written only with GNLINK_LIVENESS_TRACE).
  static uint64_t LtNowUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
  }
  std::atomic<uint64_t> ltCodecCallSinceUs_{0};
  // apk-decoder-inbox r1 (Codex codex_apk_decoder_inbox_decision_r1.md (3)), written only with GNLINK_LIVENESS_TRACE:
  // the inbox's high-water, the age of a frame when the worker takes it and when it goes into the codec, how much a
  // worker pass takes from the inbox and how long that takes, the receive thread's wait for / hold of mu_, and each
  // codec API call's own time by kind. Maxima since the start.
  static void LtMax(std::atomic<uint64_t>& a, uint64_t v) {
    uint64_t cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
  }
  struct LtAgeHist {
    std::atomic<uint64_t> maxUs{0};
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> bucket[6] = {};  // ms: <50 <100 <250 <500 <1000 >=1000
  };
  static void LtAge(LtAgeHist& h, uint64_t us) {
    LtMax(h.maxUs, us);
    h.count.fetch_add(1, std::memory_order_relaxed);
    const uint64_t ms = us / 1000;
    const int b = ms < 50 ? 0 : ms < 100 ? 1 : ms < 250 ? 2 : ms < 500 ? 3 : ms < 1000 ? 4 : 5;
    h.bucket[b].fetch_add(1, std::memory_order_relaxed);
  }
  std::atomic<uint64_t> ltInboxHwFrames_{0};
  std::atomic<uint64_t> ltInboxHwBytes_{0};
  std::atomic<uint64_t> ltPassFramesMax_{0};
  std::atomic<uint64_t> ltPassServiceMaxUs_{0};
  std::atomic<uint64_t> ltRxLockWaitMaxUs_{0};
  std::atomic<uint64_t> ltRxLockHoldMaxUs_{0};
  uint64_t ltPassFrames_ = 0;    // the worker's own (under mu_)
  uint64_t ltPassStartUs_ = 0;
  // r2: each AU's enqueue time by its identity (stream generation + seq), under mu_ (RX writes, the worker reads)
  struct LtEnq {
    uint64_t gen = 0;
    uint32_t seq = 0;
    uint64_t us = 0;
  };
  static constexpr size_t kLtEnqRing = 64;
  LtEnq ltEnq_[kLtEnqRing];
  std::atomic<uint64_t> ltAgeQueueUnmatched_{0};
  std::atomic<uint64_t> ltAgeQueueReplay_{0};
  std::atomic<uint64_t> ltInboxPeakPreFrames_{0};
  std::atomic<uint64_t> ltInboxPeakPreBytes_{0};
  LtAgeHist ltAgePop_;
  LtAgeHist ltAgeQueue_;
  std::atomic<uint64_t> ltApiCount_[kApiKinds] = {};
  std::atomic<uint64_t> ltApiTotalUs_[kApiKinds] = {};
  std::atomic<uint64_t> ltApiMaxUs_[kApiKinds] = {};
  std::atomic<uint64_t> ltWorkerPassUs_{0};
  std::atomic<uint64_t> ltWorkerTid_{0};
  std::atomic<uint64_t> ltInbox_{0};
  // At most one decoded frame waits here for its slot. Handing it over sooner would put it in
  // a queue the view drains newest-first, where an earlier frame is simply thrown away.
  bool heldOutputValid_ = false;
  bool heldOutputScheduled_ = false;
  size_t heldOutputIndex_ = 0;
  uint64_t heldOutputPresentUs_ = 0;
  // Counted by the view, once per frame it actually latched.
  std::atomic<uint64_t> displayedFrameCount_{0};
  // sh-filter r1: (buffer timestamp ns -> handoff monotonic ns) of the last frames released to the
  // output Surface. Written under the decoder lock, read lock-free (HandoffNsForTimestamp).
  static constexpr size_t kHandoffRing = 256;
  std::atomic<int64_t> handoffKey_[kHandoffRing] = {};
  std::atomic<int64_t> handoffNs_[kHandoffRing] = {};
  // r2: (stream generation << 32 | codec epoch) and (visible width << 32 | visible height) of that frame.
  std::atomic<uint64_t> handoffMeta_[kHandoffRing] = {};
  std::atomic<uint64_t> handoffSize_[kHandoffRing] = {};
  size_t handoffNext_ = 0;
  std::atomic<uint32_t> codecEpoch_{0};
  uint64_t displayedWindowBase_ = 0;
  uint64_t pendingFrameCount_ = 0;
  uint64_t pendingFrameQueueRetryCount_ = 0;
  // Set when a delta had to be discarded before reaching the codec; consumed by the session,
  // which turns it into a rate-limited IDR request.
  bool decoderKeyframeRequest_ = false;
  // apk-decoder-inbox r1: each time the want is set, by cause (always counted; DebugStatus `keyWant=`). Setting it is
  // unchanged -- the same flag at the same places; the session consumes it per frame / answer.
  enum KeyWant : int {
    kWantOversize = 0, kWantInboxOverflow, kWantGapDelta, kWantAwaitKey, kWantPendingSlot, kWantSelectionAbort,
    kWantCodecUp, kWantKinds
  };
  uint64_t keyWant_[kWantKinds] = {};
  void NoteKeyWantLocked(int cause) {
    decoderKeyframeRequest_ = true;
    ++keyWant_[cause];
  }
  // Intervals between the times frames actually reach the display, drained once a second by
  // the session and reported to the host. A scheduled release lands at its presentation
  // timestamp, not at the moment we called release -- draining several ready buffers in one
  // pass would otherwise read as a burst of near-zero gaps while the screen was fine.
  std::vector<uint32_t> presentGapsUs_;
  uint64_t lastPresentSteadyUs_ = 0;
  uint64_t presentWindowStartUs_ = 0;
  uint32_t presentScheduledCount_ = 0;
  uint32_t presentImmediateCount_ = 0;
  uint64_t presentWindowReanchorBase_ = 0;
  uint64_t lastInputQueueSteadyUs_ = 0;
  uint64_t bootstrapReplayCount_ = 0;
  uint64_t staleFrameDropCount_ = 0;
  uint64_t oversizedInputFrameDropCount_ = 0;
  // keepalive r5 D1: what the codec itself answered, for the video_debug line -- "out=0" alone does not
  // say whether no picture was ever dequeued, one is held, or the input side is what stopped.
  int64_t lastInputDequeue_ = 0;    // last dequeueInputBuffer result (>= 0 an index)
  int64_t lastOutputDequeue_ = 0;   // last dequeueOutputBuffer result (-1 try again, -2 format, -3 buffers)
  uint64_t outputDequeued_ = 0;     // output buffers dequeued by this codec instance (pictures or not)
  uint64_t formatChanges_ = 0;      // output-format-changed answers, this codec instance
  uint64_t codecsStarted_ = 0;      // codec instances started over the sink's life
  // viewer-sharpness r1 A: this codec instance's last output format -- the coded size, and where the visible
  // size (outputWidth_/Height_) came from: "none" (no format yet: the wire size), "rect" / "keys" (a valid crop),
  // "absent" / "invalid_rect" / "invalid_keys" (the coded size is shown).
  uint32_t formatWidth_ = 0;
  uint32_t formatHeight_ = 0;
  const char* cropSource_ = "none";
  int32_t cropLeft_ = 0, cropTop_ = 0, cropRight_ = 0, cropBottom_ = 0;
  // keepalive r5 D1: this codec instance has had no IDR queued yet -- deltas are not fed to it (an IDR is asked
  // for instead). The surface can start a codec from the SPS/PPS of an IDR it never received.
  bool codecAwaitsKey_ = false;
  uint64_t deltaBeforeKeyDrops_ = 0;
  // apk-0232 r2 (N2): the frame held for an input buffer, with the gate asked again when it is queued and no newer
  // frame overtaking it (decoder_pending_slot.hpp, shared with the Windows tests).
  remote60::native_poc::DecoderPendingSlot pendingFrame_;
  std::optional<remote60::native_poc::UdpH264AssembledFrame> bootstrapFrame_;
  std::vector<uint8_t> csd0_;
  std::vector<uint8_t> csd1_;
};

}  // namespace remote60::android_direct
