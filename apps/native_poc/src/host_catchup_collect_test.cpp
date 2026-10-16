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
// r11 (Codex r11 s3.5): the await tick the product calls (catchup_await_tick / catchup_await_observe_handed,
// host_catchup_await.hpp) driven over the same fake MFT + real H264Encoder + real KickState, with a
// recording emit in place of the sender:
//   [5] ready AU -> observed via collect, ProcessInput 0, emitted once, accepted count unchanged;
//   [6] a real input accepted between the catch-up's acceptance and the next tick drains the catch-up's
//       AU in its own encode call -> observed via "other"; the next tick is idle, nothing emitted twice;
//   [7] paused stream -> cancelled, no collect (no ProcessOutput), no emit;
//   [8] codec instance swapped (shutdown + initialize) -> cancelled;
//   [9] never released -> no-output ticks, deferred at the 1.15 s deadline (late watch on); the AU then
//       comes out in a later encode call -> logged as late output, not as the await's success;
//  [10] the collect's drain fails after HaveOutput -> collect-error: nothing sent, deferred;
//  [11] the emit stage drops the collected AU (epoch gate / fence) -> not observed (pending), deferred later.
// Sender / receiver behaviour is the shipping binary's e2e, not this test.

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

#include <sstream>

#include "host_catchup_await.hpp"
#include "host_fake_encoder_mft.hpp"
#include "host_kick.hpp"
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
  bool failNextProcessOutput = false;  // [10]: the next ProcessOutput with a ready output fails

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
    if (failNextProcessOutput) {
      failNextProcessOutput = false;
      return E_FAIL;
    }
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
         "[2] the next input's encode call brings the held catch-up AU out (a codec fact; r11 makes no extra input)");
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


// ---- r11: the await tick over the real encoder -------------------------------------------------------
using remote60::native_poc::CatchupAwaitIdentity;
using remote60::native_poc::CatchupAwaitStep;
using remote60::native_poc::CatchupHandedAu;
using remote60::native_poc::KickState;
using remote60::native_poc::catchup_await_observe_handed;
using remote60::native_poc::catchup_await_tick;
using remote60::native_poc::kEmitHanded;

constexpr uint64_t kT0 = 100000000;

struct Recorder {
  std::vector<uint64_t> ordinals;
  int verdict = kEmitHanded;  // what the "emit stage" answers
  int operator()(H264AccessUnit& au) {
    ordinals.push_back(au.acceptedInputOrdinal);
    return verdict;
  }
};

CatchupAwaitIdentity identity_of(H264Encoder& enc) {
  CatchupAwaitIdentity id;
  id.codecInstance = enc.instance_id();
  id.sessionEpoch = 11;
  id.inputEpoch = 7;
  id.streamGeneration = 2;
  id.streamActive = true;
  return id;
}

// the product's acceptance step: catch-up accepted (ordinal) with no AU of it handed in its own call
void open_await(KickState& k, H264Encoder& enc, uint64_t ordinal) {
  k.OpenAwait(enc.instance_id(), 11, 7, 2, ordinal, 349, kT0);
}

std::ostringstream gLog;

void TestTickReadyAu() {
  auto* fake = new FakeAsyncEncoderMft();
  H264Encoder enc;
  expect(start(enc, fake), "[5] init");
  std::vector<H264AccessUnit> units;
  encode(enc, kSyntheticReasonCatchup, 349, 3333333, &units);
  KickState k;
  open_await(k, enc, enc.accepted_input_count());
  fake->ReleaseHeld();
  Recorder rec;
  const auto r = catchup_await_tick(k, enc, identity_of(enc), kT0 + 10000, rec, gLog);
  expect(r.step == CatchupAwaitStep::Observed && k.awaitObservedByCollect == 1 && !k.await.active,
         "[5] the ready AU is observed via collect");
  expect(fake->processInputCalls == 1 && enc.accepted_input_count() == 1, "[5] no ProcessInput, accepted count unchanged");
  expect(rec.ordinals.size() == 1 && rec.ordinals[0] == 1 && r.handed == 1, "[5] emitted once, handed to the sender");
  enc.shutdown();
}

void TestTickMergedWithRealInput() {
  auto* fake = new FakeAsyncEncoderMft();
  fake->policy = FakeAsyncEncoderMft::Policy::OnNextInput;
  H264Encoder enc;
  expect(start(enc, fake), "[6] init");
  std::vector<H264AccessUnit> units;
  encode(enc, kSyntheticReasonCatchup, 349, 3333333, &units);
  KickState k;
  open_await(k, enc, enc.accepted_input_count());
  // a real frame arrives before the next tick: the product notes the acceptance and settles on what
  // its own call handed to the sender
  units.clear();
  encode(enc, 0, 350, 3666666, &units);
  k.NoteAwaitOtherInputAccepted();
  std::vector<CatchupHandedAu> handed;
  for (const auto& au : units) handed.push_back({au.acceptedInputOrdinal, au.inputEpoch, au.syntheticReason});
  const bool obs = catchup_await_observe_handed(k, enc.instance_id(), handed, "other", kT0 + 20000, gLog);
  expect(obs && k.awaitObservedByOther == 1 && !k.await.active && k.awaitOtherInputs == 1,
         "[6] the real input's call drained the catch-up AU -> observed via other");
  Recorder rec;
  const auto r = catchup_await_tick(k, enc, identity_of(enc), kT0 + 30000, rec, gLog);
  expect(r.step == CatchupAwaitStep::Idle && rec.ordinals.empty(), "[6] the next tick is idle: nothing emitted twice");
  expect(fake->processInputCalls == 2, "[6] only the catch-up and the real input went in (no extra input)");
  enc.shutdown();
}

void TestTickPausedAndInstanceSwap() {
  {
    auto* fake = new FakeAsyncEncoderMft();
    H264Encoder enc;
    expect(start(enc, fake), "[7] init");
    std::vector<H264AccessUnit> units;
    encode(enc, kSyntheticReasonCatchup, 349, 3333333, &units);
    KickState k;
    open_await(k, enc, 1);
    fake->ReleaseHeld();
    const int outBefore = fake->processOutputCalls;
    CatchupAwaitIdentity id = identity_of(enc);
    id.streamActive = false;
    Recorder rec;
    const auto r = catchup_await_tick(k, enc, id, kT0 + 10000, rec, gLog);
    expect(r.step == CatchupAwaitStep::Cancelled && k.awaitCancelled == 1 && !k.await.active && !k.lateWatch.active,
           "[7] paused -> cancelled, not watched");
    expect(rec.ordinals.empty() && fake->processOutputCalls == outBefore, "[7] no collect, no emit");
    enc.shutdown();
  }
  {
    auto* fake = new FakeAsyncEncoderMft();
    fake->AddRef();  // adopted again by the second initialize
    H264Encoder enc;
    expect(start(enc, fake), "[8] init");
    std::vector<H264AccessUnit> units;
    encode(enc, kSyntheticReasonCatchup, 349, 3333333, &units);
    KickState k;
    open_await(k, enc, 1);
    const uint64_t oldInst = enc.instance_id();
    enc.shutdown();
    expect(start(enc, fake), "[8] the encoder is rebuilt (new instance)");
    Recorder rec;
    const auto r = catchup_await_tick(k, enc, identity_of(enc), kT0 + 10000, rec, gLog);
    expect(enc.instance_id() != oldInst && r.step == CatchupAwaitStep::Cancelled && k.awaitCancelled == 1,
           "[8] codec instance swapped -> cancelled");
    expect(rec.ordinals.empty(), "[8] nothing emitted for the old instance");
    enc.shutdown();
    fake->Release();
  }
}

void TestTickDeadlineAndLateOutput() {
  auto* fake = new FakeAsyncEncoderMft();
  H264Encoder enc;
  expect(start(enc, fake), "[9] init");
  std::vector<H264AccessUnit> units;
  encode(enc, kSyntheticReasonCatchup, 349, 3333333, &units);
  KickState k;
  open_await(k, enc, 1);
  Recorder rec;
  auto r = catchup_await_tick(k, enc, identity_of(enc), kT0 + 16000, rec, gLog);
  expect(r.step == CatchupAwaitStep::NoOutput && k.await.active, "[9] held: a tick finds no output, the await stays");
  r = catchup_await_tick(k, enc, identity_of(enc), kT0 + KickState::kAwaitDeadlineUs - 1, rec, gLog);
  expect(r.step == CatchupAwaitStep::NoOutput, "[9] still open one microsecond before the deadline");
  r = catchup_await_tick(k, enc, identity_of(enc), kT0 + KickState::kAwaitDeadlineUs, rec, gLog);
  expect(r.step == CatchupAwaitStep::Deferred && k.awaitDeferred == 1 && k.lateWatch.active && rec.ordinals.empty(),
         "[9] deferred at the deadline, late watch on, nothing emitted, no input made");
  expect(fake->processInputCalls == 1 && k.await.collectAttempts == 0 && k.awaitCollectCalls == 3,
         "[9] three collects, no extra input (the await state is reset once ended)");
  // the AU comes out later inside an ordinary encode call: late output, not the await's success
  fake->ReleaseHeld();
  units.clear();
  encode(enc, 0, 360, 4000000, &units);
  std::vector<CatchupHandedAu> handed;
  for (const auto& au : units) handed.push_back({au.acceptedInputOrdinal, au.inputEpoch, au.syntheticReason});
  const bool obs = catchup_await_observe_handed(k, enc.instance_id(), handed, "other", kT0 + 2000000, gLog);
  expect(!obs && k.awaitLateOutputs == 1 && !k.lateWatch.active && k.awaitObservedByOther == 0,
         "[9] its later output is logged as late, never counted as observed");
  enc.shutdown();
}

void TestTickCollectErrorAndDroppedAu() {
  {
    auto* fake = new FakeAsyncEncoderMft();
    H264Encoder enc;
    expect(start(enc, fake), "[10] init");
    std::vector<H264AccessUnit> units;
    encode(enc, kSyntheticReasonCatchup, 349, 3333333, &units);
    KickState k;
    open_await(k, enc, 1);
    fake->ReleaseHeld();
    fake->failNextProcessOutput = true;
    Recorder rec;
    const auto r = catchup_await_tick(k, enc, identity_of(enc), kT0 + 10000, rec, gLog);
    expect(r.step == CatchupAwaitStep::CollectError && r.collectFailed && k.awaitCollectErrors == 1,
           "[10] a failed drain is a collect error (not no-output)");
    expect(rec.ordinals.empty() && k.awaitDeferred == 1 && !k.await.active && r.cs.drainErrors == 1,
           "[10] nothing sent, deferred; the caller runs the output-failure recovery");
    enc.shutdown();
  }
  {
    auto* fake = new FakeAsyncEncoderMft();
    H264Encoder enc;
    expect(start(enc, fake), "[11] init");
    std::vector<H264AccessUnit> units;
    encode(enc, kSyntheticReasonCatchup, 349, 3333333, &units);
    KickState k;
    open_await(k, enc, 1);
    fake->ReleaseHeld();
    Recorder rec;
    rec.verdict = 0;  // the emit stage dropped it
    auto r = catchup_await_tick(k, enc, identity_of(enc), kT0 + 10000, rec, gLog);
    expect(r.step == CatchupAwaitStep::Pending && k.await.active && rec.ordinals.size() == 1 && r.handed == 0,
           "[11] an AU the emit stage dropped does not satisfy the await (output-observed needs a handed AU)");
    r = catchup_await_tick(k, enc, identity_of(enc), kT0 + KickState::kAwaitDeadlineUs, rec, gLog);
    expect(r.step == CatchupAwaitStep::Deferred && k.awaitObservedByCollect == 0, "[11] ... and is deferred at the deadline");
    enc.shutdown();
  }
}

}  // namespace

int main() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION);
  TestReadyOutputIsCollectedWithoutAnInput();
  TestHeldUntilNextInputNeedsTheOneKick();
  TestNothingPendingIsANoOp();
  TestNotRunningFailsClosed();
  TestTickReadyAu();
  TestTickMergedWithRealInput();
  TestTickPausedAndInstanceSwap();
  TestTickDeadlineAndLateOutput();
  TestTickCollectErrorAndDroppedAu();
  MFShutdown();
  CoUninitialize();
  std::printf("host_catchup_collect_test: %s (%d checks, %d failed)\n", gFailures == 0 ? "PASS" : "FAIL", gChecks,
              gFailures);
  return gFailures == 0 ? 0 : 1;
}
