// Encoder-bakeoff r3 -- control-loop MODEL (NOT the product send path). MEASUREMENT-ONLY, not shipped.
//
// ⚠️ r3 RE-LABEL (Codex): this uses the REAL encoder (H264Encoder), the REAL RateGovernor
// (host_rate_governor.hpp) and the REAL admission gate (host_encode_admission.hpp), but it does NOT
// run the product's WireLimiter / sender / stats tick / receiver. The wire is a single-threaded
// arbitrary-queue + R/fps credit MODEL. Its specific divergences from the product (so no reader
// mistakes it for the product path):
//   * cap OFF here still drains the model link at exactly R/fps (off only disables the admission
//     gate), so a "queue grows unbounded" under cap OFF is a property of THIS fixed virtual link, not
//     of the product's cap-off send policy.
//   * idle wire credit is unbounded (no product bounded bucket/ledger), which shifts the transient
//     and the skip count.
//   * the governor's avg-frame-bytes input here is the last drained queue.front() tail, not the
//     product's txDelta/completed-frame average.
//   * fps steps only change input sampling here; no codec reinit / product reset / target change.
//   * the 1 s tick is fps iterations, not real elapsed; encode failures are ignored.
// Therefore the r2 conclusions "AMD self-corrects in ~6 s" and "cap OFF queue explodes, so the cap is
// needed" are RETRACTED as product claims and kept only as observations of this combined-policy
// MODEL. A product claim needs the real sender/limiter/stats + receiver display/queue (see report r3
// §C: structure proposed to the verifier before building).
//
// The governor is fed the product's generated-load estimate (sent + skips*avgSentFrame; matches
// host_stage_stats_h264.cpp:183,208) so the ladder moves as in the field. No product default changed.
//
// Usage: remote60_host_encoder_product_trace [backend=mft_hw] [rateKbps=3000] [fps=30]
//            [phaseSec=8] [cap=on|off]

#include "bakeoff_common.hpp"
#include "host_rate_governor.hpp"
#include "host_encode_admission.hpp"

#include <deque>
#include <string>
#include <thread>
#include <vector>

using namespace bakeoff;
using remote60::native_poc::RateGovernor;
using remote60::native_poc::RateDecision;
using remote60::native_poc::EncodeAdmission;
using remote60::native_poc::EncodeAdmissionInputs;
using remote60::native_poc::decide_encode_admission;

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::string backend = argc > 1 ? argv[1] : "mft_hw";
  const uint32_t rateKbps = argc > 2 ? (uint32_t)std::atoi(argv[2]) : 3000u;
  const uint32_t fps = argc > 3 ? (uint32_t)std::max(1, std::atoi(argv[3])) : 30u;
  const uint32_t phaseSec = argc > 4 ? (uint32_t)std::max(2, std::atoi(argv[4])) : 8u;
  const bool capOn = !(argc > 5 && std::string(argv[5]) == "off");
  const uint32_t bitrate = rateKbps * 1000u;
  const uint32_t keyintFrames = 10u * fps;

  std::printf("host_encoder_product_trace: backend=%s rate=%uk fps=%u phase=%us cap=%s "
              "(governor+admission+wire-limiter, idle->motion->idle)\n",
              backend.c_str(), rateKbps, fps, phaseSec, capOn ? "ON" : "OFF(control)");
  MFStartup(MF_VERSION);

  _putenv_s("REMOTE60_NATIVE_MAX_QP", "");  // product default ceiling; the governor moves it live
  auto enc = make_encoder(backend);
  if (!enc || !enc->init(kW, kH, fps, bitrate, keyintFrames)) { std::printf("init failed\n"); return 2; }
  std::printf("backend=%s vbv~200ms targetBps=%u\n", enc->backend_name(), bitrate);

  RateGovernor gov;
  std::vector<uint8_t> nv12(kNv12, 128);

  // single-threaded wire model: credit accrues R/fps bytes per frame; the 2-AU sender queue holds
  // remaining bytes per queued AU; the gate reads queue depth before each encode.
  std::deque<uint32_t> queue;            // remaining bytes per queued AU
  double wireCredit = 0.0;
  const double bytesPerFrame = double(bitrate) / 8.0 / fps;
  uint32_t curMaxQp = 32, curFps = fps, fpsAccumulator = 0;
  double avgSentFrameBytes = bytesPerFrame;

  const uint32_t totalFrames = 3 * phaseSec * fps;
  uint64_t secGenBytes = 0, secSentBytes = 0; uint32_t secSkips = 0, secEncoded = 0;
  std::printf("\n sec | phase  | tgtKbps | genKbps(est) sentKbps | encoded skips qDepth | maxQp fps | note\n");

  uint64_t frameUs = 1'000'000ULL / fps;
  uint64_t deadline = now_us();
  for (uint32_t i = 0; i < totalFrames; ++i) {
    deadline += frameUs;
    const uint32_t phase = i / (phaseSec * fps);              // 0 idle, 1 motion, 2 idle
    const char* phaseName = phase == 1 ? "motion" : "idle";
    const std::string scene = phase == 1 ? "motion" : "static";
    fill_scene(scene, nv12, i);

    // frame-rate step: encode only enough frames to hit curFps (governor may drop below user fps).
    fpsAccumulator += curFps;
    const bool fpsTick = fpsAccumulator >= fps;
    if (fpsTick) fpsAccumulator -= fps;

    const bool key = (i == 0);
    bool encoded = false;
    if (fpsTick) {
      EncodeAdmissionInputs ad;
      ad.wireCapActive = capOn;
      ad.keyWanted = key;
      ad.senderQueueDepth = static_cast<uint32_t>(queue.size());
      ad.senderQueueMax = 2;
      if (decide_encode_admission(ad) == EncodeAdmission::SkipOverloaded) {
        ++secSkips;
      } else {
        std::vector<H264AccessUnit> units;
        enc->encode(nv12, key, (int64_t)i * (int64_t)(10'000'000LL / fps), &units);
        for (const auto& u : units) {
          const uint32_t sz = (uint32_t)u.bytes.size();
          queue.push_back(sz);
          secGenBytes += sz;
          encoded = true;
        }
      }
    }
    if (encoded) ++secEncoded;

    // wire drains at the user bitrate
    wireCredit += bytesPerFrame;
    while (!queue.empty() && wireCredit > 0) {
      if (queue.front() <= wireCredit) {
        wireCredit -= queue.front();
        secSentBytes += queue.front();
        avgSentFrameBytes = avgSentFrameBytes * 0.9 + queue.front() * 0.1;
        queue.pop_front();
      } else {
        queue.front() -= (uint32_t)wireCredit;
        secSentBytes += (uint32_t)wireCredit;
        wireCredit = 0;
      }
    }

    if ((i + 1) % fps == 0) {  // one second
      // The product feeds the governor the GENERATED-load estimate (sent + skipped*avgSentFrame), NOT
      // raw sent -- otherwise the cap pins sent at target and the governor never sees the overshoot
      // (host_stage_stats_h264.cpp:183,208). Match that so the QP/fps ladder reacts as it does in the field.
      const uint64_t generatedBytes = (uint64_t)(secSentBytes + secSkips * avgSentFrameBytes);
      const uint64_t genKbpsEst = (uint64_t)(generatedBytes * 8.0 / 1000.0);
      const uint64_t sentKbps = secSentBytes * 8 / 1000;
      const RateDecision dec = gov.OnSecond(generatedBytes, bitrate, fps);
      const char* note = "";
      if (dec.maxQp != curMaxQp) { bool ok = enc->set_max_qp(dec.maxQp); note = ok ? "qp-applied" : "qp-set-FAILED"; }
      if (dec.fps != curFps) note = "fps-step";
      std::printf("%4u | %-6s | %7u | %11llu %8llu | %7u %5u %6zu | %5u %3u | %s\n",
                  (i + 1) / fps, phaseName, rateKbps, (unsigned long long)genKbpsEst,
                  (unsigned long long)sentKbps, secEncoded, secSkips, queue.size(),
                  dec.maxQp, dec.fps, note);
      curMaxQp = dec.maxQp; curFps = dec.fps;
      secGenBytes = secSentBytes = 0; secSkips = secEncoded = 0;
    }

    const uint64_t n = now_us();
    if (n < deadline) std::this_thread::sleep_for(std::chrono::microseconds(deadline - n));
  }
  enc->shutdown();
  std::printf("\nhost_encoder_product_trace: DONE\n");
  MFShutdown();
  return 0;
}
