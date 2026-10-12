// motion-settle r1 M2: FastPressure (host_fast_pressure.hpp) and the governor's fast/calm inputs
// (host_rate_governor.hpp) on a hand-made clock -- when an oversized motion AU raises the ceiling, the
// one desired value the governor then holds, and every bound on the fast path.

#include "host_fast_pressure.hpp"
#include "host_rate_governor.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
int gChecks = 0;
void expect(bool ok, const std::string& what) {
  ++gChecks;
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++gFailures;
}
constexpr uint64_t kMs = 1000;
constexpr uint64_t kBudget = 16000;  // ~16 KB a frame: 4.5 Mbps payload at 30 fps (the field)

void TestDetection() {
  FastPressure f;
  f.Configure(FastPressureConfig{});
  expect(!f.NoteRealP(15000, kBudget, true), "M2: a P inside the budget never latches");
  expect(!f.NoteRealP(45000, kBudget, false), "M2: one 2.8x P without a backlog does not latch on its own");
  expect(f.NoteRealP(45000, kBudget, true), "M2: one 2.8x P with the sender backlogged latches (field: 45-71 KB vs 16-19 KB)");
  f.ResetRun();
  expect(!f.NoteRealP(36000, kBudget, false), "M2: a first 2.25x P alone does not latch");
  expect(f.NoteRealP(36000, kBudget, false), "M2: ...two in a row do");
  f.ResetRun();
  expect(!f.NoteRealP(36000, kBudget, false) && !f.NoteRealP(10000, kBudget, false) && !f.NoteRealP(36000, kBudget, false),
         "M2: an in-budget P between two big ones breaks the run");
  FastPressureConfig off;
  off.enabled = false;
  FastPressure g;
  g.Configure(off);
  expect(!g.NoteRealP(100000, kBudget, true), "M2: REMOTE60_NATIVE_FAST_PRESSURE=0 -- never latches");
}

void TestBounds() {
  FastPressure f;
  f.Configure(FastPressureConfig{});
  int taken = 0;
  for (int i = 0; i < 20; ++i)
    if (f.TakeBoost(static_cast<uint64_t>(i) * 300 * kMs)) ++taken;  // a boost every 300 ms for 6 s
  expect(taken == 6, "M2: at most 6 boosts in 10 s -- a stop/start storm is not a QP toggle storm (" + std::to_string(taken) + ")");
  expect(f.boosts_refused() == 14, "M2: ...the rest are refused and counted");
  expect(f.TakeBoost(20000 * kMs), "M2: ...and the window clears");
  expect(f.CanSetNow(0), "M2: the first Set may go at once");
  f.NoteSet(1000 * kMs);
  expect(!f.CanSetNow(1249 * kMs) && f.CanSetNow(1250 * kMs), "M2: at most one ceiling Set per 250 ms");
  f.NoteFailure(7);
  expect(!f.Degraded(7), "M2: one refused fast Set is not yet degraded");
  f.NoteFailure(7);
  expect(f.Degraded(7) && !f.Degraded(8), "M2: two on the same instance degrade the fast path for THAT instance only");
}

void TestGovernorFastAndCalm() {
  RateGovernor g;  // legacy ladder 32..40, fast 38
  expect(g.maxQp() == 32 && !g.fastActive(), "M2: the governor rests at 32");
  expect(g.OnFastPressure() && g.maxQp() == 38 && g.slowQp() == 32, "M2: fast pressure holds the one desired ceiling at 38");
  expect(!g.OnFastPressure(), "M2: a second pressure while held changes nothing (no extra Set)");
  // Slow seconds while the boost is held: over-target seconds still step the slow ladder; the desired
  // ceiling is the higher of the two -- the slow tick does not wipe the boost while the motion runs.
  const uint32_t budget = 3000000;
  for (int i = 0; i < 2; ++i) (void)g.OnSecond(budget / 8 * 15 / 10, budget, 30);
  expect(g.fastActive() && g.slowQp() == 35 && g.maxQp() == 38, "M2: over-target seconds move the slow ladder under the boost (35 < 38)");
  for (int i = 0; i < 2; ++i) (void)g.OnSecond(budget / 8 * 15 / 10, budget, 30);
  expect(g.slowQp() == 38 && g.maxQp() == 38, "M2: ...and once the slow ladder passes it, its value is the desired one");
  expect(g.OnCalm() || g.maxQp() == 32, "M2: an explicit calm (quiet, nothing new, backlog drained)...");
  expect(g.maxQp() == 32 && !g.fastActive() && g.slowQp() == 32, "M2: ...brings both halves back to 32 together");
  // A whole calm second also ends a boost.
  (void)g.OnFastPressure();
  (void)g.OnSecond(budget / 8 / 10, budget, 30);
  expect(!g.fastActive() && g.maxQp() == 32, "M2: a calm second (<40% of budget) ends the boost too");
  // Reset (new target / fps / instance) clears the boost.
  (void)g.OnFastPressure();
  g.Reset();
  expect(!g.fastActive() && g.maxQp() == 32, "M2: a new target or instance (Reset) clears the boost -- the old episode cannot overwrite it");
  // A static-ceiling ladder (NVIDIA: base == cap == 51) has no fast stage.
  RateGovernorConfig st;
  st.baseQp = 51;
  st.capQp = 51;
  RateGovernor n(st);
  expect(!n.OnFastPressure() && n.maxQp() == 51, "M2: a static ceiling (NVIDIA 51) has no fast stage");
}

}  // namespace

int main() {
  TestDetection();
  TestBounds();
  TestGovernorFastAndCalm();
  std::printf("host_fast_pressure_test: %s (%d checks, %d failed)\n", gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
