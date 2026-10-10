// Encoder rate-control bakeoff (encoder-bakeoff r2) -- MEASUREMENT-ONLY, not shipped.
//
// r2 corrects the r1 measurement boundaries Codex flagged:
//  - the warm-up is paced on the same real deadlines as the body (no "overloaded run shown as 30fps");
//    input / output / accepted FPS, elapsed and deadline-miss count are reported.
//  - AU bytes are attributed to their SOURCE PTS (acceptedInputOrdinal / sampleTimeHns), not the
//    current encode call, and the 1 s / 250 ms peaks are true media-time windows over that series.
//  - the average uses NOMINAL seconds (measured frames / fps); it is labelled as such.
//  - the async tail is accounted (submitted / returned / pending at end); encode latency is
//    input-submit -> AU-return (not the encode_frame call wall time).
//  - scene generation and (PSNR) decode CPU are measured and EXCLUDED from the encoder CPU figure.
//  - PSNR matches by EXACT PTS; decode failures / unmatched frames are counted, never scored.
//  - VBV is reported in ms; IDR counts NAL type 5 only (au.rawIdr), not every clean point.
//  - a run with any INIT/ENCODE failure is marked invalid rather than folded into DONE.
//
// It drives the SAME product encoder path (H264Encoder, backend via REMOTE60_NATIVE_ENCODER_BACKEND)
// and an OpenH264 (BSD) adapter; no product default is changed, no GPL is linked. This is the bare
// encoder (NO RateGovernor / wire cap / sender queue) -- that product control path is measured
// separately by remote60_host_encoder_product_trace.
//
// Usage: remote60_host_encoder_bakeoff [sec=20] [scenes=static,scroll,drag,motion]
//            [backends=mft_sw] [ratesKbps=1500,3000,6000] [fps=30] [keyintSec=10]
//   env BAKEOFF_PSNR=1       -> SW-decode and report exact-PTS luma PSNR (CPU excluded from encoder).
//   env BAKEOFF_COLDQP=X     -> cold-init the encoder's MaxQP ceiling at X (REMOTE60_NATIVE_MAX_QP).
//   env BAKEOFF_LIVEQP=X     -> run the first half at the default ceiling, then set_max_qp(X) live and
//                              run the second half; the two halves are reported separately (does the
//                              governor's live QP change actually move the output?).

#include "bakeoff_common.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace bakeoff;

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out; std::string cur;
  for (char c : s) { if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

struct Half {
  double avgKbps = 0;              // NOMINAL: total AU bytes in the half * 8 / (frames/fps)
  double win1sMaxKbps = 0, win250MaxKbps = 0;     // media-time windows by source PTS
  double win1sRatio = 0, win250Ratio = 0;
  double frMean = 0; uint32_t frP95 = 0, frMax = 0;
  uint32_t idrCount = 0, idrMaxBytes = 0; double idrPerSec = 0;
  double psnr = 0; uint32_t psnrMatched = 0, psnrUnmatched = 0, psnrDecodeFail = 0;
};

struct Row {
  bool valid = false;
  std::string scene, backend; uint32_t targetKbps = 0, fps = 0;
  // pacing / accounting
  uint32_t submitted = 0, returnedAUs = 0, pendingAtEnd = 0, deadlineMisses = 0;
  double inFps = 0, outFps = 0;      // measured
  double encoderCpuCores = 0;        // scene-gen + decode CPU subtracted
  double sceneGenCores = 0, decodeCores = 0;
  uint64_t latP50 = 0, latP95 = 0, latMax = 0;  // input-submit -> AU-return, us
  double vbvMs = 0;
  Half whole;                        // the measured region as one half (COLDQP / plain)
  bool split = false; Half a, b; uint32_t liveQp = 0;  // LIVEQP: before/after the live change
};

// media-time windowed peak over a per-source-frame byte series: max bytes in any window of
// `winFrames` consecutive source frames, expressed as kbps (bytes*8/1000 / windowSeconds).
double window_peak_kbps(const std::vector<uint32_t>& bytesByFrame, uint32_t winFrames, uint32_t fps) {
  if (bytesByFrame.empty() || winFrames == 0) return 0;
  uint64_t acc = 0; double mx = 0;
  for (size_t k = 0; k < bytesByFrame.size(); ++k) {
    acc += bytesByFrame[k];
    if (k >= winFrames) acc -= bytesByFrame[k - winFrames];
    if (k + 1 >= winFrames) mx = std::max(mx, static_cast<double>(acc));
  }
  const double winSec = static_cast<double>(winFrames) / fps;
  return mx * 8.0 / 1000.0 / winSec;
}

void reduce_half(Half& h, const std::vector<uint32_t>& bytesByFrame, const std::vector<uint32_t>& idrBytes,
                 uint32_t fps, uint32_t targetKbps) {
  uint64_t total = 0; std::vector<uint32_t> sorted;
  for (uint32_t b : bytesByFrame) { total += b; sorted.push_back(b); }
  std::sort(sorted.begin(), sorted.end());
  const double nominalSec = bytesByFrame.empty() ? 0 : static_cast<double>(bytesByFrame.size()) / fps;
  h.avgKbps = nominalSec > 0 ? total * 8.0 / 1000.0 / nominalSec : 0;
  h.frMean = bytesByFrame.empty() ? 0 : static_cast<double>(total) / bytesByFrame.size();
  h.frMax = sorted.empty() ? 0 : sorted.back();
  h.frP95 = sorted.empty() ? 0 : sorted[(sorted.size() * 95) / 100];
  const uint32_t w1 = fps, w250 = std::max(1u, (fps + 2) / 4);
  h.win1sMaxKbps = window_peak_kbps(bytesByFrame, w1, fps);
  h.win250MaxKbps = window_peak_kbps(bytesByFrame, w250, fps);
  h.win1sRatio = targetKbps ? h.win1sMaxKbps / targetKbps : 0;
  h.win250Ratio = targetKbps ? h.win250MaxKbps / targetKbps : 0;
  h.idrCount = static_cast<uint32_t>(idrBytes.size());
  h.idrMaxBytes = idrBytes.empty() ? 0 : *std::max_element(idrBytes.begin(), idrBytes.end());
  h.idrPerSec = nominalSec > 0 ? h.idrCount / nominalSec : 0;
}

Row run(const std::string& scene, const std::string& backend, uint32_t targetKbps, uint32_t seconds,
        uint32_t fps, uint32_t keyintSec, bool doPsnr, uint32_t coldQp, uint32_t liveQp) {
  Row r; r.scene = scene; r.backend = backend; r.targetKbps = targetKbps; r.fps = fps; r.liveQp = liveQp;
  r.split = liveQp != 0;
  const uint32_t bitrate = targetKbps * 1000u;
  const uint32_t keyintFrames = std::max(1u, keyintSec * fps);
  const uint64_t frameUs = 1'000'000ULL / fps;
  r.vbvMs = 200.0;  // product low_latency_vbv_bytes = bitrate/40 -> (bitrate/40)*8*1000/bitrate = 200ms

  if (coldQp) _putenv_s("REMOTE60_NATIVE_MAX_QP", std::to_string(coldQp).c_str());
  else _putenv_s("REMOTE60_NATIVE_MAX_QP", "");  // product default (32)

  auto enc = make_encoder(backend);
  if (!enc) { std::printf("  (%s: harness built without this backend)\n", backend.c_str()); return r; }
  if (!enc->init(kW, kH, fps, bitrate, keyintFrames)) return r;

  H264Decoder dec; bool decOk = false;
  if (doPsnr) { _putenv_s("REMOTE60_NATIVE_DECODER_BACKEND", "mft_sw"); decOk = dec.initialize(kW, kH, fps); }

  std::vector<uint8_t> nv12(kNv12, 128);
  const uint32_t warmup = 2 * fps;                 // paced, excluded from stats
  const uint32_t body = seconds * fps;
  const uint32_t total = warmup + body;
  const uint32_t liveSwitchAt = warmup + body / 2;  // LIVEQP: switch at the half point

  // per-source-frame byte series (indexed by source frame ordinal within the measured body)
  std::vector<uint32_t> bytesByFrame(body, 0), idrBytes;
  std::vector<uint32_t> bytesA, idrA, bytesB, idrB;  // LIVEQP halves (by source frame)
  std::vector<uint64_t> lat;                          // input-submit -> AU-return us
  std::map<uint64_t, uint64_t> submitUsByOrdinal;     // ordinal -> submit wall us
  std::map<int64_t, std::vector<uint8_t>> srcLumaByPts; // PSNR: exact-PTS source luma
  double mseSum = 0; uint32_t mseN = 0, psnrUnmatched = 0, psnrDecodeFail = 0;

  // Scene-generation cost, measured ALONE (encoder idle, single-threaded) so the async encoder's
  // own threads are not misattributed to it. Subtracted from the measured CPU to leave encoder cost.
  uint64_t perFrameGenUs = 0;
  { const uint64_t c0 = proc_cpu_us();
    for (uint32_t k = 0; k < fps; ++k) fill_scene(scene, nv12, warmup + k);
    perFrameGenUs = (proc_cpu_us() - c0) / std::max(1u, fps); }

  uint64_t sceneGenUs = 0, decodeUs = 0;
  uint64_t cpu0 = 0, wall0 = 0, deadline = now_us();
  uint64_t submittedOrdinal = 0;
  bool switched = false;

  for (uint32_t i = 0; i < total; ++i) {
    if (i == warmup) { cpu0 = proc_cpu_us(); wall0 = now_us(); }  // begin measured region
    if (r.split && !switched && i == liveSwitchAt) { enc->set_max_qp(liveQp); switched = true; }
    deadline += frameUs;

    fill_scene(scene, nv12, i);  // gen cost accounted via the calibration above, not inline

    const int64_t tsHns = static_cast<int64_t>(i) * static_cast<int64_t>(10'000'000LL / fps);
    const bool key = (i == 0);
    const uint64_t submitWall = now_us();
    // the encoder assigns acceptedInputOrdinal; it accepts 1 input per call, so the NEXT ordinal is
    // submittedOrdinal+1. Record submit time against it for the input->AU latency.
    ++submittedOrdinal;
    submitUsByOrdinal[submittedOrdinal] = submitWall;
    if (i >= warmup) ++r.submitted;
    if (doPsnr && decOk) srcLumaByPts[tsHns] = std::vector<uint8_t>(nv12.begin(), nv12.begin() + kLuma);

    std::vector<H264AccessUnit> units;
    if (!enc->encode(nv12, key, tsHns, &units)) { enc->shutdown(); if (decOk) dec.shutdown(); return r; }
    const uint64_t auWall = now_us();

    for (const auto& u : units) {
      // input->AU latency by exact ordinal (async-correct)
      auto it = submitUsByOrdinal.find(u.acceptedInputOrdinal);
      const uint64_t subWall = it != submitUsByOrdinal.end() ? it->second : submitWall;
      const int64_t srcPts = u.sampleTimeHns;
      const int64_t srcFrameIdx = fps ? srcPts / static_cast<int64_t>(10'000'000LL / fps) : -1;
      const bool inBody = srcFrameIdx >= static_cast<int64_t>(warmup) &&
                          srcFrameIdx < static_cast<int64_t>(total);
      if (inBody) {
        ++r.returnedAUs;
        lat.push_back(auWall - subWall);
        const uint32_t idx = static_cast<uint32_t>(srcFrameIdx - warmup);
        const uint32_t sz = static_cast<uint32_t>(u.bytes.size());
        if (idx < bytesByFrame.size()) bytesByFrame[idx] += sz;
        if (u.rawIdr) idrBytes.push_back(sz);
        if (r.split) {
          const bool firstHalf = srcFrameIdx < static_cast<int64_t>(liveSwitchAt);
          if (firstHalf) { if (idx < bytesByFrame.size()) { if (bytesA.size() <= idx) bytesA.resize(idx + 1, 0); bytesA[idx] += sz; } if (u.rawIdr) idrA.push_back(sz); }
          else { if (bytesB.size() <= idx) bytesB.resize(idx + 1, 0); bytesB[idx] += sz; if (u.rawIdr) idrB.push_back(sz); }
        }
      }
      if (doPsnr && decOk) {
        const uint64_t d0 = proc_cpu_us();
        std::vector<DecodedFrameNv12> outF;
        dec.decode_access_unit(u.bytes, u.keyFrame, u.sampleTimeHns, &outF);
        decodeUs += proc_cpu_us() - d0;
        for (const auto& f : outF) {
          auto sit = srcLumaByPts.find(f.sampleTimeHns);  // EXACT PTS match only
          if (sit == srcLumaByPts.end()) { ++psnrUnmatched; continue; }
          if (f.bytes.size() < kLuma) { ++psnrDecodeFail; continue; }
          const int64_t fidx = fps ? f.sampleTimeHns / static_cast<int64_t>(10'000'000LL / fps) : -1;
          if (fidx < static_cast<int64_t>(warmup)) continue;  // warm-up frames not scored
          const uint32_t vh = std::min(f.visibleHeight ? f.visibleHeight : kH, kH);
          const uint32_t vw = std::min(f.visibleWidth ? f.visibleWidth : kW, kW);
          const uint32_t dstStride = f.width ? f.width : kW;
          double sse = 0;
          for (uint32_t y = 0; y < vh; ++y) {
            const uint8_t* sp = sit->second.data() + static_cast<size_t>(y) * kW;
            const uint8_t* dp = f.bytes.data() + static_cast<size_t>(y + f.visibleTop) * dstStride + f.visibleLeft;
            for (uint32_t x = 0; x < vw; ++x) { const int e = int(sp[x]) - int(dp[x]); sse += double(e) * e; }
          }
          mseSum += sse / (double(vh) * vw); ++mseN;
          srcLumaByPts.erase(sit);
        }
      }
    }
    const uint64_t n = now_us();
    if (n < deadline) std::this_thread::sleep_for(std::chrono::microseconds(deadline - n));
    else if (i >= warmup && (n - deadline) > frameUs / 2) ++r.deadlineMisses;  // fell >half a frame behind
  }

  const uint64_t wall = now_us() - wall0;
  const uint64_t cpu = proc_cpu_us() - cpu0;
  r.pendingAtEnd = enc->pending_depth();
  enc->shutdown();
  if (decOk) dec.shutdown();
  if (wall == 0) return r;

  const double wallSec = wall / 1e6;
  sceneGenUs = perFrameGenUs * body;  // gen happens every measured frame; calibrated single-thread cost
  r.inFps = r.submitted / wallSec;
  r.outFps = r.returnedAUs / wallSec;
  const uint64_t encCpu = cpu > (sceneGenUs + decodeUs) ? cpu - sceneGenUs - decodeUs : 0;
  r.encoderCpuCores = double(encCpu) / double(wall);
  r.sceneGenCores = double(sceneGenUs) / double(wall);
  r.decodeCores = double(decodeUs) / double(wall);
  std::sort(lat.begin(), lat.end());
  if (!lat.empty()) { r.latP50 = lat[lat.size()/2]; r.latP95 = lat[(lat.size()*95)/100]; r.latMax = lat.back(); }

  reduce_half(r.whole, bytesByFrame, idrBytes, fps, targetKbps);
  if (r.split) { reduce_half(r.a, bytesA, idrA, fps, targetKbps); reduce_half(r.b, bytesB, idrB, fps, targetKbps); }
  if (doPsnr) { r.whole.psnr = mseN ? psnr_from_mse(mseSum / mseN) : 0; r.whole.psnrMatched = mseN;
                r.whole.psnrUnmatched = psnrUnmatched; r.whole.psnrDecodeFail = psnrDecodeFail; }
  r.valid = true;
  return r;
}

void print_header() {
  std::printf("\n%-7s %-9s %5s %4s | valid inFps outFps dmiss pend | encCore(gen/dec) | lat us p50/p95/max | "
              "avgKbps frMeanB frP95B frMaxB | 1s/x 250ms/x | idr/s idrMaxB | vbvMs | PSNR(match/unm/fail)\n",
              "scene","backend","tgt","fps");
}
void print_half(const char* tag, const Row& r, const Half& h) {
  std::printf("%-7s %-9s %5u %4u | %s %5.1f %5.1f %5u %4u | %5.2f(%.2f/%.2f) | %6llu/%6llu/%7llu | "
              "%7.0f %7.0f %6u %6u | %.0f/%.2f %.0f/%.2f | %.2f %6u | %.0f | %.2f(%u/%u/%u) %s\n",
              r.scene.c_str(), r.backend.c_str(), r.targetKbps, r.fps, r.valid?"ok ":"BAD",
              r.inFps, r.outFps, r.deadlineMisses, r.pendingAtEnd,
              r.encoderCpuCores, r.sceneGenCores, r.decodeCores,
              (unsigned long long)r.latP50,(unsigned long long)r.latP95,(unsigned long long)r.latMax,
              h.avgKbps, h.frMean, h.frP95, h.frMax, h.win1sMaxKbps, h.win1sRatio, h.win250MaxKbps, h.win250Ratio,
              h.idrPerSec, h.idrMaxBytes, r.vbvMs, h.psnr, h.psnrMatched, h.psnrUnmatched, h.psnrDecodeFail, tag);
}
void print_row(const Row& r) {
  if (!r.valid) { std::printf("%-7s %-9s %5u %4u | INIT/ENCODE FAILED (invalid run)\n",
                              r.scene.c_str(), r.backend.c_str(), r.targetKbps, r.fps); return; }
  if (r.split) { char ta[32], tb[32]; std::snprintf(ta,sizeof(ta),"[live before:qp32]"); std::snprintf(tb,sizeof(tb),"[live after:qp%u]", r.liveQp);
                 print_half(ta, r, r.a); print_half(tb, r, r.b); }
  else print_half("", r, r.whole);
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const uint32_t seconds = argc > 1 ? static_cast<uint32_t>(std::max(4, std::atoi(argv[1]))) : 20u;
  const std::string scenesArg = argc > 2 ? argv[2] : "static,scroll,drag,motion";
  const std::string backendsArg = argc > 3 ? argv[3] : "mft_sw";
  const std::string ratesArg = argc > 4 ? argv[4] : "1500,3000,6000";
  const uint32_t fps = argc > 5 ? static_cast<uint32_t>(std::max(1, std::atoi(argv[5]))) : 30u;
  const uint32_t keyintSec = argc > 6 ? static_cast<uint32_t>(std::max(1, std::atoi(argv[6]))) : 10u;
  const bool doPsnr = []{ const char* e = std::getenv("BAKEOFF_PSNR"); return e && *e && std::strcmp(e,"0"); }();
  const uint32_t coldQp = []{ const char* e = std::getenv("BAKEOFF_COLDQP"); return e&&*e?(uint32_t)std::atoi(e):0u; }();
  const uint32_t liveQp = []{ const char* e = std::getenv("BAKEOFF_LIVEQP"); return e&&*e?(uint32_t)std::atoi(e):0u; }();

  const auto scenes = split(scenesArg, ','); const auto backends = split(backendsArg, ',');
  std::vector<uint32_t> rates; for (auto& s : split(ratesArg, ',')) rates.push_back((uint32_t)std::atoi(s.c_str()));

  std::printf("host_encoder_bakeoff r2 (1080p %ufps PACED incl warm-up, %us/config, keyint=%us=%uframes)\n",
              fps, seconds, keyintSec, keyintSec*fps);
  std::printf("scenes=%s backends=%s ratesKbps=%s PSNR=%s coldQp=%u liveQp=%u\n",
              scenesArg.c_str(), backendsArg.c_str(), ratesArg.c_str(), doPsnr?"on":"off", coldQp, liveQp);
  MFStartup(MF_VERSION);
  print_header();
  bool anyInvalid = false;
  for (const auto& sc : scenes) for (uint32_t br : rates) for (const auto& b : backends) {
    const Row r = run(sc, b, br, seconds, fps, keyintSec, doPsnr, coldQp, liveQp);
    print_row(r);
    if (!r.valid) anyInvalid = true;
  }
  std::printf("\nhost_encoder_bakeoff: %s\n", anyInvalid ? "DONE (some INVALID runs -- see BAD rows)" : "DONE (all runs valid)");
  MFShutdown();
  return 0;
}
