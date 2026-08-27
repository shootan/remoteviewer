#pragma once

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

// Decides which of the frames the desktop offers actually enter the encoder.
//
// Desktop duplication reports changes, not a cadence. On a busy screen it offers far more
// than any target needs, and the job is to pick an evenly spaced subset. On a quiet screen it
// offers less than the target, and there is nothing to pick -- so whatever irregularity the
// desktop had goes straight through.
//
// That second case is the one that hurts, and it is worth being precise about why it cannot
// be fixed here. A 60 fps request against a desktop offering ~33 updates a second gave seven
// frames in one second and fifty-two in the next. Pacing to the rate the source can sustain
// was tried and measured: it barely moves the unevenness, because the gate can only discard,
// and in the second that offered seven there is nothing to discard. Evening that out needs a
// buffer deep enough to lend frames across the quiet second, which is a second of latency --
// not a remote desktop. The receiver's playout clock already smooths what a shallow buffer
// can; past that the irregularity is the screen's own, and showing fewer frames while less
// is happening is the correct answer rather than a defect.
class CaptureCadenceGate {
 public:
  void SetRequestedIntervalUs(uint64_t intervalUs) {
    requestedIntervalUs_ = (intervalUs > 0) ? intervalUs : 1;
  }

  void SetEnabled(bool enabled) { enabled_ = enabled; }
  void SetEarlyTolerancePercent(uint32_t percent) { earlyTolerancePercent_ = percent; }

  // Forgets the measured rate. Used when the stream restarts, where the old rate describes
  // content and a target that no longer apply. The lifetime counters are deliberately left
  // alone -- they are cumulative telemetry, like the host's captureRestarts.
  void Reset() {
    nextContentDueUs_ = 0;
  }

  /** True when this frame should be encoded. `hasNewContent` is false for an offer that
   *  carried no desktop update -- a pointer-only report -- which says nothing about the rate
   *  at which content is arriving and must not be measured as if it did.
   *
   *  Only content advances the content clock. A pointer-only offer on the old shared clock could
   *  claim the next content slot and, because duplication is change-driven and never re-sends
   *  those pixels, silently drop the real frame that followed (a right-click menu that then never
   *  appeared); pointer-only offers are now dropped outright and never touch the clock. */
  bool ShouldAccept(uint64_t nowUs, bool hasNewContent) {
    // There used to be an EWMA of the observed offer interval here, kept per content offer on the
    // capture-callback thread. Nothing read it: EffectiveIntervalUs() returns the requested
    // interval, and its two accessors had no callers anywhere in the tree, tests included. It was
    // arithmetic and two fields of state maintained every frame for nobody. (Ledger H-15.)
    if (!enabled_) return true;

    const uint64_t intervalUs = EffectiveIntervalUs();
    const uint64_t earlyToleranceUs =
        std::max<uint64_t>(1500, intervalUs * earlyTolerancePercent_ / 100);

    if (hasNewContent) {
      ++offerContentCount_;
      // A content frame turned away here is gone: duplication never re-sends it.
      if (nextContentDueUs_ != 0 && nowUs + earlyToleranceUs < nextContentDueUs_) {
        ++gateDropContentCount_;
        return false;
      }
      // Keep the phase while it still describes the present, so accepted frames stay evenly
      // spaced instead of re-basing on every arrival.
      const bool phaseStillUseful =
          nextContentDueUs_ != 0 && nowUs <= nextContentDueUs_ + intervalUs * 2;
      nextContentDueUs_ =
          phaseStillUseful ? nextContentDueUs_ + intervalUs : nowUs + intervalUs;
      ++acceptContentCount_;
      return true;
    }

    // Pointer-only offers carry no desktop update. The DXGI backend does not composite the
    // hardware cursor into the desktop texture (capture_backend_dxgi.cpp forwards only
    // AccumulatedFrames), so a pointer-only frame is byte-identical to the last content frame:
    // the frame-gating stage downstream would drop it anyway, after paying for a full readback.
    // Drop it here so it never advances the content clock -- the menu-loss bug this fix is about
    // -- and never costs a readback. WGC composites the cursor and delivers motion as content
    // (hasNewContent=true), taking the branch above; if DXGI cursor composition is ever added, a
    // separate pointer cadence (min(activeFps, 30)) can re-enable these.
    ++offerPointerCount_;
    ++gateDropPointerCount_;
    return false;
  }

  uint64_t EffectiveIntervalUs() const { return requestedIntervalUs_; }

  // Lifetime counters, split by content vs pointer-only offers. Cumulative across resets.
  //
  // Taken as one snapshot rather than five getters, because the caller must hold the same lock
  // the capture-callback thread mutates them under (CaptureState::cadenceMu) and per-getter
  // reads invited exactly the bug this replaces: the stats line read four of them with no lock
  // at all while the drain watchdog carefully locked for the fifth. (Ledger H-05.)
  struct Counters {
    uint64_t offerContent = 0;
    uint64_t offerPointer = 0;
    uint64_t gateDropContent = 0;
    uint64_t gateDropPointer = 0;
    uint64_t acceptContent = 0;
  };
  Counters SnapshotCounters() const {
    Counters c;
    c.offerContent = offerContentCount_;
    c.offerPointer = offerPointerCount_;
    c.gateDropContent = gateDropContentCount_;
    c.gateDropPointer = gateDropPointerCount_;
    c.acceptContent = acceptContentCount_;
    return c;
  }

 private:
  uint64_t requestedIntervalUs_ = 33333;
  uint64_t nextContentDueUs_ = 0;
  // Non-atomic: the gate runs on the capture-callback thread, and every reader takes
  // CaptureState::cadenceMu -- the mutex the callback holds while calling ShouldAccept. This is
  // NOT telemetry-only state, whatever an earlier comment here claimed: acceptContent is the
  // numerator the readback-drain watchdog restarts capture on. (Ledger H-05.)
  uint64_t offerContentCount_ = 0;
  uint64_t offerPointerCount_ = 0;
  uint64_t gateDropContentCount_ = 0;
  uint64_t gateDropPointerCount_ = 0;
  uint64_t acceptContentCount_ = 0;
  uint32_t earlyTolerancePercent_ = 25;
  bool enabled_ = true;
};

}  // namespace remote60::native_poc
