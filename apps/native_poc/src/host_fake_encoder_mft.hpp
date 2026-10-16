#pragma once

// A fake H.264 encoder MFT for the rate-control seam test (encoder-rc-budget C5).
//
// Role:    stands in for a vendor transform through H264Encoder::set_transform_for_test, so a test
//          can see the ORDER of what the product does to a new instance (ICodecAPI SetValue before or
//          after SetInputType/SetOutputType), WHICH values it sets, how many times, and what it does
//          to a running instance -- things a real MFT cannot be made to report deterministically. It
//          encodes nothing: the test never calls ProcessInput. Its attribute store carries the vendor
//          id the product reads its identity from (MFT_ENUM_HARDWARE_VENDOR_ID_Attribute), so the
//          NVIDIA and legacy policies are selected the way they are for a real activate, by vendor id.
// Readback: GetValue returns what was last set, or a configured "stuck" value per key to model the
//          NVIDIA post-types behaviour (S_OK on SetValue, readback unchanged).
// Thread:  test only, single-threaded.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <codecapi.h>
#include <icodecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace remote60::native_poc::testing {

inline bool guid_eq(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

inline std::string rc_key_name(const GUID& key) {
  if (guid_eq(key, CODECAPI_AVEncCommonRateControlMode)) return "mode";
  if (guid_eq(key, CODECAPI_AVEncCommonMeanBitRate)) return "mean";
  if (guid_eq(key, CODECAPI_AVEncCommonMaxBitRate)) return "peak";
  if (guid_eq(key, CODECAPI_AVEncCommonBufferSize)) return "vbv";
  if (guid_eq(key, CODECAPI_AVEncVideoMaxQP)) return "maxQp";
  if (guid_eq(key, CODECAPI_AVEncMPVGOPSize)) return "gop";
  if (guid_eq(key, CODECAPI_AVEncCommonQualityVsSpeed)) return "qvs";
  if (guid_eq(key, CODECAPI_AVLowLatencyMode)) return "lowLatency";
  if (guid_eq(key, CODECAPI_AVEncCommonLowLatency)) return "commonLowLatency";
  if (guid_eq(key, CODECAPI_AVEncCommonRealTime)) return "realTime";
  if (guid_eq(key, CODECAPI_AVEncMPVDefaultBPictureCount)) return "bframes";
  if (guid_eq(key, CODECAPI_AVEncMPVGOPOpen)) return "gopOpen";
  if (guid_eq(key, CODECAPI_AVEncVideoForceKeyFrame)) return "forceKey";
  return "other";
}

struct GuidLess {
  bool operator()(const GUID& a, const GUID& b) const { return std::memcmp(&a, &b, sizeof(GUID)) < 0; }
};

class FakeEncoderMft : public IMFTransform, public ICodecAPI {
 public:
  // One recorded call: "SetValue:mean=1500000", "SetInputType", "SetOutputType", "ProcessMessage:<n>".
  std::vector<std::string> calls;
  // Values GetValue reports regardless of what was set (models "accepted, not applied").
  std::map<GUID, uint32_t, GuidLess> stuck;
  // Keys whose SetValue should fail (models an unsupported property).
  std::map<GUID, HRESULT, GuidLess> rejectWith;
  // GetValue fails for every key (models an MFT that accepts Sets but reports nothing: "unknown").
  bool readbackUnsupported = false;
  // Keys whose first N SetValues fail, then succeed (models a property refused before some state).
  std::map<GUID, int, GuidLess> rejectFirstN;
  // With stuckUntilSet, a `stuck` readback applies only until that key's SetValue first SUCCEEDS.
  bool stuckUntilSet = false;
  bool typesSet = false;

  explicit FakeEncoderMft(const wchar_t* vendorId, const wchar_t* friendlyName) {
    MFCreateAttributes(&attrs_, 4);
    if (attrs_) {
      if (vendorId) attrs_->SetString(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, vendorId);
      if (friendlyName) attrs_->SetString(MFT_FRIENDLY_NAME_Attribute, friendlyName);
    }
  }

  // What the product has set so far for a key (0 if never).
  uint32_t set_value(const GUID& key) const {
    auto it = values_.find(key);
    return it == values_.end() ? 0u : it->second;
  }
  size_t count_set(const std::string& name) const {
    size_t n = 0;
    for (const auto& c : calls)
      if (c.rfind("SetValue:" + name + "=", 0) == 0) ++n;
    return n;
  }
  // Index of the first call matching prefix, or -1.
  int first_index(const std::string& prefix) const {
    for (size_t i = 0; i < calls.size(); ++i)
      if (calls[i].rfind(prefix, 0) == 0) return static_cast<int>(i);
    return -1;
  }
  int last_index(const std::string& prefix) const {
    for (size_t i = calls.size(); i-- > 0;)
      if (calls[i].rfind(prefix, 0) == 0) return static_cast<int>(i);
    return -1;
  }
  void clear_calls() { calls.clear(); }

  // ---- IUnknown -------------------------------------------------------------------------------
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFTransform)) *ppv = static_cast<IMFTransform*>(this);
    else if (riid == __uuidof(ICodecAPI)) *ppv = static_cast<ICodecAPI*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }

  // ---- ICodecAPI ------------------------------------------------------------------------------
  STDMETHODIMP IsSupported(const GUID* key) override {
    if (!key) return E_POINTER;
    const std::string n = rc_key_name(*key);
    return (n == "other") ? S_FALSE : S_OK;
  }
  STDMETHODIMP IsModifiable(const GUID* key) override { return IsSupported(key); }
  STDMETHODIMP GetParameterRange(const GUID*, VARIANT*, VARIANT*, VARIANT*) override { return E_NOTIMPL; }
  STDMETHODIMP GetParameterValues(const GUID*, VARIANT**, ULONG*) override { return E_NOTIMPL; }
  STDMETHODIMP GetDefaultValue(const GUID*, VARIANT*) override { return E_NOTIMPL; }
  STDMETHODIMP GetValue(const GUID* key, VARIANT* value) override {
    if (!key || !value) return E_POINTER;
    calls.push_back("GetValue:" + rc_key_name(*key));
    if (readbackUnsupported) return E_NOTIMPL;
    auto st = stuck.find(*key);
    auto it = values_.find(*key);
    if (st == stuck.end() && it == values_.end()) return E_FAIL;  // never set, nothing stuck: "unknown"
    VariantInit(value);
    value->vt = VT_UI4;
    value->ulVal = st != stuck.end() ? st->second : it->second;
    return S_OK;
  }
  STDMETHODIMP SetValue(const GUID* key, VARIANT* value) override {
    if (!key || !value) return E_POINTER;
    const std::string n = rc_key_name(*key);
    uint32_t v = 0;
    if (value->vt == VT_UI4) v = value->ulVal;
    else if (value->vt == VT_I4) v = static_cast<uint32_t>(value->lVal);
    else if (value->vt == VT_BOOL) v = value->boolVal ? 1u : 0u;
    calls.push_back("SetValue:" + n + "=" + std::to_string(v) + (typesSet ? ":post-types" : ":pre-types"));
    auto rj = rejectWith.find(*key);
    if (rj != rejectWith.end()) return rj->second;
    auto rf = rejectFirstN.find(*key);
    if (rf != rejectFirstN.end() && rf->second > 0) { --rf->second; return E_FAIL; }
    values_[*key] = v;
    if (stuckUntilSet) stuck.erase(*key);
    return S_OK;
  }
  STDMETHODIMP RegisterForEvent(const GUID*, LONG_PTR) override { return E_NOTIMPL; }
  STDMETHODIMP UnregisterForEvent(const GUID*) override { return E_NOTIMPL; }
  STDMETHODIMP SetAllDefaults() override { return E_NOTIMPL; }
  STDMETHODIMP SetValueWithNotify(const GUID*, VARIANT*, GUID**, ULONG*) override { return E_NOTIMPL; }
  STDMETHODIMP SetAllDefaultsWithNotify(GUID**, ULONG*) override { return E_NOTIMPL; }
  STDMETHODIMP GetAllSettings(IStream*) override { return E_NOTIMPL; }
  STDMETHODIMP SetAllSettings(IStream*) override { return E_NOTIMPL; }
  STDMETHODIMP SetAllSettingsWithNotify(IStream*, GUID**, ULONG*) override { return E_NOTIMPL; }

  // ---- IMFTransform ---------------------------------------------------------------------------
  STDMETHODIMP GetStreamLimits(DWORD* a, DWORD* b, DWORD* c, DWORD* d) override {
    if (a) *a = 1; if (b) *b = 1; if (c) *c = 1; if (d) *d = 1;
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
    info->cbSize = 1 << 20;
    return S_OK;
  }
  STDMETHODIMP GetAttributes(IMFAttributes** out) override {
    if (!out) return E_POINTER;
    if (!attrs_) return E_NOTIMPL;
    *out = attrs_.Get();
    (*out)->AddRef();
    return S_OK;
  }
  STDMETHODIMP GetInputStreamAttributes(DWORD, IMFAttributes**) override { return E_NOTIMPL; }
  STDMETHODIMP GetOutputStreamAttributes(DWORD, IMFAttributes**) override { return E_NOTIMPL; }
  STDMETHODIMP DeleteInputStream(DWORD) override { return E_NOTIMPL; }
  STDMETHODIMP AddInputStreams(DWORD, DWORD*) override { return E_NOTIMPL; }
  STDMETHODIMP GetInputAvailableType(DWORD, DWORD, IMFMediaType**) override { return MF_E_NO_MORE_TYPES; }
  STDMETHODIMP GetOutputAvailableType(DWORD, DWORD, IMFMediaType**) override { return MF_E_NO_MORE_TYPES; }
  STDMETHODIMP SetInputType(DWORD, IMFMediaType* type, DWORD flags) override {
    if (flags & MFT_SET_TYPE_TEST_ONLY) return S_OK;
    calls.push_back("SetInputType");
    inType_ = type;
    typesSet = true;
    return S_OK;
  }
  STDMETHODIMP SetOutputType(DWORD, IMFMediaType* type, DWORD flags) override {
    if (flags & MFT_SET_TYPE_TEST_ONLY) return S_OK;
    calls.push_back("SetOutputType");
    outType_ = type;
    typesSet = true;
    return S_OK;
  }
  STDMETHODIMP GetInputCurrentType(DWORD, IMFMediaType** t) override {
    if (!t) return E_POINTER;
    if (!inType_) return MF_E_TRANSFORM_TYPE_NOT_SET;
    *t = inType_.Get(); (*t)->AddRef();
    return S_OK;
  }
  STDMETHODIMP GetOutputCurrentType(DWORD, IMFMediaType** t) override {
    if (!t) return E_POINTER;
    if (!outType_) return MF_E_TRANSFORM_TYPE_NOT_SET;
    *t = outType_.Get(); (*t)->AddRef();
    return S_OK;
  }
  STDMETHODIMP GetInputStatus(DWORD, DWORD* f) override { if (f) *f = MFT_INPUT_STATUS_ACCEPT_DATA; return S_OK; }
  STDMETHODIMP GetOutputStatus(DWORD* f) override { if (f) *f = 0; return S_OK; }
  STDMETHODIMP SetOutputBounds(LONGLONG, LONGLONG) override { return E_NOTIMPL; }
  STDMETHODIMP ProcessEvent(DWORD, IMFMediaEvent*) override { return E_NOTIMPL; }
  STDMETHODIMP ProcessMessage(MFT_MESSAGE_TYPE msg, ULONG_PTR) override {
    calls.push_back("ProcessMessage:" + std::to_string(static_cast<unsigned>(msg)));
    if (msg == MFT_MESSAGE_NOTIFY_START_OF_STREAM) started_ = true;
    if (msg == MFT_MESSAGE_NOTIFY_END_OF_STREAM) {
      // The product's shutdown(): the same fake may be adopted again by the next initialize(), and
      // that is a NEW instance -- its SetValues before SetInputType are pre-types again.
      started_ = false;
      typesSet = false;
      inType_.Reset();
      outType_.Reset();
      values_.clear();
    }
    return S_OK;
  }
  STDMETHODIMP ProcessInput(DWORD, IMFSample*, DWORD) override { return E_NOTIMPL; }
  STDMETHODIMP ProcessOutput(DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER*, DWORD*) override { return E_NOTIMPL; }

  bool started() const { return started_; }

 protected:
  // encoder-output-poll r1: protected so an async variant (host_encoder_poll_test.cpp) can derive and own
  // its lifetime
  ~FakeEncoderMft() = default;

 private:
  std::atomic<ULONG> refs_{1};
  Microsoft::WRL::ComPtr<IMFAttributes> attrs_;
  Microsoft::WRL::ComPtr<IMFMediaType> inType_, outType_;
  std::map<GUID, uint32_t, GuidLess> values_;
  bool started_ = false;
};

}  // namespace remote60::native_poc::testing
