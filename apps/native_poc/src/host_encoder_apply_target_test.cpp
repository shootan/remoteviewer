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
  // motion-settle r3: M2 (fast pressure) is a default-OFF comparison feature; section [8] tests it, so
  // this process opts in. The default (off) is checked on the shipping configuration's own log.
  _putenv_s("REMOTE60_NATIVE_FAST_PRESSURE", "1");  // the CRT copy getenv reads
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

  // ------------------------------------------------------------------ [5] r3 S2: governor ceiling vs the live instance
  {
    Rig rig(L"VEN_1002", L"AMDh264Encoder");
    check("[5] legacy first target", rig.apply(30, 6'000'000));
    rig.clearKey();
    RateGovernor& gov = rig.encoder.rateGovernor;
    const uint32_t configured = H264Encoder::configured_max_qp();
    // Drive the loop to its cap the way the stats tick does: 3x over for several seconds.
    for (int i = 0; i < 8; ++i) (void)gov.OnSecond(6'000'000 / 8 * 3, 6'000'000, 30);
    check("[5] the governor reaches 40", gov.maxQp() == 40, s(gov.maxQp()));
    // The stats tick's reconcile after a decision: the live legacy instance follows the loop.
    (void)rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[5] ...and the live instance runs 40", rig.fake->set_value(CODECAPI_AVEncVideoMaxQP) == 40 && rig.encoder.codec.plan_max_qp() == 40,
          s(rig.fake->set_value(CODECAPI_AVEncVideoMaxQP)));
    // A new R: the tick Resets the loop to its floor. The INSTANCE must come back to the floor too
    // (Codex S2: before r3 only the next-instance seed moved and the live 40 stayed until the next step).
    gov.Reset();
    const auto rc = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[5] r3 S2: after Reset the live instance is brought back to the floor (32), not left at 40",
          rig.fake->set_value(CODECAPI_AVEncVideoMaxQP) == 32 && rig.encoder.codec.plan_max_qp() == 32 && rc.attempted,
          "codec=" + s(rig.fake->set_value(CODECAPI_AVEncVideoMaxQP)) + " governor=" + s(gov.maxQp()));
    // Same desired again on the next tick: nothing issued.
    rig.fake->clear_calls();
    const auto rc2 = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[5] r3 S2: a second reconcile at the same ceiling issues nothing", !rc2.attempted && rig.fake->count_set("maxQp") == 0);
    // The MFT refuses the ceiling: the failure is reported, retried a bounded number of times, then held.
    for (int i = 0; i < 5; ++i) (void)gov.OnSecond(6'000'000 / 8 * 3, 6'000'000, 30);
    rig.fake->rejectWith[CODECAPI_AVEncVideoMaxQP] = E_FAIL;
    rig.fake->clear_calls();
    int attempts = 0;
    for (int tick = 0; tick < 6; ++tick) {
      const auto r = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
      if (r.attempted) ++attempts;
    }
    check("[5] r3 S2: a refused ceiling is retried a bounded number of times (3), then held as degraded",
          attempts == 3 && rig.encoder.governorQpApplyFailures == 3, "attempts=" + s(attempts));
    check("[5] r3 S2: ...and the instance's recorded ceiling is NOT advanced to the refused value",
          rig.encoder.codec.plan_max_qp() != gov.maxQp() || !rig.encoder.codec.rc_plan_effective(), s(rig.encoder.codec.plan_max_qp()));
    // The next instance is seeded with the loop's current ceiling, so a rebuild does not fall back to 32.
    rig.fake->rejectWith.clear();
    (void)rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[5] a rebuild starts at the loop's current ceiling (seeded)",
          rig.apply(60, 6'000'000) && rig.encoder.codec.instance_id() == 2 && rig.fake->set_value(CODECAPI_AVEncVideoMaxQP) == gov.maxQp(),
          s(rig.fake->set_value(CODECAPI_AVEncVideoMaxQP)) + " vs " + s(gov.maxQp()));
  }
  {
    // Static profile: the reconcile never issues a SetValue, whatever the governor's state.
    Rig rig(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    check("[5n] nvidia first target", rig.apply(30, 6'000'000));
    RateGovernor& gov = rig.encoder.rateGovernor;
    gov.Reconfigure(governor_config_for(RcProfile::NvidiaPreTypes, H264Encoder::configured_max_qp(), 44));
    rig.fake->clear_calls();
    const auto rc = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), H264Encoder::configured_max_qp());
    gov.Reset();
    const auto rc2 = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), H264Encoder::configured_max_qp());
    check("[5n] r3 S2: a static-ceiling instance is never poked by the reconcile (0 SetValue, the plan's 51 stays)",
          !rc.attempted && !rc2.attempted && rig.fake->count_set("maxQp") == 0 && rig.fake->set_value(CODECAPI_AVEncVideoMaxQP) == 51);
  }

  // ------------------------------------------------------------------ [6] r4 Q1: the tick's QP path, counted per tick
  // The stats tick's ceiling handling is ReconcileGovernorCeiling once per decision (the result feeds the
  // log) -- there is no second setter call. These count the SetValue(maxQp) the fake receives per tick.
  {
    // Q1a: a refused ceiling -- exactly ONE write per tick, three ticks, then nothing (degraded).
    Rig rig(L"VEN_1002", L"AMDh264Encoder");
    check("[6a] legacy first target", rig.apply(30, 6'000'000));
    RateGovernor& gov = rig.encoder.rateGovernor;
    const uint32_t configured = H264Encoder::configured_max_qp();
    for (int i = 0; i < 2; ++i) (void)gov.OnSecond(6'000'000 / 8 * 3, 6'000'000, 30);
    check("[6a] the governor stepped to 35", gov.maxQp() == 35, s(gov.maxQp()));
    rig.fake->rejectWith[CODECAPI_AVEncVideoMaxQP] = E_FAIL;
    int total = 0, maxPerTick = 0;
    for (int tick = 0; tick < 6; ++tick) {
      rig.fake->clear_calls();
      (void)rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);  // the tick's one apply path
      const int n = static_cast<int>(rig.fake->count_set("maxQp"));
      total += n;
      maxPerTick = std::max(maxPerTick, n);
    }
    check("[6a] r4 Q1a: a refused ceiling costs at most ONE SetValue per tick and three in total, then the instance is left alone",
          maxPerTick == 1 && total == 3, "perTick=" + s(maxPerTick) + " total=" + s(total));
    check("[6a] ...and the degraded state is visible", rig.encoder.governorQpApplyFailures == 3);
  }
  {
    // Q1b: the MEAN is stuck (its readback never follows) while the ceiling is fine -- the ceiling's
    // reconcile must judge its OWN field and issue nothing, not re-write the whole plan every tick.
    Rig rig(L"VEN_1002", L"AMDh264Encoder");
    check("[6b] legacy first target", rig.apply(30, 6'000'000));
    RateGovernor& gov = rig.encoder.rateGovernor;
    const uint32_t configured = H264Encoder::configured_max_qp();
    (void)rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    rig.fake->stuck[CODECAPI_AVEncCommonMeanBitRate] = 1;  // from now on the MFT reports a wrong mean
    (void)rig.encoder.codec.reconfigure_bitrate(rig.encoder.EncoderBudgetFor(5'000'000, 30));  // a live change: mean now mismatched
    check("[6b] the plan as a whole is not effective (mean mismatch)", !rig.encoder.codec.rc_plan_effective());
    rig.fake->clear_calls();
    for (int tick = 0; tick < 10; ++tick) (void)rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[6b] r4 Q1b: ten ticks with a fine ceiling and a stuck mean: ZERO SetValue of any RC field (no whole-plan rewrite loop)",
          rig.fake->count_set("maxQp") == 0 && rig.fake->count_set("mean") == 0 && rig.fake->count_set("mode") == 0,
          "maxQp=" + s(rig.fake->count_set("maxQp")) + " mean=" + s(rig.fake->count_set("mean")));
    check("[6b] ...and the ceiling is not counted as a failure", rig.encoder.governorQpApplyFailures == 0);
  }
  {
    // Q1c: the failure budget is per (instance, desired): a new instance gets its own bounded attempts.
    Rig rig(L"VEN_1002", L"AMDh264Encoder");
    check("[6c] legacy first target", rig.apply(30, 6'000'000));
    RateGovernor& gov = rig.encoder.rateGovernor;
    const uint32_t configured = H264Encoder::configured_max_qp();
    for (int i = 0; i < 2; ++i) (void)gov.OnSecond(6'000'000 / 8 * 3, 6'000'000, 30);  // 35
    rig.fake->rejectWith[CODECAPI_AVEncVideoMaxQP] = E_FAIL;
    for (int tick = 0; tick < 5; ++tick) (void)rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[6c] three refusals on instance 1, then held", rig.encoder.governorQpApplyFailures == 3);
    rig.fake->clear_calls();
    (void)rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[6c] ...same instance, same desired: no further attempt", rig.fake->count_set("maxQp") == 0);
    // A geometry change builds a new instance (the seed carries the desired 35; the fake refuses it at
    // init, so the new instance also runs without the ceiling) -- the runtime budget starts over.
    check("[6c] a new instance", rig.apply(30, 6'000'000, 1280, 720) && rig.encoder.codec.instance_id() == 2);
    rig.fake->clear_calls();
    const auto r = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[6c] r4 Q1c: the NEW instance with the same desired gets a fresh bounded attempt (budget is per instance+desired)",
          r.attempted && rig.fake->count_set("maxQp") == 1 && rig.encoder.governorQpApplyFailures == 1,
          "attempted=" + s(r.attempted) + " failures=" + s(rig.encoder.governorQpApplyFailures));
    rig.fake->rejectWith.clear();
    (void)rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[6c] ...and once the field is confirmed the failure state is cleared", rig.encoder.governorQpApplyFailures == 0 &&
                                                                                      rig.fake->set_value(CODECAPI_AVEncVideoMaxQP) == 35);
  }

  // ------------------------------------------------------------------ [7] r4 H1: the E-only sequence on the real scheduler
  // What the Home bundle's E-only row must find on a static profile, pinned here on the real manager +
  // scheduler + fake MFT with the interval shortened: adopt / cancel inside the interval / latest
  // target wins / repeat is nothing. Timeline (interval I): A start; +>I B adopt(reinit); B->A defer;
  // A->B cancel (desired == applied) before the deadline; wait; C adopt(reinit); D defer; E defer
  // (replaces D); due -> E adopted once; E again -> nothing.
  {
    Rig rig(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    check("[7] A: first target", rig.apply(30, 6'000'000) && rig.encoder.codec.instance_id() == 1);
    rig.waitInterval();
    rig.clearKey();
    check("[7] B after the interval: adopted at once (instance 2)", rig.apply(30, 5'000'000) && rig.encoder.codec.instance_id() == 2 && rig.keyRequested());
    rig.clearKey();
    check("[7] A inside the interval: deferred", rig.apply(30, 6'000'000) && rig.encoder.encoderRetarget.deferred && rig.encoder.codec.instance_id() == 2);
    check("[7] B again before the deadline: the deferral is CANCELLED (desired == applied), nothing built, no key",
          rig.apply(30, 5'000'000) && !rig.encoder.encoderRetarget.deferred && rig.encoder.codec.instance_id() == 2 && !rig.keyRequested());
    rig.waitInterval();
    check("[7] past the deadline a same-target call is nothing (the cancelled deferral never fires)",
          rig.apply(30, 5'000'000) && rig.encoder.codec.instance_id() == 2 && !rig.keyRequested());
    check("[7] C after the interval: adopted at once (instance 3)", rig.apply(30, 5'500'000) && rig.encoder.codec.instance_id() == 3);
    rig.clearKey();
    check("[7] D inside the interval: deferred", rig.apply(30, 5'200'000) && rig.encoder.encoderRetarget.deferred && rig.encoder.codec.instance_id() == 3);
    check("[7] E inside the interval: still deferred, the latest target replaces D",
          rig.apply(30, 5'400'000) && rig.encoder.encoderRetarget.deferred && rig.encoder.codec.instance_id() == 3 && rig.encoder.activeBitrate == 5'400'000);
    rig.waitInterval();
    const uint32_t eE = rig.encoder.EncoderBudgetFor(5'400'000, 30);
    check("[7] due: E adopted ONCE (instance 4) with E(5.4M); D was never built",
          rig.apply(30, 5'400'000) && rig.encoder.codec.instance_id() == 4 && rig.encoder.applied_encoder_bps() == eE && rig.keyRequested());
    rig.clearKey();
    check("[7] E repeated: nothing", rig.apply(30, 5'400'000) && rig.encoder.codec.instance_id() == 4 && !rig.keyRequested() && !rig.encoder.encoderRetarget.deferred);
    check("[7] instances over the whole sequence: 4 (A, B, C, E) -- not 5, not 3", rig.encoder.codec.instance_id() == 4);
  }

  // ------------------------------------------------------------------ [8] motion-settle r1 M2: the ceiling alone
  // A ceiling step (slow or fast) writes AVEncVideoMaxQP and nothing else: before M2, set_max_qp went
  // through apply_rate_control and re-issued mode / mean / peak / VBV with every step.
  {
    Rig rig(L"VEN_1002", L"AMDh264Encoder");
    check("[8] AMD legacy first target", rig.apply(30, 6'000'000));
    check("[8] the fast path is eligible on the AMD legacy instance", rig.encoder.FastPressureEligible());
    RateGovernor& gov = rig.encoder.rateGovernor;
    const uint32_t configured = H264Encoder::configured_max_qp();
    rig.fake->clear_calls();
    check("[8] fast pressure raises the one desired ceiling to 38", gov.OnFastPressure() && gov.maxQp() == 38, s(gov.maxQp()));
    const auto up = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[8] ...applied with ONE Set: maxQp only -- no mode, mean, peak or VBV re-issued",
          up.attempted && up.result == RcRuntimeResult::Applied && rig.fake->count_set("maxQp") == 1 &&
              rig.fake->count_set("mode") == 0 && rig.fake->count_set("mean") == 0 && rig.fake->count_set("peak") == 0 &&
              rig.fake->count_set("vbv") == 0 && rig.encoder.codec.plan_max_qp() == 38,
          "calls=" + s(rig.fake->calls.size()) + " maxQp=" + s(rig.fake->count_set("maxQp")) + " mean=" + s(rig.fake->count_set("mean")));
    check("[8] ...and the other fields keep their confirmed state", rig.encoder.codec.rc_plan_effective());
    rig.fake->clear_calls();
    const auto same = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[8] the same desired again: 0 Set", !same.attempted && rig.fake->count_set("maxQp") == 0);
    check("[8] calm brings the one ceiling back to 32", gov.OnCalm() && gov.maxQp() == 32);
    const auto down = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), configured);
    check("[8] ...again a maxQp-only Set", down.attempted && rig.fake->count_set("maxQp") == 1 && rig.fake->count_set("mean") == 0 &&
                                               rig.encoder.codec.plan_max_qp() == 32);
  }
  {
    Rig rig(L"VEN_8086", L"Intel Quick Sync Video H.264 Encoder MFT");
    check("[8u] unknown legacy first target", rig.apply(30, 6'000'000));
    check("[8u] an unknown legacy MFT keeps the slow loop only (fast path not eligible)", !rig.encoder.FastPressureEligible());
  }
  {
    Rig rig(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    check("[8n] nvidia first target", rig.apply(30, 6'000'000));
    RateGovernor& gov = rig.encoder.rateGovernor;
    gov.Reconfigure(governor_config_for(RcProfile::NvidiaPreTypes, H264Encoder::configured_max_qp(), 51));
    rig.fake->clear_calls();
    const bool moved = gov.OnFastPressure();
    const auto rc = rig.encoder.ReconcileGovernorCeiling(gov.maxQp(), H264Encoder::configured_max_qp());
    check("[8n] NVIDIA: not eligible, no fast stage (base == cap), 0 runtime Set, static 51 stays",
          !rig.encoder.FastPressureEligible() && !moved && !rc.attempted && rig.fake->count_set("maxQp") == 0 &&
              rig.fake->set_value(CODECAPI_AVEncVideoMaxQP) == 51);
  }

  // ------------------------------------------------------------------ [9] motion-settle r2 F: the settle dip
  // One owner: the effective ceiling is the dip while it holds, the ordinary desired otherwise; the
  // next instance is seeded with the ORDINARY desired (never the dip); the restore goes back to the
  // ordinary desired of that moment (a fast 38 included), not to a remembered or fixed value.
  {
    Rig rig(L"VEN_1002", L"AMDh264Encoder");
    check("[9] AMD legacy first target", rig.apply(30, 6'000'000));
    RateGovernor& gov = rig.encoder.rateGovernor;
    rig.encoder.settleDipQp = 20;
    rig.encoder.settleDipEpisode = 1;
    rig.encoder.settleDipInstance = rig.encoder.codec.instance_id();
    check("[9] the dip is the effective ceiling (20) while the ordinary one stays 32",
          rig.encoder.SettleDipActive() && rig.encoder.EffectiveDesiredQp() == 20 && gov.maxQp() == 32);
    rig.fake->clear_calls();
    const auto dip = rig.encoder.ReconcileEffectiveCeiling();
    check("[9] ...applied with one maxQp Set (20), nothing else",
          dip.attempted && rig.fake->count_set("maxQp") == 1 && rig.fake->count_set("mean") == 0 && rig.encoder.codec.plan_max_qp() == 20);
    check("[9] ...and the next instance is seeded with the ORDINARY ceiling, not the dip",
          rig.encoder.codec.initial_max_qp() == 0 /* 0 = the configured 32 */, s(rig.encoder.codec.initial_max_qp()));
    // A fast boost arrives while the dip holds: the ordinary desired moves to 38; the restore takes THAT.
    (void)gov.OnFastPressure();
    rig.encoder.settleDipEpisode = 0;  // the episode ends (new motion)
    const auto restore = rig.encoder.ReconcileEffectiveCeiling();
    check("[9] the restore goes to the ordinary desired of that moment (fast 38), not to 32 or the dip",
          restore.attempted && rig.encoder.codec.plan_max_qp() == 38 && rig.encoder.codec.initial_max_qp() == 38,
          s(rig.encoder.codec.plan_max_qp()));
    // A dip on an instance that is rebuilt: the new instance starts at the ordinary seed and the dip is not live on it.
    (void)gov.OnCalm();
    rig.encoder.settleDipEpisode = 2;
    rig.encoder.settleDipInstance = rig.encoder.codec.instance_id();
    (void)rig.encoder.ReconcileEffectiveCeiling();
    check("[9] dip live again (20)", rig.encoder.codec.plan_max_qp() == 20);
    check("[9] a rebuild starts at the ordinary seed (32), and the old instance's dip is not live on the new one",
          rig.apply(60, 6'000'000) && rig.encoder.codec.instance_id() == 2 && rig.fake->set_value(CODECAPI_AVEncVideoMaxQP) == 32 &&
              !rig.encoder.SettleDipActive() && rig.encoder.EffectiveDesiredQp() == 32,
          s(rig.fake->set_value(CODECAPI_AVEncVideoMaxQP)));
  }

  // ------------------------------------------------------------------ [10] motion-settle r3 (c): MinQP alone
  {
    Rig rig(L"VEN_1002", L"AMDh264Encoder");
    rig.fake->stuck[CODECAPI_AVEncVideoMinQP] = 0;  // the instance's own MinQP before anyone sets it
    rig.fake->stuckUntilSet = true;
    check("[10] AMD legacy first target", rig.apply(30, 6'000'000));
    check("[10] the instance's ordinary MinQP is read at start (0) and confirmed",
          rig.encoder.codec.ordinary_min_qp() == 0 && rig.encoder.codec.confirmed_min_qp() == 0, s(rig.encoder.codec.ordinary_min_qp()));
    rig.fake->clear_calls();
    const auto up = rig.encoder.codec.set_min_qp(26);
    check("[10] a floor of 26 is ONE MinQP Set (no mode/mean/peak/vbv/maxQp) and confirmed by readback",
          up == RcRuntimeResult::Applied && rig.fake->count_set("minQp") == 1 && rig.fake->count_set("maxQp") == 0 &&
              rig.fake->count_set("mean") == 0 && rig.fake->count_set("mode") == 0 && rig.encoder.codec.confirmed_min_qp() == 26);
    rig.fake->clear_calls();
    check("[10] the same floor again: Unchanged, 0 Set",
          rig.encoder.codec.set_min_qp(26) == RcRuntimeResult::Unchanged && rig.fake->count_set("minQp") == 0);
    check("[10] the release goes back to the ordinary value (0), confirmed",
          rig.encoder.codec.set_min_qp(0) == RcRuntimeResult::Applied && rig.encoder.codec.confirmed_min_qp() == 0);
    rig.fake->rejectWith[CODECAPI_AVEncVideoMinQP] = E_FAIL;
    check("[10] a refused Set is Failed and the confirmed value stays the readback (0)",
          rig.encoder.codec.set_min_qp(26) == RcRuntimeResult::Failed && rig.encoder.codec.confirmed_min_qp() == 0);
    rig.fake->rejectWith.clear();
    rig.fake->stuckUntilSet = false;
    rig.fake->stuck[CODECAPI_AVEncVideoMinQP] = 0;  // S_OK, readback stuck at 0
    check("[10] an S_OK with a stuck readback is NOT applied",
          rig.encoder.codec.set_min_qp(28) == RcRuntimeResult::Failed && rig.encoder.codec.confirmed_min_qp() == 0);
  }
  {
    Rig rig(L"VEN_1002", L"AMDh264Encoder");  // MinQP never readable on this instance
    check("[10u] first target", rig.apply(30, 6'000'000));
    rig.fake->clear_calls();
    check("[10u] no ordinary MinQP known -> no floor: Unproven, 0 Set",
          rig.encoder.codec.ordinary_min_qp() == -1 && rig.encoder.codec.set_min_qp(26) == RcRuntimeResult::Unproven &&
              rig.fake->count_set("minQp") == 0);
  }
  {
    Rig rig(L"VEN_10DE", L"NVIDIA H.264 Encoder MFT");
    check("[10n] nvidia first target", rig.apply(30, 6'000'000));
    check("[10n] NVIDIA is not a live-ceiling instance: no floor, no dip (Set 0 by construction)", !rig.encoder.LiveCeilingEligible());
  }

  MFShutdown();
  CoUninitialize();
  std::printf("\nhost_encoder_apply_target_test: %s (%d checks, %d failed)\n", g_failed == 0 ? "PASS" : "FAILED", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
