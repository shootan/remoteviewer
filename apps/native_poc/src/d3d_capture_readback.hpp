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
  // the pixel work happens on the worker.
  bool cropActive = false;
  uint32_t cropX = 0;
  uint32_t cropY = 0;
  uint32_t cropW = 0;
  uint32_t cropH = 0;
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

  uint64_t BusyDrops() const { return busyDrops_.load(std::memory_order_relaxed); }
  uint64_t SupersededDrops() const { return supersededDrops_.load(std::memory_order_relaxed); }
  uint64_t BufferReuseCount() const { return bufferPool_.ReuseCount(); }

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

  CaptureBufferPool bufferPool_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> busyDrops_{0};
  std::atomic<uint64_t> supersededDrops_{0};
};

}  // namespace remote60::native_poc
