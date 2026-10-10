// Encoder rate-control bakeoff (encoder-bakeoff r1) -- MEASUREMENT-ONLY experiment, not shipped.
//
// Why: the shipping HW MFT overshoots its target (incident 10-11: 3 Mbps target -> 8-12 Mbps
// generated; the wire cap then drops frames and periodic/scene IDRs serialise into ~0.2-0.5 s
// stalls). OSLink on the same PC stays near its target with a software encoder. This tool drives
// the SAME product encoder path (H264Encoder, backend chosen by REMOTE60_NATIVE_ENCODER_BACKEND)
// and, for OSS encoders, a parallel wrapper, over identical deterministic 1080p30 PACED scenes at
// 1.5/3/6 Mbps, and reports how well each keeps the target plus its CPU/latency/quality cost.
//
// It changes NO product default and links NO GPL code into any product target. mft_sw and the SW
// decoder used for PSNR need no GPU, so this is safe to run beside an unrelated GPU measurement;
// mft_hw uses the GPU encoder and must be run when the GPU is free (pass it explicitly).
//
// Usage: remote60_host_encoder_bakeoff [secondsPerConfig=20] [scenes=static,scroll,drag,motion]
//                                      [backends=mft_sw] [ratesKbps=1500,3000,6000] [keyint=300]
//   env BAKEOFF_PSNR=1  -> decode (forced SW) and report luma PSNR vs source (slower).
//   Rate-control knobs are the product env vars (REMOTE60_NATIVE_RATE_CONTROL, _RC_MODE_NUM,
//   _PEAK_BITRATE_PERCENT, _MAX_QP, ...); set them before a run to measure an RC variant.

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
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace remote60::native_poc;

namespace {
constexpr uint32_t kW = 1920;
constexpr uint32_t kH = 1080;
constexpr uint32_t kFps = 30;
constexpr uint64_t kFrameUs = 1'000'000ULL / kFps;  // 33333
constexpr int64_t kHnsPerUs = 10;
constexpr size_t kLuma = static_cast<size_t>(kW) * kH;
constexpr size_t kNv12 = kLuma * 3 / 2;

// ---- deterministic scenes ---------------------------------------------------------------------
// Every backend sees byte-identical frames for a given (scene, frameIndex), so comparisons are fair.
// Chroma is flat 128 (monochrome): luma carries essentially all the H.264 bits and all the motion we
// are testing, and a flat UV keeps the input reproducible without a colour model. A deterministic
// hash gives high-spatial-frequency "desktop-like" texture that does not compress to nothing.

inline uint8_t tex(uint32_t x, uint32_t y) {
  uint32_t h = x * 2654435761u ^ y * 2246822519u;
  h ^= h >> 13;
  h *= 3266489917u;
  h ^= h >> 16;
  return static_cast<uint8_t>(h);
}

// Smooth-ish desktop background: large low-frequency gradient + a faint fixed texture, so "static"
// is realistic (not a flat field that encodes to ~0) yet compresses well when unchanged.
inline uint8_t deskbg(uint32_t x, uint32_t y) {
  const int grad = static_cast<int>((x >> 3) + (y >> 3)) & 0x7F;      // slow ramp, 0..127
  const int fleck = (tex(x >> 2, y >> 2) & 0x1F) - 16;                // +-16 fixed speckle
  int v = 64 + grad + fleck;
  return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

void fill_scene(const std::string& scene, std::vector<uint8_t>& nv12, uint32_t i) {
  uint8_t* Y = nv12.data();
  if (scene == "static") {
    // A detailed still desktop. Identical every frame after 0 -> tests how small a steady frame is
    // and how a periodic IDR spikes against it.
    for (uint32_t y = 0; y < kH; ++y)
      for (uint32_t x = 0; x < kW; ++x) Y[static_cast<size_t>(y) * kW + x] = deskbg(x, y);
  } else if (scene == "scroll") {
    // Vertical text-scroll: the whole frame shifts up by 4 px/frame over a textured page. Global
    // uniform motion -- the easy case for inter prediction, but constant change.
    const uint32_t dy = (i * 4);
    for (uint32_t y = 0; y < kH; ++y) {
      const uint32_t sy = y + dy;
      uint8_t* row = &Y[static_cast<size_t>(y) * kW];
      for (uint32_t x = 0; x < kW; ++x) {
        // page texture: horizontal "lines of text" (dark bands) over deskbg
        const bool textline = ((sy % 24) < 3) && ((tex(x >> 3, sy >> 3) & 1) != 0);
        row[x] = textline ? 24 : deskbg(x, sy);
      }
    }
  } else if (scene == "drag") {
    // Dragging a window: a sharp-edged opaque rectangle slides diagonally across a still textured
    // desktop. Localised motion + a moving hard edge (occlusion), the common desktop case.
    const uint32_t wW = 900, wH = 600;
    const uint32_t bx = (i * 9) % (kW - wW);
    const uint32_t by = (i * 6) % (kH - wH);
    for (uint32_t y = 0; y < kH; ++y) {
      uint8_t* row = &Y[static_cast<size_t>(y) * kW];
      for (uint32_t x = 0; x < kW; ++x) {
        if (x >= bx && x < bx + wW && y >= by && y < by + wH) {
          // window chrome: title bar + content with its own fine texture
          const bool title = (y - by) < 32;
          row[x] = title ? 200 : static_cast<uint8_t>(140 + (tex(x, y) & 0x3F) - 32);
        } else {
          row[x] = deskbg(x, y);
        }
      }
    }
  } else {  // "motion" -- full-screen high motion (video / game lock-screen): every pixel changes.
    const uint32_t ox = i * 13, oy = i * 7;
    for (uint32_t y = 0; y < kH; ++y) {
      uint8_t* row = &Y[static_cast<size_t>(y) * kW];
      for (uint32_t x = 0; x < kW; ++x) {
        // scrolling plasma + moving high-frequency detail: hard to inter-predict, like the stutter
        // lock-screen animation that provoked the incident.
        const int plasma = static_cast<int>(
            128 + 100 * std::sin((x + ox) * 0.015) * std::cos((y + oy) * 0.018));
        const int detail = (tex(x + ox, y + oy) & 0x3F) - 32;
        int v = plasma + detail;
        row[x] = static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
      }
    }
  }
  std::fill(nv12.begin() + kLuma, nv12.end(), static_cast<uint8_t>(128));  // flat chroma
}

// ---- timing / cpu -----------------------------------------------------------------------------
uint64_t proc_cpu_us() {
  FILETIME c{}, e{}, k{}, u{};
  if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
  const auto to_us = [](const FILETIME& f) {
    ULARGE_INTEGER v; v.LowPart = f.dwLowDateTime; v.HighPart = f.dwHighDateTime; return v.QuadPart / 10ULL;
  };
  return to_us(k) + to_us(u);
}
uint64_t now_us() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

double psnr_from_mse(double mse) {
  if (mse <= 1e-9) return 99.0;                 // identical -> report a ceiling
  return 10.0 * std::log10(255.0 * 255.0 / mse);
}

// ---- encoder abstraction ----------------------------------------------------------------------
// Both the product MFT path and the OSS OpenH264 path feed NV12 and hand back H264AccessUnit-shaped
// output, so the metrics loop is identical for either.
struct IEnc {
  virtual bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate, uint32_t keyint) = 0;
  virtual bool encode(const std::vector<uint8_t>& nv12, bool key, int64_t tsHns,
                      std::vector<H264AccessUnit>* out) = 0;
  virtual void shutdown() = 0;
  virtual ~IEnc() = default;
};

// The product encoder, backend chosen by REMOTE60_NATIVE_ENCODER_BACKEND (set by the caller).
struct MftEnc : IEnc {
  H264Encoder enc;
  bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate, uint32_t keyint) override {
    return enc.initialize(w, h, fps, bitrate, keyint);
  }
  bool encode(const std::vector<uint8_t>& nv12, bool key, int64_t tsHns,
              std::vector<H264AccessUnit>* out) override {
    return enc.encode_frame(nv12, key, tsHns, out);
  }
  void shutdown() override { enc.shutdown(); }
};

#ifdef BAKEOFF_OPENH264
#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
// OpenH264 (Cisco, BSD). Loaded at runtime (WelsCreateSVCEncoder / WelsDestroySVCEncoder) from the
// DLL named by BAKEOFF_OPENH264_DLL (default openh264.dll on PATH / next to the exe). NV12 -> I420
// (chroma is flat 128, so the U and V planes are constant). Low-latency: single spatial/temporal
// layer, RC_BITRATE_MODE, frame-skip OFF (we measure true generation vs target, like the MFT path).
struct OpenH264Enc : IEnc {
  using CreateFn = int (*)(ISVCEncoder**);
  using DestroyFn = void (*)(ISVCEncoder*);
  HMODULE dll = nullptr;
  DestroyFn destroy = nullptr;
  ISVCEncoder* enc = nullptr;
  uint32_t w_ = 0, h_ = 0;
  std::vector<uint8_t> uPlane, vPlane;

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
    p.iMaxBitrate = static_cast<int>(bitrate);   // CBR-ish cap at target, like the streaming case
    p.iRCMode = RC_BITRATE_MODE;
    p.bEnableFrameSkip = false;                   // do not drop frames -> measure real overshoot
    p.uiIntraPeriod = keyint;
    p.iTemporalLayerNum = 1;
    p.iSpatialLayerNum = 1;
    p.iComplexityMode = MEDIUM_COMPLEXITY;
    p.bEnableDenoise = false;
    p.bEnableSceneChangeDetect = true;           // like a real encoder's periodic+scene IDR behaviour
    p.iMultipleThreadIdc = 1;                     // single-threaded: deterministic, CPU-cost comparable
    p.sSpatialLayers[0].iVideoWidth = static_cast<int>(w);
    p.sSpatialLayers[0].iVideoHeight = static_cast<int>(h);
    p.sSpatialLayers[0].fFrameRate = static_cast<float>(fps);
    p.sSpatialLayers[0].iSpatialBitrate = static_cast<int>(bitrate);
    p.sSpatialLayers[0].iMaxSpatialBitrate = static_cast<int>(bitrate);
    if (enc->InitializeExt(&p) != 0) { std::printf("  (openh264: InitializeExt failed)\n"); return false; }
    int traceLevel = 0;  // WELS_LOG_QUIET -- keep the bakeoff table clean
    enc->SetOption(ENCODER_OPTION_TRACE_LEVEL, &traceLevel);
    int fmt = videoFormatI420;
    enc->SetOption(ENCODER_OPTION_DATAFORMAT, &fmt);
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
    pic.pData[0] = const_cast<uint8_t*>(nv12.data());  // Y plane is the NV12 luma, byte-identical
    pic.pData[1] = uPlane.data();
    pic.pData[2] = vPlane.data();
    pic.uiTimeStamp = tsHns / 10000;                   // hns -> ms
    SFrameBSInfo info{};
    const int rv = enc->EncodeFrame(&pic, &info);
    if (rv != 0) return false;
    if (info.eFrameType == videoFrameTypeSkip || info.iLayerNum <= 0) return true;  // nothing emitted
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
    out->push_back(std::move(au));
    return true;
  }

  void shutdown() override {
    if (enc) { enc->Uninitialize(); if (destroy) destroy(enc); enc = nullptr; }
    if (dll) { FreeLibrary(dll); dll = nullptr; }
  }
};
#endif  // BAKEOFF_OPENH264

std::unique_ptr<IEnc> make_encoder(const std::string& backend) {
  if (backend == "openh264") {
#ifdef BAKEOFF_OPENH264
    return std::make_unique<OpenH264Enc>();
#else
    return nullptr;  // harness built without OpenH264
#endif
  }
  _putenv_s("REMOTE60_NATIVE_ENCODER_BACKEND", backend.c_str());  // mft_hw / mft_sw / mft_auto
  return std::make_unique<MftEnc>();
}

struct Row {
  bool ok = false;
  std::string scene, backend;
  uint32_t targetKbps = 0;
  double cpuCores = 0, cpuMsPerFrame = 0;
  uint64_t encP50 = 0, encP95 = 0, encMax = 0;
  double frMean = 0;
  uint32_t frP95 = 0, frMax = 0;
  double avgKbps = 0;                            // mean emitted bitrate over the paced run
  double win1sMaxKbps = 0, win250Maxkbps = 0;    // sliding-window peaks
  double win1sRatio = 0, win250Ratio = 0;        // peak / target
  uint32_t idrCount = 0, idrMaxBytes = 0;
  double idrPerSec = 0;
  double psnr = 0;
  uint32_t psnrFrames = 0;
  uint32_t outFrames = 0, inFrames = 0;
};

// ---- the paced run ----------------------------------------------------------------------------
Row run_one(const std::string& scene, const char* backend, uint32_t bitrateKbps, uint32_t seconds,
            uint32_t keyint, bool doPsnr) {
  Row r;
  r.scene = scene;
  r.backend = backend;
  r.targetKbps = bitrateKbps;
  const uint32_t bitrate = bitrateKbps * 1000u;

  auto encp = make_encoder(backend);
  if (!encp) { std::printf("  (%s: harness built without this backend)\n", backend); return r; }
  if (!encp->init(kW, kH, kFps, bitrate, keyint)) return r;

  H264Decoder dec;
  bool decOk = false;
  if (doPsnr) {
    _putenv_s("REMOTE60_NATIVE_DECODER_BACKEND", "mft_sw");  // force SW decode -> no GPU for PSNR
    decOk = dec.initialize(kW, kH, kFps);
  }

  std::vector<uint8_t> nv12(kNv12, 128);
  const uint32_t nframes = seconds * kFps;
  // 2 s warm-up excluded from CPU/stat windows: enough for the async MFT to drain the large startup
  // IDR (it can surface several frames late) so a one-off bootstrap spike does not pollute the
  // steady-state windowed-peak metric. Mid-stream periodic/scene IDRs stay in the measured window.
  const uint32_t kWarmup = 2 * kFps;

  std::vector<uint64_t> enc_us; enc_us.reserve(nframes);
  std::vector<uint32_t> frameBytes(nframes, 0);  // emitted AU bytes attributed to each paced frame
  std::vector<uint32_t> idrBytes;

  // PSNR: keep a small ring of recent source luma keyed by sampleTimeHns so a decoded frame can find
  // its source without storing the whole clip. Low-latency no-B output stays in order; the ring just
  // tolerates a few frames of decoder lag.
  struct SrcLuma { int64_t ts; std::vector<uint8_t> y; };
  std::deque<SrcLuma> srcRing;
  double mseSum = 0; uint32_t mseFrames = 0;

  uint64_t cpu0 = 0, wall0 = 0, deadline = 0;
  for (uint32_t i = 0; i < nframes + kWarmup; ++i) {
    if (i == kWarmup) { cpu0 = proc_cpu_us(); wall0 = now_us(); deadline = now_us(); }
    deadline += kFrameUs;
    fill_scene(scene, nv12, i);
    // Relative, frame-based media timeline from 0 (monotonic, +1 frame each step). Pacing/CPU still
    // use the real wall clock (now_us); only the SAMPLE timestamp fed to the codec is this clean
    // timeline -- so OpenH264 reads a correct 30 fps and PSNR matching stays unambiguous.
    const int64_t tsHns = static_cast<int64_t>(i) * (10'000'000LL / kFps);

    std::vector<H264AccessUnit> units;
    const uint64_t t0 = now_us();
    if (!encp->encode(nv12, i == 0, tsHns, &units)) { encp->shutdown(); if (decOk) dec.shutdown(); return r; }
    const uint64_t dt = now_us() - t0;

    if (doPsnr && decOk) {
      srcRing.push_back(SrcLuma{tsHns, std::vector<uint8_t>(nv12.begin(), nv12.begin() + kLuma)});
      while (srcRing.size() > 24) srcRing.pop_front();
    }

    if (i >= kWarmup) {
      enc_us.push_back(dt);
      ++r.inFrames;
    }
    for (const auto& u : units) {
      if (i >= kWarmup) {
        const uint32_t sz = static_cast<uint32_t>(u.bytes.size());
        frameBytes[i - kWarmup] += sz;
        ++r.outFrames;
        if (u.keyFrame || u.rawIdr) { idrBytes.push_back(sz); }
      }
      if (doPsnr && decOk) {
        std::vector<DecodedFrameNv12> outF;
        dec.decode_access_unit(u.bytes, u.keyFrame, u.sampleTimeHns, &outF);
        for (const auto& f : outF) {
          // find source by nearest sampleTime in the ring
          const SrcLuma* best = nullptr; int64_t bestD = INT64_MAX;
          for (const auto& s : srcRing) {
            const int64_t d = std::llabs(s.ts - f.sampleTimeHns);
            if (d < bestD) { bestD = d; best = &s; }
          }
          if (best && f.bytes.size() >= kLuma && i >= kWarmup) {
            // compare the visible luma region row by row (decoded plane may be 1088 tall / padded)
            const uint32_t vh = f.visibleHeight ? f.visibleHeight : kH;
            const uint32_t vw = f.visibleWidth ? f.visibleWidth : kW;
            const uint32_t srcStride = kW, dstStride = f.width ? f.width : kW;
            double sse = 0; const uint32_t hh = std::min(vh, kH), ww = std::min(vw, kW);
            for (uint32_t y = 0; y < hh; ++y) {
              const uint8_t* sp = best->y.data() + static_cast<size_t>(y) * srcStride;
              const uint8_t* dp = f.bytes.data() + static_cast<size_t>(y + f.visibleTop) * dstStride + f.visibleLeft;
              for (uint32_t x = 0; x < ww; ++x) { const int e = static_cast<int>(sp[x]) - static_cast<int>(dp[x]); sse += double(e) * e; }
            }
            mseSum += sse / (double(hh) * ww);
            ++mseFrames;
          }
        }
      }
    }

    const uint64_t n = now_us();
    if (n < deadline) std::this_thread::sleep_for(std::chrono::microseconds(deadline - n));
  }
  const uint64_t wall = now_us() - wall0;
  const uint64_t cpu = proc_cpu_us() - cpu0;
  encp->shutdown();
  if (decOk) dec.shutdown();
  if (enc_us.empty() || wall == 0) return r;

  // ---- reduce ----
  std::sort(enc_us.begin(), enc_us.end());
  r.encP50 = enc_us[enc_us.size() / 2];
  r.encP95 = enc_us[(enc_us.size() * 95) / 100];
  r.encMax = enc_us.back();
  r.cpuCores = double(cpu) / double(wall);
  r.cpuMsPerFrame = r.outFrames ? (double(cpu) / 1000.0 / r.outFrames) : 0.0;

  uint64_t totalBytes = 0; std::vector<uint32_t> fb;
  for (uint32_t b : frameBytes) { totalBytes += b; fb.push_back(b); }
  std::vector<uint32_t> sorted = fb; std::sort(sorted.begin(), sorted.end());
  r.frMax = sorted.empty() ? 0 : sorted.back();
  r.frP95 = sorted.empty() ? 0 : sorted[(sorted.size() * 95) / 100];
  r.frMean = fb.empty() ? 0 : double(totalBytes) / fb.size();
  const double durS = double(fb.size()) / kFps;
  r.avgKbps = durS > 0 ? double(totalBytes) * 8.0 / 1000.0 / durS : 0;

  // sliding windows over the per-frame byte series (30 frames = 1 s, 8 frames ~= 250 ms)
  const uint32_t w1 = kFps, w250 = (kFps + 2) / 4;  // 30, 8
  uint64_t acc = 0; double max1 = 0;
  for (size_t k = 0; k < fb.size(); ++k) {
    acc += fb[k];
    if (k >= w1) acc -= fb[k - w1];
    if (k + 1 >= w1) max1 = std::max(max1, double(acc));
  }
  acc = 0; double max250 = 0;
  for (size_t k = 0; k < fb.size(); ++k) {
    acc += fb[k];
    if (k >= w250) acc -= fb[k - w250];
    if (k + 1 >= w250) max250 = std::max(max250, double(acc));
  }
  r.win1sMaxKbps = max1 * 8.0 / 1000.0;                       // bytes in 1 s -> kbps
  r.win250Maxkbps = max250 * 8.0 / 1000.0 / (double(w250) / kFps);  // normalise 250 ms -> per-second
  r.win1sRatio = bitrateKbps ? r.win1sMaxKbps / bitrateKbps : 0;
  r.win250Ratio = bitrateKbps ? r.win250Maxkbps / bitrateKbps : 0;

  r.idrCount = static_cast<uint32_t>(idrBytes.size());
  r.idrMaxBytes = idrBytes.empty() ? 0 : *std::max_element(idrBytes.begin(), idrBytes.end());
  r.idrPerSec = durS > 0 ? r.idrCount / durS : 0;

  if (doPsnr && mseFrames) { r.psnr = psnr_from_mse(mseSum / mseFrames); r.psnrFrames = mseFrames; }
  r.ok = true;
  return r;
}

void print_header() {
  std::printf("\n%-7s %-8s %6s | %7s %8s | %9s %8s %8s | %8s %8s | %6s/%-6s %6s/%-6s | %5s %7s | %6s\n",
              "scene", "backend", "tgtKbps", "cpuCore", "cpuMs/f", "frMeanB", "frP95B", "frMaxB",
              "avgKbps", "idrMaxB", "1s", "x", "250ms", "x", "idr/s", "encP95us", "PSNR");
}
void print_row(const Row& r) {
  if (!r.ok) { std::printf("%-7s %-8s %6u | INIT/ENCODE FAILED\n", r.scene.c_str(), r.backend.c_str(), r.targetKbps); return; }
  std::printf("%-7s %-8s %6u | %7.2f %8.2f | %9.0f %8u %8u | %8.0f %8u | %6.0f/%-6.2f %6.0f/%-6.2f | %5.2f %7llu | %6.2f\n",
              r.scene.c_str(), r.backend.c_str(), r.targetKbps, r.cpuCores, r.cpuMsPerFrame,
              r.frMean, r.frP95, r.frMax, r.avgKbps, r.idrMaxBytes,
              r.win1sMaxKbps, r.win1sRatio, r.win250Maxkbps, r.win250Ratio, r.idrPerSec,
              (unsigned long long)r.encP95, r.psnr);
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out; std::string cur;
  for (char c : s) { if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
  if (!cur.empty()) out.push_back(cur);
  return out;
}
}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const uint32_t seconds = argc > 1 ? static_cast<uint32_t>(std::max(4, std::atoi(argv[1]))) : 20u;
  const std::string scenesArg = argc > 2 ? argv[2] : "static,scroll,drag,motion";
  const std::string backendsArg = argc > 3 ? argv[3] : "mft_sw";
  const std::string ratesArg = argc > 4 ? argv[4] : "1500,3000,6000";
  const uint32_t keyint = argc > 5 ? static_cast<uint32_t>(std::max(1, std::atoi(argv[5]))) : 300u;
  const bool doPsnr = []() { const char* e = std::getenv("BAKEOFF_PSNR"); return e && *e && std::strcmp(e, "0") != 0; }();

  const auto scenes = split(scenesArg, ',');
  const auto backends = split(backendsArg, ',');
  std::vector<uint32_t> rates; for (auto& s : split(ratesArg, ',')) rates.push_back(static_cast<uint32_t>(std::atoi(s.c_str())));

  std::printf("host_encoder_bakeoff (1080p %ufps PACED, %us/config, keyint=%u, PSNR=%s)\n",
              kFps, seconds, keyint, doPsnr ? "on(SW decode)" : "off");
  std::printf("scenes=%s backends=%s ratesKbps=%s\n", scenesArg.c_str(), backendsArg.c_str(), ratesArg.c_str());
  const char* rc = std::getenv("REMOTE60_NATIVE_RATE_CONTROL");
  const char* pk = std::getenv("REMOTE60_NATIVE_PEAK_BITRATE_PERCENT");
  const char* mq = std::getenv("REMOTE60_NATIVE_MAX_QP");
  std::printf("rc_env: RATE_CONTROL=%s PEAK_BITRATE_PERCENT=%s MAX_QP=%s\n",
              rc ? rc : "(default VBR)", pk ? pk : "(default)", mq ? mq : "(default 32)");

  MFStartup(MF_VERSION);
  print_header();
  for (const auto& scene : scenes) {
    for (uint32_t br : rates) {
      for (const auto& b : backends) {
        const Row r = run_one(scene, b.c_str(), br, seconds, keyint, doPsnr);
        print_row(r);
      }
    }
  }
  std::printf("\nhost_encoder_bakeoff: DONE\n");
  MFShutdown();
  return 0;
}
