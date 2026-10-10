#pragma once
// encoder-bakeoff r2 shared pieces: deterministic scenes + an encoder abstraction over the product
// H264Encoder (MFT HW/SW) and an OpenH264 (BSD) adapter. MEASUREMENT-ONLY experiment; no product
// default is changed and no GPL is linked. Kept in a header so the measurement bakeoff and the
// product-path trace drive byte-identical input through the same encoders.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>

#include "mf_h264_codec.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace bakeoff {

using namespace remote60::native_poc;

constexpr uint32_t kW = 1920;
constexpr uint32_t kH = 1080;
constexpr size_t kLuma = static_cast<size_t>(kW) * kH;
constexpr size_t kNv12 = kLuma * 3 / 2;

inline uint64_t now_us() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline uint64_t proc_cpu_us() {
  FILETIME c{}, e{}, k{}, u{};
  if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
  const auto to_us = [](const FILETIME& f) {
    ULARGE_INTEGER v; v.LowPart = f.dwLowDateTime; v.HighPart = f.dwHighDateTime; return v.QuadPart / 10ULL;
  };
  return to_us(k) + to_us(u);
}

// ---- deterministic scenes (luma; flat 128 chroma) ---------------------------------------------
// NOTE (Codex 10db65c): deskbg + black bands are NOT real small Korean/Latin text; luma-PSNR here is
// indicative, not a DesktopText legibility pass. Scenes are a motion/entropy sweep, not a text test.
inline uint8_t tex(uint32_t x, uint32_t y) {
  uint32_t h = x * 2654435761u ^ y * 2246822519u;
  h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
  return static_cast<uint8_t>(h);
}
inline uint8_t deskbg(uint32_t x, uint32_t y) {
  const int grad = static_cast<int>((x >> 3) + (y >> 3)) & 0x7F;
  const int fleck = (tex(x >> 2, y >> 2) & 0x1F) - 16;
  int v = 64 + grad + fleck;
  return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}
inline void fill_scene(const std::string& scene, std::vector<uint8_t>& nv12, uint32_t i) {
  uint8_t* Y = nv12.data();
  if (scene == "static") {
    for (uint32_t y = 0; y < kH; ++y)
      for (uint32_t x = 0; x < kW; ++x) Y[static_cast<size_t>(y) * kW + x] = deskbg(x, y);
  } else if (scene == "scroll") {
    const uint32_t dy = (i * 4);
    for (uint32_t y = 0; y < kH; ++y) {
      const uint32_t sy = y + dy;
      uint8_t* row = &Y[static_cast<size_t>(y) * kW];
      for (uint32_t x = 0; x < kW; ++x) {
        const bool textline = ((sy % 24) < 3) && ((tex(x >> 3, sy >> 3) & 1) != 0);
        row[x] = textline ? 24 : deskbg(x, sy);
      }
    }
  } else if (scene == "drag") {
    const uint32_t wW = 900, wH = 600;
    const uint32_t bx = (i * 9) % (kW - wW);
    const uint32_t by = (i * 6) % (kH - wH);
    for (uint32_t y = 0; y < kH; ++y) {
      uint8_t* row = &Y[static_cast<size_t>(y) * kW];
      for (uint32_t x = 0; x < kW; ++x) {
        if (x >= bx && x < bx + wW && y >= by && y < by + wH) {
          const bool title = (y - by) < 32;
          row[x] = title ? 200 : static_cast<uint8_t>(140 + (tex(x, y) & 0x3F) - 32);
        } else {
          row[x] = deskbg(x, y);
        }
      }
    }
  } else {  // "motion": full-screen high motion. Near-random high entropy -> a STRESS upper bound,
            // harsher than real structured video/game content.
    const uint32_t ox = i * 13, oy = i * 7;
    for (uint32_t y = 0; y < kH; ++y) {
      uint8_t* row = &Y[static_cast<size_t>(y) * kW];
      for (uint32_t x = 0; x < kW; ++x) {
        const int plasma = static_cast<int>(128 + 100 * std::sin((x + ox) * 0.015) * std::cos((y + oy) * 0.018));
        const int detail = (tex(x + ox, y + oy) & 0x3F) - 32;
        int v = plasma + detail;
        row[x] = static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
      }
    }
  }
  std::fill(nv12.begin() + kLuma, nv12.end(), static_cast<uint8_t>(128));
}

// ---- encoder abstraction ----------------------------------------------------------------------
struct IEnc {
  virtual bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate, uint32_t keyint) = 0;
  // Returns the AUs emitted for this call; out AUs carry acceptedInputOrdinal (exact input->AU link).
  virtual bool encode(const std::vector<uint8_t>& nv12, bool key, int64_t tsHns,
                      std::vector<H264AccessUnit>* out) = 0;
  virtual bool set_max_qp(uint32_t /*qp*/) { return false; }  // live QP change; false = unsupported here
  virtual uint32_t pending_depth() { return 0; }              // async MFT accepted-but-undrained inputs
  virtual const char* backend_name() { return "?"; }
  virtual void shutdown() = 0;
  virtual ~IEnc() = default;
};

struct MftEnc : IEnc {
  H264Encoder enc;
  bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate, uint32_t keyint) override {
    return enc.initialize(w, h, fps, bitrate, keyint);
  }
  bool encode(const std::vector<uint8_t>& nv12, bool key, int64_t tsHns,
              std::vector<H264AccessUnit>* out) override {
    return enc.encode_frame(nv12, key, tsHns, out);
  }
  bool set_max_qp(uint32_t qp) override { return enc.set_max_qp(qp); }
  uint32_t pending_depth() override { return enc.pending_input_depth(); }
  const char* backend_name() override { return enc.backend_name(); }
  void shutdown() override { enc.shutdown(); }
};

#ifdef BAKEOFF_OPENH264
#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
// OpenH264 (Cisco, BSD), runtime-loaded. I420 input (flat 128 chroma). Low-latency single layer,
// RC_BITRATE_MODE, frame-skip OFF (measure true generation). scene-change IDR configurable (r2:
// default OFF for the capped-stream candidate).
struct OpenH264Enc : IEnc {
  using CreateFn = int (*)(ISVCEncoder**);
  using DestroyFn = void (*)(ISVCEncoder*);
  HMODULE dll = nullptr;
  DestroyFn destroy = nullptr;
  ISVCEncoder* enc = nullptr;
  uint32_t w_ = 0, h_ = 0;
  uint64_t ordinal_ = 0;
  bool sceneChangeIdr_ = false;
  std::vector<uint8_t> uPlane, vPlane;
  explicit OpenH264Enc(bool sceneChangeIdr) : sceneChangeIdr_(sceneChangeIdr) {}
  bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate, uint32_t keyint) override {
    w_ = w; h_ = h;
    const char* dllName = std::getenv("BAKEOFF_OPENH264_DLL");
    dll = LoadLibraryA(dllName && *dllName ? dllName : "openh264.dll");
    if (!dll) { std::printf("  (openh264: LoadLibrary failed err=%lu)\n", GetLastError()); return false; }
    auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll, "WelsCreateSVCEncoder"));
    destroy = reinterpret_cast<DestroyFn>(GetProcAddress(dll, "WelsDestroySVCEncoder"));
    if (!create || !destroy || create(&enc) != 0 || !enc) { std::printf("  (openh264: create failed)\n"); return false; }
    SEncParamExt p{};
    enc->GetDefaultParams(&p);
    p.iUsageType = CAMERA_VIDEO_REAL_TIME;
    p.iPicWidth = static_cast<int>(w);
    p.iPicHeight = static_cast<int>(h);
    p.fMaxFrameRate = static_cast<float>(fps);
    p.iTargetBitrate = static_cast<int>(bitrate);
    p.iMaxBitrate = static_cast<int>(bitrate);
    p.iRCMode = RC_BITRATE_MODE;
    p.bEnableFrameSkip = false;
    p.uiIntraPeriod = keyint;
    p.iTemporalLayerNum = 1;
    p.iSpatialLayerNum = 1;
    p.iComplexityMode = MEDIUM_COMPLEXITY;
    p.bEnableDenoise = false;
    p.bEnableSceneChangeDetect = sceneChangeIdr_;   // r2: OFF by default for the capped candidate
    p.iMultipleThreadIdc = 1;
    p.sSpatialLayers[0].iVideoWidth = static_cast<int>(w);
    p.sSpatialLayers[0].iVideoHeight = static_cast<int>(h);
    p.sSpatialLayers[0].fFrameRate = static_cast<float>(fps);
    p.sSpatialLayers[0].iSpatialBitrate = static_cast<int>(bitrate);
    p.sSpatialLayers[0].iMaxSpatialBitrate = static_cast<int>(bitrate);
    if (enc->InitializeExt(&p) != 0) { std::printf("  (openh264: InitializeExt failed)\n"); return false; }
    int traceLevel = 0; enc->SetOption(ENCODER_OPTION_TRACE_LEVEL, &traceLevel);
    int fmt = videoFormatI420; enc->SetOption(ENCODER_OPTION_DATAFORMAT, &fmt);
    uPlane.assign(static_cast<size_t>(w / 2) * (h / 2), 128);
    vPlane.assign(static_cast<size_t>(w / 2) * (h / 2), 128);
    return true;
  }
  bool encode(const std::vector<uint8_t>& nv12, bool key, int64_t tsHns,
              std::vector<H264AccessUnit>* out) override {
    if (key) enc->ForceIntraFrame(true);
    SSourcePicture pic{};
    pic.iColorFormat = videoFormatI420;
    pic.iPicWidth = static_cast<int>(w_);
    pic.iPicHeight = static_cast<int>(h_);
    pic.iStride[0] = static_cast<int>(w_);
    pic.iStride[1] = static_cast<int>(w_ / 2);
    pic.iStride[2] = static_cast<int>(w_ / 2);
    pic.pData[0] = const_cast<uint8_t*>(nv12.data());
    pic.pData[1] = uPlane.data();
    pic.pData[2] = vPlane.data();
    pic.uiTimeStamp = tsHns / 10000;
    SFrameBSInfo info{};
    const int rv = enc->EncodeFrame(&pic, &info);
    ++ordinal_;
    if (rv != 0) return false;
    if (info.eFrameType == videoFrameTypeSkip || info.iLayerNum <= 0) return true;
    H264AccessUnit au;
    for (int l = 0; l < info.iLayerNum; ++l) {
      const SLayerBSInfo& li = info.sLayerInfo[l];
      size_t layerBytes = 0;
      for (int n = 0; n < li.iNalCount; ++n) layerBytes += static_cast<size_t>(li.pNalLengthInByte[n]);
      au.bytes.insert(au.bytes.end(), li.pBsBuf, li.pBsBuf + layerBytes);
    }
    au.keyFrame = (info.eFrameType == videoFrameTypeIDR || info.eFrameType == videoFrameTypeI);
    au.rawIdr = (info.eFrameType == videoFrameTypeIDR);
    au.sampleTimeHns = tsHns;
    au.acceptedInputOrdinal = ordinal_;  // sync: this input produced this AU
    out->push_back(std::move(au));
    return true;
  }
  const char* backend_name() override { return "openh264"; }
  void shutdown() override {
    if (enc) { enc->Uninitialize(); if (destroy) destroy(enc); enc = nullptr; }
    if (dll) { FreeLibrary(dll); dll = nullptr; }
  }
};
#endif  // BAKEOFF_OPENH264

// backend: "mft_hw"/"mft_sw"/"mft_auto" (product path, env-selected) or "openh264" / "openh264_scd".
inline std::unique_ptr<IEnc> make_encoder(const std::string& backend) {
  if (backend == "openh264" || backend == "openh264_scd") {
#ifdef BAKEOFF_OPENH264
    return std::make_unique<OpenH264Enc>(backend == "openh264_scd");
#else
    return nullptr;
#endif
  }
  _putenv_s("REMOTE60_NATIVE_ENCODER_BACKEND", backend.c_str());
  return std::make_unique<MftEnc>();
}

inline double psnr_from_mse(double mse) {
  if (mse <= 1e-9) return 99.0;
  return 10.0 * std::log10(255.0 * 255.0 / mse);
}

}  // namespace bakeoff
