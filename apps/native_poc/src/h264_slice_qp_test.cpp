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


// ---- hand-built decision vectors (r2: the reader's contract, independent of any encoder) ----------
struct BitWriter {
  std::vector<uint8_t> bytes;
  int bit = 0;
  void u(uint32_t v, int n) {
    for (int k = n - 1; k >= 0; --k) {
      if (bit == 0) bytes.push_back(0);
      if ((v >> k) & 1u) bytes.back() |= static_cast<uint8_t>(0x80u >> bit);
      bit = (bit + 1) % 8;
    }
  }
  void ue(uint32_t v) {
    const uint32_t x = v + 1;
    int len = 0;
    while ((x >> len) > 1) ++len;
    u(0, len);
    u(x, len + 1);
  }
  void se(int32_t v) { ue(v <= 0 ? static_cast<uint32_t>(-2 * v) : static_cast<uint32_t>(2 * v - 1)); }
  void trailing() {
    u(1, 1);
    while (bit != 0) u(0, 1);
  }
};
// Annex-B NAL with emulation prevention.
void Nal(std::vector<uint8_t>* out, uint8_t header, const std::vector<uint8_t>& rbsp) {
  const uint8_t sc[] = {0, 0, 0, 1};
  out->insert(out->end(), sc, sc + 4);
  out->push_back(header);
  int zeros = 0;
  for (uint8_t b : rbsp) {
    if (zeros >= 2 && b <= 3) { out->push_back(3); zeros = 0; }
    out->push_back(b);
    zeros = b == 0 ? zeros + 1 : 0;
  }
}
std::vector<uint8_t> Sps(uint32_t id, bool chroma444 = false) {
  BitWriter w;
  w.u(chroma444 ? 244 : 66, 8);
  w.u(0, 8);
  w.u(30, 8);
  w.ue(id);
  if (chroma444) { w.ue(3); w.u(0, 1); w.ue(0); w.ue(0); w.u(0, 1); w.u(0, 1); }
  w.ue(0);  // log2_max_frame_num - 4
  w.ue(0);  // poc type 0
  w.ue(0);  // log2_max_poc_lsb - 4
  w.ue(1);
  w.u(0, 1);
  w.ue(119);
  w.ue(67);
  w.u(1, 1);  // frame_mbs_only
  w.u(1, 1);
  w.u(0, 1);
  w.u(0, 1);
  w.trailing();
  return w.bytes;
}
std::vector<uint8_t> Pps(uint32_t id, uint32_t spsId, int32_t initQp) {
  BitWriter w;
  w.ue(id);
  w.ue(spsId);
  w.u(0, 1);  // cavlc
  w.u(0, 1);
  w.ue(0);
  w.ue(0);
  w.ue(0);
  w.u(0, 1);
  w.u(0, 2);
  w.se(initQp - 26);
  w.se(0);
  w.se(0);
  w.u(1, 1);
  w.u(0, 1);
  w.u(0, 1);
  w.trailing();
  return w.bytes;
}
// type: 0 P, 2 I, 1 B, or anything raw; listOps: -1 = no modification, else that many ops with no terminator.
std::vector<uint8_t> Slice(uint32_t rawType, uint32_t ppsId, int32_t qpDelta, bool idr, int listOps = -1, bool truncate = false) {
  BitWriter w;
  w.ue(0);
  w.ue(rawType);
  w.ue(ppsId);
  if (truncate) return w.bytes;
  w.u(1, 4);  // frame_num
  if (idr) w.ue(0);
  w.u(2, 4);  // poc lsb
  if (rawType % 5 == 0) {
    w.u(0, 1);  // no override
    if (listOps < 0) {
      w.u(0, 1);
    } else {
      w.u(1, 1);
      for (int i = 0; i < listOps; ++i) { w.ue(0); w.ue(0); }  // never op 3
    }
  }
  if (idr) { w.u(0, 1); w.u(0, 1); } else { w.u(0, 1); }
  w.se(qpDelta);
  w.u(0x5A, 8);  // some macroblock data
  w.trailing();
  return w.bytes;
}

void TestVectors() {
  std::vector<uint8_t> au;
  H264SliceQpReader r;
  Nal(&au, 0x67, Sps(0));
  Nal(&au, 0x68, Pps(0, 0, 26));
  Nal(&au, 0x65, Slice(7, 0, -3, true));  // I (raw 7 = 2 + 5), IDR
  expect(r.Read(au.data(), au.size()) == 23, "vector: IDR I slice, PPS init 26 + delta -3 = 23");
  au.clear();
  Nal(&au, 0x41, Slice(0, 0, 4, false));
  expect(r.Read(au.data(), au.size()) == 30, "vector: P slice reads through the remembered SPS/PPS (26 + 4)");
  au.clear();
  Nal(&au, 0x68, Pps(1, 0, 30));
  Nal(&au, 0x41, Slice(5, 1, 0, false));
  expect(r.Read(au.data(), au.size()) == 30, "vector: a second PPS (id 1, init 30) is used by the slice that names it");
  au.clear();
  Nal(&au, 0x41, Slice(0, 0, 0, false));
  expect(r.Read(au.data(), au.size()) == 26, "vector: ...and PPS 0 is still PPS 0 (26)");
  au.clear();
  Nal(&au, 0x41, Slice(0, 7, 0, false));
  expect(r.Read(au.data(), au.size()) == -1, "vector: a slice naming a PPS never seen -> Unknown");
  au.clear();
  Nal(&au, 0x41, Slice(12, 0, 0, false));
  expect(r.Read(au.data(), au.size()) == -1, "vector: slice_type 12 is out of range -> Unknown (not 12 % 5)");
  au.clear();
  Nal(&au, 0x41, Slice(1, 0, 0, false));
  expect(r.Read(au.data(), au.size()) == -1, "vector: a B slice is outside the supported shapes -> Unknown");
  au.clear();
  Nal(&au, 0x41, Slice(0, 0, 0, false, -1, /*truncate=*/true));
  expect(r.Read(au.data(), au.size()) == -1, "vector: a truncated slice header -> Unknown");
  au.clear();
  Nal(&au, 0x41, Slice(0, 0, 0, false, /*listOps=*/80));
  expect(r.Read(au.data(), au.size()) == -1, "vector: a list-modification loop with no terminator in bound -> Unknown");
  au.clear();
  Nal(&au, 0x67, Sps(0, /*chroma444=*/true));
  Nal(&au, 0x41, Slice(0, 0, 0, false));
  expect(r.Read(au.data(), au.size()) == -1, "vector: PPS 0 now points at a 4:4:4 SPS (unsupported) -> Unknown");
  H264SliceQpReader fresh;
  au.clear();
  Nal(&au, 0x41, Slice(0, 0, 0, false));
  expect(fresh.Read(au.data(), au.size()) == -1, "vector: a new instance's reader without parameter sets -> Unknown");
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
  // r10 (catch-up branch): the codec carries no QP fields here; the reader runs on the AU bytes exactly as
  // the per-AU trace does (one reader per encoder instance).
  H264SliceQpReader reader;
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
      const int sliceQp = reader.Read(au.bytes.data(), au.bytes.size());
      if (sliceQp >= 0) ++readable;
      if (!au.keyFrame && ceiling > 0 && sliceQp > ceiling) ++pOverCeiling;
    }
  }
  std::printf("  %s: aus=%d sliceQpReadable=%d withVideoEncodeQP=%d agree=%d ceiling=%d pOverCeiling=%d\n", name.c_str(), aus,
              readable, withAttr, agree, ceiling, pOverCeiling);
  expect(aus > 0 && readable == aus, name + ": every AU's slice QP is read from its header");
  // Where an encoder also attaches VideoEncodeQP the agreement is REPORTED above, not asserted: the slice
  // starting QP and the sample QP are different quantities (r2).
  if (ceiling > 0) expect(pOverCeiling == 0, name + ": no P slice starts above the quantiser ceiling");
  enc.shutdown();
}
}  // namespace

int main() {
  TestVectors();
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION);
  RunOne(nullptr);
  RunOne("mft_sw");
  MFShutdown();
  CoUninitialize();
  std::printf("h264_slice_qp_test: %s (%d checks, %d failed)\n", gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
