import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'
F = S + r'\host_encode_epoch_test.cpp'

FAKE = '''
// ---------------------------------------------------------------------------------------------
// A fake Media Foundation transform, for [5] only: a real encoder cannot be made to accept 64
// inputs without producing output and then fail its drain, which is exactly the sequence A06 is
// about. It is synchronous (no IMFMediaEventGenerator), accepts every input, and answers
// ProcessOutput as the test tells it to.
// ---------------------------------------------------------------------------------------------
class FakeTransform : public IMFTransform {
 public:
  // What the next ProcessOutput calls do.
  enum class OutputMode {
    NeedMoreInput,   // the ordinary "nothing ready yet"
    FailDrain,       // a hard error: the encoder's drain returns false -> encode_frame_* fails
    OneThenFail,     // one real sample, then the hard error (the partial-output case)
  };
  void SetOutputMode(OutputMode mode) { mode_ = mode; }
  uint32_t inputs_accepted() const { return inputsAccepted_; }

  // --- IUnknown: the encoder holds it in a ComPtr, and must NOT find an event generator on it
  // (that would take the asynchronous path). ---
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFTransform)) {
      *ppv = static_cast<IMFTransform*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }

  // --- IMFTransform: only what H264Encoder actually calls. ---
  STDMETHODIMP GetStreamLimits(DWORD* inMin, DWORD* inMax, DWORD* outMin, DWORD* outMax) override {
    if (inMin) *inMin = 1; if (inMax) *inMax = 1; if (outMin) *outMin = 1; if (outMax) *outMax = 1;
    return S_OK;
  }
  STDMETHODIMP GetStreamCount(DWORD* in, DWORD* out) override {
    if (in) *in = 1; if (out) *out = 1;
    return S_OK;
  }
  STDMETHODIMP GetStreamIDs(DWORD, DWORD*, DWORD, DWORD*) override { return E_NOTIMPL; }
  STDMETHODIMP GetInputStreamInfo(DWORD, MFT_INPUT_STREAM_INFO* info) override {
    if (info) *info = MFT_INPUT_STREAM_INFO{};
    return S_OK;
  }
  STDMETHODIMP GetOutputStreamInfo(DWORD, MFT_OUTPUT_STREAM_INFO* info) override {
    if (!info) return E_POINTER;
    *info = MFT_OUTPUT_STREAM_INFO{};
    info->cbSize = 64 * 1024;  // the encoder allocates the sample itself
    return S_OK;
  }
  STDMETHODIMP GetAttributes(IMFAttributes**) override { return E_NOTIMPL; }
  STDMETHODIMP GetInputStreamAttributes(DWORD, IMFAttributes**) override { return E_NOTIMPL; }
  STDMETHODIMP GetOutputStreamAttributes(DWORD, IMFAttributes**) override { return E_NOTIMPL; }
  STDMETHODIMP DeleteInputStream(DWORD) override { return E_NOTIMPL; }
  STDMETHODIMP AddInputStreams(DWORD, DWORD*) override { return E_NOTIMPL; }
  STDMETHODIMP GetInputAvailableType(DWORD, DWORD, IMFMediaType**) override { return MF_E_NO_MORE_TYPES; }
  STDMETHODIMP GetOutputAvailableType(DWORD, DWORD, IMFMediaType**) override { return MF_E_NO_MORE_TYPES; }
  STDMETHODIMP SetInputType(DWORD, IMFMediaType*, DWORD) override { return S_OK; }
  STDMETHODIMP SetOutputType(DWORD, IMFMediaType* type, DWORD flags) override {
    if ((flags & MFT_SET_TYPE_TEST_ONLY) == 0) outputType_ = type;
    return S_OK;
  }
  STDMETHODIMP GetInputCurrentType(DWORD, IMFMediaType**) override { return E_NOTIMPL; }
  STDMETHODIMP GetOutputCurrentType(DWORD, IMFMediaType** type) override {
    if (!type) return E_POINTER;
    *type = outputType_.Get();
    if (*type) (*type)->AddRef();
    return *type ? S_OK : E_FAIL;
  }
  STDMETHODIMP GetInputStatus(DWORD, DWORD* flags) override {
    if (flags) *flags = MFT_INPUT_STATUS_ACCEPT_DATA;
    return S_OK;
  }
  STDMETHODIMP GetOutputStatus(DWORD*) override { return E_NOTIMPL; }
  STDMETHODIMP SetOutputBounds(LONGLONG, LONGLONG) override { return E_NOTIMPL; }
  STDMETHODIMP ProcessEvent(DWORD, IMFMediaEvent*) override { return E_NOTIMPL; }
  STDMETHODIMP ProcessMessage(MFT_MESSAGE_TYPE, ULONG_PTR) override { return S_OK; }
  STDMETHODIMP ProcessInput(DWORD, IMFSample*, DWORD) override {
    ++inputsAccepted_;  // always accepted: this is how the FIFO fills without any output
    return S_OK;
  }
  STDMETHODIMP ProcessOutput(DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER* buffers, DWORD* status) override {
    if (status) *status = 0;
    switch (mode_) {
      case OutputMode::NeedMoreInput:
        return MF_E_TRANSFORM_NEED_MORE_INPUT;
      case OutputMode::FailDrain:
        return E_FAIL;  // the encoder's drain gives up -> the encode call returns false
      case OutputMode::OneThenFail: {
        mode_ = OutputMode::FailDrain;  // one sample, then the failure
        if (!buffers || !buffers[0].pSample) return E_FAIL;
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(buffers[0].pSample->GetBufferByIndex(0, &buffer)) || !buffer) return E_FAIL;
        BYTE* data = nullptr;
        DWORD maxLen = 0;
        if (FAILED(buffer->Lock(&data, &maxLen, nullptr))) return E_FAIL;
        // A minimal Annex-B access unit so the encoder's output path treats it as one.
        static const BYTE kAu[] = {0, 0, 0, 1, 0x65, 0x11, 0x22, 0x33};
        const DWORD len = (maxLen < sizeof(kAu)) ? maxLen : static_cast<DWORD>(sizeof(kAu));
        std::memcpy(data, kAu, len);
        buffer->Unlock();
        buffer->SetCurrentLength(len);
        (void)buffers[0].pSample->SetSampleTime(0);
        return S_OK;
      }
    }
    return MF_E_TRANSFORM_NEED_MORE_INPUT;
  }

 private:
  std::atomic<ULONG> refs_{1};
  OutputMode mode_ = OutputMode::NeedMoreInput;
  uint32_t inputsAccepted_ = 0;
  Microsoft::WRL::ComPtr<IMFMediaType> outputType_;
};

// [5] A06 end to end on the stage's own call path: a real accepted-input overflow, then an encode
// call that FAILS, and the owner still ends up with the gate closed and a rebuild pending -- which
// is what the early return used to lose. Nothing is set by hand: the FIFO overflows because the
// transform accepts input and never produces output, and the encode fails because the drain does.
void test_provenance_survives_failed_encode_calls() {
  std::printf("[5] A06 on the encode call path: overflow + a failing encode still closes the gate and asks for a rebuild""" + NL + """);
  const std::vector<uint8_t> bgra(static_cast<size_t>(kW) * kH * 4, 0x40);
  for (int variant = 0; variant < 2; ++variant) {
    const bool partialOutput = (variant == 1);
    std::printf("  variant %s""" + NL + """, partialOutput ? "(b) partial output, then the drain fails"
                                                           : "(a) the drain fails, no unit at all");
    Microsoft::WRL::ComPtr<FakeTransform> fake(new FakeTransform());
    fake->Release();  // the ComPtr took its own reference; drop the constructor's

    CaptureState capture;
    EncoderState enc;
    enc.activeEncodeW = kW;
    enc.activeEncodeH = kH;
    enc.activeFps = 60;
    enc.activeBitrate = 2000000;
    enc.activeKeyint = 600;
    capture.inputEpoch.store(3, std::memory_order_release);
    enc.epochGate.epoch = 3;
    enc.codec.set_transform_for_test(fake.Get());
    if (!enc.codec.initialize(kW, kH, 60, 2000000, 600)) {
      std::printf("  fake transform: initialize failed""" + NL + """);
      ++gFailures;
      return;
    }
    // Fill the accepted-input FIFO past its bound. The transform accepts every input and produces
    // nothing, which is precisely the starvation the overflow rule is for.
    std::vector<H264AccessUnit> units;
    H264EncodeFrameStats stats{};
    for (int i = 0; i < 70 && !enc.codec.provenance_invalid(); ++i) {
      enc.codec.set_next_input_epoch(3);
      units.clear();
      const auto call = enc.EncodeBgraWithProvenance(bgra.data(), kW, kH, kW * 4, false,
                                                     static_cast<int64_t>(i + 1) * 10000, &units, &stats);
      CHECK(call.ok);          // still succeeding: nothing has failed yet
      CHECK(units.empty());    // and nothing came out
    }
    CHECK(enc.codec.provenance_invalid());   // the FIFO overflowed on its own
    CHECK(enc.epochGate.provenanceInvalid);  // and the call that saw it closed the gate
    CHECK(enc.provenanceResyncPending);
    std::printf("  overflow after %u accepted inputs; gate closed=%d pending=%d""" + NL + """,
                fake->inputs_accepted(), enc.epochGate.provenanceInvalid ? 1 : 0,
                enc.provenanceResyncPending ? 1 : 0);

    // Now the encode call itself fails -- with or without a unit first. The stage returns early
    // here; the provenance handling must already have happened inside the call.
    enc.epochGate.provenanceInvalid = false;   // pretend the previous episode was resolved...
    enc.provenanceResyncPending = false;       // ... so this call is the only thing that can latch
    fake->SetOutputMode(partialOutput ? FakeTransform::OutputMode::OneThenFail
                                      : FakeTransform::OutputMode::FailDrain);
    units.clear();
    enc.codec.set_next_input_epoch(3);
    const auto failing = enc.EncodeBgraWithProvenance(bgra.data(), kW, kH, kW * 4, false, 990000,
                                                      &units, &stats);
    CHECK(!failing.ok);                       // the encode failed, as the stage's early return sees
    CHECK(enc.epochGate.provenanceInvalid);   // and the gate is closed all the same
    CHECK(enc.provenanceResyncPending);       // with a rebuild pending for the tick to retry
    CHECK(failing.provenanceLatched);         // this call is the one that latched it
    if (partialOutput) CHECK(!units.empty() || true);  // a unit may or may not survive the failure

    // The tick retry (no new frame): the shared budget applies, a failure keeps the request, and a
    // successful rebuild is what clears the latch.
    for (int i = 0; i < 3; ++i) CHECK(epoch_gate_take_reset_budget(enc.epochGate, 1000000));
    CHECK(!enc.TryProvenanceResync(capture, 1000000));      // budget spent this window
    CHECK(enc.provenanceResyncPending && enc.epochGate.provenanceInvalid);
    CHECK(enc.provenanceResyncFailed == 0);                 // a spent budget is not a failure
    enc.activeEncodeW = 0;                                  // make the rebuild itself fail
    enc.activeEncodeH = 0;
    const uint64_t nextWindowUs = 1000000 + EpochGate::kResetWindowUs + 1;
    CHECK(!enc.TryProvenanceResync(capture, nextWindowUs));
    CHECK(enc.provenanceResyncPending && enc.epochGate.provenanceInvalid);
    CHECK(enc.provenanceResyncFailed == 1);
    enc.activeEncodeW = kW;                                 // and now let it succeed
    enc.activeEncodeH = kH;
    enc.codec.set_transform_for_test(nullptr);              // back to a real encoder for the rebuild
    const uint64_t thirdWindowUs = nextWindowUs + EpochGate::kResetWindowUs + 1;
    if (enc.TryProvenanceResync(capture, thirdWindowUs)) {
      CHECK(!enc.provenanceResyncPending);
      CHECK(!enc.epochGate.provenanceInvalid);
      CHECK(!enc.codec.provenance_invalid());
      CHECK(enc.forceKeyNext);
    } else {
      std::printf("  note: no real encoder available for the final rebuild; the pending half was checked""" + NL + """);
    }
    enc.codec.shutdown();
  }
}
'''

edit(F, [
("void test_provenance_fifo_latch_and_gate() {", FAKE.lstrip('\n') + "\nvoid test_provenance_fifo_latch_and_gate() {"),
("  test_provenance_fifo_latch_and_gate();\n",
 "  test_provenance_fifo_latch_and_gate();\n  test_provenance_survives_failed_encode_calls();\n"),
("#include <algorithm>", "#include <algorithm>\n#include <atomic>\n#include <cstring>"),
])
print('fake transform regression ok')
