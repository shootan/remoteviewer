// NVIDIA rate-control probe (encoder-bakeoff r3) -- MEASUREMENT-ONLY, hand to the NVIDIA machine.
//
// This PC is AMD; the stutter incident is on NVIDIA, where the NAS logs show CODECAPI_AVEncVideoMaxQP
// reading back 51 regardless of 32..40 requested. A single self-contained exe (no OpenH264; ~1.5 min;
// one result file via stdout redirect) to separate "reported vs applied" rate control.
//
// r3 fixes (Codex / verifier): the probe now runs AMD's strong QP-discriminating conditions
// (static/scroll/drag @1.5 Mbps, where cold MaxQP 32 overshoots ~5x and 44 honors the target) so the
// SAME exe run on THIS PC (AMD) is a POSITIVE CONTROL that 32->44 really diverges; motion@3M (where
// even qp32 is ~1.0x, hence non-discriminating) is kept only as a reference row. Each row pins the
// FULL set of product-read REMOTE60_NATIVE_* rate-control vars (no inherited override leaks in) and
// prints the inherited values at start. A raw IDR falls inside the measured window. A row with an
// encode failure / no output / leftover pending / an output-fps shortfall is marked INVALID, not
// folded into DONE. "No difference across MaxQP" is an observation of THIS run's condition, not a
// universal proof that the lever is inert.
//
// Run on the target PC:   host_nvidia_probe.exe > nvidia_probe_result.txt   (send back the whole file)

#include "bakeoff_common.hpp"

#include <algorithm>
#include <string>
#include <thread>
#include <vector>

using namespace bakeoff;

namespace {

// Every product-read rate-control env var (mf_h264_codec.cpp). Pinned per row so nothing inherited leaks.
const char* kRcEnv[] = {
    "REMOTE60_NATIVE_ENCODER_BACKEND", "REMOTE60_NATIVE_RATE_CONTROL", "REMOTE60_NATIVE_RC_MODE_NUM",
    "REMOTE60_NATIVE_RC_MEAN_OVERRIDE", "REMOTE60_NATIVE_PEAK_BITRATE_PERCENT", "REMOTE60_NATIVE_MAX_QP",
    "REMOTE60_NATIVE_QUALITY_VS_SPEED", "REMOTE60_NATIVE_ENCODER_TUNE_MODE", "REMOTE60_NATIVE_RC_BEFORE_TYPES",
    "REMOTE60_NATIVE_LOW_LATENCY", "REMOTE60_NATIVE_REALTIME"};

std::string env_get(const char* n) { const char* v = std::getenv(n); return v ? v : ""; }

struct Row { const char* label; const char* scene; uint32_t rateKbps; uint32_t coldQp; const char* rc; const char* qvs; };

void probe(const Row& r, uint32_t fps, uint32_t seconds) {
  // Pin the full RC env set: this row's values, everything else cleared to the product default.
  for (const char* n : kRcEnv) _putenv_s(n, "");
  _putenv_s("REMOTE60_NATIVE_ENCODER_BACKEND", "mft_hw");         // the PC's hardware MFT (NVIDIA there)
  _putenv_s("REMOTE60_NATIVE_MAX_QP", std::to_string(r.coldQp).c_str());
  if (r.rc && *r.rc) _putenv_s("REMOTE60_NATIVE_RATE_CONTROL", r.rc);
  if (r.qvs && *r.qvs) _putenv_s("REMOTE60_NATIVE_QUALITY_VS_SPEED", r.qvs);

  const uint32_t bitrate = r.rateKbps * 1000u;
  const uint32_t keyintFrames = 4u * fps;   // a raw IDR ~every 4s -> at least one inside the 8s body
  std::printf("\n=== %s | scene=%s target=%uk coldMaxQp=%u rc=%s qvs=%s ===\n", r.label, r.scene,
              r.rateKbps, r.coldQp, (r.rc && *r.rc) ? r.rc : "vbr(default)",
              (r.qvs && *r.qvs) ? r.qvs : "(default)");
  // the encoder prints its own mft-identity + "rate-control ... readbackMaxQp=.." here (REPORTED):
  MftEnc enc;
  if (!enc.init(kW, kH, fps, bitrate, keyintFrames)) { std::printf("  RESULT: INVALID (init failed)\n"); return; }

  std::vector<uint8_t> nv12(kNv12, 128);
  const uint32_t warmup = fps, body = seconds * fps;
  std::vector<uint32_t> bytesByFrame(body, 0);
  uint32_t idrMax = 0, idrCount = 0, returned = 0; bool encFail = false;
  const uint64_t frameUs = 1'000'000ULL / fps;
  uint64_t deadline = now_us(), wall0 = 0;
  for (uint32_t i = 0; i < warmup + body; ++i) {
    if (i == warmup) wall0 = now_us();
    deadline += frameUs;
    fill_scene(r.scene, nv12, i);
    std::vector<H264AccessUnit> units;
    if (!enc.encode(nv12, i == 0, (int64_t)i * (int64_t)(10'000'000LL / fps), &units)) { encFail = true; break; }
    for (const auto& u : units) {
      const int64_t fidx = u.sampleTimeHns / (int64_t)(10'000'000LL / fps);
      if (fidx >= (int64_t)warmup && fidx < (int64_t)(warmup + body)) {
        bytesByFrame[(size_t)(fidx - warmup)] += (uint32_t)u.bytes.size();
        ++returned;
        if (u.rawIdr) { idrMax = std::max(idrMax, (uint32_t)u.bytes.size()); ++idrCount; }
      }
    }
    const uint64_t n = now_us();
    if (n < deadline) std::this_thread::sleep_for(std::chrono::microseconds(deadline - n));
  }
  const double wallSec = (now_us() - wall0) / 1e6;
  const uint32_t pending = enc.pending_depth();
  enc.shutdown();

  uint64_t total = 0, win1 = 0, acc1 = 0, win267 = 0, acc267 = 0;
  const uint32_t w267 = std::max(1u, (fps + 2) / 4);  // 8 frames @30 = 266.7ms
  for (size_t k = 0; k < bytesByFrame.size(); ++k) {
    total += bytesByFrame[k];
    acc1 += bytesByFrame[k]; if (k >= fps) acc1 -= bytesByFrame[k - fps]; if (k + 1 >= fps) win1 = std::max(win1, acc1);
    acc267 += bytesByFrame[k]; if (k >= w267) acc267 -= bytesByFrame[k - w267]; if (k + 1 >= w267) win267 = std::max(win267, acc267);
  }
  const double avg = total * 8.0 / 1000.0 / seconds;
  const double p1 = win1 * 8.0 / 1000.0;
  const double p267 = win267 * 8.0 / 1000.0 / (double(w267) / fps);
  const double outFps = wallSec > 0 ? returned / wallSec : 0;
  const bool fpsShort = outFps < 0.9 * fps;
  const bool valid = !encFail && returned > 0 && !fpsShort;  // pending reported separately (async tail)
  std::printf("  avgKbps=%.0f (x%.2f)  1sPeak=%.0f (x%.2f)  267msPeak=%.0f (x%.2f)  idrMax=%u idrN=%u  "
              "outFps=%.1f pending=%u  RESULT: %s%s\n",
              avg, avg / r.rateKbps, p1, p1 / r.rateKbps, p267, p267 / r.rateKbps, idrMax, idrCount,
              outFps, pending, valid ? "VALID" : "INVALID",
              encFail ? "(encode-false)" : returned == 0 ? "(no-output)" : fpsShort ? "(fps-short)" : "");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("host_nvidia_probe (encoder-bakeoff r3): MaxQP reported-vs-applied + RC-key probe\n");
  std::printf("Run on the target PC. This exe is the POSITIVE CONTROL when run on AMD (32->44 must\n");
  std::printf("diverge on static/scroll/drag@1.5M). 'No difference' is this run's observation only.\n");
  std::printf("\nInherited REMOTE60_NATIVE_* at start (pinned per row below; verify no secrets):\n");
  for (const char* n : kRcEnv) { const std::string v = env_get(n); std::printf("  %s=%s\n", n, v.empty() ? "(unset)" : v.c_str()); }

  MFStartup(MF_VERSION);
  const uint32_t fps = 30, sec = 8;  // body >= 8s; ~10 rows -> ~1.5 min
  const Row rows[] = {
      // POSITIVE CONTROL + QP sweep: on AMD 32 overshoots ~5x, 44 honors. On NVIDIA, do they diverge?
      {"QPsweep",   "drag",   1500, 32, "", ""},
      {"QPsweep",   "drag",   1500, 40, "", ""},
      {"QPsweep",   "drag",   1500, 44, "", ""},
      {"QPsweep",   "drag",   1500, 51, "", ""},
      {"control",   "static", 1500, 32, "", ""},
      {"control",   "static", 1500, 44, "", ""},
      {"control",   "scroll", 1500, 32, "", ""},
      {"control",   "scroll", 1500, 44, "", ""},
      // REFERENCE: motion@3M (the verifier's smoke condition -- non-discriminating, qp32 already ~1.0x).
      {"reference", "motion", 3000, 32, "", ""},
      {"reference", "motion", 3000, 44, "", ""},
      // RC-KEY probe at the discriminating condition: does CBR or a lower QualityVsSpeed bind it?
      {"rc-key",    "drag",   1500, 32, "cbr", ""},
      {"rc-key",    "drag",   1500, 32, "",   "33"},
  };
  uint32_t nValid = 0, nTotal = 0;
  for (const auto& r : rows) { ++nTotal; probe(r, fps, sec); }  // per-row VALID/INVALID printed above
  std::printf("\nhost_nvidia_probe: DONE. %u rows. Read each row's RESULT: VALID/INVALID and compare the\n", nTotal);
  std::printf("avg/peak across coldMaxQp 32/40/44/51 for scene=drag/static/scroll@1.5M (the control).\n");
  std::printf("Send this whole file back.\n");
  (void)nValid;
  MFShutdown();
  return 0;
}
