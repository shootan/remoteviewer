#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
#include <atomic>
#include <iostream>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
// Only this isolated test TU opens the fields to inject an IMFTransform. Product
// source/header remains unchanged; no real hardware transform is instantiated.
#define private public
#include "mf_h264_codec.hpp"
#undef private

class FakeTransform final : public IMFTransform {
 public:
  explicit FakeTransform(bool acceptThenFail) : acceptThenFail_(acceptThenFail) {}
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFTransform)) {
      *out = static_cast<IMFTransform*>(this); AddRef(); return S_OK;
    }
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE GetStreamLimits(DWORD*,DWORD*,DWORD*,DWORD*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetStreamCount(DWORD*,DWORD*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetStreamIDs(DWORD,DWORD*,DWORD,DWORD*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetInputStreamInfo(DWORD,MFT_INPUT_STREAM_INFO*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetOutputStreamInfo(DWORD,MFT_OUTPUT_STREAM_INFO* info) override {
    *info = {}; info->dwFlags = MFT_OUTPUT_STREAM_PROVIDES_SAMPLES; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetAttributes(IMFAttributes**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetInputStreamAttributes(DWORD,IMFAttributes**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetOutputStreamAttributes(DWORD,IMFAttributes**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE DeleteInputStream(DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE AddInputStreams(DWORD,DWORD*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetInputAvailableType(DWORD,DWORD,IMFMediaType**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetOutputAvailableType(DWORD,DWORD,IMFMediaType**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SetInputType(DWORD,IMFMediaType*,DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SetOutputType(DWORD,IMFMediaType*,DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetInputCurrentType(DWORD,IMFMediaType**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetOutputCurrentType(DWORD,IMFMediaType**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetInputStatus(DWORD,DWORD*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetOutputStatus(DWORD*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SetOutputBounds(LONGLONG,LONGLONG) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE ProcessEvent(DWORD,IMFMediaEvent*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE ProcessMessage(MFT_MESSAGE_TYPE,ULONG_PTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE ProcessInput(DWORD,IMFSample*,DWORD) override {
    ++inputCalls;
    if (acceptThenFail_) { ++acceptedInputs; return S_OK; }
    return inputCalls == 1 ? MF_E_NOTACCEPTING : E_FAIL;
  }
  HRESULT STDMETHODCALLTYPE ProcessOutput(DWORD,DWORD,MFT_OUTPUT_DATA_BUFFER* out,DWORD* status) override {
    ++outputCalls;
    if (outputCalls != 1) return acceptThenFail_ ? E_FAIL : MF_E_TRANSFORM_NEED_MORE_INPUT;
    Microsoft::WRL::ComPtr<IMFSample> sample;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateSample(&sample); if (FAILED(hr)) return hr;
    hr = MFCreateMemoryBuffer(8, &buffer); if (FAILED(hr)) return hr;
    BYTE* p = nullptr; DWORD max = 0, current = 0;
    hr = buffer->Lock(&p, &max, &current); if (FAILED(hr)) return hr;
    const BYTE bytes[8] = {0,0,0,1,0x41,0x88,0x84,0x21};
    memcpy(p, bytes, sizeof(bytes)); buffer->Unlock();
    buffer->SetCurrentLength(sizeof(bytes)); sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(1000); sample->SetSampleDuration(333333);
    out[0].pSample = sample.Detach(); out[0].dwStatus = 0; out[0].pEvents = nullptr;
    *status = 0; return S_OK;
  }
  unsigned inputCalls = 0, outputCalls = 0, acceptedInputs = 0;
 private:
  std::atomic<ULONG> refs_{1};
  bool acceptThenFail_;
};

int main() {
  assert(SUCCEEDED(MFStartup(MF_VERSION)));
  for (bool acceptThenFail : {false, true}) {
    remote60::native_poc::H264Encoder encoder;
    auto* transform = new FakeTransform(acceptThenFail);
    encoder.enc_.Attach(transform);
    encoder.started_ = true;
    encoder.width_ = 2; encoder.height_ = 2; encoder.fps_ = 30;
    encoder.sampleDurationHns_ = 333333;
    encoder.pendingInputSampleTimesHns_.push_back(1000); // already accepted A
    encoder.pendingInputSynthetic_.push_back(false);
    encoder.pendingInputEpoch_.push_back(1);
    encoder.set_next_input_epoch(1);
    std::vector<remote60::native_poc::H264AccessUnit> units;
    remote60::native_poc::H264EncodeFrameStats stats;
    const bool ok = encoder.encode_frame(std::vector<uint8_t>(6,128), false, 2000, &units, &stats);
    assert(!ok && units.size() == 1 && units[0].sampleTimeHns == 1000);
    assert(units[0].inputEpoch == 1 && !units[0].keyFrame);
    assert(transform->acceptedInputs == (acceptThenFail ? 1u : 0u));
    std::cout << "case=" << (acceptThenFail ? "accepted_B_then_output_error" : "retry_B_rejected")
              << " encode_return=" << ok << " input_calls=" << transform->inputCalls
              << " B_accepted=" << transform->acceptedInputs << " produced_units=" << units.size()
              << " produced_source_stamp=" << units[0].sampleTimeHns
              << " produced_source_epoch=" << units[0].inputEpoch
              << " pending_depth=" << stats.pendingInputDepth << '\n';
  }
  MFShutdown();
}
