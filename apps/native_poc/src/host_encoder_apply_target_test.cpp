// encoder-rc-budget r2 (Codex R1): the REAL EncoderState::ApplyTarget and the due-tick path, on the
// real H264Encoder driving a fake MFT (host_fake_encoder_mft.hpp) -- not the scheduler's Decide alone.
//
//   [1] a cancelled deferral: A -> B (deferred inside the interval) -> A before the deadline clears
//       the deferral; after the deadline, repeated same-target calls build nothing and request no key.
//   [2] convergence: A -> B -> C inside the interval adopts C once at the deadline (one new instance,
//       one forced key); a further same-target call does nothing.
//   [3] a true no-op (same target as active) issues no RC write, builds nothing, requests no key, and
//       leaves the input epoch alone; a geometry change still gets its forced key (C4).
//   [4] legacy (AMD) profile: a bitrate-only change is a live reconfigure (no new instance) and does
//       request the key the output change owes.
// The pre-r2 manager fails [1] (deferred stays set; every due tick requests a key) and [3] (a no-op
// requests a key). Build: remote60_host_encoder_apply_target_test. Run: "...: PASS", exit 0.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <mfapi.h>
#include <objbase.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "host_capture_session.hpp"
#include "host_encoded_sender.hpp"
#include "host_encoder_manager.hpp"
#include "host_fake_encoder_mft.hpp"
#include "host_frame_gate.hpp"
#include "host_input_router.hpp"

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
std::string s(uint64_t v) { return std::to_string(v); }

struct Rig {
  CaptureState capture;
  CaptureResources res;
  FrameGatingState frameGating;
  InputRouterState inputRouter;
  SenderState sender;
  EncoderState encoder;
  FakeEncoderMft* fake = nullptr;
  std::ostringstream log;
  std::streambuf* oldCout = nullptr;

  explicit Rig(const wchar_t* vendor, const wchar_t* name) {
    fake = new FakeEncoderMft(vendor, name);
    encoder.codec.set_transform_for_test(fake);
    encoder.wireMtu = 1200;
    encoder.encodeSourceW = 1920;
    encoder.encodeSourceH = 1080;
    encoder.encoderRetarget.minIntervalUs = 300'000;  // the policy's 2 s, shortened so the test runs in seconds
    sender.wireCapEnabled = false;                      // the wire side is exercised elsewhere (wire cap tests)
    frameGating.staticFps = 5;                          // startup config in the product; RefreshFrameIntervals divides by it
    oldCout = std::cout.rdbuf(log.rdbuf());
  }
  ~Rig() {
    std::cout.rdbuf(oldCout);
    encoder.codec.shutdown();
    encoder.codec.set_transform_for_test(nullptr);
    fake->Release();
  }
  bool apply(uint32_t fps, uint32_t wireBps, uint32_t w = 1920, uint32_t h = 1080, uint32_t keyint = 300) {
    return encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, w, h, fps, wireBps, keyint);
  }
  void clearKey() { encoder.forceKeyNext = false; encoder.keyReasons = 0; }
  bool keyRequested() const { return encoder.forceKeyNext; }
  uint64_t epoch() const { return capture.inputEpoch.load(std::memory_order_acquire); }
  size_t count(const std::string& needle) const {
    const std::string t = log.str();
    size_t n = 0, at = 0;
    while ((at = t.find(needle, at)) != std::string::npos) { ++n; at += needle.size(); }
    return n;
  }
  void waitInterval() { std::this_thread::sleep_for(std::chrono::milliseconds(350)); }
};
}  // namespace

int main() {
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
  if (FAILED(MFStartup(MF_VERSION))) return 2;

  // ------------------------------------------------------------------ [1] cancelled deferral
  {
    Rig rig(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    check("[1] first target builds the instance", rig.apply(30, 6'000'000) && rig.encoder.codec.instance_id() == 1);
    const uint32_t eA = rig.encoder.EncoderBudgetFor(6'000'000, 30);
    check("[1] the instance holds E(6M), not R", rig.encoder.applied_encoder_bps() == eA && eA < 6'000'000, s(eA));
    check("[1] the first build requests the forced key", rig.keyRequested());
    rig.clearKey();
    const uint64_t epoch0 = rig.epoch();

    check("[1] A->B inside the interval is deferred on the NVIDIA profile (no new instance)",
          rig.apply(30, 5'000'000) && rig.encoder.encoderRetarget.deferred && rig.encoder.codec.instance_id() == 1);
    check("[1] ...the wire budget R moved at once", rig.encoder.activeBitrate == 5'000'000);
    check("[1] ...and no key is requested for a deferred change", !rig.keyRequested());
    check("[1] B->A before the deadline CANCELS the deferral (desired == applied again)",
          rig.apply(30, 6'000'000) && !rig.encoder.encoderRetarget.deferred, rig.encoder.encoderRetarget.deferred ? "deferred still set" : "");
    check("[1] ...a pure wire change back to A builds nothing and requests no key",
          rig.encoder.codec.instance_id() == 1 && !rig.keyRequested());
    rig.waitInterval();
    const size_t rcBefore = rig.fake->calls.size();
    for (int i = 0; i < 3; ++i) {
      check("[1] a same-target call after the deadline is a no-op (tick " + s(i) + ")",
            rig.apply(30, 6'000'000) && rig.encoder.codec.instance_id() == 1 && !rig.encoder.encoderRetarget.deferred &&
                !rig.keyRequested() && rig.epoch() == epoch0,
            "instance=" + s(rig.encoder.codec.instance_id()) + " deferred=" + s(rig.encoder.encoderRetarget.deferred) +
                " key=" + s(rig.keyRequested()) + " epoch=" + s(rig.epoch()) + "/" + s(epoch0));
    }
    check("[1] ...and issued no SetValue / type call to the MFT", rig.fake->calls.size() == rcBefore, s(rig.fake->calls.size() - rcBefore));
    check("[1] the deferred-applied log line never appears for a cancelled deferral", rig.count("deferred retarget applied") == 0);
  }

  // ------------------------------------------------------------------ [2] convergence to the latest
  {
    Rig rig(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    check("[2] first target", rig.apply(30, 6'000'000) && rig.encoder.codec.instance_id() == 1);
    rig.clearKey();
    check("[2] A->B deferred", rig.apply(30, 5'000'000) && rig.encoder.encoderRetarget.deferred && rig.encoder.codec.instance_id() == 1);
    check("[2] B->C still deferred (latest target replaces B)", rig.apply(30, 4'000'000) && rig.encoder.encoderRetarget.deferred &&
                                                                    rig.encoder.codec.instance_id() == 1 && rig.encoder.activeBitrate == 4'000'000);
    check("[2] ...no key while deferred", !rig.keyRequested());
    rig.waitInterval();
    check("[2] the due call adopts C: exactly one new instance", rig.apply(30, 4'000'000) && rig.encoder.codec.instance_id() == 2);
    const uint32_t eC = rig.encoder.EncoderBudgetFor(4'000'000, 30);
    check("[2] ...with E(4M) as its mean, before its types",
          rig.encoder.applied_encoder_bps() == eC && rig.fake->set_value(CODECAPI_AVEncCommonMeanBitRate) == eC &&
              rig.fake->first_index("SetValue:mean=" + s(eC) + ":pre-types") >= 0, s(eC));
    check("[2] ...and the new instance requests its forced key", rig.keyRequested());
    check("[2] ...deferral cleared", !rig.encoder.encoderRetarget.deferred);
    rig.clearKey();
    check("[2] a further same-target call does nothing", rig.apply(30, 4'000'000) && rig.encoder.codec.instance_id() == 2 && !rig.keyRequested());
    check("[2] B was never built (one adoption, not two)", rig.fake->count_set("mean") == 2 /* instance 1 + instance 2 */, s(rig.fake->count_set("mean")));
  }

  // ------------------------------------------------------------------ [3] no-op vs a semantic change
  {
    Rig rig(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    check("[3] first target", rig.apply(30, 6'000'000));
    rig.waitInterval();
    rig.clearKey();
    const uint64_t epoch0 = rig.epoch();
    const size_t calls0 = rig.fake->calls.size();
    for (int i = 0; i < 3; ++i) (void)rig.apply(30, 6'000'000);
    check("[3] three same-target calls: 0 RC writes, 0 instances, 0 keys, same epoch",
          rig.fake->calls.size() == calls0 && rig.encoder.codec.instance_id() == 1 && !rig.keyRequested() && rig.epoch() == epoch0,
          "calls+" + s(rig.fake->calls.size() - calls0) + " key=" + s(rig.keyRequested()));
    check("[3] a geometry change still rebuilds and requests the forced key (C4)",
          rig.apply(30, 6'000'000, 1280, 720) && rig.encoder.codec.instance_id() == 2 && rig.keyRequested());
    rig.clearKey();
    check("[3] an fps change rebuilds and requests the key", rig.apply(60, 6'000'000, 1280, 720) && rig.encoder.codec.instance_id() == 3 && rig.keyRequested());
  }

  // ------------------------------------------------------------------ [4] legacy: live change, key owed
  {
    Rig rig(L"VEN_1002", L"AMDh264Encoder");
    check("[4] first target (legacy)", rig.apply(30, 6'000'000) && rig.encoder.codec.rc_profile() == RcProfile::Legacy);
    rig.clearKey();
    const uint32_t eB = rig.encoder.EncoderBudgetFor(3'000'000, 30);
    check("[4] a bitrate-only change is a LIVE reconfigure: same instance, mean E(3M) set",
          rig.apply(30, 3'000'000) && rig.encoder.codec.instance_id() == 1 && rig.fake->set_value(CODECAPI_AVEncCommonMeanBitRate) == eB &&
              rig.encoder.applied_encoder_bps() == eB && !rig.encoder.encoderRetarget.deferred);
    check("[4] ...and the output change owes a forced key", rig.keyRequested());
    rig.clearKey();
    const size_t calls0 = rig.fake->calls.size();
    check("[4] the same target again: 0 writes, 0 keys", rig.apply(30, 3'000'000) && rig.fake->calls.size() == calls0 && !rig.keyRequested());
  }

  MFShutdown();
  CoUninitialize();
  std::printf("\nhost_encoder_apply_target_test: %s (%d checks, %d failed)\n", g_failed == 0 ? "PASS" : "FAILED", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
