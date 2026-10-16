// encoder-output-poll r1 (Codex motion-settle r13 s3/s4): the async encode call's post-input event poll,
// at the real H264Encoder boundary, with an asynchronous fake MFT and a fake clock (set_clock_for_test --
// test seam only; GetEvent advances it, so the time budget is crossed deterministically, never by
// real Sleep timing).
//
// Field case (6M/60, r12): the first GetEvent(NO_WAIT) after the input returned NeedInput ~10 ms later,
// past the poll budget (40 % of 16.7 ms = 6.7 ms); the loop stopped and the HaveOutput right behind it
// waited for the next encode call. Expected from the candidate rule: the time budget stops EMPTY-poll
// retries only; after a successful event the next event is read, bounded by asyncPollMax (4) events.
//   [1] N at +10 ms, then H (this input's AU)  -> the AU comes out in the SAME call, no extra input
//   [2] N at +10 ms every read                  -> stops at asyncPollMax (4) reads: bounded, no spin
//   [3] N at +10 ms, then NO_EVENTS            -> stops at the empty read (budget passed): 2 reads
//   [4] N, then GetEvent E_FAIL                 -> the call fails with E_FAIL recorded (not "nothing yet")
//   [5] N, then S_OK with no event              -> the call fails (null event)
//   [6] N, then an event whose GetType fails    -> the call fails (GetType), HRESULT recorded
//   [7] H within the budget                     -> drained as before (unchanged behaviour)
// Every case: accepted_input_count == inputs given, no AU ordinal produced twice, no new ordinal.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>

#include <cstdio>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "host_fake_encoder_mft.hpp"
#include "mf_h264_codec.hpp"

using remote60::native_poc::H264AccessUnit;
using remote60::native_poc::H264EncodeFrameStats;
using remote60::native_poc::H264Encoder;
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

uint64_t gNow = 1000000000;
uint64_t fake_clock() { return gNow; }

// An event that forwards everything to a real MF event except GetType, which fails ([6]).
class BadTypeEvent : public IMFMediaEvent {
 public:
  explicit BadTypeEvent(IMFMediaEvent* inner) : inner_(inner) {}
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFAttributes) || riid == __uuidof(IMFMediaEvent)) {
      *ppv = static_cast<IMFMediaEvent*>(this);
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
  STDMETHODIMP GetType(MediaEventType* t) override {
    if (t) *t = MEUnknown;
    return E_FAIL;
  }
  STDMETHODIMP GetExtendedType(GUID* g) override { return inner_->GetExtendedType(g); }
  STDMETHODIMP GetStatus(HRESULT* h) override { return inner_->GetStatus(h); }
  STDMETHODIMP GetValue(PROPVARIANT* v) override { return inner_->GetValue(v); }
  STDMETHODIMP GetItem(REFGUID k, PROPVARIANT* v) override { return inner_->GetItem(k, v); }
  STDMETHODIMP GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t) override { return inner_->GetItemType(k, t); }
  STDMETHODIMP CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) override { return inner_->CompareItem(k, v, r); }
  STDMETHODIMP Compare(IMFAttributes* a, MF_ATTRIBUTES_MATCH_TYPE m, BOOL* r) override { return inner_->Compare(a, m, r); }
  STDMETHODIMP GetUINT32(REFGUID k, UINT32* v) override { return inner_->GetUINT32(k, v); }
  STDMETHODIMP GetUINT64(REFGUID k, UINT64* v) override { return inner_->GetUINT64(k, v); }
  STDMETHODIMP GetDouble(REFGUID k, double* v) override { return inner_->GetDouble(k, v); }
  STDMETHODIMP GetGUID(REFGUID k, GUID* v) override { return inner_->GetGUID(k, v); }
  STDMETHODIMP GetStringLength(REFGUID k, UINT32* l) override { return inner_->GetStringLength(k, l); }
  STDMETHODIMP GetString(REFGUID k, LPWSTR s, UINT32 n, UINT32* l) override { return inner_->GetString(k, s, n, l); }
  STDMETHODIMP GetAllocatedString(REFGUID k, LPWSTR* s, UINT32* l) override { return inner_->GetAllocatedString(k, s, l); }
  STDMETHODIMP GetBlobSize(REFGUID k, UINT32* n) override { return inner_->GetBlobSize(k, n); }
  STDMETHODIMP GetBlob(REFGUID k, UINT8* b, UINT32 n, UINT32* l) override { return inner_->GetBlob(k, b, n, l); }
  STDMETHODIMP GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* n) override { return inner_->GetAllocatedBlob(k, b, n); }
  STDMETHODIMP GetUnknown(REFGUID k, REFIID r, LPVOID* p) override { return inner_->GetUnknown(k, r, p); }
  STDMETHODIMP SetItem(REFGUID k, REFPROPVARIANT v) override { return inner_->SetItem(k, v); }
  STDMETHODIMP DeleteItem(REFGUID k) override { return inner_->DeleteItem(k); }
  STDMETHODIMP DeleteAllItems() override { return inner_->DeleteAllItems(); }
  STDMETHODIMP SetUINT32(REFGUID k, UINT32 v) override { return inner_->SetUINT32(k, v); }
  STDMETHODIMP SetUINT64(REFGUID k, UINT64 v) override { return inner_->SetUINT64(k, v); }
  STDMETHODIMP SetDouble(REFGUID k, double v) override { return inner_->SetDouble(k, v); }
  STDMETHODIMP SetGUID(REFGUID k, REFGUID v) override { return inner_->SetGUID(k, v); }
  STDMETHODIMP SetString(REFGUID k, LPCWSTR v) override { return inner_->SetString(k, v); }
  STDMETHODIMP SetBlob(REFGUID k, const UINT8* b, UINT32 n) override { return inner_->SetBlob(k, b, n); }
  STDMETHODIMP SetUnknown(REFGUID k, IUnknown* u) override { return inner_->SetUnknown(k, u); }
  STDMETHODIMP LockStore() override { return inner_->LockStore(); }
  STDMETHODIMP UnlockStore() override { return inner_->UnlockStore(); }
  STDMETHODIMP GetCount(UINT32* n) override { return inner_->GetCount(n); }
  STDMETHODIMP GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v) override { return inner_->GetItemByIndex(i, k, v); }
  STDMETHODIMP CopyAllItems(IMFAttributes* d) override { return inner_->CopyAllItems(d); }

 private:
  virtual ~BadTypeEvent() = default;
  std::atomic<ULONG> refs_{1};
  Microsoft::WRL::ComPtr<IMFMediaEvent> inner_;
};

// The async fake: GetEvent answers from a script; each step can advance the fake clock first.
class ScriptedAsyncMft : public FakeEncoderMft, public IMFMediaEventGenerator {
 public:
  enum class Kind { NeedInput, HaveOutput, NoEvents, Fail, NullOk, BadType };
  struct Step {
    Kind kind;
    uint64_t advanceUs;
    HRESULT hr;
  };
  std::deque<Step> script;
  int processInputCalls = 0, processOutputCalls = 0, getEventCalls = 0;

  ScriptedAsyncMft() : FakeEncoderMft(L"VEN_1002", L"AMD H.264 Encoder MFT (scripted async)") {
    Microsoft::WRL::ComPtr<IMFAttributes> a;
    if (SUCCEEDED(FakeEncoderMft::GetAttributes(&a)) && a) a->SetUINT32(MF_TRANSFORM_ASYNC, 1);
  }
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
  STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** out) override {
    ++getEventCalls;
    if (!out) return E_POINTER;
    *out = nullptr;
    if (script.empty()) return (flags & MF_EVENT_FLAG_NO_WAIT) ? MF_E_NO_EVENTS_AVAILABLE : E_NOTIMPL;
    const Step st = script.front();
    script.pop_front();
    gNow += st.advanceUs;
    switch (st.kind) {
      case Kind::NoEvents: return MF_E_NO_EVENTS_AVAILABLE;
      case Kind::Fail: return st.hr;
      case Kind::NullOk: return S_OK;
      case Kind::NeedInput: return MFCreateMediaEvent(METransformNeedInput, GUID_NULL, S_OK, nullptr, out);
      case Kind::HaveOutput:
        if (held_ > 0) {
          --held_;
          ++ready_;
        }
        return MFCreateMediaEvent(METransformHaveOutput, GUID_NULL, S_OK, nullptr, out);
      case Kind::BadType: {
        Microsoft::WRL::ComPtr<IMFMediaEvent> real;
        const HRESULT hr = MFCreateMediaEvent(METransformNeedInput, GUID_NULL, S_OK, nullptr, &real);
        if (FAILED(hr)) return hr;
        *out = new BadTypeEvent(real.Get());
        return S_OK;
      }
    }
    return E_UNEXPECTED;
  }
  STDMETHODIMP BeginGetEvent(IMFAsyncCallback*, IUnknown*) override { return E_NOTIMPL; }
  STDMETHODIMP EndGetEvent(IMFAsyncResult*, IMFMediaEvent**) override { return E_NOTIMPL; }
  STDMETHODIMP QueueEvent(MediaEventType, REFGUID, HRESULT, const PROPVARIANT*) override { return E_NOTIMPL; }
  STDMETHODIMP ProcessInput(DWORD, IMFSample*, DWORD) override {
    ++processInputCalls;
    ++held_;
    return S_OK;
  }
  STDMETHODIMP ProcessOutput(DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER* odb, DWORD* status) override {
    ++processOutputCalls;
    if (status) *status = 0;
    if (ready_ == 0) return MF_E_TRANSFORM_NEED_MORE_INPUT;
    if (!odb || !odb->pSample) return E_POINTER;
    --ready_;
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
  ~ScriptedAsyncMft() = default;
  std::atomic<ULONG> refs_{1};
  int held_ = 0, ready_ = 0;
};

constexpr uint32_t kW = 320, kH = 192;
using K = ScriptedAsyncMft::Kind;

struct Run {
  ScriptedAsyncMft* fake = nullptr;
  H264Encoder enc;
  bool ok = false;
  std::vector<H264AccessUnit> units;
  H264EncodeFrameStats st{};
  int eventsBefore = 0;
};

// one encode call at 60 fps with the scripted post-input events
void call(Run& r, const char* tag, std::initializer_list<ScriptedAsyncMft::Step> steps) {
  r.fake = new ScriptedAsyncMft();
  r.enc.set_transform_for_test(r.fake);
  r.enc.set_clock_for_test(&fake_clock);
  expect(r.enc.initialize(kW, kH, 60, 6000000, 600), std::string(tag) + " initialize (60 fps)");
  r.fake->script.clear();
  r.eventsBefore = r.fake->getEventCalls;
  for (const auto& s : steps) r.fake->script.push_back(s);
  std::vector<uint8_t> nv12(static_cast<size_t>(kW) * kH * 3 / 2, 90);
  r.ok = r.enc.encode_frame(nv12, false, 3333333, &r.units, &r.st);
}

int reads(const Run& r) { return r.fake->getEventCalls - r.eventsBefore; }

void common(const Run& r, const std::string& tag) {
  expect(r.enc.accepted_input_count() == 1 && r.fake->processInputCalls == 1, tag + " exactly one input, no extra");
  std::set<uint64_t> seen;
  bool dup = false;
  for (const auto& u : r.units) {
    dup |= !seen.insert(u.acceptedInputOrdinal).second;
    expect(u.acceptedInputOrdinal == 1, tag + " AU ordinal is the input's (no new ordinal)");
  }
  expect(!dup, tag + " no AU produced twice");
}

}  // namespace

int main() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION);
  {
    Run r;
    call(r, "[1]", {{K::NeedInput, 10000, S_OK}, {K::HaveOutput, 0, S_OK}});
    expect(r.ok && r.units.size() == 1, "[1] N at +10 ms (past the 6.7 ms budget) then H: the AU comes out in the SAME call");
    expect(reads(r) >= 2, "[1] the HaveOutput behind the late NeedInput was read");
    common(r, "[1]");
    r.enc.shutdown();
  }
  {
    Run r;
    call(r, "[2]", {{K::NeedInput, 10000, S_OK}, {K::NeedInput, 10000, S_OK}, {K::NeedInput, 10000, S_OK},
                    {K::NeedInput, 10000, S_OK}, {K::NeedInput, 10000, S_OK}, {K::NeedInput, 10000, S_OK}});
    expect(r.ok && r.units.empty(), "[2] only NeedInputs: no AU, the call succeeds");
    expect(reads(r) == 4, "[2] bounded by asyncPollMax: exactly 4 event reads (no spin, no drain-to-empty)");
    common(r, "[2]");
    r.enc.shutdown();
  }
  {
    Run r;
    call(r, "[3]", {{K::NeedInput, 10000, S_OK}, {K::NoEvents, 0, S_OK}, {K::HaveOutput, 0, S_OK}});
    expect(r.ok && r.units.empty(), "[3] N then NO_EVENTS: no AU, success");
    expect(reads(r) == 2, "[3] the empty read past the budget ends the poll (2 reads, the later H not reached)");
    common(r, "[3]");
    r.enc.shutdown();
  }
  {
    Run r;
    call(r, "[4]", {{K::NeedInput, 10000, S_OK}, {K::Fail, 0, E_FAIL}});
    expect(!r.ok && r.st.asyncEventFailure == 1 && r.st.asyncEventFailHr == static_cast<int32_t>(E_FAIL),
           "[4] GetEvent E_FAIL: the call fails with the original HRESULT (not 'nothing yet')");
    common(r, "[4]");
    r.enc.shutdown();
  }
  {
    Run r;
    call(r, "[5]", {{K::NeedInput, 10000, S_OK}, {K::NullOk, 0, S_OK}});
    expect(!r.ok && r.st.asyncEventFailure == 2, "[5] GetEvent S_OK with no event: the call fails (null event)");
    common(r, "[5]");
    r.enc.shutdown();
  }
  {
    Run r;
    call(r, "[6]", {{K::NeedInput, 10000, S_OK}, {K::BadType, 0, S_OK}});
    expect(!r.ok && r.st.asyncEventFailure == 3 && r.st.asyncEventFailHr == static_cast<int32_t>(E_FAIL),
           "[6] the event's GetType fails: the call fails with that HRESULT");
    common(r, "[6]");
    r.enc.shutdown();
  }
  {
    Run r;
    call(r, "[7]", {{K::HaveOutput, 1000, S_OK}});
    expect(r.ok && r.units.size() == 1, "[7] H within the budget: drained in the same call (unchanged)");
    common(r, "[7]");
    r.enc.shutdown();
  }
  MFShutdown();
  CoUninitialize();
  std::printf("host_encoder_poll_test: %s (%d checks, %d failed)\n", gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
