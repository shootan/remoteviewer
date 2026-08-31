#include "d3d_capture_readback.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Off unless REMOTE60_NATIVE_READBACK_DEBUG=1; traces submits and worker polls when the
// pipeline publishes nothing and the cause is not obvious from the per-second stats.
#define RB_DEBUG(...)                                                        \
  do {                                                                       \
    static const bool rbDebugOn = [] {                                       \
      const char* v = std::getenv("REMOTE60_NATIVE_READBACK_DEBUG");         \
      return v && *v && *v != '0';                                           \
    }();                                                                     \
    if (rbDebugOn) {                                                         \
      std::fprintf(stderr, "[readback] " __VA_ARGS__);                       \
      std::fputc('\n', stderr);                                              \
    }                                                                        \
  } while (0)

namespace remote60::native_poc {

namespace {

uint64_t qpc_us() {
  LARGE_INTEGER f{};
  LARGE_INTEGER c{};
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return static_cast<uint64_t>(c.QuadPart) * 1000000ULL / static_cast<uint64_t>(f.QuadPart);
}

}  // namespace

size_t pick_latest_ready_slot(const std::vector<uint64_t>& submitSeq,
                              const std::vector<bool>& ready, size_t* outSuperseded) {
  size_t best = SIZE_MAX;
  size_t readyCount = 0;
  for (size_t i = 0; i < submitSeq.size() && i < ready.size(); ++i) {
    if (!ready[i]) continue;
    ++readyCount;
    if (best == SIZE_MAX || submitSeq[i] > submitSeq[best]) best = i;
  }
  if (outSuperseded) *outSuperseded = (readyCount > 0) ? (readyCount - 1) : 0;
  return best;
}

std::shared_ptr<std::vector<uint8_t>> CaptureBufferPool::Acquire(size_t bytes) {
  std::unique_ptr<std::vector<uint8_t>> storage;
  {
    std::lock_guard<std::mutex> lk(state_->mu);
    if (!state_->free.empty()) {
      storage = std::move(state_->free.back());
      state_->free.pop_back();
    }
  }
  if (storage) {
    state_->reuse.fetch_add(1, std::memory_order_relaxed);
    storage->resize(bytes);
  } else {
    storage = std::make_unique<std::vector<uint8_t>>(bytes);
  }
  // The deleter keeps the pool state alive and returns the storage when the LAST holder --
  // encode path, frame slot, or the gating reference held across frames -- lets go. Nothing
  // is ever recycled while something still reads it.
  auto* raw = storage.release();
  std::shared_ptr<State> state = state_;
  return std::shared_ptr<std::vector<uint8_t>>(raw, [state](std::vector<uint8_t>* p) {
    std::lock_guard<std::mutex> lk(state->mu);
    constexpr size_t kMaxPooled = 6;
    if (state->free.size() < kMaxPooled) {
      state->free.emplace_back(p);
    } else {
      delete p;
    }
  });
}

bool D3dCaptureReadbackPipeline::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                                            std::mutex* contextMu, uint32_t width,
                                            uint32_t height, uint32_t slotCount,
                                            PublishFn publish) {
  if (!device || !context || !contextMu || !publish || slotCount == 0) {
    std::fprintf(stderr,
                 "[readback] init precondition failed dev=%d ctx=%d mu=%d publish=%d slots=%u\n",
                 device ? 1 : 0, context ? 1 : 0, contextMu ? 1 : 0, publish ? 1 : 0, slotCount);
    return false;
  }
  Shutdown();
  device_ = device;
  context_ = context;
  contextMu_ = contextMu;
  publish_ = std::move(publish);
  {
    std::lock_guard<std::mutex> lk(slotMu_);
    slots_.resize(slotCount);
    if (!CreateSlotsLocked(width, height)) return false;
  }
  running_.store(true, std::memory_order_release);
  worker_ = std::thread([this] { WorkerLoop(); });
  return true;
}

bool D3dCaptureReadbackPipeline::CreateSlotsLocked(uint32_t width, uint32_t height) {
  width_ = width;
  height_ = height;
  ++generation_;
  for (auto& slot : slots_) {
    // Reclaim whatever NV12 lease this slot still holds before its meta is thrown away.
    // This is the SINGLE owner of leases belonging to a superseded generation: a worker that is
    // mid-Map, and a Submit still in its Submitting window, both see their identity check fail
    // and deliberately do not release -- by then the index may already have been re-leased to a
    // newer frame, and freeing it would hand the same texture to two consumers.
    // (Ledger H-03c / H-23.)
    ReleaseNv12SlotLocked(slot.meta.nv12Slot, slot.meta.nv12Generation);
    slot.meta = CaptureFrameMeta{};
    slot.staging.Reset();
    slot.query.Reset();
    slot.state = SlotState::Free;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    const HRESULT texHr = device_->CreateTexture2D(&desc, nullptr, &slot.staging);
    if (FAILED(texHr)) {
      std::fprintf(stderr, "[readback] staging create failed hr=0x%08lx removed=0x%08lx\n",
                   static_cast<unsigned long>(texHr),
                   static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
      return false;
    }
    D3D11_QUERY_DESC queryDesc{};
    queryDesc.Query = D3D11_QUERY_EVENT;
    const HRESULT queryHr = device_->CreateQuery(&queryDesc, &slot.query);
    if (FAILED(queryHr)) {
      std::fprintf(stderr, "[readback] query create failed hr=0x%08lx\n",
                   static_cast<unsigned long>(queryHr));
      return false;
    }
  }
  return true;
}

void D3dCaptureReadbackPipeline::SetOutputSize(uint32_t width, uint32_t height) {
  std::lock_guard<std::mutex> lk(slotMu_);
  outputW_ = width;
  outputH_ = height;
}

bool D3dCaptureReadbackPipeline::EnsurePreprocessLocked(uint32_t srcW, uint32_t srcH,
                                                        uint32_t dstW, uint32_t dstH) {
  if (preprocessBroken_) return false;
  if (vpProcessor_ && preSrcTexture_ && preDstTexture_ && vpInputView_ && vpOutputView_ &&
      vpSrcW_ == srcW && vpSrcH_ == srcH && vpDstW_ == dstW && vpDstH_ == dstH) {
    return true;
  }
  if (!videoDevice_) {
    if (FAILED(device_.As(&videoDevice_)) || !videoDevice_ ||
        FAILED(context_.As(&videoContext_)) || !videoContext_) {
      preprocessBroken_ = true;
      return false;
    }
  }
  vpEnumerator_.Reset();
  vpProcessor_.Reset();
  preSrcTexture_.Reset();
  preDstTexture_.Reset();
  vpInputView_.Reset();
  vpOutputView_.Reset();

  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
  desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputWidth = srcW;
  desc.InputHeight = srcH;
  desc.OutputWidth = dstW;
  desc.OutputHeight = dstH;
  desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  if (FAILED(videoDevice_->CreateVideoProcessorEnumerator(&desc, &vpEnumerator_)) ||
      !vpEnumerator_) {
    return false;
  }
  UINT formatSupport = 0;
  if (FAILED(vpEnumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                      &formatSupport))) {
    return false;
  }
  const UINT required =
      D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT;
  if ((formatSupport & required) != required) return false;
  if (FAILED(videoDevice_->CreateVideoProcessor(vpEnumerator_.Get(), 0, &vpProcessor_)) ||
      !vpProcessor_) {
    return false;
  }

  D3D11_TEXTURE2D_DESC texDesc{};
  texDesc.Width = srcW;
  texDesc.Height = srcH;
  texDesc.MipLevels = 1;
  texDesc.ArraySize = 1;
  texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  texDesc.SampleDesc.Count = 1;
  texDesc.Usage = D3D11_USAGE_DEFAULT;
  texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  if (FAILED(device_->CreateTexture2D(&texDesc, nullptr, &preSrcTexture_))) return false;
  texDesc.Width = dstW;
  texDesc.Height = dstH;
  if (FAILED(device_->CreateTexture2D(&texDesc, nullptr, &preDstTexture_))) return false;

  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inView{};
  inView.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  if (FAILED(videoDevice_->CreateVideoProcessorInputView(preSrcTexture_.Get(),
                                                         vpEnumerator_.Get(), &inView,
                                                         &vpInputView_))) {
    return false;
  }
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outView{};
  outView.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  if (FAILED(videoDevice_->CreateVideoProcessorOutputView(preDstTexture_.Get(),
                                                          vpEnumerator_.Get(), &outView,
                                                          &vpOutputView_))) {
    return false;
  }
  vpSrcW_ = srcW;
  vpSrcH_ = srcH;
  vpDstW_ = dstW;
  vpDstH_ = dstH;
  ++vpConfigVersion_;
  return true;
}

void D3dCaptureReadbackPipeline::SetNv12Enabled(bool enabled) {
  std::lock_guard<std::mutex> lk(slotMu_);
  nv12Enabled_ = enabled;
}

uint64_t D3dCaptureReadbackPipeline::OldestGpuPendingAgeUs() {
  std::lock_guard<std::mutex> lk(slotMu_);
  uint64_t oldestSubmitUs = 0;
  for (const auto& s : slots_) {
    if (s.state != SlotState::GpuPending || s.meta.submitUs == 0) continue;
    if (oldestSubmitUs == 0 || s.meta.submitUs < oldestSubmitUs) oldestSubmitUs = s.meta.submitUs;
  }
  if (oldestSubmitUs == 0) return 0;
  // Same clock as meta.submitUs (set from qpc_us() at submit), so the subtraction is meaningful
  // without the caller having to pass a timestamp on a possibly different clock.
  const uint64_t nowUs = qpc_us();
  return nowUs > oldestSubmitUs ? nowUs - oldestSubmitUs : 0;
}

uint64_t D3dCaptureReadbackPipeline::OldestSubmittingAgeUs() {
  std::lock_guard<std::mutex> lk(slotMu_);
  uint64_t oldestUs = 0;
  for (const auto& s : slots_) {
    if (s.state != SlotState::Submitting || s.reservedUs == 0) continue;
    if (oldestUs == 0 || s.reservedUs < oldestUs) oldestUs = s.reservedUs;
  }
  if (oldestUs == 0) return 0;
  const uint64_t nowUs = qpc_us();
  return nowUs > oldestUs ? nowUs - oldestUs : 0;
}

uint32_t D3dCaptureReadbackPipeline::GpuPendingCount() {
  std::lock_guard<std::mutex> lk(slotMu_);
  uint32_t pending = 0;
  for (const auto& s : slots_) {
    if (s.state == SlotState::GpuPending) ++pending;
  }
  return pending;
}

bool D3dCaptureReadbackPipeline::EnsureNv12Locked(uint32_t outW, uint32_t outH) {
  if (nv12Broken_ || !vpEnumerator_) return false;
  constexpr size_t kNv12Slots = 4;
  if (!nv12Slots_.empty() && nv12W_ == outW && nv12H_ == outH &&
      nv12ViewsConfigVersion_ == vpConfigVersion_) {
    return true;
  }
  UINT formatSupport = 0;
  if (FAILED(vpEnumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &formatSupport)) ||
      (formatSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
    nv12Broken_ = true;
    return false;
  }
  // A new generation invalidates every outstanding slot reference; late releases with the
  // old generation become no-ops.
  ++nv12Generation_;
  nv12Slots_.clear();
  nv12Slots_.resize(kNv12Slots);
  for (auto& slot : nv12Slots_) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = outW;
    desc.Height = outH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &slot.texture))) {
      nv12Slots_.clear();
      nv12Broken_ = true;
      return false;
    }
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outView{};
    outView.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    if (FAILED(videoDevice_->CreateVideoProcessorOutputView(slot.texture.Get(),
                                                            vpEnumerator_.Get(), &outView,
                                                            &slot.outputView))) {
      nv12Slots_.clear();
      nv12Broken_ = true;
      return false;
    }
  }
  nv12W_ = outW;
  nv12H_ = outH;
  nv12ViewsConfigVersion_ = vpConfigVersion_;
  return true;
}

void D3dCaptureReadbackPipeline::ReleaseNv12SlotLocked(int32_t slot, uint64_t generation) {
  if (slot < 0 || generation != nv12Generation_) return;
  if (static_cast<size_t>(slot) < nv12Slots_.size()) {
    nv12Slots_[static_cast<size_t>(slot)].busy = false;
  }
}

void D3dCaptureReadbackPipeline::ReleaseNv12Slot(int32_t slot, uint64_t generation) {
  std::lock_guard<std::mutex> lk(slotMu_);
  ReleaseNv12SlotLocked(slot, generation);
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> D3dCaptureReadbackPipeline::Nv12SlotTexture(
    int32_t slot, uint64_t generation) {
  std::lock_guard<std::mutex> lk(slotMu_);
  if (slot < 0 || generation != nv12Generation_ ||
      static_cast<size_t>(slot) >= nv12Slots_.size()) {
    return nullptr;
  }
  return nv12Slots_[static_cast<size_t>(slot)].texture;
}

bool D3dCaptureReadbackPipeline::Reconfigure(uint32_t width, uint32_t height) {
  if (!device_) return false;
  std::lock_guard<std::mutex> lk(slotMu_);
  // Bumping the generation invalidates anything the worker has not consumed yet; it checks
  // the generation again before publishing.
  return CreateSlotsLocked(width, height);
}

void D3dCaptureReadbackPipeline::Shutdown() {
  if (running_.exchange(false)) {
    workerCv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }
  std::lock_guard<std::mutex> lk(slotMu_);
  slots_.clear();
  vpEnumerator_.Reset();
  vpProcessor_.Reset();
  preSrcTexture_.Reset();
  preDstTexture_.Reset();
  vpInputView_.Reset();
  vpOutputView_.Reset();
  videoDevice_.Reset();
  videoContext_.Reset();
  vpSrcW_ = vpSrcH_ = vpDstW_ = vpDstH_ = 0;
  preprocessBroken_ = false;
  nv12Slots_.clear();
  nv12W_ = nv12H_ = 0;
  nv12Broken_ = false;
  ++nv12Generation_;
  device_.Reset();
  context_.Reset();
  contextMu_ = nullptr;
}

bool D3dCaptureReadbackPipeline::Submit(ID3D11Texture2D* src, const CaptureFrameMeta& meta) {
  RB_DEBUG("submit enter src=%p running=%d", static_cast<void*>(src),
           running_.load(std::memory_order_acquire) ? 1 : 0);
  if (!src || !running_.load(std::memory_order_acquire)) return false;
  Slot* slot = nullptr;
  size_t slotIndex = SIZE_MAX;
  uint64_t submitSeqAtSubmit = 0;
  // Local refs keep the resources alive if a reconfigure resets the slot mid-copy.
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  Microsoft::WRL::ComPtr<ID3D11Query> query;
  uint64_t generationAtSubmit = 0;
  CaptureFrameMeta slotMeta = meta;
  bool preprocess = false;
  uint32_t outW = 0;
  uint32_t outH = 0;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> nv12OutView;
  {
    std::lock_guard<std::mutex> lk(slotMu_);
    if (meta.width != width_ || meta.height != height_) return false;

    // The GPU front end runs only when the encode box is an exact aspect fit of the source
    // content and never upscales; anything else takes the plain path so the consumer sees
    // the true content size and re-fits the encode target like before.
    uint32_t srcContentW = meta.width;
    uint32_t srcContentH = meta.height;
    uint32_t cropX = 0;
    uint32_t cropY = 0;
    if (meta.cropActive && meta.cropW >= 2 && meta.cropH >= 2 &&
        meta.cropX + meta.cropW <= meta.width && meta.cropY + meta.cropH <= meta.height) {
      srcContentW = meta.cropW & ~1u;
      srcContentH = meta.cropH & ~1u;
      cropX = meta.cropX & ~1u;
      cropY = meta.cropY & ~1u;
    }
    outW = outputW_;
    outH = outputH_;
    const bool sizeDiffers = (srcContentW != outW || srcContentH != outH);
    const bool cropNeeded = meta.cropActive;
    bool aspectOk = false;
    if (outW >= 2 && outH >= 2 && srcContentW >= outW && srcContentH >= outH) {
      const int64_t aspectDelta =
          static_cast<int64_t>(srcContentW) * outH - static_cast<int64_t>(srcContentH) * outW;
      const int64_t aspectTolerance = static_cast<int64_t>((std::max)(outW, outH));
      aspectOk = (aspectDelta >= -aspectTolerance && aspectDelta <= aspectTolerance);
    }
    if (aspectOk && (sizeDiffers || cropNeeded) && !preprocessBroken_) {
      preprocess = EnsurePreprocessLocked(meta.width, meta.height, outW, outH);
      if (!preprocess) {
        preprocessFallbacks_.fetch_add(1, std::memory_order_relaxed);
      }
    }
    // NV12 conversion also covers the same-size case (color conversion only). The slot stays
    // busy until the frame consumer releases it, so a full ring simply means this frame
    // encodes from CPU bytes.
    if (nv12Enabled_ && !nv12Broken_ && aspectOk) {
      if (EnsurePreprocessLocked(meta.width, meta.height, outW, outH) &&
          EnsureNv12Locked(outW, outH)) {
        for (size_t i = 0; i < nv12Slots_.size(); ++i) {
          if (!nv12Slots_[i].busy) {
            nv12Slots_[i].busy = true;
            slotMeta.nv12Slot = static_cast<int32_t>(i);
            slotMeta.nv12Generation = nv12Generation_;
            slotMeta.nv12W = outW;
            slotMeta.nv12H = outH;
            nv12OutView = nv12Slots_[i].outputView;
            break;
          }
        }
        if (slotMeta.nv12Slot < 0) {
          nv12RingBusy_.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
    if (preprocess) {
      slotMeta.payloadW = outW;
      slotMeta.payloadH = outH;
      slotMeta.preprocessed = true;
      slotMeta.cropX = cropX;
      slotMeta.cropY = cropY;
      slotMeta.cropW = srcContentW;
      slotMeta.cropH = srcContentH;
    } else {
      slotMeta.payloadW = meta.width;
      slotMeta.payloadH = meta.height;
      slotMeta.preprocessed = false;
    }

    for (auto& candidate : slots_) {
      if (candidate.state == SlotState::Free) {
        slot = &candidate;
        break;
      }
    }
    if (!slot) {
      // The staging ring is full, so this frame is dropped -- but the NV12 lease reserved a
      // few lines up would stay busy forever. A busy drop is ordinary load-time behaviour, not
      // an exception path, so four of them exhausted the four-slot ring and the zero-copy
      // encode path silently degraded to CPU NV12 for the rest of the session -- precisely
      // when the GPU is contended and the surface path matters most. (Ledger H-03e.)
      ReleaseNv12SlotLocked(slotMeta.nv12Slot, slotMeta.nv12Generation);
      busyDrops_.fetch_add(1, std::memory_order_relaxed);
      return true;  // dropped, but not a caller-visible failure
    }
    // Reserved, not yet visible: the worker polls GpuPending only. Promotion happens after the
    // D3D work, once the meta is final. (Ledger H-23.)
    slot->state = SlotState::Submitting;
    slot->reservedUs = qpc_us();
    slot->submitSeq = ++submitSeq_;
    slot->meta = slotMeta;
    slotIndex = static_cast<size_t>(slot - slots_.data());
    submitSeqAtSubmit = slot->submitSeq;
    staging = slot->staging;
    query = slot->query;
    generationAtSubmit = generation_;
  }

  const uint64_t submitStartUs = qpc_us();
  uint64_t lockWaitUs = 0;
  // Declared outside the context lock: the failure handling that reads them has to run AFTER
  // that lock is released. See the lock-order note below. (Ledger H-23.)
  bool preprocessedOk = false;
  bool nv12Ok = false;
  {
    const uint64_t lockWaitStartUs = submitStartUs;
    std::lock_guard<std::mutex> d3dLock(*contextMu_);
    const uint64_t lockAcquiredUs = qpc_us();
    lockWaitUs = lockAcquiredUs - lockWaitStartUs;
    if (preprocess || nv12OutView) {
      // Crop and scale in one blt: source rect selects the window client area, the
      // destination is the whole encode-size texture. Own-texture copy first, because
      // capture textures do not reliably accept video processor input views.
      context_->CopyResource(preSrcTexture_.Get(), src);
      RECT srcRect{};
      srcRect.left = static_cast<LONG>(slotMeta.cropX);
      srcRect.top = static_cast<LONG>(slotMeta.cropY);
      srcRect.right = static_cast<LONG>(slotMeta.cropX + slotMeta.cropW);
      srcRect.bottom = static_cast<LONG>(slotMeta.cropY + slotMeta.cropH);
      if (!preprocess || !slotMeta.cropW || !slotMeta.cropH) {
        srcRect = RECT{0, 0, static_cast<LONG>(meta.width), static_cast<LONG>(meta.height)};
      }
      RECT dstRect{0, 0, static_cast<LONG>(outW), static_cast<LONG>(outH)};
      videoContext_->VideoProcessorSetOutputTargetRect(vpProcessor_.Get(), TRUE, &dstRect);
      videoContext_->VideoProcessorSetStreamSourceRect(vpProcessor_.Get(), 0, TRUE, &srcRect);
      videoContext_->VideoProcessorSetStreamDestRect(vpProcessor_.Get(), 0, TRUE, &dstRect);
      videoContext_->VideoProcessorSetStreamFrameFormat(vpProcessor_.Get(), 0,
                                                        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
      // Input is full-range RGB either way. Leaving color spaces unset lets the driver
      // assume studio range on one side and crush levels. Auto-processing rings around UI
      // text; keep it off.
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputSpace{};
      inputSpace.RGB_Range = 0;
      inputSpace.YCbCr_Matrix = 1;
      inputSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
      videoContext_->VideoProcessorSetStreamColorSpace(vpProcessor_.Get(), 0, &inputSpace);
      videoContext_->VideoProcessorSetStreamAutoProcessingMode(vpProcessor_.Get(), 0, FALSE);
      D3D11_VIDEO_PROCESSOR_STREAM stream{};
      stream.Enable = TRUE;
      stream.pInputSurface = vpInputView_.Get();

      if (preprocess) {
        videoContext_->VideoProcessorSetOutputColorSpace(vpProcessor_.Get(), &inputSpace);
        if (SUCCEEDED(videoContext_->VideoProcessorBlt(vpProcessor_.Get(), vpOutputView_.Get(),
                                                       0, 1, &stream))) {
          D3D11_BOX box{0, 0, 0, outW, outH, 1};
          context_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, preDstTexture_.Get(), 0,
                                          &box);
          preprocessedOk = true;
          preprocessCount_.fetch_add(1, std::memory_order_relaxed);
        } else {
          preprocessFallbacks_.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (nv12OutView) {
        // BGRA -> NV12 for the zero-copy encoder input: BT.709, limited range, matching
        // what apply_video_colorimetry declares on the encoder input type.
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE nv12Space{};
        nv12Space.RGB_Range = 0;
        nv12Space.YCbCr_Matrix = 1;
        nv12Space.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        videoContext_->VideoProcessorSetOutputColorSpace(vpProcessor_.Get(), &nv12Space);
        nv12Ok = SUCCEEDED(videoContext_->VideoProcessorBlt(vpProcessor_.Get(),
                                                            nv12OutView.Get(), 0, 1, &stream));
        if (nv12Ok) {
          nv12Converted_.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
    if (!preprocessedOk) {
      context_->CopyResource(staging.Get(), src);
    }
    context_->End(query.Get());
    // Without a flush the copy can sit in the command buffer indefinitely on an otherwise
    // idle context: the query never signals and no frame is ever published. Flushing here
    // also submits the copy to the GPU before the DXGI duplication frame is released, which
    // preserves ordering against the next desktop update.
    context_->Flush();
  }
  // Everything that needs slotMu_ happens here, AFTER the context lock is released.
  //
  // The two locks have exactly one legal order, slotMu_ -> contextMu_, because that is what
  // WorkerLoop takes (it holds slotMu_ across its GetData sweep). Taking slotMu_ while still
  // holding contextMu_ -- which the blt-failure handling used to do -- is the opposite order and
  // deadlocks against that sweep. The NV12 branch is the one a driver without NV12 support takes
  // on its very first frame, so it was reachable.
  //
  // Releasing the context lock first is only safe because the slot is still Submitting: the
  // worker cannot see it until this block promotes it. Doing the promotion last is the whole
  // point -- the first version of this fix left the slot GpuPending throughout, which traded the
  // deadlock for the worker publishing a half-written meta. (Ledger H-23.)
  const uint64_t submitDoneUs = qpc_us();
  bool promoted = false;
  {
    std::lock_guard<std::mutex> lk(slotMu_);
    // Identity, not just generation: within one generation a slot can be freed by the worker and
    // re-reserved by a newer Submit, and it keeps the same staging texture across that -- so the
    // texture pointer is not a fence. submitSeq is.
    Slot* mine = (slotIndex < slots_.size()) ? &slots_[slotIndex] : nullptr;
    const bool slotStillOurs =
        mine && readback_slot_is_current(generation_, generationAtSubmit, mine->state,
                                         SlotState::Submitting, mine->submitSeq, submitSeqAtSubmit);
    if (slotStillOurs) {
      if (preprocess && !preprocessedOk) {
        // The blt failed after the meta was already stamped as preprocessed; fix it up so
        // the worker reads capture-size bytes and applies the CPU crop.
        mine->meta.payloadW = meta.width;
        mine->meta.payloadH = meta.height;
        mine->meta.preprocessed = false;
        mine->meta.cropX = meta.cropX;
        mine->meta.cropY = meta.cropY;
        mine->meta.cropW = meta.cropW;
        mine->meta.cropH = meta.cropH;
      }
      if (nv12OutView && !nv12Ok) {
        // One failed conversion disables the path for the session; retrying per frame would
        // burn a blt per frame for nothing. The release is inside the identity check: if a
        // reconfigure reclaimed this lease and handed the same index to a newer frame, returning
        // it from here would free that frame's surface (ABA).
        ReleaseNv12SlotLocked(mine->meta.nv12Slot, mine->meta.nv12Generation);
        mine->meta.nv12Slot = -1;
        nv12Broken_ = true;
        RB_DEBUG("nv12 blt failed; surface path disabled");
      }
      mine->meta.d3dWaitUs = lockWaitUs;
      mine->meta.submitCopyUs = submitDoneUs - submitStartUs - lockWaitUs;
      mine->meta.submitUs = submitDoneUs;
      mine->state = SlotState::GpuPending;  // now, and only now, visible to the worker
      promoted = true;
    } else if (nv12OutView && !nv12Ok) {
      // Our slot is gone (reconfigure), so CreateSlotsLocked already reclaimed the lease that
      // went with it. Record the capability loss without touching the ring.
      nv12Broken_ = true;
      RB_DEBUG("nv12 blt failed on a superseded slot; surface path disabled");
    }
  }
  RB_DEBUG("submit seq=%llu gen=%llu promoted=%d", static_cast<unsigned long long>(submitSeqAtSubmit),
           static_cast<unsigned long long>(generationAtSubmit), promoted ? 1 : 0);
  if (promoted) workerCv_.notify_one();
  return true;
}

void D3dCaptureReadbackPipeline::WorkerLoop() {
  // Backoff for the "copies in flight, none finished yet" case. The CV predicate is true while
  // anything is GpuPending, so waiting on it returns immediately and the loop would otherwise
  // re-take the context lock every 500us -- ~2000 acquisitions a second contending with the
  // capture callback's CopyResource and the GPU scaler. Start at the old 500us so a ready frame
  // is still picked up promptly, then back off to 2ms while nothing completes, and reset the
  // moment one does. (Ledger H-17.)
  constexpr auto kPollMinUs = std::chrono::microseconds(500);
  constexpr auto kPollMaxUs = std::chrono::microseconds(2000);
  auto pollBackoff = kPollMinUs;
  while (running_.load(std::memory_order_acquire)) {
    Slot slotCopy;
    Slot* slotRef = nullptr;
    uint64_t generationAtPick = 0;
    uint64_t submitSeqAtPick = 0;
    {
      std::unique_lock<std::mutex> lk(slotMu_);
      workerCv_.wait_for(lk, std::chrono::milliseconds(1), [&] {
        if (!running_.load(std::memory_order_acquire)) return true;
        for (const auto& s : slots_) {
          if (s.state == SlotState::GpuPending) return true;
        }
        return false;
      });
      if (!running_.load(std::memory_order_acquire)) return;

      // Which pending copies has the GPU finished? Checked outside the slot loop so the
      // latest-wins pick sees a consistent snapshot.
      std::vector<uint64_t> seq(slots_.size(), 0);
      std::vector<bool> ready(slots_.size(), false);
      {
        std::lock_guard<std::mutex> d3dLock(*contextMu_);
        for (size_t i = 0; i < slots_.size(); ++i) {
          if (slots_[i].state != SlotState::GpuPending || !slots_[i].query) continue;
          seq[i] = slots_[i].submitSeq;
          BOOL done = FALSE;
          // DONOTFLUSH: Submit already calls Flush() right after End(query), so the copy is on
          // the GPU by the time we poll. Without the flag every poll is allowed to flush the
          // context -- and this loop polls while holding the immediate-context lock the capture
          // callback needs to submit the NEXT frame. (Ledger H-17.)
          const HRESULT hr = context_->GetData(slots_[i].query.Get(), &done, sizeof(done),
                                               D3D11_ASYNC_GETDATA_DONOTFLUSH);
          ready[i] = (hr == S_OK && done);
          if (FAILED(hr) && hr != S_FALSE) {
            // Device loss: this query will never signal. Free the slot instead of leaving it
            // GpuPending forever -- a frozen ring is what turned one driver hiccup into a
            // dead capture pipeline.
            ReleaseNv12SlotLocked(slots_[i].meta.nv12Slot, slots_[i].meta.nv12Generation);
            slots_[i].meta.nv12Slot = -1;
            slots_[i].state = SlotState::Free;
            seq[i] = 0;
          }
        }
      }
      size_t pendingCount = 0;
      for (const auto& s : slots_) {
        if (s.state == SlotState::GpuPending) ++pendingCount;
      }
      static unsigned long long rbPollCount = 0;
      if (pendingCount > 0 && (++rbPollCount % 512) == 1) {
        RB_DEBUG("worker poll pending=%zu ready0=%d ready1=%d ready2=%d", pendingCount,
                 ready.size() > 0 ? (int)ready[0] : -1, ready.size() > 1 ? (int)ready[1] : -1,
                 ready.size() > 2 ? (int)ready[2] : -1);
      }
      size_t superseded = 0;
      const size_t pick = pick_latest_ready_slot(seq, ready, &superseded);
      if (pick == SIZE_MAX) {
        // Copies are in flight but the GPU has not finished any; the CV predicate would
        // return immediately, so yield briefly instead of spinning on GetData.
        lk.unlock();
        std::this_thread::sleep_for(pollBackoff);
        pollBackoff = (pollBackoff * 2 > kPollMaxUs) ? kPollMaxUs : pollBackoff * 2;
        continue;
      }
      pollBackoff = kPollMinUs;  // something completed; poll eagerly again
      for (size_t i = 0; i < slots_.size(); ++i) {
        if (i != pick && ready[i]) {
          // A superseded frame's NV12 slot would otherwise leak busy forever.
          ReleaseNv12SlotLocked(slots_[i].meta.nv12Slot, slots_[i].meta.nv12Generation);
          slots_[i].meta.nv12Slot = -1;
          slots_[i].state = SlotState::Free;
        }
      }
      if (superseded > 0) supersededDrops_.fetch_add(superseded, std::memory_order_relaxed);
      slotRef = &slots_[pick];
      slotCopy.staging = slotRef->staging;
      slotCopy.meta = slotRef->meta;
      generationAtPick = generation_;
      submitSeqAtPick = slotRef->submitSeq;
    }

    // Map/copy outside slotMu_ so the callback can keep submitting into other slots. The
    // query already confirmed completion, so this Map does not stall on the GPU.
    const CaptureFrameMeta& meta = slotCopy.meta;
    const uint64_t gpuPendingUs =
        (meta.submitUs > 0) ? (qpc_us() > meta.submitUs ? qpc_us() - meta.submitUs : 0) : 0;
    const uint32_t payloadW = (meta.payloadW >= 2) ? meta.payloadW : meta.width;
    const uint32_t payloadH = (meta.payloadH >= 2) ? meta.payloadH : meta.height;
    const uint32_t stride = payloadW * 4;
    auto payload = bufferPool_.Acquire(static_cast<size_t>(stride) * payloadH);
    uint64_t mapUs = 0;
    uint64_t memcpyUs = 0;
    bool mapped = false;
    D3D11_MAPPED_SUBRESOURCE map{};
    bool mapHeld = false;
    {
      std::lock_guard<std::mutex> d3dLock(*contextMu_);
      const uint64_t mapStartUs = qpc_us();
      if (SUCCEEDED(context_->Map(slotCopy.staging.Get(), 0, D3D11_MAP_READ, 0, &map))) {
        mapUs = qpc_us() - mapStartUs;
        mapHeld = true;
      }
    }
    if (mapHeld) {
      // Copy outside the context lock. A mapping is a property of the resource, not of the
      // context, so reading it needs no lock -- and this is a whole 1080p frame, ~1 ms of
      // pure memcpy. Holding the immediate context across it blocked the capture thread
      // inside Submit, and the capture thread blocks there while it is still holding a
      // desktop duplication frame. Desktop duplication reports nothing new while a frame is
      // held, so every readback quietly cost the capture its next update: 27 frames a second
      // arriving for a 30 fps request, with no counter anywhere showing why.
      const uint64_t memcpyStartUs = qpc_us();
      const auto* srcRow = reinterpret_cast<const uint8_t*>(map.pData);
      auto* dst = payload->data();
      if (map.RowPitch == stride) {
        std::memcpy(dst, srcRow, static_cast<size_t>(stride) * payloadH);
      } else {
        for (uint32_t y = 0; y < payloadH; ++y) {
          std::memcpy(dst + static_cast<size_t>(y) * stride,
                      srcRow + static_cast<size_t>(y) * map.RowPitch, stride);
        }
      }
      memcpyUs = qpc_us() - memcpyStartUs;
      {
        std::lock_guard<std::mutex> d3dLock(*contextMu_);
        context_->Unmap(slotCopy.staging.Get(), 0);
      }
      mapped = true;
    }

    uint32_t outW = payloadW;
    uint32_t outH = payloadH;
    uint32_t outStride = stride;
    if (mapped && !meta.preprocessed && meta.cropActive && meta.cropW >= 2 && meta.cropH >= 2 &&
        meta.cropX + meta.cropW <= meta.width && meta.cropY + meta.cropH <= meta.height &&
        (meta.cropW < meta.width || meta.cropH < meta.height || meta.cropX > 0 ||
         meta.cropY > 0)) {
      const uint32_t croppedStride = meta.cropW * 4;
      const uint64_t cropStartUs = qpc_us();
      auto cropped = bufferPool_.Acquire(static_cast<size_t>(croppedStride) * meta.cropH);
      const auto* srcBase = payload->data();
      auto* dstBase = cropped->data();
      for (uint32_t y = 0; y < meta.cropH; ++y) {
        std::memcpy(dstBase + static_cast<size_t>(y) * croppedStride,
                    srcBase + static_cast<size_t>(meta.cropY + y) * stride +
                        static_cast<size_t>(meta.cropX) * 4u,
                    croppedStride);
      }
      memcpyUs += qpc_us() - cropStartUs;
      payload = std::move(cropped);
      outW = meta.cropW;
      outH = meta.cropH;
      outStride = croppedStride;
    }

    bool handOff = false;
    {
      std::lock_guard<std::mutex> lk(slotMu_);
      // Only free the slot if a reconfigure has not replaced it, and if a newer Submit has not
      // already taken it over -- the staging texture survives slot reuse, so submitSeq is the
      // identity here too. (Ledger H-23.)
      if (slotRef && readback_slot_is_current(generation_, generationAtPick, slotRef->state,
                                             SlotState::GpuPending, slotRef->submitSeq,
                                             submitSeqAtPick)) {
        slotRef->state = SlotState::Free;
        handOff = mapped && static_cast<bool>(publish_);
        // The NV12 lease travels with the frame. Hand it to the consumer when the frame is
        // actually published (whoever pops the frame releases it), and give it back here when
        // it is not -- a failed Map used to skip publish_ and strand the slot. Either way the
        // slot's own copy is cleared, so `meta.nv12Slot >= 0` keeps meaning "the ring still
        // owns this lease", which is the invariant CreateSlotsLocked reclaims in bulk from.
        // (Ledger H-03a.)
        if (!handOff) {
          ReleaseNv12SlotLocked(slotCopy.meta.nv12Slot, slotCopy.meta.nv12Generation);
        }
        slotRef->meta.nv12Slot = -1;
      }
      // else: a reconfigure replaced this slot. Deliberately NOT released from here -- see the
      // note in CreateSlotsLocked, which already reclaimed it. (Ledger H-03b.)
    }
    if (handOff) {
      publish_(std::move(payload), outW, outH, outStride, meta, gpuPendingUs, mapUs, memcpyUs);
    }
  }
}

}  // namespace remote60::native_poc
