// motion-settle r1 M0: H264SliceQpReader against real encoders on this machine.
//   - every AU of a short stream (a changing picture, then the same picture repeated) yields a slice QP;
//   - where the MFT also attaches MFSampleExtension_VideoEncodeQP, the two agree on single-slice
//     pictures (the slice QP is the slice's starting QP; an encoder that varies QP per macroblock may
//     report another figure -- counted, not asserted);
//   - under a quantiser ceiling the P slices never start above it.
// Runs the default (hardware if present) and the Microsoft software encoder. gpu / pure-logic.

#include "h264_slice_qp.hpp"
#include "mf_h264_codec.hpp"

#include <windows.h>
#include <mfapi.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace remote60::native_poc;

namespace {
int gFailures = 0, gChecks = 0;
void expect(bool ok, const std::string& what) {
  ++gChecks;
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++gFailures;
}

void RunOne(const char* backend) {
  if (backend) SetEnvironmentVariableA("REMOTE60_NATIVE_ENCODER_BACKEND", backend);
  else SetEnvironmentVariableA("REMOTE60_NATIVE_ENCODER_BACKEND", nullptr);
  const uint32_t w = 640, h = 368;
  H264Encoder enc;
  if (!enc.initialize(w, h, 30, 1500000, 120)) {
    std::printf("SKIP %s: encoder initialize failed\n", backend ? backend : "default");
    return;
  }
  const std::string name = std::string(backend ? backend : "default") + "(" + enc.backend_name() + ")";
  std::vector<uint8_t> nv12(static_cast<size_t>(w) * h * 3 / 2, 128);
  int aus = 0, readable = 0, withAttr = 0, agree = 0, pOverCeiling = 0;
  const int ceiling = static_cast<int>(enc.plan_max_qp());
  for (int f = 0; f < 90; ++f) {
    if (f < 45) {  // a moving pattern, then 45 identical frames (the "still" a refinement re-encodes)
      for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) nv12[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(((x + f * 7) ^ (y * 3)) & 0xFF);
    }
    std::vector<H264AccessUnit> units;
    enc.encode_frame(nv12, f == 0, static_cast<int64_t>(f) * 333333, &units);
    for (const H264AccessUnit& au : units) {
      ++aus;
      if (au.sliceQp >= 0) ++readable;
      if (au.encodeQp >= 0) {
        ++withAttr;
        if (au.encodeQp == au.sliceQp) ++agree;
      }
      if (!au.keyFrame && ceiling > 0 && au.sliceQp > ceiling) ++pOverCeiling;
    }
  }
  std::printf("  %s: aus=%d sliceQpReadable=%d withVideoEncodeQP=%d agree=%d ceiling=%d pOverCeiling=%d\n", name.c_str(), aus,
              readable, withAttr, agree, ceiling, pOverCeiling);
  expect(aus > 0 && readable == aus, name + ": every AU's slice QP is read from its header");
  if (withAttr > 0) expect(agree * 10 >= withAttr * 9, name + ": slice QP agrees with VideoEncodeQP on >= 90% of AUs");
  if (ceiling > 0) expect(pOverCeiling == 0, name + ": no P slice starts above the quantiser ceiling");
  enc.shutdown();
}
}  // namespace

int main() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION);
  RunOne(nullptr);
  RunOne("mft_sw");
  MFShutdown();
  CoUninitialize();
  std::printf("h264_slice_qp_test: %s (%d checks, %d failed)\n", gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
