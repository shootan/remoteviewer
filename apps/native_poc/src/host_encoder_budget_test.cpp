// encoder-rc-budget: the pure policy -- E from R (host_encoder_budget.hpp), the RC plan per MFT
// identity, the runtime controls per profile, the governor config per profile and the retarget
// coalescing (host_encoder_rc_policy.hpp). No socket, no MFT.
#include <cstdio>
#include <string>

#include "host_encoder_budget.hpp"
#include "host_encoder_rc_policy.hpp"
#include "poc_protocol.hpp"

using namespace remote60::native_poc;

namespace {
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok, const std::string& detail = std::string()) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}
std::string s(uint64_t v) { return std::to_string(v); }
}  // namespace

int main() {
  // ------------------------------------------------------------------ C1: E from R
  WireGeometry shipped;
  shipped.mtuBytes = 1200;
  shipped.chunkHeaderBytes = static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  shipped.fecGroupSize = kUdpVideoFecGroupSize;
  {
    const EncoderBudgetBreakdown b = encoder_budget_for(6'000'000, shipped, EncoderBudgetPolicy{});
    check("fec factor is 1 + 1/groupSize (groups of 8 -> 1.125)", b.fecFactorX1000 == 1125, s(b.fecFactorX1000));
    const uint32_t hdr = static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
    const uint32_t expectHeaderX1000 = 1000 + 1000 * (hdr + 28) / (1200 - hdr);
    check("header factor is 1 + (chunkHeader+28)/(mtu-chunkHeader), from the real (packed) header size",
          b.headerFactorX1000 == expectHeaderX1000, s(b.headerFactorX1000) + " expected " + s(expectHeaderX1000));
    check("the 6 Mbps seed lands near 0.70 R (0.66..0.74), a consequence of the geometry not a constant",
          b.encoderBps > 6'000'000 * 66 / 100 && b.encoderBps < 6'000'000 * 74 / 100, s(b.encoderBps));
    check("the packed chunk header is 88 bytes (what the doc values and the default geometry state)", hdr == 88 && WireGeometry{}.chunkHeaderBytes == hdr, s(hdr));
    check("E is strictly below R", b.encoderBps < 6'000'000);
    check("the NACK reserve is taken from R once", b.wireAfterReserveBps == 5'700'000, s(b.wireAfterReserveBps));
  }
  {
    uint32_t prev = 0;
    bool monotone = true, below = true;
    for (uint32_t r = 100'000; r <= 50'000'000; r += 250'000) {
      const uint32_t e = encoder_payload_budget_bps(r, shipped);
      if (e < prev) monotone = false;
      if (r > 200'000 && e >= r) below = false;  // at the 100 kbps floor E == R by design
      prev = e;
    }
    check("E is monotone in R over 0.1..50 Mbps", monotone);
    check("...and below R everywhere", below);
    check("E never drops under the encoder's 100 kbps floor for R >= 100 kbps",
          encoder_payload_budget_bps(100'000, shipped) == 100'000);
  }
  {
    // A larger MTU costs fewer headers per payload byte -> a larger E; a smaller group costs more
    // parity -> a smaller E. The budget follows the geometry, which is the point of not hardcoding 27%.
    WireGeometry bigMtu = shipped;
    bigMtu.mtuBytes = 1400;
    WireGeometry smallGroup = shipped;
    smallGroup.fecGroupSize = 4;
    const uint32_t base = encoder_payload_budget_bps(3'000'000, shipped);
    check("a bigger MTU gives the encoder more", encoder_payload_budget_bps(3'000'000, bigMtu) > base);
    check("a smaller FEC group gives the encoder less", encoder_payload_budget_bps(3'000'000, smallGroup) < base);
    WireGeometry degenerate;
    degenerate.mtuBytes = 10;
    degenerate.chunkHeaderBytes = 48;
    degenerate.fecGroupSize = 0;
    const EncoderBudgetBreakdown d = encoder_budget_for(3'000'000, degenerate, EncoderBudgetPolicy{});
    check("a degenerate geometry does not divide by zero: factors fall back to 1.0",
          d.fecFactorX1000 == 1000 && d.headerFactorX1000 == 1000 && d.encoderBps > 0);
    EncoderBudgetPolicy noMargins;
    noMargins.nackReservePercent = 0;
    noMargins.encoderOvershootPercent = 0;
    const EncoderBudgetBreakdown n = encoder_budget_for(1'199'200, shipped, noMargins);
    check("with both allowances at 0, E is exactly R over the fec and header factors",
          n.encoderBps == n.payloadWireBps && n.wireAfterReserveBps == 1'199'200, s(n.encoderBps));
  }

  // ------------------------------------------------------------------ r2: the per-AU model at a frame rate
  {
    const EncoderBudgetBreakdown asym = encoder_budget_for(1'500'000, shipped, EncoderBudgetPolicy{});
    const EncoderBudgetBreakdown at60 = encoder_budget_for(1'500'000, shipped, EncoderBudgetPolicy{}, 60);
    // Codex's counter-example: an encoder holding the asymptotic E exactly, at 60 fps, costs more wire
    // than R -- two data chunks plus a full-stride parity plus three headers per ~2 KB AU.
    const uint64_t asymWireAt60 = wire_bps_at_fps(asym.payloadWireBps, 60, shipped);
    check("1.5 Mbps / 60 fps: the asymptotic payload would cost MORE wire than the budget (the ceil cost the asymptote ignores)",
          asymWireAt60 > asym.wireAfterReserveBps, s(asymWireAt60) + " vs " + s(asym.wireAfterReserveBps));
    check("...so the per-AU model hands the encoder less than the asymptote", at60.encoderBps < asym.encoderBps,
          s(at60.encoderBps) + " < " + s(asym.encoderBps));
    check("...and its own wire cost at 60 fps fits the budget", at60.wireAtFpsBps <= at60.wireAfterReserveBps && at60.wireAtFpsBps > 0,
          s(at60.wireAtFpsBps) + " <= " + s(at60.wireAfterReserveBps));
    check("...the AU it priced is the payload budget per frame (payloadWireBps/8/fps, before the overshoot slack)",
          at60.auBytesAtFps == at60.payloadWireBps / 8 / 60, s(at60.auBytesAtFps));
    const EncoderBudgetBreakdown at30big = encoder_budget_for(6'000'000, shipped, EncoderBudgetPolicy{}, 30);
    const EncoderBudgetBreakdown asym6 = encoder_budget_for(6'000'000, shipped, EncoderBudgetPolicy{});
    // ~18 KB AUs: 17 chunks -> a THIRD parity group that is one chunk deep; the ceil costs ~5% here,
    // which is exactly the cost the asymptote cannot see (and why the product uses this model).
    check("6 Mbps / 30 fps (AUs of ~18 KB): the per-AU model is below the asymptote and within 12% of it",
          at30big.encoderBps <= asym6.encoderBps && at30big.encoderBps >= asym6.encoderBps * 88 / 100,
          s(at30big.encoderBps) + " vs " + s(asym6.encoderBps));
    check("the model is monotone in the frame rate (more frames per second, less per frame and more header cost)",
          encoder_budget_for(3'000'000, shipped, EncoderBudgetPolicy{}, 60).encoderBps <=
              encoder_budget_for(3'000'000, shipped, EncoderBudgetPolicy{}, 30).encoderBps);
    // One AU's exact cost, as the product chunks it: 2134 B -> 2 chunks of stride 1112 + 1 parity of 1112 + 3 x (88+28).
    const uint32_t hdr = static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
    const uint64_t stride = 1200 - hdr;
    check("wire_bytes_per_au matches udp_chunk_geometry for a 2-chunk AU", wire_bytes_per_au(2134, shipped) == 2134 + stride + 3 * (hdr + 28),
          s(wire_bytes_per_au(2134, shipped)));
    check("...and for a single-chunk AU the parity is the AU's own size (tight stride)",
          wire_bytes_per_au(500, shipped) == 500 + 500 + 2 * (hdr + 28), s(wire_bytes_per_au(500, shipped)));
  }

  // ------------------------------------------------------------------ r2 (Codex R4): the governor's unit
  {
    // 2.5 Mbps of generated PAYLOAD at R = 3 Mbps / 30 fps: against R it reads 83% ("under", the loop
    // would raise quality while the wire is already full); against the payload the wire can carry it
    // reads over 100% (in band or over). The governor must be fed the latter.
    const uint32_t payloadBudget = encoder_budget_for(3'000'000, shipped, EncoderBudgetPolicy{}, 30).payloadWireBps;
    check("the payload budget at R=3M/30fps sits between 2.0 and 2.5 Mbps (per-AU cost at ~9 KB AUs)",
          payloadBudget > 2'000'000 && payloadBudget < 2'500'000, s(payloadBudget));
    const uint64_t vsWire = 2'500'000ULL * 100 / 3'000'000ULL;
    const uint64_t vsPayload = 2'500'000ULL * 100 / payloadBudget;
    check("2.5 Mbps payload: 83% of R but >= 100% of the payload budget -- the unit the loop is now compared in",
          vsWire == 83 && vsPayload >= 100, "vsWire=" + s(vsWire) + "% vsPayload=" + s(vsPayload) + "%");
    check("E is the payload budget with the overshoot slack removed (E == budget / 1.12)",
          encoder_budget_for(3'000'000, shipped, EncoderBudgetPolicy{}, 30).encoderBps == payloadBudget * 100 / 112);
  }

  // ------------------------------------------------------------------ C2: identity -> profile
  {
    MftIdentity nv;
    nv.friendlyName = "NVIDIA H.264 Encoder MFT";
    nv.vendorId = "VEN_10DE";
    check("VEN_10DE selects the NVIDIA pre-types profile", rc_profile_for(nv) == RcProfile::NvidiaPreTypes);
    MftIdentity nvLower = nv;
    nvLower.vendorId = "ven_10de";
    check("...case-insensitively", rc_profile_for(nvLower) == RcProfile::NvidiaPreTypes);
    MftIdentity amd;
    amd.friendlyName = "AMDh264Encoder";
    amd.vendorId = "VEN_1002";
    check("VEN_1002 (AMD) stays on the legacy profile", rc_profile_for(amd) == RcProfile::Legacy);
    MftIdentity nameOnly;
    nameOnly.friendlyName = "NVIDIA H.264 Encoder MFT";
    nameOnly.vendorId = "?";
    check("a friendly name alone is NOT identity: 'NVIDIA' in the name with no vendor id is legacy",
          rc_profile_for(nameOnly) == RcProfile::Legacy);
    MftIdentity unknown;
    check("an unidentified MFT is legacy", rc_profile_for(unknown) == RcProfile::Legacy);
    MftIdentity intel;
    intel.vendorId = "VEN_8086";
    check("another vendor is legacy, not guessed into the NVIDIA policy", rc_profile_for(intel) == RcProfile::Legacy);
  }

  // ------------------------------------------------------------------ C2: the plans
  {
    RcPlanInputs in;
    in.meanBps = 2'121'000;  // E for a 3 Mbps R
    in.peakPercent = 200;
    in.legacyVbvBytes = 2'121'000 / 40;
    in.legacyMaxQp = 32;
    const RcPlan nv = make_rc_plan(RcProfile::NvidiaPreTypes, in);
    check("nvidia plan: vbr_peak (mode 1)", nv.rcMode == 1);
    check("nvidia plan: mean is E", nv.meanBps == 2'121'000);
    check("nvidia plan: peak keeps the current multiplier (200%)", nv.peakBps == 4'242'000, s(nv.peakBps));
    check("nvidia plan: VBV is 200 ms of E, in BYTES (E/8*0.2)", nv.vbvBytes == 2'121'000 / 8 * 200 / 1000, s(nv.vbvBytes));
    check("nvidia plan: MaxQP is SET, explicitly, to 44 (not 32, not omitted)", nv.maxQpSet && nv.maxQp == 44);
    const RcPlan lg = make_rc_plan(RcProfile::Legacy, in);
    check("legacy plan: mode 1 unless cbr", lg.rcMode == 1);
    check("legacy plan: today's VBV and the configured 32 ceiling", lg.vbvBytes == 2'121'000 / 40 && lg.maxQpSet && lg.maxQp == 32);
    RcPlanInputs cbr = in;
    cbr.cbr = true;
    check("legacy plan: REMOTE60_NATIVE_RATE_CONTROL=cbr -> mode 0", make_rc_plan(RcProfile::Legacy, cbr).rcMode == 0);
    RcPlanInputs noQp = in;
    noQp.legacyMaxQp = 0;
    check("legacy plan: ceiling 0 means the MaxQP SetValue is not issued", !make_rc_plan(RcProfile::Legacy, noQp).maxQpSet);
    RcPlanInputs modeNum = in;
    modeNum.rcModeOverride = 0;
    check("the diagnostic mode override reaches the nvidia plan too", make_rc_plan(RcProfile::NvidiaPreTypes, modeNum).rcMode == 0);
    RcPlanInputs small = in;
    small.meanBps = 200'000;
    check("nvidia VBV never below 16 KB", make_rc_plan(RcProfile::NvidiaPreTypes, small).vbvBytes == 16384);
    // Equality is the no-op test (C3): same five values = same plan; a different ceiling is a different plan.
    RcPlan a = nv, b = nv;
    check("same values compare equal", a == b);
    b.maxQp = 40;
    check("a different ceiling is a different plan", a != b);
    RcPlan c = lg, d = lg;
    c.maxQpSet = d.maxQpSet = false;
    c.maxQp = 7; d.maxQp = 9;
    check("when MaxQP is not set its value does not matter", c == d);
  }

  // ------------------------------------------------------------------ C3/C4: runtime controls, governor config
  {
    const RcRuntimeControls nv = rc_runtime_controls_for(RcProfile::NvidiaPreTypes);
    check("nvidia profile: neither the mean nor the ceiling is live on a running instance", !nv.liveMean && !nv.liveMaxQp);
    const RcRuntimeControls lg = rc_runtime_controls_for(RcProfile::Legacy);
    check("legacy profile: both are live (AMD, read back as set)", lg.liveMean && lg.liveMaxQp);
    const RateGovernorConfig g = governor_config_for(RcProfile::NvidiaPreTypes, 32, 44);
    check("nvidia governor: base == cap == the static ceiling (44); 32 is never restored", g.baseQp == 44 && g.capQp == 44);
    RateGovernor gov(g);
    for (int i = 0; i < 10; ++i) gov.OnSecond(6'000'000 / 8 * 3, 6'000'000, 60);  // 3x over for 10 s
    check("...so the quantiser stage never moves", gov.maxQp() == 44);
    check("...and the frame-rate stage engages directly (over at the cap -> fps step)", gov.fpsStep() >= 1, s(gov.fpsStep()));
    for (int i = 0; i < 10; ++i) gov.OnSecond(6'000'000 / 8 / 10, 6'000'000, 60);  // calm
    check("...calm returns to the static ceiling, not to 32", gov.maxQp() == 44 && gov.fpsStep() == 0);
    const RateGovernorConfig lgCfg = governor_config_for(RcProfile::Legacy, 32, 0);
    check("legacy governor: unchanged ladder 32..40", lgCfg.baseQp == 32 && lgCfg.capQp == 40);
  }

  // ------------------------------------------------------------------ C3: retarget coalescing
  {
    using V = EncoderRetargetScheduler::Verdict;
    EncoderRetargetScheduler sch;
    check("same E on the live instance: nothing to do", sch.Decide(1'000'000, 2'000'000, 2'000'000) == V::NoChange);
    check("first ever change: reinit now", sch.Decide(1'000'000, 1'500'000, 2'000'000) == V::ReinitNow);
    sch.NoteReinit(1'000'000);
    check("a second change inside the interval is deferred, not applied",
          sch.Decide(1'500'000, 1'200'000, 1'500'000) == V::Defer && sch.deferred && sch.deferredUntilUs == 3'000'000);
    check("...not due before the interval ends", !sch.Due(2'999'999));
    check("...due at the interval's end", sch.Due(3'000'000));
    check("a third change in the interval still just defers (latest target wins at the end)",
          sch.Decide(2'000'000, 900'000, 1'500'000) == V::Defer && sch.deferredUntilUs == 3'000'000);
    check("at the interval's end the (latest) change is applied now", sch.Decide(3'000'000, 900'000, 1'500'000) == V::ReinitNow);
    sch.NoteReinit(3'000'000);
    check("a deferred change that became moot (E back to applied) clears the deferral",
          sch.Decide(3'500'000, 900'000, 900'000) == V::NoChange && !sch.deferred);
    EncoderRetargetScheduler sch2;
    sch2.NoteReinit(10'000'000);  // an fps/geometry reinit also restarts the interval
    check("any reinit restarts the interval", sch2.Decide(10'500'000, 1, 2) == V::Defer);
    check("the interval is 2 s by default", sch2.minIntervalUs == 2'000'000);
  }

  std::printf("\nhost_encoder_budget_test: %s (%d checks, %d failed)\n", g_failed == 0 ? "PASS" : "FAILED", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
