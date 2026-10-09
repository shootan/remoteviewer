#pragma once

// Runtime GOP-clamp detection from the REAL NAL cadence (stutter-keyframe r3 B).
//
// A clamping MFT (the company PC clamps AVEncMPVGOPSize to its frame rate) emits its OWN periodic IDR
// -- an accepted key AU with NO host reason ("encoder_gop") -- far sooner than the policy GOP. This
// detector judges a clamp ONLY from the measured interval, never from vendor id or bitrate (Codex
// contract 1): if the last kClampWindow consecutive encoder-self IDRs all arrived at fewer than
// activeKeyint / kClampRatioDiv inputs since the previous key, the encoder is clamping.
//
// It is fed ONLY by encoder-self IDRs (keyReasons == none). A host-requested key (viewer / scheduled /
// barrier) is not evidence either way and is not passed in; a forced key also restarts the encoder's
// own GOP, so the next self-IDR's interval still reflects the true encoder cadence. kClampWindow
// consecutive short self-IDRs guards against a one-off short interval.
//
// This is detection + a latched flag only; the RESPONSE (a SW-encoder transition, a clamp-aware
// admission change, ...) is decided separately. Pure, so every branch is a unit test.

#include <cstdint>

namespace remote60::native_poc {

inline constexpr uint32_t kClampWindow = 3;     // consecutive short self-IDRs needed to latch a clamp
inline constexpr uint32_t kClampRatioDiv = 2;   // "far shorter than policy" == interval < policy/2

struct GopClampDetector {
  uint32_t shortStreak = 0;  // consecutive encoder-self IDRs with interval < policy/kClampRatioDiv
  bool clamped = false;      // latched once detected (cleared only on a fresh encoder init / Reset)

  // Call on each ENCODER-SELF IDR. intervalInputs = real inputs since the previous key of any reason;
  // activeKeyint = policy max GOP (frames). Returns true on the tick the clamp is FIRST latched.
  bool OnEncoderSelfKey(uint32_t intervalInputs, uint32_t activeKeyint) {
    if (activeKeyint == 0) return false;
    const uint32_t threshold = activeKeyint / kClampRatioDiv;
    if (threshold > 0 && intervalInputs < threshold) {
      if (shortStreak < kClampWindow) ++shortStreak;
    } else {
      shortStreak = 0;  // a policy-length self-IDR breaks the streak
    }
    const bool nowClamped = shortStreak >= kClampWindow;
    const bool firstLatch = nowClamped && !clamped;
    if (nowClamped) clamped = true;
    return firstLatch;
  }

  // A forced key / epoch boundary / lost provenance breaks the consecutive self-IDR streak (so a
  // short streak cannot span a boundary, G2) but does NOT clear a clamp already latched.
  void NoteBoundary() { shortStreak = 0; }

  void Reset() {
    shortStreak = 0;
    clamped = false;
  }
};

// r4 G: decide how a key AU feeds the detector, from its PER-AU provenance (not host globals). Only a
// real IDR (rawIdr) produced by a non-forced input with known provenance and the same codec/input
// epoch as the last self-IDR yields a valid self-IDR interval (in accepted-input ordinals). A forced
// key, an epoch change, or lost provenance (ordinal 0) is a BOUNDARY: it re-baselines and breaks the
// streak, never counting as a short interval (G1/G2). Pure, so the wiring counter-examples are tests.
struct SelfIdrSample {
  bool valid = false;        // a real self-IDR with a measurable self->self interval from the last one
  bool baseline = false;     // a real self-IDR that STARTS a baseline (first one, or the first after a
                             // boundary/epoch change): set lastOrdinal, but produce NO interval
  bool boundary = false;     // a forced/unknown key: break the streak AND invalidate the baseline, so
                             // the next self-IDR starts fresh (never a forced->self interval, G2)
  uint32_t intervalInputs = 0;
};
inline SelfIdrSample classify_self_idr(bool rawIdr, bool inputWasForcedKey, uint64_t ordinal,
                                       uint64_t epoch, uint64_t lastOrdinal, uint64_t lastEpoch) {
  SelfIdrSample s;
  if (!rawIdr) return s;  // not a real IDR -> ignore entirely (CleanPoint-only AUs do not count)
  if (inputWasForcedKey || ordinal == 0) { s.boundary = true; return s; }  // forced / unknown provenance
  if (lastOrdinal == 0 || epoch != lastEpoch || ordinal <= lastOrdinal) {  // no prior baseline in this
    s.baseline = true;  // epoch -> this self-IDR starts one; do not count a cross-boundary interval
    return s;
  }
  s.valid = true;
  s.intervalInputs = static_cast<uint32_t>(ordinal - lastOrdinal);
  return s;
}

}  // namespace remote60::native_poc
