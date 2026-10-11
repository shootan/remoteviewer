// HW vs SW H.264 encode cost at 1080p, 30 fps PACED (stutter-keyframe r3 B, corrected per Codex).
//
// The earlier version fed inputs flat-out, so SW's "~3.5 cores" was the SATURATED utilisation, not
// the 30 fps steady-state cost. This paces the encoder at a real 30 fps (absolute frame deadlines, no
// busy-spin so the idle time is not counted as CPU) for `seconds` and reports what the clamp->SW
// decision actually needs:
//   - cpuCores  = process CPU-seconds per wall-second (kernel+user) -- the real steady-state core use.
//   - cpuMsPerOutFrame = process CPU-ms per emitted AU.
//   - encP50/P95/Max = per encode_frame() call wall time.
// Plus the clamp->SW transition's REAL wall-clock first-output latency, also paced.
//
// Backend forced through the product selector (REMOTE60_NATIVE_ENCODER_BACKEND = mft_hw | mft_sw);
// each row is the real production encoder path. Measurement tool (prints a table), exit 0.
// Usage: remote60_host_encoder_backend_bench [secondsPerConfig]  (default 60)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>

#include "mf_h264_codec.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

void fill(std::vector<uint8_t>& nv12, uint32_t i) {
  for (uint32_t y = 0; y < kH; ++y) {
    uint8_t* row = &nv12[static_cast<size_t>(y) * kW];
    for (uint32_t x = 0; x < kW; ++x) row[x] = static_cast<uint8_t>((x + y + i * 3) & 0xFF);
  }
  const uint32_t bx = (i * 11) % (kW - 200), by = (i * 7) % (kH - 200);
  for (uint32_t y = by; y < by + 200; ++y)
    for (uint32_t x = bx; x < bx + 200; ++x) nv12[static_cast<size_t>(y) * kW + x] = 235;
  std::fill(nv12.begin() + static_cast<size_t>(kW) * kH, nv12.end(), static_cast<uint8_t>(128));
}

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

struct Row { bool ok=false; double cpuCores=0, cpuMsPerFrame=0; uint64_t p50=0,p95=0,mx=0; uint32_t outFrames=0, inFrames=0; };

Row bench_paced(const char* backend, uint32_t bitrate, uint32_t seconds) {
  Row r;
  _putenv_s("REMOTE60_NATIVE_ENCODER_BACKEND", backend);
  H264Encoder enc;
  if (!enc.initialize(kW, kH, kFps, bitrate, 300)) return r;
  std::vector<uint8_t> nv12(static_cast<size_t>(kW) * kH * 3 / 2, 128);
  const uint32_t nframes = seconds * kFps;
  std::vector<uint64_t> per; per.reserve(nframes);
  uint64_t cpu0 = 0, wall0 = 0, deadline = 0;
  const uint32_t kWarmup = kFps;  // 1s warm-up (SW buffers its first outputs); not in CPU/enc stats
  for (uint32_t i = 0; i < nframes + kWarmup; ++i) {
    if (i == kWarmup) { cpu0 = proc_cpu_us(); wall0 = now_us(); deadline = now_us(); }
    deadline += kFrameUs;
    fill(nv12, i);
    std::vector<H264AccessUnit> u;
    const uint64_t t0 = now_us();
    if (!enc.encode_frame(nv12, i == 0, static_cast<int64_t>(now_us()) * kHnsPerUs, &u)) { enc.shutdown(); return r; }
    const uint64_t dt = now_us() - t0;
    if (i >= kWarmup) { per.push_back(dt); r.outFrames += static_cast<uint32_t>(u.size()); ++r.inFrames; }
    const uint64_t n = now_us();
    if (n < deadline) std::this_thread::sleep_for(std::chrono::microseconds(deadline - n));  // idle, not CPU
  }
  const uint64_t wall = now_us() - wall0;
  const uint64_t cpu = proc_cpu_us() - cpu0;
  enc.shutdown();
  if (per.empty() || wall == 0) return r;
  std::sort(per.begin(), per.end());
  r.ok = true;
  r.cpuCores = static_cast<double>(cpu) / static_cast<double>(wall);
  r.cpuMsPerFrame = r.outFrames ? (static_cast<double>(cpu) / 1000.0 / r.outFrames) : 0.0;
  r.p50 = per[per.size() / 2];
  r.p95 = per[(per.size() * 95) / 100];
  r.mx = per.back();
  return r;
}

// (The clamp->SW transition-latency measurement was removed with the SW auto-transition: the user
// chose option 1, IDR burst, so the SW path is not a shipping response. The HW-vs-SW 30fps paced CPU
// table below remains as the corrected record -- backend selected via the env, no runtime seam.)
}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const uint32_t seconds = argc > 1 ? static_cast<uint32_t>(std::max(4, std::atoi(argv[1]))) : 60u;
  std::printf("host_encoder_backend_bench (1080p %ufps PACED, %us/config)\n", kFps, seconds);
  MFStartup(MF_VERSION);
  const uint32_t rates[] = {1500000u, 3000000u, 6000000u};
  const char* backends[] = {"mft_hw", "mft_sw"};
  std::printf("  %-7s %-9s %8s %10s %8s %8s %8s %7s\n", "backend", "bitrate", "cpuCores", "cpuMs/frm",
              "encP50us", "encP95us", "encMaxus", "outFrm");
  for (uint32_t br : rates) {
    for (const char* b : backends) {
      const Row r = bench_paced(b, br, seconds);
      if (!r.ok) { std::printf("  %-7s %-9u  INIT/ENCODE FAILED\n", b, br); continue; }
      std::printf("  %-7s %-9u %8.2f %10.2f %8llu %8llu %8llu %7u\n", b, br, r.cpuCores, r.cpuMsPerFrame,
                  (unsigned long long)r.p50, (unsigned long long)r.p95, (unsigned long long)r.mx, r.outFrames);
    }
  }
  std::printf("host_encoder_backend_bench: DONE\n");
  MFShutdown();
  return 0;
}
