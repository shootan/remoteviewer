// NVIDIA rate-control probe (encoder-bakeoff r2) -- MEASUREMENT-ONLY, hand to the NVIDIA machine.
//
// This PC is AMD; the stutter incident is on NVIDIA, where the NAS logs show CODECAPI_AVEncVideoMaxQP
// reads back 51 from init regardless of the 32..40 requested. A single, self-contained executable the
// user can run on the NVIDIA PC (no install; ~40 s; one result file via stdout redirect) to separate
// "reported vs applied": for each cold-init combo it (1) lets the product encoder print its own
// "[native-video-host] h264 rate-control ... readbackMaxQp=.." line (REPORTED), then (2) encodes a
// short fixed clip and prints avg kbps / 1 s peak / IDR size (APPLIED EFFECT). If avg/peak do not move
// across MaxQP 32/40/44/51, the QP lever is inert on that encoder; the CBR/VBR and QualityVsSpeed rows
// show what, if anything, does bind the bitrate.
//
// Build: self-contained (no OpenH264); drives the product H264Encoder (whatever HW MFT the PC has).
// Run on the NVIDIA PC:   host_nvidia_probe.exe > nvidia_probe_result.txt
// It changes no product default (sets only this process's own env for each probe) and uses the GPU
// encoder only briefly; ask the user to run it when they are not mid-measurement.

#include "bakeoff_common.hpp"

#include <string>
#include <thread>
#include <vector>

using namespace bakeoff;

namespace {
struct Combo { const char* rc; uint32_t coldQp; const char* qualityVsSpeed; };

void probe(const Combo& c, uint32_t rateKbps, uint32_t fps, uint32_t seconds) {
  const uint32_t bitrate = rateKbps * 1000u;
  const uint32_t keyintFrames = 10u * fps;
  _putenv_s("REMOTE60_NATIVE_ENCODER_BACKEND", "mft_hw");     // the PC's hardware MFT (NVIDIA here)
  _putenv_s("REMOTE60_NATIVE_RATE_CONTROL", c.rc);            // cbr / (empty = product VBR default)
  _putenv_s("REMOTE60_NATIVE_MAX_QP", std::to_string(c.coldQp).c_str());
  if (c.qualityVsSpeed && *c.qualityVsSpeed) _putenv_s("REMOTE60_NATIVE_QUALITY_VS_SPEED", c.qualityVsSpeed);
  else _putenv_s("REMOTE60_NATIVE_QUALITY_VS_SPEED", "");

  std::printf("\n=== PROBE rc=%s coldMaxQp=%u qualityVsSpeed=%s target=%uk fps=%u ===\n",
              c.rc && *c.rc ? c.rc : "vbr(default)", c.coldQp,
              c.qualityVsSpeed && *c.qualityVsSpeed ? c.qualityVsSpeed : "(default)", rateKbps, fps);
  // The encoder prints its own mft-identity + rate-control readback lines here (REPORTED values):
  MftEnc enc;
  if (!enc.init(kW, kH, fps, bitrate, keyintFrames)) { std::printf("  init FAILED\n"); return; }

  std::vector<uint8_t> nv12(kNv12, 128);
  const uint32_t warmup = fps, body = seconds * fps;
  std::vector<uint32_t> bytesByFrame(body, 0); uint32_t idrMax = 0;
  const uint64_t frameUs = 1'000'000ULL / fps;
  uint64_t deadline = now_us();
  for (uint32_t i = 0; i < warmup + body; ++i) {
    deadline += frameUs;
    fill_scene("motion", nv12, i);  // motion: the case the incident stutters on
    std::vector<H264AccessUnit> units;
    enc.encode(nv12, i == 0, (int64_t)i * (int64_t)(10'000'000LL / fps), &units);
    for (const auto& u : units) {
      const int64_t fidx = u.sampleTimeHns / (int64_t)(10'000'000LL / fps);
      if (fidx >= (int64_t)warmup && fidx < (int64_t)(warmup + body)) {
        bytesByFrame[(size_t)(fidx - warmup)] += (uint32_t)u.bytes.size();
        if (u.rawIdr) idrMax = std::max(idrMax, (uint32_t)u.bytes.size());
      }
    }
    const uint64_t n = now_us();
    if (n < deadline) std::this_thread::sleep_for(std::chrono::microseconds(deadline - n));
  }
  enc.shutdown();
  uint64_t total = 0; uint64_t win1 = 0, acc = 0;
  for (size_t k = 0; k < bytesByFrame.size(); ++k) {
    total += bytesByFrame[k]; acc += bytesByFrame[k];
    if (k >= fps) acc -= bytesByFrame[k - fps];
    if (k + 1 >= fps) win1 = std::max(win1, acc);
  }
  const double sec = double(bytesByFrame.size()) / fps;
  std::printf("  APPLIED EFFECT: avgKbps=%.0f (x%.2f target)  1sPeakKbps=%.0f (x%.2f)  idrMaxBytes=%u\n",
              total * 8.0 / 1000.0 / sec, (total * 8.0 / 1000.0 / sec) / rateKbps,
              win1 * 8.0 / 1000.0, (win1 * 8.0 / 1000.0) / rateKbps, idrMax);
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("host_nvidia_probe (encoder-bakeoff r2): MaxQP reported-vs-applied + RC-key probe\n");
  std::printf("Run on the NVIDIA PC. One result file. Reads back the encoder's own rate-control log +\n");
  std::printf("measures whether avg/peak move across MaxQP 32/40/44/51 and CBR/VBR/QualityVsSpeed.\n");
  MFStartup(MF_VERSION);
  const uint32_t rate = 3000, fps = 30, sec = 3;  // ~40s total across the combos below
  // MaxQP sweep at the target the incident used (3 Mbps), product VBR:
  for (uint32_t qp : {32u, 40u, 44u, 51u}) probe(Combo{"", qp, ""}, rate, fps, sec);
  // Mode / quality levers at the product default ceiling:
  probe(Combo{"cbr", 32u, ""}, rate, fps, sec);
  probe(Combo{"", 32u, "33"}, rate, fps, sec);   // lower QualityVsSpeed (favour speed/smaller)
  std::printf("\nhost_nvidia_probe: DONE. Send this whole file back.\n");
  MFShutdown();
  return 0;
}
