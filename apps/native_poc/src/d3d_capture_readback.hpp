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
  // Callback-side timings, filled by Submit().
  uint64_t d3dWaitUs = 0;
  uint64_t submitCopyUs = 0;
  uint64_t submitUs = 0;
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

 private:
  enum class SlotState : uint8_t { Free, GpuPending };

  struct Slot {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    Microsoft::WRL::ComPtr<ID3D11Query> query;
    SlotState state = SlotState::Free;
    uint64_t submitSeq = 0;
    CaptureFrameMeta meta;
  };

  bool CreateSlotsLocked(uint32_t width, uint32_t height);
  bool EnsurePreprocessLocked(uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH);
  bool EnsureNv12Locked(uint32_t outW, uint32_t outH);
  void ReleaseNv12SlotLocked(int32_t slot, uint64_t generation);
  void WorkerLoop();

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
};

}  // namespace remote60::native_poc
