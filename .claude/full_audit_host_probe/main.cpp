#include "capture_cadence_gate.hpp"
#include "host_kick.hpp"
#include "host_epoch_gate.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

int main() {
  using namespace remote60::native_poc;
  for (unsigned fps : {30u, 60u}) {
    CaptureCadenceGate gate;
    gate.SetEnabled(true);
    gate.SetRequestedIntervalUs(1000000 / fps);
    gate.SetEarlyTolerancePercent(25);
    unsigned cachedPixelRevision = 0;
    auto offer = [&](uint64_t t, unsigned revision, bool content) {
      bool accept = gate.ShouldAccept(t, content);
      if (accept) cachedPixelRevision = revision;
      return accept;
    };
    assert(offer(1000000, 1, true));
    bool finalAccepted = offer(1005000, 2, true);
    assert(!finalAccepted);
    // Capture timeout never invokes PublishCapturedTexture. Simulate pointer-only
    // reports; product gate rejects them and they cannot supply a new readback.
    assert(!offer(1100000, 2, false));
    assert(!offer(1200000, 2, false));
    KickState kick;
    kick.Arm(1000000, true);
    kick.lastRealInputCaptureUs = 1000000;
    assert(kick.Due(1150000));
    assert(kick.NeedKick(false));
    // KickTryFill returns the existing bootstrap payload; this variable models
    // only that cache assignment, not DXGI/GPU/encoder execution.
    const unsigned syntheticRevision = cachedPixelRevision;
    assert(syntheticRevision == 1);
    assert(offer(1300000, 3, true));
    assert(cachedPixelRevision == 3);
    std::cout << "fps=" << fps << " final_revision_2_accepted=" << finalAccepted
              << " idle_synthetic_revision=" << syntheticRevision
              << " recovered_on_separate_revision=" << cachedPixelRevision << '\n';
  }
  // Exact current host output-count lease rule; model, not a real MFT.
  uint64_t outputs = 0;
  uint64_t leaseARequired = outputs + 1;  // A accepted, no output.
  uint64_t leaseBRequired = outputs + 1;  // B accepted; this call emits only A.
  outputs += 1;
  bool releaseA = leaseARequired <= outputs;
  bool releaseB = leaseBRequired <= outputs;
  assert(releaseA && releaseB);
  std::cout << "async_MFT_model_output=A release_A=" << releaseA
            << " release_B=" << releaseB << " B_still_in_MFT=1\n";
  EpochGate epoch;
  auto first = epoch_gate_judge(epoch, 7, 7, true, 100, 1000000);
  auto lost = epoch_gate_judge(epoch, 7, 0, false, 100, 1010000);
  auto dependent = epoch_gate_judge(epoch, 7, 7, false, 100, 1020000);
  assert(first == EpochVerdict::AcceptKey);
  assert(lost == EpochVerdict::DropUnknownEpoch);
  assert(dependent == EpochVerdict::Emit);
  std::cout << "open_epoch_unknown_drop=" << epoch_verdict_name(lost)
            << " following_dependent_P=" << epoch_verdict_name(dependent)
            << " awaitingKey=" << epoch.awaitingKey << '\n';
}
