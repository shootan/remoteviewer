// HW vs SW H.264 encode measurement at 1080p30 (stutter-keyframe r3 B). The clamp fix candidate is a
// one-time SW-encoder transition on a clamping MFT; r3 requires measuring HW vs SW CPU / encode
// p50/p95 / first-frame on THIS PC first so the transition is a measured decision, not a guess. This
// is a measurement tool (prints a table, exit 0 unless a backend fails to initialize), not a PASS/FAIL
// gate -- the numbers go in the report.
//
// Backend is forced through the product's own selector (REMOTE60_NATIVE_ENCODER_BACKEND = mft_hw |
// mft_sw), so each row is the real production encoder path, not a stub.

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
#include <vector>

using namespace remote60::native_poc;

namespace {
constexpr uint32_t kW = 1920;
constexpr uint32_t kH = 1080;
constexpr uint32_t kFps = 30;
constexpr int64_t kHnsPerUs = 10;

// A moving 1080p picture: a diagonal gradient that scrolls, plus a moving bright block. Non-trivial to
// encode (real motion each frame), not pure noise (which would be an unrealistic worst case).
void fill(std::vector<uint8_t>& nv12, uint32_t i) {
  for (uint32_t y = 0; y < kH; ++y) {
    uint8_t* row = &nv12[static_cast<size_t>(y) * kW];
    for (uint32_t x = 0; x < kW; ++x) row[x] = static_cast<uint8_t>((x + y + i * 3) & 0xFF);
  }
  const uint32_t bx = (i * 11) % (kW - 200), by = (i * 7) % (kH - 200);
  for (uint32_t y = by; y < by + 200; ++y)
    for (uint32_t x = bx; x < bx + 200; ++x) nv12[static_cast<size_t>(y) * kW + x] = 235;
  // UV plane (second third) left mid-grey.
  std::fill(nv12.begin() + static_cast<size_t>(kW) * kH, nv12.end(), static_cast<uint8_t>(128));
}

uint64_t proc_cpu_us() {
  FILETIME c{}, e{}, k{}, u{};
  if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
  const auto to_us = [](const FILETIME& f) {
    ULARGE_INTEGER v;
    v.LowPart = f.dwLowDateTime;
    v.HighPart = f.dwHighDateTime;
    return v.QuadPart / 10ULL;  // 100ns -> us
  };
  return to_us(k) + to_us(u);
}

uint64_t now_us() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

struct Row { bool ok=false; uint64_t p50=0,p95=0,mx=0,first=0; double cpuPct=0; uint32_t frames=0; };

Row bench(const char* backend, uint32_t bitrate, uint32_t frames) {
  Row r;
  _putenv_s("REMOTE60_NATIVE_ENCODER_BACKEND", backend);
  H264Encoder enc;
  if (!enc.initialize(kW, kH, kFps, bitrate, 300)) return r;
  std::printf("    backend=%s actual=%s\n", backend, enc.backend_name());
  std::vector<uint8_t> nv12(static_cast<size_t>(kW) * kH * 3 / 2, 128);
  std::vector<uint64_t> per;
  per.reserve(frames);
  const uint64_t cpu0 = proc_cpu_us();
  const uint64_t wall0 = now_us();
  for (uint32_t i = 0; i < frames; ++i) {
    fill(nv12, i);
    std::vector<H264AccessUnit> units;
    const uint64_t t0 = now_us();
    const bool ok = enc.encode_frame(nv12, i == 0, static_cast<int64_t>(now_us()) * kHnsPerUs, &units);
    const uint64_t dt = now_us() - t0;
    if (!ok) { enc.shutdown(); return r; }
    per.push_back(dt);
    if (i == 0) r.first = dt;
  }
  const uint64_t wall = now_us() - wall0;
  const uint64_t cpu = proc_cpu_us() - cpu0;
  enc.shutdown();
  std::sort(per.begin(), per.end());
  r.ok = true;
  r.frames = frames;
  r.p50 = per[per.size() / 2];
  r.p95 = per[(per.size() * 95) / 100];
  r.mx = per.back();
  r.cpuPct = wall > 0 ? (100.0 * static_cast<double>(cpu) / static_cast<double>(wall)) : 0.0;
  return r;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("host_encoder_backend_bench (1080p30 HW vs SW)\n");
  MFStartup(MF_VERSION);
  const uint32_t rates[] = {1500000u, 3000000u, 6000000u};
  const char* backends[] = {"mft_hw", "mft_sw"};
  std::printf("  %-7s %-9s %8s %8s %8s %8s %8s\n", "backend", "bitrate", "encP50us", "encP95us",
              "encMaxus", "firstus", "cpu%%");
  for (uint32_t br : rates) {
    for (const char* b : backends) {
      const Row r = bench(b, br, 150);
      if (!r.ok) { std::printf("  %-7s %-9u  INIT/ENCODE FAILED (may be unavailable)\n", b, br); continue; }
      std::printf("  %-7s %-9u %8llu %8llu %8llu %8llu %7.1f\n", b, br,
                  (unsigned long long)r.p50, (unsigned long long)r.p95, (unsigned long long)r.mx,
                  (unsigned long long)r.first, r.cpuPct);
    }
  }
  MFShutdown();
  std::printf("host_encoder_backend_bench: DONE\n");
  return 0;
}
