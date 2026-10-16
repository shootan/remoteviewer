// motion-settle r10 (Codex r10 s1, s4.1): the output-only collect at the real H264Encoder boundary.
//
// An asynchronous fake MFT (MF_TRANSFORM_ASYNC + IMFMediaEventGenerator) adopted through
// set_transform_for_test counts ProcessInput / ProcessOutput / GetEvent and releases each input's output
// by a policy the test picks:
//   [1] Manual -- the output becomes ready only when the test says so, AFTER the encode call returned
//       (r8's "the AU was ready but nobody called"): collect_outputs hands it out with NO ProcessInput,
//       the accepted-input count does not move, and its provenance is the catch-up input's.
//   [2] OnNextInput -- the MFT holds an input until the next one: collect finds nothing (and makes no
//       input); only the one extra input (the kick) brings the held AU out -- exactly one ProcessInput.
//   [3] nothing pending: collect makes no ProcessOutput (async: ProcessOutput only after HaveOutput) and
//       no input; a NeedInput event it reads changes nothing.
//   [4] collect on an encoder that is not running fails closed (no calls).
// The host-side branch (collect satisfied -> no kick; unsatisfied at 150 ms -> one kick; collect-only
// never kicks) is pinned in host_kick_test.cpp (TestCatchupAwaitCollect).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>

#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "host_fake_encoder_mft.hpp"
#include "mf_h264_codec.hpp"

using remote60::native_poc::H264AccessUnit;
using remote60::native_poc::H264CollectStats;
using remote60::native_poc::H264Encoder;
using remote60::native_poc::kSyntheticReasonCatchup;
using remote60::native_poc::kSyntheticReasonKick;
using remote60::native_poc::testing::FakeEncoderMft;

namespace {

int gChecks = 0, gFailures = 0;
void expect(bool c, const std::string& what) {
  ++gChecks;
  if (!c) {
    ++gFailures;
    std::printf("  FAIL %s\n", what.c_str());
  }
}

class FakeAsyncEncoderMft : public FakeEncoderMft, public IMFMediaEventGenerator {
 public:
  enum class Policy { Manual, OnNextInput };
  Policy policy = Policy::Manual;
  int processInputCalls = 0, processOutputCalls = 0, getEventCalls = 0;

  FakeAsyncEncoderMft() : FakeEncoderMft(L"VEN_1002", L"AMD H.264 Encoder MFT (fake async)") {
    Microsoft::WRL::ComPtr<IMFAttributes> a;
    if (SUCCEEDED(FakeEncoderMft::GetAttributes(&a)) && a) a->SetUINT32(MF_TRANSFORM_ASYNC, 1);
  }
  // Manual policy: the held outputs become ready now (one METransformHaveOutput each).
  void ReleaseHeld() {
    while (held_ > 0) {
      --held_;
      ++ready_;
      events_.push_back(METransformHaveOutput);
    }
  }
  void PostNeedInput() { events_.push_back(METransformNeedInput); }
  int held() const { return held_; }

  // ---- IUnknown (one refcount for both bases; this object deletes itself as what it is)
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IMFMediaEventGenerator)) {
      *ppv = static_cast<IMFMediaEventGenerator*>(this);
      AddRef();
      return S_OK;
    }
    return FakeEncoderMft::QueryInterface(riid, ppv);
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }

  // ---- IMFMediaEventGenerator (GetEvent with MF_EVENT_FLAG_NO_WAIT is all the product uses)
  STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** out) override {
    ++getEventCalls;
    if (!out) return E_POINTER;
    *out = nullptr;
    if (events_.empty()) return (flags & MF_EVENT_FLAG_NO_WAIT) ? MF_E_NO_EVENTS_AVAILABLE : E_NOTIMPL;
    const MediaEventType t = events_.front();
    events_.pop_front();
    return MFCreateMediaEvent(t, GUID_NULL, S_OK, nullptr, out);
  }
  STDMETHODIMP BeginGetEvent(IMFAsyncCallback*, IUnknown*) override { return E_NOTIMPL; }
  STDMETHODIMP EndGetEvent(IMFAsyncResult*, IMFMediaEvent**) override { return E_NOTIMPL; }
  STDMETHODIMP QueueEvent(MediaEventType, REFGUID, HRESULT, const PROPVARIANT*) override { return E_NOTIMPL; }

  // ---- the encode half
  STDMETHODIMP ProcessInput(DWORD, IMFSample*, DWORD) override {
    ++processInputCalls;
    if (policy == Policy::OnNextInput && held_ > 0) ReleaseHeld();
    ++held_;
    events_.push_back(METransformNeedInput);
    return S_OK;
  }
  STDMETHODIMP ProcessOutput(DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER* odb, DWORD* status) override {
    ++processOutputCalls;
    if (status) *status = 0;
    if (ready_ == 0) return MF_E_TRANSFORM_NEED_MORE_INPUT;
    if (!odb || !odb->pSample) return E_POINTER;
    --ready_;
    // a non-IDR slice NAL behind a start code: enough for the drain's Annex-B / key checks
    static const uint8_t kAu[] = {0x00, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x24, 0x6c, 0x42, 0x10, 0x00};
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buf;
    if (FAILED(odb->pSample->ConvertToContiguousBuffer(&buf)) || !buf) return E_FAIL;
    BYTE* p = nullptr;
    DWORD maxLen = 0;
    if (FAILED(buf->Lock(&p, &maxLen, nullptr)) || maxLen < sizeof(kAu)) return E_FAIL;
    memcpy(p, kAu, sizeof(kAu));
    buf->Unlock();
    buf->SetCurrentLength(sizeof(kAu));
    return S_OK;
  }

 private:
  ~FakeAsyncEncoderMft() = default;
  std::atomic<ULONG> refs_{1};
  std::deque<MediaEventType> events_;
  int held_ = 0, ready_ = 0;
};

constexpr uint32_t kW = 320, kH = 192;

bool start(H264Encoder& enc, FakeAsyncEncoderMft* fake) {
  enc.set_transform_for_test(fake);
  return enc.initialize(kW, kH, 30, 1500000, 300);
}

bool encode(H264Encoder& enc, uint8_t reason, uint64_t version, int64_t ts, std::vector<H264AccessUnit>* units) {
  std::vector<uint8_t> nv12(static_cast<size_t>(kW) * kH * 3 / 2, 100);
  enc.set_next_input_synthetic(reason != 0);
  enc.set_next_input_synthetic_reason(reason);
  enc.set_next_input_content_version(version);
  enc.set_next_input_epoch(7);
  return enc.encode_frame(nv12, false, ts, units);
}

void TestReadyOutputIsCollectedWithoutAnInput() {
  auto* fake = new FakeAsyncEncoderMft();
  fake->policy = FakeAsyncEncoderMft::Policy::Manual;
  H264Encoder enc;
  expect(start(enc, fake), "[1] initialize with the async fake");
  std::vector<H264AccessUnit> units;
  expect(encode(enc, kSyntheticReasonCatchup, 349, 3333333, &units), "[1] the catch-up input is encoded");
  expect(units.empty() && enc.accepted_input_count() == 1 && fake->processInputCalls == 1,
         "[1] accepted, no AU in its own call (the r8 case)");
  fake->ReleaseHeld();  // the output becomes ready AFTER the call returned
  std::vector<H264AccessUnit> got;
  H264CollectStats cs{};
  expect(enc.collect_outputs(&got, &cs), "[1] collect succeeds");
  expect(fake->processInputCalls == 1, "[1] collect makes NO ProcessInput (kick input count stays 0)");
  expect(enc.accepted_input_count() == 1, "[1] an output-only collect is never an accepted input");
  expect(got.size() == 1 && cs.aus == 1 && cs.haveOutput == 1, "[1] the ready AU is handed out by the collect");
  if (!got.empty()) {
    expect(got[0].acceptedInputOrdinal == 1 && got[0].syntheticReason == kSyntheticReasonCatchup &&
               got[0].contentVersion == 349 && got[0].inputEpoch == 7 && got[0].inputAcceptedAtUs != 0,
           "[1] it carries the catch-up input's provenance (ordinal / reason / version / epoch / accept time)");
  }
  enc.shutdown();
}

void TestHeldUntilNextInputNeedsTheOneKick() {
  auto* fake = new FakeAsyncEncoderMft();
  fake->policy = FakeAsyncEncoderMft::Policy::OnNextInput;
  H264Encoder enc;
  expect(start(enc, fake), "[2] initialize with the async fake");
  std::vector<H264AccessUnit> units;
  expect(encode(enc, kSyntheticReasonCatchup, 400, 3333333, &units) && units.empty(), "[2] catch-up accepted, held");
  std::vector<H264AccessUnit> got;
  H264CollectStats cs{};
  expect(enc.collect_outputs(&got, &cs) && got.empty() && cs.haveOutput == 0,
         "[2] the MFT holds it: collect finds nothing");
  expect(fake->processInputCalls == 1 && enc.accepted_input_count() == 1, "[2] ... and makes no input");
  units.clear();
  expect(encode(enc, kSyntheticReasonKick, 400, 3666666, &units), "[2] the one extra input (kick) is encoded");
  expect(fake->processInputCalls == 2, "[2] exactly one extra ProcessInput");
  expect(units.size() == 1 && units[0].acceptedInputOrdinal == 1 && units[0].syntheticReason == kSyntheticReasonCatchup,
         "[2] the kick's call brings the held catch-up AU out");
  enc.shutdown();
}

void TestNothingPendingIsANoOp() {
  auto* fake = new FakeAsyncEncoderMft();
  H264Encoder enc;
  expect(start(enc, fake), "[3] initialize with the async fake");
  fake->PostNeedInput();
  const int outBefore = fake->processOutputCalls;
  std::vector<H264AccessUnit> got;
  H264CollectStats cs{};
  expect(enc.collect_outputs(&got, &cs), "[3] collect succeeds with nothing pending");
  expect(got.empty() && cs.needInput == 1 && cs.haveOutput == 0, "[3] it read the NeedInput, found no output");
  expect(fake->processOutputCalls == outBefore, "[3] async: no ProcessOutput without METransformHaveOutput");
  expect(fake->processInputCalls == 0 && enc.accepted_input_count() == 0, "[3] and no input");
  enc.shutdown();
}

void TestNotRunningFailsClosed() {
  H264Encoder enc;  // never initialized
  std::vector<H264AccessUnit> got;
  H264CollectStats cs{};
  expect(!enc.collect_outputs(&got, &cs) && got.empty(), "[4] collect on a stopped encoder fails closed");
}

}  // namespace

int main() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION);
  TestReadyOutputIsCollectedWithoutAnInput();
  TestHeldUntilNextInputNeedsTheOneKick();
  TestNothingPendingIsANoOp();
  TestNotRunningFailsClosed();
  MFShutdown();
  CoUninitialize();
  std::printf("host_catchup_collect_test: %s (%d checks, %d failed)\n", gFailures == 0 ? "PASS" : "FAIL", gChecks,
              gFailures);
  return gFailures == 0 ? 0 : 1;
}
