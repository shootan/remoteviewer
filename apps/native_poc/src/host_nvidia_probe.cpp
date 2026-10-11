// NVIDIA rate-control probe v2 (encoder-bakeoff, task t-m9nlnthi) -- MEASUREMENT-ONLY, hand to the
// NVIDIA machine. Product code and defaults are unchanged; the product H264Encoder runs as shipped.
//
// v1 showed NVIDIA reading back MaxQP=51 and peak=vbv=15552000 whatever was requested, and 4 rows
// failing encode. v2 records WHAT the MFT was told and WHAT it said, per property and per moment:
//   * A logging proxy (IMFTransform + ICodecAPI + IMFMediaEventGenerator) is handed to the product
//     encoder through its existing test seam H264Encoder::set_transform_for_test. Every SetValue the
//     PRODUCT issues is logged with key / VARIANT type / value / raw HRESULT and the phase it fell in.
//     The inner transform is enumerated exactly as the product's "mft_hw" path does
//     (MFTEnumEx ENCODER, SYNCMFT|HARDWARE|SORTANDFILTER, NV12->H264, first activatable).
//   * Readback snapshots (GetValue HR / VT / value, + MF_LOW_LATENCY attribute) at: caps (fresh MFT,
//     also IsSupported/IsModifiable/GetDefaultValue/GetParameterRange), pre-types, post-types (before
//     the hints), stream-start (after hints + rate control), after the first output frame.
//   * encode-false: the proxy keeps the abnormal HRESULTs of ProcessInput / ProcessOutput / async
//     events (call, hr, count, frame) so a failed row says WHERE it failed; missing body frames are
//     split into body vs tail (last 8 frames).
//   * Tolerated vs abnormal: an ASYNC MFT answers ProcessOutput with E_UNEXPECTED when no
//     METransformHaveOutput is pending. The product's drain loop ends on exactly that code
//     (mf_h264_codec.cpp drain_outputs: asyncTransform_ && po == E_UNEXPECTED -> break), so on AMD
//     every successful drain ends with one E_UNEXPECTED. The proxy counts it as "tolerated", not
//     abnormal (AMD smoke 2026-10-11 first flagged it as abnormal and looked like a proxy defect).
//   * product-stats: every row sums the product's own H264EncodeFrameStats (direct counters only; no
//     derived E_UNEXPECTED count -- the product keeps none).
// Since r2 every row runs through the proxy (proxy=1); the r1 direct-vs-proxy neutrality check
// (baseline-direct == baseline-proxy on AMD and on the Home NVIDIA) was done with the r1 binary and is
// NOT repeated by this binary. Proxy path vs the product path (Codex 17519d8 item 1): every row pins
// REMOTE60_NATIVE_ENCODER_BACKEND=mft_hw (the product's own enumeration logs backend=mft_enum_hw), so
// NO row takes the AMF-specific configure_types branch (strcmp
// "amf_mft_h264enc"); that branch only differs from an Auto / explicit-AMF product setting, which this
// probe does not measure. The seam sets backendName_="test_transform" and usingHardware_=false; no
// codec-configuration use of usingHardware_ was found in this CPU-NV12 path. MF attribute writes
// (GetAttributes()->SetUINT32, e.g. MF_LOW_LATENCY=1) go to the inner object directly and are NOT in
// the proxy's set[] log -- the snapshots read the attribute back instead.
//
// r3 (Codex 17519d8 F1..F3):
//   F1  the proxy keeps a bounded raw-call history (call / hr / frame / phase, newest last) and, on
//       encode-false, LATCHES it together with the latest abnormal HRESULT and the last ProcessInput
//       HRESULT BEFORE the product's shutdown() runs (shutdown-time errors cannot mask the cause).
//       MF_E_NOTACCEPTING: the first one in an encode call is tolerated (the product drains and
//       retries once); a second one in the same call = the retry failed = abnormal
//       (ProcessInput-retry). E_UNEXPECTED from ProcessOutput on an async MFT is reported as
//       "product-tolerated E_UNEXPECTED" (the product's drain loop ends on it); no derived count.
//       GetEvent's own GetType/GetStatus failures and GetOutputStreamInfo failures are abnormal.
//       A failing encode call with no MFT call recorded for that frame = unknown-before-MFT (the
//       failure happened before the transform, e.g. sample creation) -- a collection boundary.
//   F2  snapshot moments, in product order: caps (fresh MFT) / pre-types (just before the first real
//       SetType; in RC_BEFORE_TYPES rows the pre RC is already in) / types-applied (both types
//       accepted, before the product writes MF_LOW_LATENCY) / pre-first-set (first ICodecAPI
//       SetValue after the types; the MF_LOW_LATENCY attribute is already written by then) /
//       post-hints / before-begin-streaming / after-start-of-stream (START_OF_STREAM returned) /
//       first-output-sample (first S_OK ProcessOutput with a sample) / after-governor /
//       after-retarget. Every Get is logged with its HRESULT: "Key=UI4:n" means hr==S_OK exactly,
//       "Key=UI4:n(hr=0x...)" a non-S_OK success, "Key=!0x..." a failure. The product's own
//       readback GetValue calls are logged as get[phase].
//   F3  RESULT is split: exec (encode calls) / coverage (unique body frames returned, missing,
//       duplicates; an 8-frame bounded drain after the body recovers in-flight tail output, what is
//       still missing is tail-incomplete) / config (after START_OF_STREAM: each rate-control key the
//       product requested vs its readback -> applied | not-applied{...} | not-requested{...};
//       a mismatch is a FINDING, not an encoder failure) / events (abnormal HRESULT count).
//       VALID = exec ok AND no missing body frame after the drain AND no abnormal AND fps ok.
//       Retarget rows log the boundary (first input after the change) and how many AUs returned
//       after it belong to pre-change inputs (attributed to "before" by PTS). R*-sham rows re-apply
//       the SAME values at the same moment (same call pattern, no change) as the control; the
//       unchanged anchors also print both halves. GOP is 4 s and the body 5 s, so a periodic IDR can
//       land only in the second half -- the RESULT line lists the IDR body frames.
//       main() exits 0 only when every row is VALID (exit 2 otherwise); DONE is not a verdict.
// r4 (Codex follow-up, "minimal additions"):
//   1. running attribution (API-level, not single-property): R1-target-only calls reconfigure_bitrate
//      (the product function re-applies Mode+Mean+Peak+VBV with the QP request unchanged -- P0 omits
//      the MaxQP Set) and R1-qp-only calls set_max_qp(40) (the same full RC re-apply; only the QP
//      request changes), next to R1 (both) and R1-sham (same values); R2-target-only on motion. Which
//      product CALL moved the output is read from these rows only; a pure CODECAPI Mean-only cause is
//      not proven by them.
//   2. re-initialisation contrast, same 1920x1080@30 configuration: RE1 = P0 plan, then at the
//      midpoint H264Encoder::shutdown() + initialize() (initialize resets maxQpOverride_,
//      mf_h264_codec.cpp:1656) on a NEW MFT instance with the SAME env, i.e. the init-API order +
//      plan re-feed only. The second instance's own snapshots show whether the chosen RC values come
//      back. RE2 = the same re-init followed by a LATE set_max_qp(32): the partial-fix risk of leaving
//      configured 0 + governor base 32 (host_stage_stats_h264.cpp:205-207 re-applies the governor
//      ceiling when override==0 && governor!=configured; with today's shipped default 32/32 that
//      condition is not met, and the real fps path :215 applies after.maxQp, not always 32).
//      NOT represented: Host ApplyTarget, an actual fps/size change, timeline/epoch, pending-surface
//      cleanup, RequestKey, wire budget, capture restart, receiver recovery. Those are closed in the
//      product integration tests, not here.
//      Before the new activation the old proxy AND the encoder's own testTransform_ reference are
//      released (set_transform_for_test(nullptr)), so the old inner MFT is gone before the new one is
//      activated -- the same lifetime as the product's non-test path (Codex b4cc57c D2).
//      rebuild-boundary-loss is an estimate: it assumes sequential input and intact FIFO provenance
//      (missing frames among the last pendingAtRebuild pre-rebuild inputs); the lost frame stays in
//      the byte-average denominator. VALID on an RE row is therefore not "lossless".
//   D1 (Codex b4cc57c): coverage dup==0 is part of VALID; --selftest-dup injects one duplicate AU into
//      the first row to show INVALID + exit 2 deterministically.
//   267msPeakWall collects AUs returned during the body only (drain-recovered AUs are in the PTS
//   windows but not in the wall-clock window). VALID = exec/coverage/events gate; it does not mean
//   low latency, quality, or lossless.
//   3. P6 is now buffer 100 ms (18750 B at 1.5M) instead of 1 s: the small-buffer direction.
//      Rejection / clamp is recorded as requested vs readback (config line).
//   4. labels say "Set omitted (fresh MFT)" for maxQp=0 and "explicit 51" for 51; every row prints a
//      delivery line (outFps / coverage / IDR count / PSNR matched) so a byte drop can be checked
//      against missing or delayed output. PSNR is not a legibility proxy.
// Rewrites (buffer size, LowLatency explicit-off) are done in the proxy and are logged as REWRITE /
// INJECT, never silently.
//
// r2 (Home PC v2 result, .claude/verify_nvidia_probe_v2_homepc.md): on NVIDIA a SetValue AFTER the
// media types returns S_OK but is not applied (only Mean sticks); BEFORE the types everything is
// applied, and then MaxQP=32 over-produces like AMD. r2 therefore runs every row with the rate
// control fed pre-types (REMOTE60_NATIVE_RC_BEFORE_TYPES=1, the product's own switch at
// mf_h264_codec.cpp:1697 -- the product then applies it again post-types, both logged) and varies:
//   P0 maxQp=0 (REMOTE60_NATIVE_MAX_QP=0: the product SKIPS the MaxQP SetValue, so a readback of 51
//      there means "never set", not "set to 51") / P1 51 / P2 44 / P3 40 / P3b 32 (r1's row)
//   P4 CBR maxQp=0 / P5 buffer 500ms / P6 buffer 1s (proxy REWRITE of the product's value)
//   R1 maxQp=0 then at the body midpoint the product's own runtime path: set_max_qp(40)
//      (apply_rate_control "governor") then reconfigure_bitrate(1.18M) ("reconfigure": Mean 1.18M,
//      peak 2.36M, buffer 29500, MaxQP 40, product set order) -> after-governor / after-retarget
//      snapshots say whether a RUNNING change is applied once the pre-types RC is in force.
//   R2 the same on motion 3M -> 2.36M.
// PSNR (luma, exact PTS, SW decoder mft_sw, last-24-frame source ring; unmatched/decode failures
// are counted, never scored) is printed per row and before/after the midpoint on R rows.
//
// Run on the target PC:   host_nvidia_probe.exe > nvidia_probe_v2_r4_result.txt   (send back the file)
// Options: --repeat N  (repeat the non-anchor rows N times; default 1, ~2 min at 1)  --no-psnr

#include "bakeoff_common.hpp"

#include <codecapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <strmif.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace bakeoff;

namespace {

const char* kRcEnv[] = {
    "REMOTE60_NATIVE_ENCODER_BACKEND", "REMOTE60_NATIVE_RATE_CONTROL", "REMOTE60_NATIVE_RC_MODE_NUM",
    "REMOTE60_NATIVE_RC_MEAN_OVERRIDE", "REMOTE60_NATIVE_PEAK_BITRATE_PERCENT", "REMOTE60_NATIVE_MAX_QP",
    "REMOTE60_NATIVE_QUALITY_VS_SPEED", "REMOTE60_NATIVE_ENCODER_TUNE_MODE", "REMOTE60_NATIVE_RC_BEFORE_TYPES",
    "REMOTE60_NATIVE_LOW_LATENCY", "REMOTE60_NATIVE_REALTIME"};

std::string env_get(const char* n) { const char* v = std::getenv(n); return v ? v : ""; }

struct Key { const char* name; GUID guid; };
const Key kKeys[] = {
    {"RCMode", CODECAPI_AVEncCommonRateControlMode},
    {"Mean", CODECAPI_AVEncCommonMeanBitRate},
    {"MaxBR", CODECAPI_AVEncCommonMaxBitRate},
    {"Buffer", CODECAPI_AVEncCommonBufferSize},
    {"MaxQP", CODECAPI_AVEncVideoMaxQP},
    {"MinQP", CODECAPI_AVEncVideoMinQP},
    {"QvS", CODECAPI_AVEncCommonQualityVsSpeed},
    {"AVLowLatency", CODECAPI_AVLowLatencyMode},
    {"CommonLowLatency", CODECAPI_AVEncCommonLowLatency},
    {"RealTime", CODECAPI_AVEncCommonRealTime},
    {"GOP", CODECAPI_AVEncMPVGOPSize},
    {"BFrames", CODECAPI_AVEncMPVDefaultBPictureCount},
};

std::string key_name(const GUID* g) {
  if (!g) return "null";
  for (const auto& k : kKeys) if (IsEqualGUID(*g, k.guid)) return k.name;
  if (IsEqualGUID(*g, CODECAPI_AVEncVideoForceKeyFrame)) return "ForceKey";
  if (IsEqualGUID(*g, CODECAPI_AVEncVideoEncodeQP)) return "EncodeQP";
  if (IsEqualGUID(*g, CODECAPI_AVEncVideoEncodeFrameTypeQP)) return "FrameTypeQP";
  wchar_t buf[64] = {};
  StringFromGUID2(*g, buf, 64);
  char out[64] = {};
  WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
  return out;
}

std::string hr_str(HRESULT hr) {
  char b[16];
  std::snprintf(b, sizeof(b), "0x%08lX", static_cast<unsigned long>(hr));
  return b;
}

std::string var_str(const VARIANT* v) {
  if (!v) return "null";
  char b[64];
  switch (v->vt) {
    case VT_EMPTY: return "EMPTY";
    case VT_UI4: std::snprintf(b, sizeof(b), "UI4:%lu", static_cast<unsigned long>(v->ulVal)); return b;
    case VT_I4: std::snprintf(b, sizeof(b), "I4:%ld", static_cast<long>(v->lVal)); return b;
    case VT_UI8: std::snprintf(b, sizeof(b), "UI8:%llu", static_cast<unsigned long long>(v->ullVal)); return b;
    case VT_I8: std::snprintf(b, sizeof(b), "I8:%lld", static_cast<long long>(v->llVal)); return b;
    case VT_BOOL: return v->boolVal ? "BOOL:true" : "BOOL:false";
    case VT_UI2: std::snprintf(b, sizeof(b), "UI2:%u", v->uiVal); return b;
    default: std::snprintf(b, sizeof(b), "VT%u", v->vt); return b;
  }
}

// ---- per-row proxy configuration ----------------------------------------------------------------
struct ProxyCfg {
  uint32_t bufferOverrideBytes = 0;   // REWRITE the product's BufferSize value to this
  uint32_t bufferMultiplier = 0;      // REWRITE BufferSize to product value * N (x8 row)
  bool llExplicitOff = false;         // INJECT LowLatency/RealTime=false + MF_LOW_LATENCY=0 at the
                                      // product's hint position (product env omits the hints)
  bool printCaps = false;
};

struct AbnormalHr { std::string call; HRESULT hr; uint32_t count; uint64_t firstFrame; uint64_t lastFrame; };
struct RawCall { std::string call; HRESULT hr; uint64_t frame; std::string phase; };

// Get HRESULT rendering (F2): plain value = S_OK exactly; (hr=...) = other success; !hr = failure.
std::string get_str(HRESULT hr, const VARIANT* v) {
  if (FAILED(hr)) return "!" + hr_str(hr);
  if (hr == S_OK) return var_str(v);
  return var_str(v) + "(hr=" + hr_str(hr) + ")";
}

const GUID kRcKeys[] = {CODECAPI_AVEncCommonRateControlMode, CODECAPI_AVEncCommonMeanBitRate,
                        CODECAPI_AVEncCommonMaxBitRate, CODECAPI_AVEncCommonBufferSize, CODECAPI_AVEncVideoMaxQP};

class LoggingTransform : public IMFTransform, public ICodecAPI, public IMFMediaEventGenerator {
 public:
  LoggingTransform(IMFTransform* inner, const ProxyCfg& cfg) : inner_(inner), cfg_(cfg) {
    inner_->QueryInterface(IID_PPV_ARGS(&api_));
    inner_->QueryInterface(IID_PPV_ARGS(&gen_));
  }
  std::atomic<uint64_t> frame{0};  // probe-side frame index, for the raw-call log
  std::string phase = "pre-types";
  bool asyncMft = false;           // MF_TRANSFORM_ASYNC on the inner MFT (set by the row before init)

  // ---- snapshots (F2: every Get carries its HRESULT) --------------------------------------
  void snapshot(const char* label) {
    std::printf("  snap %-22s", label);
    if (!api_) { std::printf(" (no ICodecAPI)\n"); return; }
    for (const auto& k : kKeys) {
      VARIANT v; VariantInit(&v);
      const HRESULT hr = api_->GetValue(&k.guid, &v);
      std::printf(" %s=%s", k.name, get_str(hr, &v).c_str());
      VariantClear(&v);
    }
    Microsoft::WRL::ComPtr<IMFAttributes> attrs;
    UINT32 ll = 0;
    if (SUCCEEDED(inner_->GetAttributes(&attrs)) && attrs) {
      const HRESULT hr = attrs->GetUINT32(MF_LOW_LATENCY, &ll);
      if (hr == S_OK) std::printf(" MF_LOW_LATENCY=%u", ll);
      else if (SUCCEEDED(hr)) std::printf(" MF_LOW_LATENCY=%u(hr=%s)", ll, hr_str(hr).c_str());
      else std::printf(" MF_LOW_LATENCY=!%s", hr_str(hr).c_str());
    }
    std::printf("\n");
  }
  void caps() {
    if (!api_) { std::printf("  caps: no ICodecAPI\n"); return; }
    for (const auto& k : kKeys) {
      const HRESULT sup = api_->IsSupported(&k.guid);
      const HRESULT mod = api_->IsModifiable(&k.guid);
      VARIANT d; VariantInit(&d);
      const HRESULT dhr = api_->GetDefaultValue(&k.guid, &d);
      VARIANT mn, mx, st; VariantInit(&mn); VariantInit(&mx); VariantInit(&st);
      const HRESULT rhr = api_->GetParameterRange(&k.guid, &mn, &mx, &st);
      VARIANT cur; VariantInit(&cur);
      const HRESULT chr = api_->GetValue(&k.guid, &cur);
      const std::string rs = SUCCEEDED(rhr) ? var_str(&mn) + ".." + var_str(&mx) + " step " + var_str(&st) +
                                                  (rhr == S_OK ? "" : "(hr=" + hr_str(rhr) + ")")
                                            : "!" + hr_str(rhr);
      std::printf("  caps %-16s supported=%s modifiable=%s default=%s range=%s cur=%s\n", k.name,
                  hr_str(sup).c_str(), hr_str(mod).c_str(), get_str(dhr, &d).c_str(), rs.c_str(),
                  get_str(chr, &cur).c_str());
      VariantClear(&d); VariantClear(&mn); VariantClear(&mx); VariantClear(&st); VariantClear(&cur);
    }
  }

  // ---- F3 config applied? requested (post-rewrite) value per RC key vs readback NOW ---------
  std::string config_status() {
    if (!api_) return "n/a(no ICodecAPI)";
    std::string notApplied, notRequested;
    for (const GUID& g : kRcKeys) {
      const std::string name = key_name(&g);
      auto it = requested_.find(name);
      VARIANT v; VariantInit(&v);
      const HRESULT hr = api_->GetValue(&g, &v);
      if (it == requested_.end()) {
        notRequested += (notRequested.empty() ? "" : ",") + name;
      } else if (FAILED(hr) || v.vt != VT_UI4 || v.ulVal != it->second) {
        notApplied += (notApplied.empty() ? "" : ",") + name + " req=" + std::to_string(it->second) +
                      " rb=" + get_str(hr, &v);
      }
      VariantClear(&v);
    }
    std::string out = notApplied.empty() ? "applied" : "not-applied{" + notApplied + "}";
    if (!notRequested.empty()) out += " not-requested{" + notRequested + "}";
    return out;
  }
  std::string config_at_start() const { return configAtStart_.empty() ? "n/a(START_OF_STREAM not seen)" : configAtStart_; }

  // ---- F1 latch + dumps -------------------------------------------------------------------
  void latch_failure() {
    latch_.set = true;
    latch_.frame = frame.load();
    latch_.phase = phase;
    latch_.lastAbnormal = last_abnormal();
    latch_.lastProcessInputHr = lastProcessInputHr_;
    latch_.lastProcessInputFrame = lastProcessInputFrame_;
    latch_.history.assign(history_.begin(), history_.end());
  }
  void dump_latch() {
    if (!latch_.set) return;
    bool mftCallThisFrame = false;
    for (const auto& c : latch_.history) if (c.frame == latch_.frame) mftCallThisFrame = true;
    std::printf("  encode-false latch (before shutdown): frame=%llu phase=%s latestAbnormal=%s lastProcessInput=%s@frame%llu%s\n",
                static_cast<unsigned long long>(latch_.frame), latch_.phase.c_str(), latch_.lastAbnormal.c_str(),
                hr_str(latch_.lastProcessInputHr).c_str(), static_cast<unsigned long long>(latch_.lastProcessInputFrame),
                mftCallThisFrame ? "" : "  cause=unknown-before-MFT (no MFT call reached the proxy in the failing frame)");
    std::printf("  raw-call history (newest last, max %zu):", kHistory);
    for (const auto& c : latch_.history)
      std::printf(" %s=%s@%llu/%s", c.call.c_str(), hr_str(c.hr).c_str(), static_cast<unsigned long long>(c.frame), c.phase.c_str());
    std::printf("\n");
  }
  void dump_abnormal() {
    std::printf("  product-tolerated E_UNEXPECTED: ProcessOutput count=%u firstFrame=%llu (async drain-end; the product breaks its drain loop on it)\n",
                toleratedUnexpected_, static_cast<unsigned long long>(toleratedFirstFrame_));
    std::printf("  tolerated ProcessInput MF_E_NOTACCEPTING (first per call, product drains+retries): %u\n", piNotAccepting_);
    if (abnormal_.empty()) std::printf("  abnormal-hr: none\n");
    for (const auto& a : abnormal_)
      std::printf("  abnormal-hr: %s hr=%s count=%u firstFrame=%llu lastFrame=%llu\n", a.call.c_str(), hr_str(a.hr).c_str(),
                  a.count, static_cast<unsigned long long>(a.firstFrame), static_cast<unsigned long long>(a.lastFrame));
  }
  uint32_t abnormal_total() const { uint32_t n = 0; for (const auto& a : abnormal_) n += a.count; return n; }
  std::string last_abnormal() const { return lastAbnormal_.empty() ? "none" : lastAbnormal_; }  // latest occurrence
  void print_events() {
    std::printf("  async-events:");
    if (evCounts_.empty()) std::printf(" none");
    for (const auto& e : evCounts_) std::printf(" type%lu=%u", static_cast<unsigned long>(e.first), e.second);
    std::printf("  processInput ok=%u notAccepting(tolerated)=%u  processOutput ok=%u  forceKey ok=%u\n", piOk_,
                piNotAccepting_, poOk_, forceKeyOk_);
  }

  // ---- IUnknown ----------------------------------------------------------------------------
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFTransform)) *ppv = static_cast<IMFTransform*>(this);
    else if (riid == __uuidof(ICodecAPI) && api_) *ppv = static_cast<ICodecAPI*>(this);
    else if (riid == __uuidof(IMFMediaEventGenerator) && gen_) *ppv = static_cast<IMFMediaEventGenerator*>(this);
    else {
      const HRESULT hr = inner_->QueryInterface(riid, ppv);  // anything else: the inner object's own (not proxied)
      wchar_t b[64] = {};
      StringFromGUID2(riid, b, 64);
      std::printf("  proxy: forwarded QI %ls -> %s (calls through this interface are NOT logged)\n", b, hr_str(hr).c_str());
      return hr;
    }
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG r = --ref_;
    if (r == 0) delete this;
    return r;
  }

  // ---- ICodecAPI ---------------------------------------------------------------------------
  STDMETHODIMP IsSupported(const GUID* a) override { return api_->IsSupported(a); }
  STDMETHODIMP IsModifiable(const GUID* a) override { return api_->IsModifiable(a); }
  STDMETHODIMP GetParameterRange(const GUID* a, VARIANT* b, VARIANT* c, VARIANT* d) override { return api_->GetParameterRange(a, b, c, d); }
  STDMETHODIMP GetParameterValues(const GUID* a, VARIANT** b, ULONG* c) override { return api_->GetParameterValues(a, b, c); }
  STDMETHODIMP GetDefaultValue(const GUID* a, VARIANT* b) override { return api_->GetDefaultValue(a, b); }
  STDMETHODIMP GetValue(const GUID* key, VARIANT* value) override {
    const HRESULT hr = api_->GetValue(key, value);  // the product's own readback, logged raw
    std::printf("  get[%s] %s -> %s\n", phase.c_str(), key_name(key).c_str(), get_str(hr, value).c_str());
    return hr;
  }
  STDMETHODIMP SetValue(const GUID* key, VARIANT* value) override {
    if (typesSet_ && !preFirstSetSnapped_) {
      preFirstSetSnapped_ = true;
      snapshot("pre-first-set(attr-done)");  // the product wrote MF_LOW_LATENCY (attribute) before this
      if (cfg_.llExplicitOff) inject_ll_off();
    }
    const bool hintKey = key && (IsEqualGUID(*key, CODECAPI_AVLowLatencyMode) ||
                                 IsEqualGUID(*key, CODECAPI_AVEncCommonLowLatency) ||
                                 IsEqualGUID(*key, CODECAPI_AVEncCommonRealTime));
    if (hintKey && typesSet_) hintsSeen_ = true;
    else if (hintsSeen_ && !postHintsSnapped_) { postHintsSnapped_ = true; snapshot("post-hints"); }
    VARIANT use = *value;  // integer/bool variants only: a shallow copy owns nothing
    std::string note;
    if (key && IsEqualGUID(*key, CODECAPI_AVEncCommonBufferSize) && value->vt == VT_UI4) {
      if (cfg_.bufferOverrideBytes) {
        use.ulVal = cfg_.bufferOverrideBytes;
        note = " REWRITE(buffer)";
      } else if (cfg_.bufferMultiplier) {
        use.ulVal = value->ulVal * cfg_.bufferMultiplier;
        note = " REWRITE(buffer x" + std::to_string(cfg_.bufferMultiplier) + ")";
      }
    }
    const HRESULT hr = api_->SetValue(key, &use);
    const bool forceKey = key && IsEqualGUID(*key, CODECAPI_AVEncVideoForceKeyFrame);
    if (forceKey && SUCCEEDED(hr)) {
      ++forceKeyOk_;  // per-IDR request: counted, not logged line by line
    } else {
      if (forceKey) note_abnormal("SetValue(ForceKey)", hr);
      const std::string vs = note.empty() ? var_str(value) : var_str(value) + "=>" + var_str(&use);
      std::printf("  set[%s] %s %s%s -> %s\n", phase.c_str(), key_name(key).c_str(), vs.c_str(), note.c_str(),
                  hr_str(hr).c_str());
      if (key && use.vt == VT_UI4) {
        for (const GUID& g : kRcKeys) if (IsEqualGUID(*key, g)) requested_[key_name(key)] = use.ulVal;  // last request wins
      }
    }
    return hr;
  }
  STDMETHODIMP RegisterForEvent(const GUID* a, LONG_PTR b) override { return api_->RegisterForEvent(a, b); }
  STDMETHODIMP UnregisterForEvent(const GUID* a) override { return api_->UnregisterForEvent(a); }
  STDMETHODIMP SetAllDefaults() override { return api_->SetAllDefaults(); }
  STDMETHODIMP SetValueWithNotify(const GUID* a, VARIANT* b, GUID** c, ULONG* d) override { return api_->SetValueWithNotify(a, b, c, d); }
  STDMETHODIMP SetAllDefaultsWithNotify(GUID** a, ULONG* b) override { return api_->SetAllDefaultsWithNotify(a, b); }
  STDMETHODIMP GetAllSettings(IStream* a) override { return api_->GetAllSettings(a); }
  STDMETHODIMP SetAllSettings(IStream* a) override { return api_->SetAllSettings(a); }
  STDMETHODIMP SetAllSettingsWithNotify(IStream* a, GUID** b, ULONG* c) override { return api_->SetAllSettingsWithNotify(a, b, c); }

  // ---- IMFMediaEventGenerator --------------------------------------------------------------
  STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** ev) override {
    const HRESULT hr = gen_->GetEvent(flags, ev);
    if (SUCCEEDED(hr) && ev && *ev) {
      MediaEventType t = MEUnknown;
      HRESULT st = S_OK;
      const HRESULT thr = (*ev)->GetType(&t);
      const HRESULT shr = (*ev)->GetStatus(&st);
      if (FAILED(thr)) note_abnormal("GetEvent.GetType", thr);
      if (FAILED(shr)) note_abnormal("GetEvent.GetStatus", shr);
      ++evCounts_[t];
      record("GetEvent(" + std::to_string(t) + ")", st);
      if (FAILED(st)) note_abnormal("event" + std::to_string(t) + ".status", st);
    } else if (FAILED(hr) && hr != MF_E_NO_EVENTS_AVAILABLE) {
      record("GetEvent", hr);
      note_abnormal("GetEvent", hr);
    }
    return hr;
  }
  STDMETHODIMP BeginGetEvent(IMFAsyncCallback* a, IUnknown* b) override { return gen_->BeginGetEvent(a, b); }
  STDMETHODIMP EndGetEvent(IMFAsyncResult* a, IMFMediaEvent** b) override { return gen_->EndGetEvent(a, b); }
  STDMETHODIMP QueueEvent(MediaEventType a, REFGUID b, HRESULT c, const PROPVARIANT* d) override { return gen_->QueueEvent(a, b, c, d); }

  // ---- IMFTransform ------------------------------------------------------------------------
  STDMETHODIMP GetStreamLimits(DWORD* a, DWORD* b, DWORD* c, DWORD* d) override { return inner_->GetStreamLimits(a, b, c, d); }
  STDMETHODIMP GetStreamCount(DWORD* a, DWORD* b) override { return inner_->GetStreamCount(a, b); }
  STDMETHODIMP GetStreamIDs(DWORD a, DWORD* b, DWORD c, DWORD* d) override { return inner_->GetStreamIDs(a, b, c, d); }
  STDMETHODIMP GetInputStreamInfo(DWORD a, MFT_INPUT_STREAM_INFO* b) override { return inner_->GetInputStreamInfo(a, b); }
  STDMETHODIMP GetOutputStreamInfo(DWORD a, MFT_OUTPUT_STREAM_INFO* b) override {
    const HRESULT hr = inner_->GetOutputStreamInfo(a, b);
    if (FAILED(hr)) { record("GetOutputStreamInfo", hr); note_abnormal("GetOutputStreamInfo", hr); }
    return hr;
  }
  STDMETHODIMP GetAttributes(IMFAttributes** a) override { return inner_->GetAttributes(a); }
  STDMETHODIMP GetInputStreamAttributes(DWORD a, IMFAttributes** b) override { return inner_->GetInputStreamAttributes(a, b); }
  STDMETHODIMP GetOutputStreamAttributes(DWORD a, IMFAttributes** b) override { return inner_->GetOutputStreamAttributes(a, b); }
  STDMETHODIMP DeleteInputStream(DWORD a) override { return inner_->DeleteInputStream(a); }
  STDMETHODIMP AddInputStreams(DWORD a, DWORD* b) override { return inner_->AddInputStreams(a, b); }
  STDMETHODIMP GetInputAvailableType(DWORD a, DWORD b, IMFMediaType** c) override { return inner_->GetInputAvailableType(a, b, c); }
  STDMETHODIMP GetOutputAvailableType(DWORD a, DWORD b, IMFMediaType** c) override { return inner_->GetOutputAvailableType(a, b, c); }
  STDMETHODIMP SetInputType(DWORD a, IMFMediaType* b, DWORD flags) override {
    before_type_set(flags);
    const HRESULT hr = inner_->SetInputType(a, b, flags);
    if (!(flags & MFT_SET_TYPE_TEST_ONLY)) record(b ? "SetInputType" : "SetInputType(null)", hr);
    if (!(flags & MFT_SET_TYPE_TEST_ONLY) && b && SUCCEEDED(hr)) inSet_ = true;
    after_type_set();
    return hr;
  }
  STDMETHODIMP SetOutputType(DWORD a, IMFMediaType* b, DWORD flags) override {
    before_type_set(flags);
    const HRESULT hr = inner_->SetOutputType(a, b, flags);
    if (!(flags & MFT_SET_TYPE_TEST_ONLY)) record(b ? "SetOutputType" : "SetOutputType(null)", hr);
    if (!(flags & MFT_SET_TYPE_TEST_ONLY) && b && SUCCEEDED(hr)) outSet_ = true;
    after_type_set();
    return hr;
  }
  STDMETHODIMP GetInputCurrentType(DWORD a, IMFMediaType** b) override { return inner_->GetInputCurrentType(a, b); }
  STDMETHODIMP GetOutputCurrentType(DWORD a, IMFMediaType** b) override { return inner_->GetOutputCurrentType(a, b); }
  STDMETHODIMP GetInputStatus(DWORD a, DWORD* b) override { return inner_->GetInputStatus(a, b); }
  STDMETHODIMP GetOutputStatus(DWORD* a) override { return inner_->GetOutputStatus(a); }
  STDMETHODIMP SetOutputBounds(LONGLONG a, LONGLONG b) override { return inner_->SetOutputBounds(a, b); }
  STDMETHODIMP ProcessEvent(DWORD a, IMFMediaEvent* b) override { return inner_->ProcessEvent(a, b); }
  STDMETHODIMP ProcessMessage(MFT_MESSAGE_TYPE m, ULONG_PTR p) override {
    if (m == MFT_MESSAGE_NOTIFY_BEGIN_STREAMING && !beginSnapped_) {
      beginSnapped_ = true;
      if (typesSet_ && !preFirstSetSnapped_) { preFirstSetSnapped_ = true; snapshot("pre-first-set(attr-done)"); }
      snapshot("before-begin-streaming");
    }
    const HRESULT hr = inner_->ProcessMessage(m, p);
    record("ProcessMessage(" + std::to_string(static_cast<int>(m)) + ")", hr);
    if (FAILED(hr)) note_abnormal("ProcessMessage" + std::to_string(static_cast<int>(m)), hr);
    if (m == MFT_MESSAGE_NOTIFY_START_OF_STREAM && !startSnapped_) {
      startSnapped_ = true;
      phase = "streaming";
      snapshot(SUCCEEDED(hr) ? "after-start-of-stream" : "after-start-of-stream(FAILED)");
      configAtStart_ = config_status();
      std::printf("  config(after-start-of-stream): %s\n", configAtStart_.c_str());
    }
    return hr;
  }
  STDMETHODIMP ProcessInput(DWORD a, IMFSample* b, DWORD c) override {
    const HRESULT hr = inner_->ProcessInput(a, b, c);
    record("ProcessInput", hr);
    lastProcessInputHr_ = hr;
    lastProcessInputFrame_ = frame.load();
    if (SUCCEEDED(hr)) ++piOk_;
    else if (hr == MF_E_NOTACCEPTING) {
      if (naFrame_ != frame.load()) { naFrame_ = frame.load(); naCount_ = 0; }
      if (++naCount_ == 1) ++piNotAccepting_;                   // the product drains and retries once
      else note_abnormal("ProcessInput-retry(NOTACCEPTING again)", hr);  // the retry failed too
    } else note_abnormal("ProcessInput", hr);
    return hr;
  }
  STDMETHODIMP ProcessOutput(DWORD flags, DWORD n, MFT_OUTPUT_DATA_BUFFER* buf, DWORD* status) override {
    const HRESULT hr = inner_->ProcessOutput(flags, n, buf, status);
    record("ProcessOutput", hr);
    if (SUCCEEDED(hr)) {
      ++poOk_;
      if (!firstSampleSnapped_ && buf && buf[0].pSample) {
        firstSampleSnapped_ = true;
        snapshot("first-output-sample");
        phase = "running";
      }
    } else if (hr == E_UNEXPECTED && asyncMft) {
      if (toleratedUnexpected_++ == 0) toleratedFirstFrame_ = frame.load();  // product-tolerated (drain-end)
    } else if (hr != MF_E_TRANSFORM_NEED_MORE_INPUT && hr != MF_E_TRANSFORM_STREAM_CHANGE) {
      note_abnormal("ProcessOutput", hr);
    }
    return hr;
  }

 private:
  ~LoggingTransform() = default;
  static constexpr size_t kHistory = 24;
  void record(const std::string& call, HRESULT hr) {
    history_.push_back({call, hr, frame.load(), phase});
    if (history_.size() > kHistory) history_.pop_front();
  }
  void before_type_set(DWORD flags) {
    if (!(flags & MFT_SET_TYPE_TEST_ONLY) && !preTypesSnapped_) { preTypesSnapped_ = true; snapshot("pre-types"); }
  }
  void after_type_set() {
    if (inSet_ && outSet_ && !typesSet_) {
      typesSet_ = true;
      phase = "post-types";
      snapshot("types-applied");  // both types accepted; the product has not written MF_LOW_LATENCY yet
    }
  }
  void inject_ll_off() {
    const GUID keys[] = {CODECAPI_AVLowLatencyMode, CODECAPI_AVEncCommonLowLatency, CODECAPI_AVEncCommonRealTime};
    for (const GUID& k : keys) {
      VARIANT v;
      VariantInit(&v);
      v.vt = VT_BOOL;
      v.boolVal = VARIANT_FALSE;
      const HRESULT hr = api_->SetValue(&k, &v);
      std::printf("  set[%s] %s BOOL:false INJECT(explicit-off) -> %s\n", phase.c_str(), key_name(&k).c_str(),
                  hr_str(hr).c_str());
    }
    Microsoft::WRL::ComPtr<IMFAttributes> attrs;
    if (SUCCEEDED(inner_->GetAttributes(&attrs)) && attrs) {
      const HRESULT hr = attrs->SetUINT32(MF_LOW_LATENCY, 0);
      std::printf("  attr[%s] MF_LOW_LATENCY=0 INJECT(explicit-off) -> %s\n", phase.c_str(), hr_str(hr).c_str());
    }
    snapshot("post-hints(explicit-off)");
  }
  void note_abnormal(const std::string& call, HRESULT hr) {
    lastAbnormal_ = call + " " + hr_str(hr) + "@frame" + std::to_string(frame.load());  // latest occurrence
    for (auto& a : abnormal_) if (a.call == call && a.hr == hr) { ++a.count; a.lastFrame = frame.load(); return; }
    abnormal_.push_back({call, hr, 1, frame.load(), frame.load()});
    std::printf("  abnormal %s hr=%s at frame %llu phase=%s\n", call.c_str(), hr_str(hr).c_str(),
                static_cast<unsigned long long>(frame.load()), phase.c_str());
  }

  std::atomic<ULONG> ref_{1};
  Microsoft::WRL::ComPtr<IMFTransform> inner_;
  Microsoft::WRL::ComPtr<ICodecAPI> api_;
  Microsoft::WRL::ComPtr<IMFMediaEventGenerator> gen_;
  ProxyCfg cfg_;
  bool inSet_ = false, outSet_ = false, typesSet_ = false;
  bool preTypesSnapped_ = false, preFirstSetSnapped_ = false, beginSnapped_ = false, startSnapped_ = false,
       firstSampleSnapped_ = false;
  bool hintsSeen_ = false, postHintsSnapped_ = false;
  uint32_t toleratedUnexpected_ = 0;
  uint64_t toleratedFirstFrame_ = 0;
  uint64_t naFrame_ = ~0ULL;
  uint32_t naCount_ = 0;
  HRESULT lastProcessInputHr_ = S_OK;
  uint64_t lastProcessInputFrame_ = 0;
  std::string lastAbnormal_;
  std::string configAtStart_;
  std::map<std::string, uint32_t> requested_;
  std::deque<RawCall> history_;
  struct { bool set = false; uint64_t frame = 0; std::string phase, lastAbnormal; HRESULT lastProcessInputHr = S_OK;
           uint64_t lastProcessInputFrame = 0; std::vector<RawCall> history; } latch_;
  std::vector<AbnormalHr> abnormal_;
  std::map<MediaEventType, uint32_t> evCounts_;
  uint32_t piOk_ = 0, piNotAccepting_ = 0, poOk_ = 0, forceKeyOk_ = 0;
};

// The product's "mft_hw" enumeration (create_h264_encoder_transform_from_mode, HardwareOnly).
IMFTransform* create_hw_encoder_like_product() {
  MFT_REGISTER_TYPE_INFO inInfo{MFMediaType_Video, MFVideoFormat_NV12};
  MFT_REGISTER_TYPE_INFO outInfo{MFMediaType_Video, MFVideoFormat_H264};
  IMFActivate** acts = nullptr;
  UINT32 n = 0;
  const HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                               &inInfo, &outInfo, &acts, &n);
  if (FAILED(hr) || n == 0) {
    std::printf("  proxy: MFTEnumEx hr=%s count=%u\n", hr_str(hr).c_str(), n);
    if (acts) CoTaskMemFree(acts);
    return nullptr;
  }
  IMFTransform* t = nullptr;
  for (UINT32 i = 0; i < n && !t; ++i) {
    if (SUCCEEDED(acts[i]->ActivateObject(IID_PPV_ARGS(&t))) && t) {
      WCHAR* name = nullptr;
      UINT32 cch = 0;
      acts[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &cch);
      GUID clsid{};
      acts[i]->GetGUID(MFT_TRANSFORM_CLSID_Attribute, &clsid);
      WCHAR* vendor = nullptr;
      WCHAR* hwUrl = nullptr;
      acts[i]->GetAllocatedString(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, &vendor, &cch);
      acts[i]->GetAllocatedString(MFT_ENUM_HARDWARE_URL_Attribute, &hwUrl, &cch);
      std::printf("  proxy: inner MFT = enum[%u/%u] \"%ls\" clsid=%s vendorId=%ls hwUrl=\"%ls\" (compare with the direct row's mft-identity line)\n",
                  i, n, name ? name : L"?", key_name(&clsid).c_str(), vendor ? vendor : L"?", hwUrl ? hwUrl : L"?");
      if (name) CoTaskMemFree(name);
      if (vendor) CoTaskMemFree(vendor);
      if (hwUrl) CoTaskMemFree(hwUrl);
    }
  }
  for (UINT32 i = 0; i < n; ++i) acts[i]->Release();
  CoTaskMemFree(acts);
  return t;
}

struct Row {
  const char* label = "";
  const char* scene = "";
  uint32_t rateKbps = 0;
  bool proxy = true;
  const char* rc = "";              // REMOTE60_NATIVE_RATE_CONTROL
  bool rcBeforeTypes = false;       // REMOTE60_NATIVE_RC_BEFORE_TYPES=1 (product then applies pre AND post)
  bool hintsOmitted = false;        // REMOTE60_NATIVE_LOW_LATENCY=0 + REMOTE60_NATIVE_REALTIME=0
  uint32_t retargetKbps = 0;        // reconfigure_bitrate at the body midpoint
  const char* maxQpEnv = nullptr;   // REMOTE60_NATIVE_MAX_QP ("0" = product skips the MaxQP Set)
  uint32_t retargetMaxQp = 0;       // set_max_qp(N) at the midpoint, BEFORE reconfigure_bitrate
  bool shamAtMid = false;           // F3 control: same calls at the midpoint with the UNCHANGED values
  bool reinitAtMid = false;         // r4-2: product rebuild path (shutdown+initialize, new MFT) at the midpoint
  uint32_t reinitThenMaxQp = 0;     // r4-2: then set_max_qp(N) like the governor does after a rebuild (0 = none)
  ProxyCfg cfg{};
};

bool g_psnr = true;
bool g_selftestDup = false;

// Luma PSNR against the deterministic source, exact PTS. The last kRing body frames are kept so a
// delayed async output still finds its source; older ones count as unmatched (never scored).
struct PsnrAcc {
  static constexpr uint32_t kRing = 24;
  std::vector<std::vector<uint8_t>> luma = std::vector<std::vector<uint8_t>>(kRing);
  std::vector<int64_t> pts = std::vector<int64_t>(kRing, -1);
  double mseA = 0, mseB = 0; uint32_t nA = 0, nB = 0, unmatched = 0, decodeFail = 0;
  void keep(uint32_t i, int64_t ts, const std::vector<uint8_t>& nv12) {
    auto& dst = luma[i % kRing];
    dst.assign(nv12.begin(), nv12.begin() + kLuma);
    pts[i % kRing] = ts;
  }
  void score(const DecodedFrameNv12& f, bool secondHalf) {
    const std::vector<uint8_t>* src = nullptr;
    for (uint32_t k = 0; k < kRing; ++k) if (pts[k] == f.sampleTimeHns) { src = &luma[k]; pts[k] = -1; break; }
    if (!src) { ++unmatched; return; }
    if (f.bytes.size() < kLuma) { ++decodeFail; return; }
    const uint32_t vh = std::min(f.visibleHeight ? f.visibleHeight : kH, kH);
    const uint32_t vw = std::min(f.visibleWidth ? f.visibleWidth : kW, kW);
    const uint32_t dstStride = f.width ? f.width : kW;
    double sse = 0;
    for (uint32_t y = 0; y < vh; ++y) {
      const uint8_t* sp = src->data() + (size_t)y * kW;
      const uint8_t* dp = f.bytes.data() + (size_t)(y + f.visibleTop) * dstStride + f.visibleLeft;
      for (uint32_t x = 0; x < vw; ++x) { const int e = int(sp[x]) - int(dp[x]); sse += double(e) * e; }
    }
    const double mse = sse / ((double)vh * vw);
    if (secondHalf) { mseB += mse; ++nB; } else { mseA += mse; ++nA; }
  }
  double all() const { return (nA + nB) ? psnr_from_mse((mseA + mseB) / (nA + nB)) : 0; }
  double a() const { return nA ? psnr_from_mse(mseA / nA) : 0; }
  double b() const { return nB ? psnr_from_mse(mseB / nB) : 0; }
};

struct Window { uint64_t total = 0; double avg = 0, p1 = 0, p267 = 0; };
Window measure(const std::vector<uint32_t>& b, size_t from, size_t to, uint32_t fps) {
  Window w;
  uint64_t acc1 = 0, win1 = 0, acc267 = 0, win267 = 0;
  const size_t w267 = std::max<uint32_t>(1u, (fps + 2) / 4);  // 8 frames @30 = 266.7ms
  for (size_t k = from; k < to; ++k) {
    w.total += b[k];
    acc1 += b[k];
    if (k >= from + fps) acc1 -= b[k - fps];
    if (k + 1 >= from + fps) win1 = std::max(win1, acc1);
    acc267 += b[k];
    if (k >= from + w267) acc267 -= b[k - w267];
    if (k + 1 >= from + w267) win267 = std::max(win267, acc267);
  }
  const double sec = double(to - from) / fps;
  w.avg = sec > 0 ? w.total * 8.0 / 1000.0 / sec : 0;
  w.p1 = win1 * 8.0 / 1000.0;
  w.p267 = win267 * 8.0 / 1000.0 / (double(w267) / fps);
  return w;
}

void probe(const Row& r, uint32_t fps, uint32_t seconds, uint32_t* invalidRows) {
  for (const char* n : kRcEnv) _putenv_s(n, "");
  _putenv_s("REMOTE60_NATIVE_ENCODER_BACKEND", "mft_hw");
  if (*r.rc) _putenv_s("REMOTE60_NATIVE_RATE_CONTROL", r.rc);
  if (r.rcBeforeTypes) _putenv_s("REMOTE60_NATIVE_RC_BEFORE_TYPES", "1");
  if (r.hintsOmitted) {
    _putenv_s("REMOTE60_NATIVE_LOW_LATENCY", "0");
    _putenv_s("REMOTE60_NATIVE_REALTIME", "0");
  }
  if (r.maxQpEnv) _putenv_s("REMOTE60_NATIVE_MAX_QP", r.maxQpEnv);

  const std::string bufDesc = r.cfg.bufferOverrideBytes ? std::to_string(r.cfg.bufferOverrideBytes) + "B"
                              : r.cfg.bufferMultiplier  ? "x" + std::to_string(r.cfg.bufferMultiplier)
                                                        : std::string("product");
  const uint32_t cfgQp = H264Encoder::configured_max_qp();
  const bool midAction = r.retargetMaxQp || r.retargetKbps || r.shamAtMid || r.reinitAtMid;
  std::string rtDesc;
  if (r.reinitAtMid) rtDesc += " @mid:REINIT shutdown()+initialize() same config, old MFT released first, new MFT, same env" +
                               (r.reinitThenMaxQp ? " then LATE set_max_qp(" + std::to_string(r.reinitThenMaxQp) + ") (partial-fix risk)" : std::string(" (plan re-fed pre-types)"));
  if (r.shamAtMid) rtDesc += " @mid:SHAM set_max_qp(" + std::to_string(cfgQp) + ")+reconfigure_bitrate(" + std::to_string(r.rateKbps) + "k) (unchanged values)";
  if (r.retargetMaxQp) rtDesc += " @mid:set_max_qp(" + std::to_string(r.retargetMaxQp) + ")";
  if (r.retargetKbps) rtDesc += " @mid:reconfigure_bitrate(" + std::to_string(r.retargetKbps) + "k)";
  const std::string qpDesc = cfgQp ? std::to_string(cfgQp) : std::string("0(no MaxQP Set; readback=MFT's own)");
  std::printf("\n=== %s | scene=%s target=%uk proxy=%d rc=%s rcBeforeTypes=%d hints=%s buffer=%s%s maxQp=%s ===\n",
              r.label, r.scene, r.rateKbps, r.proxy ? 1 : 0, *r.rc ? r.rc : "vbr_peak(default)",
              r.rcBeforeTypes ? 1 : 0,
              r.cfg.llExplicitOff ? "explicit-off" : r.hintsOmitted ? "omitted" : "on(default)",
              bufDesc.c_str(), rtDesc.c_str(), qpDesc.c_str());

  auto invalid = [&]() { if (invalidRows) ++*invalidRows; };
  MftEnc enc;
  LoggingTransform* proxy = nullptr;
  uint32_t abnormalAccum = 0;  // abnormal count of proxies already released (rebuild rows)
  std::string latestAbnormalSaved = "none";  // latest abnormal of a released proxy (D2 null-proxy boundary)
  auto latest_abnormal = [&]() -> std::string { return proxy ? proxy->last_abnormal() : latestAbnormalSaved; };
  auto make_proxy = [&](bool printCaps) -> bool {
    IMFTransform* inner = create_hw_encoder_like_product();
    if (!inner) return false;
    proxy = new LoggingTransform(inner, r.cfg);
    {
      Microsoft::WRL::ComPtr<IMFAttributes> attrs;
      UINT32 asyncFlag = 0;
      if (SUCCEEDED(inner->GetAttributes(&attrs)) && attrs &&
          SUCCEEDED(attrs->GetUINT32(MF_TRANSFORM_ASYNC, &asyncFlag)))
        proxy->asyncMft = asyncFlag != 0;
      std::printf("  proxy: inner MF_TRANSFORM_ASYNC=%u\n", asyncFlag);
    }
    inner->Release();
    if (printCaps) proxy->caps();
    enc.enc.set_transform_for_test(proxy);  // the encoder holds its own reference until destroyed
    return true;
  };
  if (r.proxy && !make_proxy(r.cfg.printCaps)) { std::printf("  RESULT: INVALID exec=no-hw-MFT-for-proxy\n"); invalid(); return; }
  const uint32_t bitrate = r.rateKbps * 1000u;
  const uint32_t keyintFrames = 4u * fps;  // a raw IDR ~every 4s -> at most one inside the 5s body
  if (!enc.init(kW, kH, fps, bitrate, keyintFrames)) {
    if (proxy) proxy->latch_failure();
    std::printf("  RESULT: INVALID exec=init-failed latestAbnormal=%s (initialize() may already have run shutdown() internally)\n", proxy ? proxy->last_abnormal().c_str() : "n/a(no proxy)");
    if (proxy) { proxy->dump_latch(); proxy->dump_abnormal(); proxy->Release(); }
    invalid();
    return;
  }

  H264Decoder dec;
  bool decOk = false;
  if (g_psnr) {
    _putenv_s("REMOTE60_NATIVE_DECODER_BACKEND", "mft_sw");
    decOk = dec.initialize(kW, kH, fps);
    if (!decOk) std::printf("  psnr: SW decoder init failed -> no PSNR this row\n");
  }
  PsnrAcc psnr;

  std::vector<uint8_t> nv12(kNv12, 128);
  const uint32_t warmup = fps, body = seconds * fps, mid = body / 2;
  const uint32_t drainN = 8;  // F3: bounded tail recovery -- extra inputs after the body, outputs attributed by PTS
  std::vector<uint32_t> bytesByFrame(body, 0), auCount(body, 0);
  std::vector<std::pair<uint64_t, uint32_t>> arrivals;  // (return wall-clock us, bytes) of body AUs
  std::vector<uint32_t> idrFrames;
  uint32_t idrMax = 0, returnedBody = 0, drainRecovered = 0, lateFromBeforeBoundary = 0;
  // Product-side counters, summed over the row (the same struct the host stage reads).
  struct { uint64_t calls = 0, drainLoops = 0, needMore = 0, streamChange = 0, errors = 0, notAccepting = 0,
                    pollNoEvent = 0, pollNeedInput = 0, pollHaveOutput = 0, needInputOnly = 0, asyncOn = 0,
                    encodeUs = 0, encodeUsMax = 0; } ps;
  bool encFail = false;
  uint32_t failFrame = 0;
  const uint64_t frameUs = 1'000'000ULL / fps;
  uint64_t deadline = now_us(), wall0 = 0, wallBodyEnd = 0;
  bool boundaryPassed = false;
  uint32_t pendingAtRebuild = 0;  // r4-2: inputs the first instance had accepted but not output when shutdown() flushed it
  for (uint32_t i = 0; i < warmup + body + drainN; ++i) {
    if (i == warmup) wall0 = now_us();
    if (i == warmup + body) wallBodyEnd = now_us();
    const bool inDrain = i >= warmup + body;
    if (proxy) proxy->frame = i;  // tag the mid-point actions with the frame they precede
    if (midAction && i == warmup + mid) {
      std::printf("  boundary: first input after the mid-point change = frame %u (body idx %u)\n", i, mid);
      if (r.reinitAtMid) {
        std::printf("  REINIT at frame %u: H264Encoder::shutdown()+initialize(%ux%u fps=%u bitrate=%u keyint=%u), same configuration, NEW MFT instance; env unchanged (pre-types plan re-fed); maxQpOverride_ reset by initialize\n",
                    i, kW, kH, fps, bitrate, keyintFrames);
        pendingAtRebuild = enc.pending_depth();
        std::printf("  REINIT: first instance pending(accepted, not yet output)=%u -> shutdown() FLUSHes them (product behaviour: in-flight frames are lost at a re-init)\n", pendingAtRebuild);
        enc.enc.shutdown();
        if (proxy) {
          std::printf("  -- first instance summary --\n");
          proxy->print_events();
          proxy->dump_abnormal();
          abnormalAccum += proxy->abnormal_total();
          latestAbnormalSaved = proxy->last_abnormal();
          // D2: drop BOTH references to the old proxy (ours and the encoder's testTransform_) BEFORE the
          // new activation, so the old inner MFT is destroyed first -- the product's non-test lifetime.
          enc.enc.set_transform_for_test(nullptr);
          const ULONG left = proxy->Release();
          proxy = nullptr;
          std::printf("  REINIT: old proxy released before the new activation (refcount after release=%lu%s)\n",
                      static_cast<unsigned long>(left), left == 0 ? ": old inner MFT destroyed" : ": STILL HELD somewhere");
          if (!make_proxy(false)) { std::printf("  REINIT: no hw MFT for the second proxy\n"); encFail = true; failFrame = i; break; }
          proxy->frame = i;
          std::printf("  -- second instance (re-initialised) --\n");
        }
        if (!enc.init(kW, kH, fps, bitrate, keyintFrames)) {
          std::printf("  REINIT: initialize failed\n");
          if (proxy) proxy->latch_failure();
          encFail = true; failFrame = i; break;
        }
        if (r.reinitThenMaxQp) {
          const bool ok = enc.enc.set_max_qp(r.reinitThenMaxQp);  // governor re-imposing its ceiling after a rebuild
          std::printf("  LATE set_max_qp(%u) after re-init ok=%d (partial-fix risk: host_stage_stats_h264.cpp:205-207 re-applies the governor ceiling when override==0 && governor!=configured)\n", r.reinitThenMaxQp, ok ? 1 : 0);
          if (proxy) { proxy->snapshot("after-late-qp(post-reinit)"); std::printf("  config(after-late-qp post-reinit): %s\n", proxy->config_status().c_str()); }
        }
      }
      if (r.shamAtMid) {
        const bool ok1 = enc.enc.set_max_qp(cfgQp);
        std::printf("  SHAM set_max_qp(%u) at frame %u ok=%d (same ceiling as configured)\n", cfgQp, i, ok1 ? 1 : 0);
        if (proxy) proxy->snapshot("after-governor(sham)");
        const bool ok2 = enc.enc.reconfigure_bitrate(bitrate);
        std::printf("  SHAM reconfigure_bitrate(%uk) at frame %u ok=%d (same target)\n", r.rateKbps, i, ok2 ? 1 : 0);
        if (proxy) { proxy->snapshot("after-retarget(sham)"); std::printf("  config(after-retarget sham): %s\n", proxy->config_status().c_str()); }
      }
      if (r.retargetMaxQp) {
        const bool ok = enc.enc.set_max_qp(r.retargetMaxQp);  // product governor path (maxQpOverride_)
        std::printf("  set_max_qp(%u) at frame %u ok=%d\n", r.retargetMaxQp, i, ok ? 1 : 0);
        if (proxy) proxy->snapshot("after-governor");
      }
      if (r.retargetKbps) {
        const bool ok = enc.enc.reconfigure_bitrate(r.retargetKbps * 1000u);
        std::printf("  retarget at frame %u -> %uk ok=%d\n", i, r.retargetKbps, ok ? 1 : 0);
        if (proxy) { proxy->snapshot("after-retarget"); std::printf("  config(after-retarget): %s\n", proxy->config_status().c_str()); }
      }
      boundaryPassed = true;
    }
    deadline += frameUs;
    fill_scene(r.scene, nv12, i);
    if (decOk && i >= warmup && !inDrain) psnr.keep(i, (int64_t)i * (int64_t)(10'000'000LL / fps), nv12);
    std::vector<H264AccessUnit> units;
    H264EncodeFrameStats st{};
    const bool ok = enc.enc.encode_frame(nv12, i == 0, (int64_t)i * (int64_t)(10'000'000LL / fps), &units, &st);
    ++ps.calls; ps.drainLoops += st.processOutputDrainLoops; ps.needMore += st.processOutputNeedMoreInputCount;
    ps.streamChange += st.processOutputStreamChangeCount; ps.errors += st.processOutputErrorCount;
    ps.notAccepting += st.processInputNotAcceptingCount; ps.pollNoEvent += st.asyncPollNoEventCount;
    ps.pollNeedInput += st.asyncPollNeedInputCount; ps.pollHaveOutput += st.asyncPollHaveOutputCount;
    ps.needInputOnly += st.asyncNeedInputOnlyCall; ps.asyncOn += st.asyncEnabled;
    ps.encodeUs += st.encodeCallUs; ps.encodeUsMax = std::max<uint64_t>(ps.encodeUsMax, st.encodeCallUs);
    if (!ok) {
      encFail = true;
      failFrame = i;
      if (proxy) proxy->latch_failure();  // F1: before the product's shutdown() can add errors
      std::printf("  encode-false at frame %u (%s) product-stats-of-call: errors=%u notAccepting=%u drainLoops=%llu\n", i,
                  inDrain ? "drain" : i < warmup ? "warmup" : "body", st.processOutputErrorCount,
                  st.processInputNotAcceptingCount, (unsigned long long)st.processOutputDrainLoops);
      break;
    }
    const uint64_t t = now_us();
    for (const auto& u : units) {
      const int64_t fidx = u.sampleTimeHns / (int64_t)(10'000'000LL / fps);
      if (fidx >= (int64_t)warmup && fidx < (int64_t)(warmup + body)) {
        const size_t k = (size_t)(fidx - warmup);
        bytesByFrame[k] += (uint32_t)u.bytes.size();
        ++auCount[k];
        if (inDrain) ++drainRecovered; else { arrivals.push_back({t, (uint32_t)u.bytes.size()}); ++returnedBody; }
        if (boundaryPassed && fidx < (int64_t)(warmup + mid)) ++lateFromBeforeBoundary;
        if (u.rawIdr) { idrMax = std::max(idrMax, (uint32_t)u.bytes.size()); idrFrames.push_back((uint32_t)k); }
      }
      if (decOk) {
        std::vector<DecodedFrameNv12> outF;
        if (!dec.decode_access_unit(u.bytes, u.keyFrame, u.sampleTimeHns, &outF)) ++psnr.decodeFail;
        for (const auto& f : outF) {
          const int64_t fi = f.sampleTimeHns / (int64_t)(10'000'000LL / fps);
          if (fi < (int64_t)warmup || fi >= (int64_t)(warmup + body)) continue;  // warm-up/drain: fed, not scored
          psnr.score(f, fi >= (int64_t)(warmup + mid));
        }
      }
    }
    if (t < deadline) std::this_thread::sleep_for(std::chrono::microseconds(deadline - t));
  }
  if (!wallBodyEnd) wallBodyEnd = now_us();
  const double wallSec = wall0 ? (wallBodyEnd - wall0) / 1e6 : 0;
  const uint32_t pending = enc.pending_depth();
  enc.shutdown();
  if (decOk) dec.shutdown();
  if (proxy) proxy->dump_latch();

  // F3 coverage: unique body frames returned / missing / duplicates, after the bounded drain.
  if (g_selftestDup) {
    static bool injected = false;
    if (!injected && !encFail) { injected = true; ++auCount[0]; std::printf("  SELFTEST: injected one duplicate AU at body idx 0 (auCount=%u)\n", auCount[0]); }
  }
  uint32_t missing = 0, dup = 0, missTail = 0, rebuildLoss = 0;
  const uint32_t judged = encFail ? (failFrame > warmup ? std::min(failFrame - warmup, body) : 0) : body;
  std::string missList, rebuildLossList;
  for (uint32_t k = 0; k < judged; ++k) {
    if (auCount[k] == 0) {
      // r4-2: a frame the first instance had accepted but not yet output when the rebuild's shutdown()
      // flushed it. Counted apart (the product loses it too), not against VALID.
      if (r.reinitAtMid && k < mid && k + pendingAtRebuild >= mid) {
        ++rebuildLoss;
        rebuildLossList += (rebuildLossList.empty() ? "" : ",") + std::to_string(k);
        continue;
      }
      ++missing; if (k + drainN >= judged) ++missTail; if (missList.size() < 60) missList += (missList.empty() ? "" : ",") + std::to_string(k);
    }
    else if (auCount[k] > 1) ++dup;
  }
  const std::string rebuildNote = rebuildLoss ? " rebuild-boundary-loss=" + std::to_string(rebuildLoss) + " idx[" + rebuildLossList + "] (in-flight input flushed by the product's shutdown(); not counted against VALID)" : "";

  // 267ms peak over RETURN wall-clock windows (when the bytes actually came out of the encoder).
  double p267wall = 0;
  {
    uint64_t acc = 0;
    size_t lo = 0;
    for (size_t hi = 0; hi < arrivals.size(); ++hi) {
      acc += arrivals[hi].second;
      while (arrivals[hi].first - arrivals[lo].first > 266'667) acc -= arrivals[lo++].second;
      p267wall = std::max(p267wall, acc * 8.0 / 1000.0 / 0.2667);
    }
  }

  const double outFps = wallSec > 0 ? returnedBody / wallSec : 0;
  const bool fpsShort = outFps < 0.9 * fps;
  const uint32_t abnormalN = (proxy ? proxy->abnormal_total() : 0) + abnormalAccum;
  const bool valid = !encFail && returnedBody > 0 && !fpsShort && missing == 0 && dup == 0 && abnormalN == 0;  // D1: dup gates too
  const Window all = measure(bytesByFrame, 0, body, fps);
  std::printf("  avgKbps=%.0f (x%.2f)  1sPeak=%.0f (x%.2f)  267msPeak=%.0f (x%.2f)  267msPeakWall=%.0f (x%.2f)  "
              "idrMax=%u idrN=%zu  outFps=%.1f(body) pending=%u drainRecovered=%u\n",
              all.avg, all.avg / r.rateKbps, all.p1, all.p1 / r.rateKbps, all.p267, all.p267 / r.rateKbps,
              p267wall, p267wall / r.rateKbps, idrMax, idrFrames.size(), outFps, pending, drainRecovered);
  if (decOk)
    std::printf("  psnr: luma=%.2f n=%u unmatched=%u decodeFail=%u | firstHalf=%.2f (n=%u) secondHalf=%.2f (n=%u)\n",
                psnr.all(), psnr.nA + psnr.nB, psnr.unmatched, psnr.decodeFail, psnr.a(), psnr.nA, psnr.b(), psnr.nB);
  // r4-4: a byte drop must be read together with delivery -- fewer bytes from missing/late output is not a gain.
  std::printf("  delivery: outFps=%.1f(body) coverage=%u/%u unique missing=%u dup=%u rebuildBoundaryLoss=%u drainRecovered=%u pending=%u idrN=%zu idrMax=%u psnrMatched=%u/%u\n",
              outFps, judged - missing - rebuildLoss, body, missing, dup, rebuildLoss, drainRecovered, pending, idrFrames.size(), idrMax,
              psnr.nA + psnr.nB, decOk ? body : 0);
  {
    // Both halves on EVERY row (F3): the unchanged rows are the control for the retarget rows.
    const Window a = measure(bytesByFrame, 0, mid, fps), b = measure(bytesByFrame, mid, body, fps);
    const uint32_t afterTarget = r.retargetKbps ? r.retargetKbps : r.rateKbps;
    std::printf("  halves%s: first avg=%.0f (x%.2f of %u) 1s=%.0f 267ms=%.0f | second avg=%.0f (x%.2f of %u) 1s=%.0f 267ms=%.0f%s\n",
                midAction ? "(retarget)" : "(no change)", a.avg, a.avg / r.rateKbps, r.rateKbps, a.p1, a.p267,
                b.avg, b.avg / afterTarget, afterTarget, b.p1, b.p267,
                midAction ? (" | AUs returned after the change from pre-change inputs=" + std::to_string(lateFromBeforeBoundary) + " (attributed to 'first' by PTS)").c_str() : "");
  }
  std::printf("  product-stats: calls=%llu async=%llu drainLoops=%llu needMoreInput=%llu streamChange=%llu errors=%llu "
              "notAccepting=%llu pollNoEvent=%llu pollNeedInput=%llu pollHaveOutput=%llu needInputOnlyCalls=%llu "
              "encodeCallAvgUs=%llu max=%llu%s\n",
              (unsigned long long)ps.calls, (unsigned long long)ps.asyncOn, (unsigned long long)ps.drainLoops,
              (unsigned long long)ps.needMore, (unsigned long long)ps.streamChange, (unsigned long long)ps.errors,
              (unsigned long long)ps.notAccepting, (unsigned long long)ps.pollNoEvent,
              (unsigned long long)ps.pollNeedInput, (unsigned long long)ps.pollHaveOutput,
              (unsigned long long)ps.needInputOnly, (unsigned long long)(ps.calls ? ps.encodeUs / ps.calls : 0),
              (unsigned long long)ps.encodeUsMax,
              proxy ? "" : " (no proxy: no raw ProcessOutput HRESULT counts; the product keeps none for E_UNEXPECTED)");
  if (proxy) { proxy->print_events(); proxy->dump_abnormal(); }
  std::string idrList;
  for (uint32_t f : idrFrames) idrList += (idrList.empty() ? "" : ",") + std::to_string(f);
  std::printf("  RESULT: %s  exec=%s  coverage=%s  config=%s  events=%s  idrBodyFrames=[%s] (GOP 4s vs body 5s: a periodic IDR can land only in the second half)\n",
              valid ? "VALID" : "INVALID",
              encFail ? ("encode-false@frame" + std::to_string(failFrame) + "(" + (failFrame < warmup ? "warmup" : failFrame < warmup + body ? "body" : "drain") + ")").c_str()
                      : returnedBody == 0 ? "ok-but-no-output" : fpsShort ? "ok-but-fps-short" : "ok",
              (missing == 0 && dup == 0) ? ("ok " + std::to_string(judged - rebuildLoss) + "/" + std::to_string(body) + " unique" + rebuildNote).c_str()
                                          : ("missing=" + std::to_string(missing) + (missTail ? "(tail-incomplete=" + std::to_string(missTail) + " after " + std::to_string(drainN) + "-frame drain)" : "") +
                                             " dup=" + std::to_string(dup) + " idx[" + missList + "]" + rebuildNote).c_str(),
              proxy ? proxy->config_at_start().c_str() : "n/a(no proxy)",
              abnormalN == 0 ? "ok" : ("abnormal=" + std::to_string(abnormalN) + " latest=" + latest_abnormal()).c_str(),
              idrList.c_str());
  if (!valid) invalid();
  if (proxy) proxy->Release();
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  uint32_t repeat = 1;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--repeat" && i + 1 < argc) repeat = static_cast<uint32_t>(std::max(1, std::atoi(argv[i + 1])));
    if (std::string(argv[i]) == "--no-psnr") g_psnr = false;
    if (std::string(argv[i]) == "--selftest-dup") g_selftestDup = true;  // D1 counter-example: must end INVALID, exit 2
  }
  if (g_selftestDup) std::printf("SELFTEST --selftest-dup: one duplicate AU is injected into the first row's coverage -> expect INVALID + exit 2.\n");
  std::printf("host_nvidia_probe v2 r5 (encoder-bakeoff t-1gvx6hx8): rate control fed PRE-TYPES, MaxQP ladder,\n");
  std::printf("r4: running attribution rows (both / sham / mean-only / qp-only), rebuild rows RE1 (plan re-fed) and\n");
  std::printf("RE2 (rebuild then set_max_qp(32) = governor order today), buffer 100ms instead of 1s, delivery line per row.\n");
  std::printf("CBR, VBV size, and the product's RUNNING levers (set_max_qp / reconfigure_bitrate) under it.\n");
  std::printf("r3 tool fixes (Codex F1-F3): encode-false latch + raw-call history before shutdown; NOTACCEPTING retry\n");
  std::printf("failure = abnormal; 'product-tolerated E_UNEXPECTED'; snapshots at types-applied / pre-first-set /\n");
  std::printf("before-begin-streaming / after-start-of-stream / first-output-sample; every Get shows its HRESULT\n");
  std::printf("(plain = S_OK exactly); RESULT split into exec / coverage(8-frame drain) / config / events; sham rows.\n");
  std::printf("Backend: every row pins mft_hw (direct row logs mft_enum_hw) -> no row takes the AMF-specific branch;\n");
  std::printf("MF attribute writes by the product are not in set[] (read back in the snapshots instead).\n");
  std::printf("maxQp=0 rows: REMOTE60_NATIVE_MAX_QP=0 -> the product never issues the MaxQP SetValue, so the\n");
  std::printf("MaxQP readback in those snapshots is the MFT's own value (51 = never set), not a request.\n");
  std::printf("rcBeforeTypes=1 rows: the product applies the rate control pre-types AND again post-types\n");
  std::printf("(both logged); on NVIDIA only the pre-types pass is applied (Home PC v2 result).\n");
  std::printf("psnr: luma vs the deterministic source, exact PTS, SW decoder; unmatched/decodeFail counted, not scored.\n");
  std::printf("All rows proxy=1 (the r1 direct-vs-proxy neutrality check is not repeated by this binary).\n");
  std::printf("RE rows: same-configuration re-initialisation (shutdown+initialize, old MFT released before the new\n");
  std::printf("activation) + plan re-feed; RE2 adds a late set_max_qp(32) = partial-fix risk (configured 0 + governor 32).\n");
  std::printf("Not represented: Host ApplyTarget / real fps or size change / epoch / RequestKey / receiver.\n");
  std::printf("target-only rows call reconfigure_bitrate = Mode+Mean+Peak+VBV re-applied with the QP request unchanged.\n");
  std::printf("\nInherited REMOTE60_NATIVE_* at start (pinned per row below):\n");
  for (const char* n : kRcEnv) {
    const std::string v = env_get(n);
    std::printf("  %s=%s\n", n, v.empty() ? "(unset)" : v.c_str());
  }
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::printf("CoInitializeEx(MTA)=%s  repeat=%u\n", hr_str(co).c_str(), repeat);

  MFStartup(MF_VERSION);
  const uint32_t fps = 30, sec = 5;
  auto buf_ms = [](uint32_t kbps, uint32_t ms) { return kbps * 1000u / 8u * ms / 1000u; };
  auto mk = [](const char* label, const char* scene, uint32_t kbps) {
    Row r;
    r.label = label;
    r.scene = scene;
    r.rateKbps = kbps;
    return r;
  };

  std::vector<Row> rows;
  auto pre = [&](const char* label, const char* scene, uint32_t kbps, const char* maxQp) {
    Row r = mk(label, scene, kbps);
    r.rcBeforeTypes = true;
    r.maxQpEnv = maxQp;
    return r;
  };
  // ---- drag @1.5M (product VBV here = bitrate/40 = 200ms = 37500B) ----
  { Row r = mk("A0-baseline-proxy(post-types,product)", "drag", 1500); r.cfg.printCaps = true; rows.push_back(r); }
  std::vector<Row> ladder;
  ladder.push_back(pre("P0-pre-vbr-maxQp0(Set omitted, fresh MFT)", "drag", 1500, "0"));
  ladder.push_back(pre("P1-pre-vbr-maxQp51(explicit 51)", "drag", 1500, "51"));
  ladder.push_back(pre("P2-pre-vbr-maxQp44", "drag", 1500, "44"));
  ladder.push_back(pre("P3-pre-vbr-maxQp40", "drag", 1500, "40"));
  ladder.push_back(pre("P3b-pre-vbr-maxQp32(r1)", "drag", 1500, "32"));
  { Row r = pre("P4-pre-cbr-maxQp0(Set omitted)", "drag", 1500, "0"); r.rc = "cbr"; ladder.push_back(r); }
  { Row r = pre("P5-pre-vbr-maxQp0(Set omitted)-buffer500ms", "drag", 1500, "0"); r.cfg.bufferOverrideBytes = buf_ms(1500, 500); ladder.push_back(r); }
  { Row r = pre("P6'-pre-vbr-maxQp0(Set omitted)-buffer100ms", "drag", 1500, "0"); r.cfg.bufferOverrideBytes = buf_ms(1500, 100); ladder.push_back(r); }
  { Row r = pre("R1-pre-vbr-maxQp0-running(qp40+1.18M both)", "drag", 1500, "0"); r.retargetMaxQp = 40; r.retargetKbps = 1180; ladder.push_back(r); }
  { Row r = pre("R1-sham-pre-vbr-maxQp0-running(same values)", "drag", 1500, "0"); r.shamAtMid = true; ladder.push_back(r); }
  { Row r = pre("R1-target-only-pre-vbr-maxQp0-running(reconfigure_bitrate 1.18M: Mean+Peak+VBV, QP unchanged)", "drag", 1500, "0"); r.retargetKbps = 1180; ladder.push_back(r); }
  { Row r = pre("R1-qp-only-pre-vbr-maxQp0-running(set_max_qp 40 via full RC re-apply; only the QP request changes)", "drag", 1500, "0"); r.retargetMaxQp = 40; ladder.push_back(r); }
  { Row r = pre("RE1-pre-vbr-maxQp0-reinit-same-config(shutdown+initialize, plan re-fed; init API order only)", "drag", 1500, "0"); r.reinitAtMid = true; ladder.push_back(r); }
  { Row r = pre("RE2-pre-vbr-maxQp0-reinit-then-late-set_max_qp32(partial-fix risk: configured 0 + governor base 32)", "drag", 1500, "0"); r.reinitAtMid = true; r.reinitThenMaxQp = 32; ladder.push_back(r); }
  for (uint32_t k = 0; k < repeat; ++k) rows.insert(rows.end(), ladder.begin(), ladder.end());
  rows.push_back(mk("A1-baseline-proxy-end(post-types,product)", "drag", 1500));
  // ---- motion @3M ----
  rows.push_back(mk("A0-baseline-proxy(post-types,product)", "motion", 3000));
  std::vector<Row> ladderM;
  ladderM.push_back(pre("P0-pre-vbr-maxQp0(Set omitted, fresh MFT)", "motion", 3000, "0"));
  ladderM.push_back(pre("P2-pre-vbr-maxQp44", "motion", 3000, "44"));
  { Row r = pre("P4-pre-cbr-maxQp0(Set omitted)", "motion", 3000, "0"); r.rc = "cbr"; ladderM.push_back(r); }
  { Row r = pre("R2-pre-vbr-maxQp0-running(qp40+2.36M both)", "motion", 3000, "0"); r.retargetMaxQp = 40; r.retargetKbps = 2360; ladderM.push_back(r); }
  { Row r = pre("R2-sham-pre-vbr-maxQp0-running(same values)", "motion", 3000, "0"); r.shamAtMid = true; ladderM.push_back(r); }
  { Row r = pre("R2-target-only-pre-vbr-maxQp0-running(reconfigure_bitrate 2.36M: Mean+Peak+VBV, QP unchanged)", "motion", 3000, "0"); r.retargetKbps = 2360; ladderM.push_back(r); }
  for (uint32_t k = 0; k < repeat; ++k) rows.insert(rows.end(), ladderM.begin(), ladderM.end());
  rows.push_back(mk("A1-baseline-proxy-end(post-types,product)", "motion", 3000));

  const uint64_t t0 = now_us();
  uint32_t invalidRows = 0;
  for (const auto& r : rows) probe(r, fps, sec, &invalidRows);
  std::printf("\nhost_nvidia_probe v2 r5: DONE. %zu rows in %.0fs. SUMMARY valid=%zu invalid=%u (exit %d). DONE is not a verdict; read each RESULT line. VALID = exec/coverage(no missing, no dup)/events gate only -- it does not mean low latency, quality, or lossless.\n",
              rows.size(), (now_us() - t0) / 1e6, rows.size() - invalidRows, invalidRows, invalidRows ? 2 : 0);
  std::printf("Fewer bytes with an encode failure / missing output / fps drop is NOT an improvement.\n");
  std::printf("Send this whole file back.\n");
  MFShutdown();
  if (SUCCEEDED(co)) CoUninitialize();
  return invalidRows ? 2 : 0;
}
