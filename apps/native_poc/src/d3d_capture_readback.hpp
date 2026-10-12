#pragma once

// Asynchronous GPU->CPU readback for the capture path.
//
// The capture callback used to do CopyResource, a blocking Map, and a full-frame memcpy
// inline -- on the DXGI path while still holding the duplication frame, so the whole desktop
// pipeline waited on a synchronous readback. The callback now only copies into a staging
// slot and records a completion query; a worker thread notices finished copies, maps them
// without stalling the GPU, and hands the bytes on. Latency policy is latest-wins: when the
// worker falls behind, older completed slots are dropped, never queued.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

namespace remote60::native_poc {

struct CaptureFrameMeta {
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t callbackUs = 0;
  uint64_t captureUs = 0;
  uint64_t captureAgeAtCallbackUs = 0;
  uint64_t captureClockSkewUs = 0;
  uint64_t streamGeneration = 0;
  // Capture attachment (session) cookie the frame was submitted under. The host bumps this on
  // every pool recreate; a readback completion whose cookie no longer matches the live one
  // belonged to a previous attachment and is dropped instead of published under the new target.
  // 0 means "no cookie carried" (e.g. the GDI path) and is never checked.
  uint64_t attachmentCookie = 0;
  // Callback-side timings, filled by Submit().
  uint64_t d3dWaitUs = 0;
  uint64_t submitCopyUs = 0;
  uint64_t submitUs = 0;
  // Worker-side attribution for a slow readback (0.2.98): time the worker spent waiting for the
  // host's own context mutex vs time spent INSIDE D3D calls (GetData / Map / Unmap) over the polls
  // that led to this publish, counted only while a slot was GpuPending (idle waits are dropped).
  // Large D3D call time with a small mutex wait is CONSISTENT with the runtime's internal device
  // lock (e.g. held by AcquireNextFrame) stalling the worker -- it narrows the suspect, it is not
  // proof; a wait-stack trace (WPA) is what confirms it.
  uint64_t workerCtxWaitUs = 0;
  uint64_t workerD3dCallUs = 0;
  // Snapshot of the window-client crop taken on the callback thread (a cheap rect query);
  // the pixel work happens on the worker or, when the GPU preprocessor runs, in the blt.
  bool cropActive = false;
  uint32_t cropX = 0;
  uint32_t cropY = 0;
  uint32_t cropW = 0;
  uint32_t cropH = 0;
  // Dimensions of the bytes the worker will read out of the staging slot. Equal to the
  // capture size on the plain path, the encode size when the GPU preprocessor ran.
  uint32_t payloadW = 0;
  uint32_t payloadH = 0;
  bool preprocessed = false;
  // GPU NV12 conversion result for the zero-copy encode path. -1 when this frame has none;
  // ownership passes to whoever pops the frame, who must release the slot.
  int32_t nv12Slot = -1;
  uint64_t nv12Generation = 0;
  uint32_t nv12W = 0;
  uint32_t nv12H = 0;
};

// Free -> Submitting -> GpuPending -> Free. See the note on the publication barrier in
// D3dCaptureReadbackPipeline. At namespace scope so the identity predicate below is testable.
enum class ReadbackSlotState : uint8_t { Free, Submitting, GpuPending, Reading };

/**
 * Is the slot a thread reserved still the same reservation?
 *
 * Generation alone is not enough. Within one generation the worker can free a slot and a newer
 * Submit can take it over, and the slot keeps the same staging texture across that -- so neither
 * the generation nor the texture pointer is an identity fence. submitSeq is. `expected` is the
 * state the caller requires: Submit finalises a slot that is still Submitting, the worker frees
 * one that is still GpuPending. (Ledger H-23.)
 */
inline bool readback_slot_is_current(uint64_t liveGeneration, uint64_t generationAtReserve,
                                     ReadbackSlotState state, ReadbackSlotState expected,
                                     uint64_t slotSubmitSeq, uint64_t submitSeqAtReserve) {
  return liveGeneration == generationAtReserve && state == expected &&
         slotSubmitSeq == submitSeqAtReserve;
}

/**
 * Pure latest-wins selection over the ring: returns the index of the ready slot with the
 * highest submit sequence, and counts every older ready slot as superseded. Returns SIZE_MAX
 * when nothing is ready. Extracted so the drop policy is unit-testable without a GPU.
 */
size_t pick_latest_ready_slot(const std::vector<uint64_t>& submitSeq,
                              const std::vector<bool>& ready, size_t* outSuperseded);

/** Reusable CPU frame buffers. Handed out as shared_ptr whose deleter returns the storage. */
class CaptureBufferPool {
 public:
  CaptureBufferPool() : state_(std::make_shared<State>()) {}

  std::shared_ptr<std::vector<uint8_t>> Acquire(size_t bytes);
  uint64_t ReuseCount() const { return state_->reuse.load(std::memory_order_relaxed); }

 private:
  struct State {
    std::mutex mu;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> free;
    std::atomic<uint64_t> reuse{0};
  };
  std::shared_ptr<State> state_;
};

class D3dCaptureReadbackPipeline {
 public:
  // payload/meta plus worker-side timings: gpuPendingUs (submit -> query done),
  // workerMapUs, workerMemcpyUs (includes the crop copy when one is active).
  using PublishFn = std::function<void(std::shared_ptr<std::vector<uint8_t>> payload,
                                       uint32_t width, uint32_t height, uint32_t stride,
                                       const CaptureFrameMeta& meta, uint64_t gpuPendingUs,
                                       uint64_t workerMapUs, uint64_t workerMemcpyUs)>;

  D3dCaptureReadbackPipeline() = default;
  ~D3dCaptureReadbackPipeline() { Shutdown(); }
  D3dCaptureReadbackPipeline(const D3dCaptureReadbackPipeline&) = delete;
  D3dCaptureReadbackPipeline& operator=(const D3dCaptureReadbackPipeline&) = delete;

  /** contextMu guards every use of the immediate context, shared with the rest of the host. */
  bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, std::mutex* contextMu,
                  uint32_t width, uint32_t height, uint32_t slotCount, PublishFn publish);
  /** Drains pending work and recreates the slots for a new capture size. */
  bool Reconfigure(uint32_t width, uint32_t height);
  void Shutdown();

  /**
   * Callback-thread entry: CopyResource into a free slot plus a query End, nothing else.
   * Returns false on a busy ring (drop) or size mismatch (caller handles resize).
   */
  bool Submit(ID3D11Texture2D* src, const CaptureFrameMeta& meta);

  /**
   * Encode-size target for the GPU crop/scale front end. Submit preprocesses only when the
   * source content aspect-fits the target exactly (no stretch); otherwise frames take the
   * plain path at capture size and the consumer's refit logic runs as before. 0x0 disables.
   */
  void SetOutputSize(uint32_t width, uint32_t height);
  // Throttle consumption after owning changed pixels, so the last update cannot disappear.
  void SetPublishIntervalUs(uint64_t intervalUs) { publishIntervalUs_.store(intervalUs); }

  /** Enables the BGRA->NV12 GPU conversion that feeds the zero-copy encoder path. */
  void SetNv12Enabled(bool enabled);
  /** The slot texture, or null when the slot/generation no longer exists. */
  Microsoft::WRL::ComPtr<ID3D11Texture2D> Nv12SlotTexture(int32_t slot, uint64_t generation);
  /** Returns a slot to the ring; safe to call with stale generation or -1. */
  void ReleaseNv12Slot(int32_t slot, uint64_t generation);

  uint64_t BusyDrops() const { return busyDrops_.load(std::memory_order_relaxed); }
  uint64_t Nv12Converted() const { return nv12Converted_.load(std::memory_order_relaxed); }
  uint64_t Nv12RingBusy() const { return nv12RingBusy_.load(std::memory_order_relaxed); }
  uint64_t SupersededDrops() const { return supersededDrops_.load(std::memory_order_relaxed); }
  uint64_t BufferReuseCount() const { return bufferPool_.ReuseCount(); }
  uint64_t PreprocessCount() const { return preprocessCount_.load(std::memory_order_relaxed); }
  uint64_t PreprocessFallbacks() const {
    return preprocessFallbacks_.load(std::memory_order_relaxed);
  }

  // Age, in microseconds, of the oldest submit still stuck in GpuPending -- or 0 if the ring is
  // idle. A DXGI/WGC capture that has gone dark under GPU contention leaves submits whose
  // completion query never signals; the host's callback-stall watchdog stays silent because those
  // backends are change-driven, so this age is the only evidence that the ring has frozen rather
  // than merely fallen quiet on a static desktop (a static desktop enqueues nothing, so its
  // oldest-pending age is 0).
  uint64_t OldestGpuPendingAgeUs();

  // Count of submits currently stuck in GpuPending. The age above says how old the freeze is; this
  // says how much of the ring it has swallowed. Reported next to it in the per-second stats so a
  // real-GPU run can tell a frozen ring (age climbing toward the restart threshold, count pinned at
  // the ring size) from a merely busy one (age small, count churning).
  uint32_t GpuPendingCount();

  // Age of the oldest slot still inside Submit (reserved, D3D work not finished). GpuPendingCount
  // and OldestGpuPendingAgeUs both skip these by construction -- the age one always did, because
  // meta.submitUs is only stamped once the work completes -- so a Submit that stalls inside a
  // capture callback has no signal at all. The DXGI path has its own worker watchdog; this is what
  // a WGC-callback stall would need. Telemetry only today. (Ledger H-25.)
  uint64_t OldestSubmittingAgeUs();

  // r12: the three readers above no longer take slotMu_. In the fourth Home run the main thread's
  // stats tick sat in OldestSubmittingAgeUs -> slotMu_ behind a worker that was itself waiting for
  // the D3D11 device lock, and the watchdog fired on the main thread (exit 43). The ring now keeps a
  // small snapshot -- oldest Submitting reservation, oldest GpuPending submit, pending count,
  // generation, when it was refreshed -- rewritten under slotMu_ at every state transition
  // (RefreshDiagLocked) and read through a bounded seqlock. The snapshot stores TIMESTAMPS, not
  // ages: a frozen writer leaves the timestamps in place, so the ages the readers compute keep
  // growing -- a stall is never disguised as an idle ring (0). `consistent` is false only when four
  // reads in a row tore against a writer; the values are then the last read, flagged, not zeroed.
  struct DiagSnapshot {
    uint64_t oldestSubmittingReservedUs = 0;
    uint64_t oldestGpuPendingSubmitUs = 0;
    uint32_t gpuPendingCount = 0;
    uint64_t generation = 0;
    uint64_t updatedUs = 0;
    bool consistent = true;
  };
  DiagSnapshot ReadDiag() const;
  // How long ago the snapshot was refreshed (0 = never). A large value with a nonzero pending count
  // means the ring's threads have not moved: the diagnostic for a frozen ring, readable without a lock.
  uint64_t DiagAgeUs() const;

  // r13 (Codex C2): what the product's stats line and watchdog consume -- ONE snapshot, its validity,
  // its generation and its freshness, with the ages already computed from that same snapshot. Calling
  // the three legacy getters one after another read three snapshots and threw the `consistent` flag
  // away, so a torn read (or an uninitialised ring) looked like a healthy idle ring (0/0/0) and the
  // frozen-ring streak was reset on it. Consumers must not treat a non-Ok view as data: no peak
  // update, no streak reset, no "recovered" -- Unknown stays Unknown.
  enum class DiagValidity : uint8_t {
    Ok,             // consistent snapshot of an initialised ring
    Inconsistent,   // torn against a writer on every bounded retry (values are the last read, not 0)
    Uninitialized,  // RefreshDiagLocked never ran (no ring yet / shut down before any slot existed)
  };
  static const char* diag_validity_name(DiagValidity v) {
    switch (v) {
      case DiagValidity::Ok: return "ok";
      case DiagValidity::Inconsistent: return "torn";
      default: return "uninit";
    }
  }
  struct DiagView {
    DiagValidity validity = DiagValidity::Uninitialized;
    uint64_t generation = 0;
    uint64_t updatedUs = 0;
    uint64_t snapshotAgeUs = 0;          // now - updatedUs (0 when uninitialised)
    uint64_t oldestGpuPendingAgeUs = 0;  // from the same snapshot
    uint64_t oldestSubmittingAgeUs = 0;
    uint32_t gpuPendingCount = 0;
    bool ok() const { return validity == DiagValidity::Ok; }
  };
  DiagView ViewDiag() const;

  // Test seam (unset in the product): runs inside RefreshDiagLocked between the two sequence
  // increments, i.e. while the snapshot is odd/"being written" -- the writer-stuck case a reader has
  // to report as Inconsistent rather than as numbers.
  std::function<void()> midDiagWriteForTest;

  // Test seams (unset in the product; the harness and unit tests use them to pin the worker at a
  // known point). beforeGpuPollForTest runs on the worker with contextMu_ held and slotMu_ NOT held,
  // right before the GetData sweep; whileSlotLockedForTest runs with slotMu_ held, right after a
  // slot was pinned Reading. Neither is a product switch: nothing reads them but the worker.
  std::function<void()> beforeGpuPollForTest;
  std::function<void()> whileSlotLockedForTest;

 private:
  // Submitting is the publication barrier. The worker only ever polls GpuPending, so a slot
  // stays invisible to it until Submit has finished the D3D work AND written the final meta --
  // the preprocess-failure fixup, the NV12-failure lease return, the submit timestamps. Without
  // it the worker could pick a slot whose meta still claimed `preprocessed` after the blt had
  // failed, or still held an NV12 lease that Submit was about to return, and hand either to a
  // consumer. (Ledger H-23, second pass.)
  using SlotState = ReadbackSlotState;

  struct Slot {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    Microsoft::WRL::ComPtr<ID3D11Query> query;
    SlotState state = SlotState::Free;
    uint64_t submitSeq = 0;
    uint64_t reservedUs = 0;  // when this slot entered Submitting (H-25)
    CaptureFrameMeta meta;
  };

  bool CreateSlotsLocked(uint32_t width, uint32_t height);
  bool EnsurePreprocessLocked(uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH);
  bool EnsureNv12Locked(uint32_t outW, uint32_t outH);
  void ReleaseNv12SlotLocked(int32_t slot, uint64_t generation);
  void WorkerLoop();
  void RefreshDiagLocked();  // slotMu_ held: recompute the DiagSnapshot atomics from slots_

  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  std::mutex* contextMu_ = nullptr;
  PublishFn publish_;

  std::mutex slotMu_;                 // guards slots_/width_/height_/generation_
  std::condition_variable workerCv_;
  std::vector<Slot> slots_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint64_t generation_ = 0;
  std::atomic<uint64_t> publishIntervalUs_{0};
  uint64_t submitSeq_ = 0;

  // GPU crop/scale front end (D3D11 video processor); guarded by slotMu_ for configuration
  // and contextMu_ for use, like everything touching the immediate context.
  uint32_t outputW_ = 0;
  uint32_t outputH_ = 0;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice_;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> vpEnumerator_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> vpProcessor_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> preSrcTexture_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> preDstTexture_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> vpInputView_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> vpOutputView_;
  uint32_t vpSrcW_ = 0;
  uint32_t vpSrcH_ = 0;
  uint32_t vpDstW_ = 0;
  uint32_t vpDstH_ = 0;
  bool preprocessBroken_ = false;
  uint64_t vpConfigVersion_ = 0;

  // NV12 ring for the zero-copy encoder path. Slots stay busy until released by the frame
  // consumer, so an MFT still reading a texture can never see it rewritten.
  struct Nv12Slot {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
    bool busy = false;
  };
  bool nv12Enabled_ = false;
  bool nv12Broken_ = false;
  std::vector<Nv12Slot> nv12Slots_;
  uint32_t nv12W_ = 0;
  uint32_t nv12H_ = 0;
  uint64_t nv12Generation_ = 0;
  uint64_t nv12ViewsConfigVersion_ = 0;

  CaptureBufferPool bufferPool_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> busyDrops_{0};
  std::atomic<uint64_t> supersededDrops_{0};
  std::atomic<uint64_t> preprocessCount_{0};
  std::atomic<uint64_t> preprocessFallbacks_{0};
  std::atomic<uint64_t> nv12Converted_{0};
  std::atomic<uint64_t> nv12RingBusy_{0};

  // r12 diagnostic snapshot (see DiagSnapshot). Odd diagSeq_ = a writer is mid-update.
  std::atomic<uint32_t> diagSeq_{0};
  std::atomic<uint64_t> diagOldestSubmittingUs_{0};
  std::atomic<uint64_t> diagOldestPendingUs_{0};
  std::atomic<uint32_t> diagPendingCount_{0};
  std::atomic<uint64_t> diagGeneration_{0};
  std::atomic<uint64_t> diagUpdatedUs_{0};
};

}  // namespace remote60::native_poc
