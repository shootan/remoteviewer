// encoder-rc-budget C5 seam test: the real H264Encoder (mf_h264_codec.cpp) against a fake MFT
// (host_fake_encoder_mft.hpp) whose attribute store names the vendor, so what the product does to a
// NEW instance and to a RUNNING one is observed as the ordered list of calls it makes:
//
//   [1] NVIDIA (VEN_10DE): the rate-control plan -- mode, mean, peak, VBV, an EXPLICIT MaxQP 44 --
//       is set BEFORE SetInputType/SetOutputType; not 32; VBV is 200 ms of the mean in bytes.
//   [2] a second initialize() (new instance) re-applies the whole plan: no cross-instance cache.
//   [3] same instance, same plan: reconfigure_bitrate(same) and set_max_qp(same) issue 0 SetValue.
//   [4] running NVIDIA instance: set_max_qp(other) and reconfigure_bitrate(other) issue 0 SetValue --
//       the profile does not trust runtime changes; the host re-initialises instead (ApplyTarget).
//   [5] readback classification: a fake that keeps mode/peak/vbv/maxQp "stuck" at post-type values
//       makes the product log a mismatch, with ONE bounded post-type re-set per initialize, not a loop.
//   [6] legacy (AMD VEN_1002 / unidentified): rate control AFTER the types, exactly today's order,
//       MaxQP 32, and live reconfigure/set_max_qp still SetValue (the AMD path is unchanged).
//
// Written to compile against the pre-change encoder too (initialize / set_transform_for_test /
// reconfigure_bitrate / set_max_qp only), so running it against that encoder is the negative control:
// [1] fails (the pre-change product sets RC after the types, 32), [3]/[4] fail (every call SetValues).
// Build: remote60_host_encoder_rc_seam_test. Run: prints "...: PASS", exit 0.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <mfapi.h>
#include <objbase.h>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "host_fake_encoder_mft.hpp"
#include "mf_h264_codec.hpp"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "ole32.lib")

using namespace remote60::native_poc;
using remote60::native_poc::testing::FakeEncoderMft;

namespace {
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok, const std::string& detail = std::string()) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}
std::string join(const std::vector<std::string>& v) {
  std::string out;
  for (const auto& s : v) {
    if (s.rfind("GetValue:", 0) == 0) continue;  // readbacks clutter the trace; the Set/Type order is the point
    out += (out.empty() ? "" : " > ") + s;
  }
  return out;
}
// Runs `fn` with std::cout captured, returns what the product printed.
template <typename Fn>
std::string capture(Fn&& fn) {
  std::ostringstream buf;
  std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
  fn();
  std::cout.rdbuf(old);
  return buf.str();
}
bool has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

constexpr uint32_t kW = 1920, kH = 1080, kFps = 30, kKeyint = 300;
constexpr uint32_t kE = 2'040'000;  // E for a 3 Mbps R under the shipped geometry

// The RC SetValues of one instance in call order (pre- or post-types tagged by the fake).
std::vector<std::string> rc_sets(const FakeEncoderMft& f) {
  std::vector<std::string> out;
  for (const auto& c : f.calls)
    if (c.rfind("SetValue:mode=", 0) == 0 || c.rfind("SetValue:mean=", 0) == 0 || c.rfind("SetValue:peak=", 0) == 0 ||
        c.rfind("SetValue:vbv=", 0) == 0 || c.rfind("SetValue:maxQp=", 0) == 0)
      out.push_back(c);
  return out;
}
size_t rc_set_count(const FakeEncoderMft& f) { return rc_sets(f).size(); }
bool all_pre_types(const std::vector<std::string>& sets) {
  if (sets.empty()) return false;
  for (const auto& s : sets)
    if (s.find(":pre-types") == std::string::npos) return false;
  return true;
}
}  // namespace

int main() {
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
  if (FAILED(MFStartup(MF_VERSION))) return 2;

  // ------------------------------------------------------------------ [1] NVIDIA: the plan, before the types
  {
    FakeEncoderMft* fake = new FakeEncoderMft(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    H264Encoder enc;
    enc.set_transform_for_test(fake);
    std::string log;
    bool ok = false;
    log = capture([&] { ok = enc.initialize(kW, kH, kFps, kE, kKeyint); });
    check("[1] initialize succeeds on the fake", ok);
    const auto sets = rc_sets(*fake);
    const int firstType = std::min(fake->first_index("SetInputType") < 0 ? 1 << 30 : fake->first_index("SetInputType"),
                                   fake->first_index("SetOutputType") < 0 ? 1 << 30 : fake->first_index("SetOutputType"));
    const int lastRc = fake->last_index("SetValue:maxQp=");
    check("[1] NVIDIA: every rate-control SetValue happens BEFORE the first SetInputType/SetOutputType",
          all_pre_types(sets) && lastRc >= 0 && lastRc < firstType, join(fake->calls));
    check("[1] NVIDIA: mode 1 (vbr_peak) is set", fake->set_value(CODECAPI_AVEncCommonRateControlMode) == 1);
    check("[1] NVIDIA: mean is E", fake->set_value(CODECAPI_AVEncCommonMeanBitRate) == kE);
    check("[1] NVIDIA: peak is 200% of E (the current multiplier)", fake->set_value(CODECAPI_AVEncCommonMaxBitRate) == kE * 2);
    check("[1] NVIDIA: VBV is 200 ms of E in bytes", fake->set_value(CODECAPI_AVEncCommonBufferSize) == kE / 8 * 200 / 1000,
          std::to_string(fake->set_value(CODECAPI_AVEncCommonBufferSize)));
    check("[1] NVIDIA: MaxQP is set EXPLICITLY to 44", fake->count_set("maxQp") >= 1 && fake->set_value(CODECAPI_AVEncVideoMaxQP) == 44,
          std::to_string(fake->set_value(CODECAPI_AVEncVideoMaxQP)));
    check("[1] NVIDIA: 32 is never written to this instance", !has(join(fake->calls), "SetValue:maxQp=32"));
    check("[1] NVIDIA: the plan is applied once, not pre AND post (5 SetValues)", rc_set_count(*fake) == 5,
          std::to_string(rc_set_count(*fake)));
    check("[1] the product logs the plan with profile, stage and readback state",
          has(log, "rc-plan") && has(log, "profile=nvidia_pre_types") && has(log, "stage=pre-types") && has(log, "stage=post-start"),
          log.substr(0, 400));
    check("[1] ...and the readback state is accepted (the fake reports what was set)", has(log, "state=accepted"));

    // ---------------------------------------------------------------- [3] same instance, same plan: no-op
    fake->clear_calls();
    (void)enc.reconfigure_bitrate(kE);
    (void)enc.set_max_qp(44);
    check("[3] NVIDIA: reconfigure_bitrate(same) + set_max_qp(same) issue 0 SetValue", rc_set_count(*fake) == 0,
          join(fake->calls));

    // ---------------------------------------------------------------- [4] running instance: not trusted
    fake->clear_calls();
    (void)enc.set_max_qp(40);
    (void)enc.reconfigure_bitrate(kE / 2);
    check("[4] NVIDIA: set_max_qp(40) and reconfigure_bitrate(E/2) on a RUNNING instance issue 0 SetValue",
          rc_set_count(*fake) == 0, join(fake->calls));
    check("[4] NVIDIA: the instance still holds its initial plan (mean E, MaxQP 44)",
          fake->set_value(CODECAPI_AVEncCommonMeanBitRate) == kE && fake->set_value(CODECAPI_AVEncVideoMaxQP) == 44);

    // ---------------------------------------------------------------- [2] a new instance: re-applied
    const uint64_t firstInstance = enc.instance_id();
    FakeEncoderMft* fake2 = new FakeEncoderMft(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    enc.set_transform_for_test(fake2);
    log = capture([&] { ok = enc.initialize(kW, kH, 60, kE, kKeyint); });  // an fps change: the host's reinit
    check("[2] the second initialize succeeds and is a new instance", ok && enc.instance_id() != firstInstance);
    const auto sets2 = rc_sets(*fake2);
    check("[2] NVIDIA: the NEW instance gets the whole plan again, before its types (no cross-instance cache)",
          sets2.size() == 5 && all_pre_types(sets2) && fake2->set_value(CODECAPI_AVEncVideoMaxQP) == 44 &&
              fake2->set_value(CODECAPI_AVEncCommonMeanBitRate) == kE,
          join(fake2->calls));
    // A new E on a new instance is the host's path for a bitrate change on this profile.
    FakeEncoderMft* fake3 = new FakeEncoderMft(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    enc.set_transform_for_test(fake3);
    ok = enc.initialize(kW, kH, 60, kE / 2, kKeyint);
    check("[2] NVIDIA: a new instance with a new E carries the new mean/peak/VBV before its types",
          ok && all_pre_types(rc_sets(*fake3)) && fake3->set_value(CODECAPI_AVEncCommonMeanBitRate) == kE / 2 &&
              fake3->set_value(CODECAPI_AVEncCommonMaxBitRate) == kE &&
              fake3->set_value(CODECAPI_AVEncCommonBufferSize) == (kE / 2) / 8 * 200 / 1000 &&
              fake3->set_value(CODECAPI_AVEncVideoMaxQP) == 44);
    enc.shutdown();
    enc.set_transform_for_test(nullptr);
    fake->Release();
    fake2->Release();
    fake3->Release();
  }

  // ------------------------------------------------------------------ [5] readback mismatch: bounded
  {
    FakeEncoderMft* fake = new FakeEncoderMft(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    // The Home PC post-types shape: SetValue S_OK, readback unchanged for everything but the mean.
    fake->stuck[CODECAPI_AVEncCommonRateControlMode] = 0;
    fake->stuck[CODECAPI_AVEncCommonMaxBitRate] = 15552000;
    fake->stuck[CODECAPI_AVEncCommonBufferSize] = 15552000;
    fake->stuck[CODECAPI_AVEncVideoMaxQP] = 51;
    H264Encoder enc;
    enc.set_transform_for_test(fake);
    bool ok = false;
    const std::string log = capture([&] { ok = enc.initialize(kW, kH, kFps, kE, kKeyint); });
    check("[5] initialize still succeeds when the readback disagrees (a mismatch is not a failure)", ok);
    check("[5] the product classifies the readback as a mismatch and names the fields",
          has(log, "state=mismatch") && has(log, "maxQp"), log.substr(log.find("stage=post-start") == std::string::npos ? 0 : log.find("stage=post-start"), 300));
    // Bounded: the pre-types plan (5) plus at most ONE post-type re-set of the mismatched fields (4).
    const size_t sets = rc_set_count(*fake);
    check("[5] at most one bounded post-type re-set of the mismatched fields (5 + <=4 SetValues, no loop)",
          sets >= 5 && sets <= 9, std::to_string(sets));
    check("[5] the retry is counted in the log, once", has(log, "retried=1"));
    fake->clear_calls();
    (void)enc.reconfigure_bitrate(kE);
    (void)enc.set_max_qp(44);
    check("[5] ...and the running instance is not poked again because of it", rc_set_count(*fake) == 0, join(fake->calls));
    enc.shutdown();
    enc.set_transform_for_test(nullptr);
    fake->Release();
  }

  // ------------------------------------------------------------------ [6] legacy: AMD and unidentified
  for (int which = 0; which < 2; ++which) {
    const bool amd = which == 0;
    FakeEncoderMft* fake = amd ? new FakeEncoderMft(L"VEN_1002", L"AMDh264Encoder") : new FakeEncoderMft(nullptr, nullptr);
    const std::string tag = amd ? "[6] AMD (VEN_1002)" : "[6] unidentified";
    H264Encoder enc;
    enc.set_transform_for_test(fake);
    bool ok = false;
    const std::string log = capture([&] { ok = enc.initialize(kW, kH, kFps, kE, kKeyint); });
    check(tag + ": initialize succeeds", ok);
    const auto sets = rc_sets(*fake);
    bool allPost = !sets.empty();
    for (const auto& s : sets)
      if (s.find(":post-types") == std::string::npos) allPost = false;
    check(tag + ": rate control is set AFTER the types (today's order, unchanged)", allPost, join(fake->calls));
    check(tag + ": MaxQP stays the configured 32", fake->set_value(CODECAPI_AVEncVideoMaxQP) == 32,
          std::to_string(fake->set_value(CODECAPI_AVEncVideoMaxQP)));
    check(tag + ": mean is E", fake->set_value(CODECAPI_AVEncCommonMeanBitRate) == kE);
    check(tag + ": the legacy profile is logged", has(log, "profile=legacy"));
    fake->clear_calls();
    (void)enc.reconfigure_bitrate(kE);
    (void)enc.set_max_qp(32);
    check(tag + ": same values on the same instance: 0 SetValue (no-op applies to every profile)", rc_set_count(*fake) == 0,
          join(fake->calls));
    fake->clear_calls();
    (void)enc.set_max_qp(35);
    check(tag + ": the governor's live ceiling change still reaches the MFT (SetValue maxQp=35)",
          fake->count_set("maxQp") >= 1 && fake->set_value(CODECAPI_AVEncVideoMaxQP) == 35, join(fake->calls));
    fake->clear_calls();
    (void)enc.reconfigure_bitrate(kE / 2);
    check(tag + ": a live mean change still reaches the MFT (SetValue mean=E/2)",
          fake->count_set("mean") >= 1 && fake->set_value(CODECAPI_AVEncCommonMeanBitRate) == kE / 2, join(fake->calls));
    enc.shutdown();
    enc.set_transform_for_test(nullptr);
    fake->Release();
  }

  MFShutdown();
  CoUninitialize();
  std::printf("\nhost_encoder_rc_seam_test: %s (%d checks, %d failed)\n", g_failed == 0 ? "PASS" : "FAILED", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
