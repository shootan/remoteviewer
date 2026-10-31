#pragma once

// One consistent copy of a decoder sink's state for the session's control thread (mobile-abr r3, Codex
// candidate review R2): whether the decoder state means anything now, how many pictures it has given out,
// its output size, and the generation of that state -- read together or not at all.
//
// Role:    a sequence lock. The sink publishes under its own mutex (a single writer); the control thread
//          reads without taking any lock and never waits on codec work. A read that overlaps a publish
//          retries a bounded number of times and then reports "no consistent copy" -- the session then
//          treats the window as not valid rather than mixing two states.
// Thread:  Publish: the sink, under its mutex (one writer). Read / generation: any thread.
// Callers: AndroidVideoDecoderSink (Publish in PublishDecodeStateLocked; Read in DrainDecodeStats;
//          generation in DecodeGeneration); client_decode_snapshot_test.cpp.
// Pure, no platform headers.
//
// Separate atomics (what c74a033 had) can be read half-old, half-new: a valid flag of one state with the
// size or count of another. Every field here is still an atomic (no data race), the sequence makes the set
// consistent.

#include <atomic>
#include <cstdint>

namespace remote60::native_poc {

struct ClientDecodeState {
  bool valid = false;           // a surface, and no selection between generations
  uint64_t decodedTotal = 0;    // valid decoded pictures given out, never reset
  uint32_t width = 0;           // decoder output format (0 = not known)
  uint32_t height = 0;
  uint64_t generation = 0;      // bumps whenever validity / surface / selection changes
  // r6 (Codex S1, codex_mobile_abr_e599123.md): the valid measurement now in progress -- its id, the time
  // it began (the sink's steady clock, us) and the count then (ClientDecodeSpanStart). A start still
  // in the future is a startup grace that has not ended: nothing is measured yet.
  uint64_t spanId = 0;
  uint64_t spanStartUs = 0;
  uint64_t spanStartTotal = 0;
};

// r6: where the valid measurement now in progress began -- the writer's side, kept by the sink under its
// mutex. A rate is the count since this start over the time since it, so time that was deliberately not
// measured (a startup grace) never enters a denominator, and the count and the time always cover the
// same stretch.
//   - a new generation (surface, selection) begins one at once -- inside a startup grace that has not
//     ended, at the grace's deadline instead;
//   - a startup grace begins one at its deadline (a grace that expires without a picture gives a real 0
//     from there);
//   - the picture that ends a grace before its deadline moves the start to that picture (counted in it);
//     after the deadline the measurement has already begun and simply goes on.
// Pure.
class ClientDecodeSpanStart {
 public:
  void OnGeneration(uint64_t nowUs, uint64_t total, bool graceActive, uint64_t graceDeadlineUs) {
    ++id_;
    startTotal_ = total;
    startUs_ = (graceActive && graceDeadlineUs > nowUs) ? graceDeadlineUs : nowUs;
  }
  void OnGraceStart(uint64_t total, uint64_t graceDeadlineUs) {
    ++id_;
    startTotal_ = total;
    startUs_ = graceDeadlineUs;
  }
  // totalBefore: the count before the picture that ends the grace.
  void OnGraceFirstPicture(uint64_t nowUs, uint64_t totalBefore) {
    if (nowUs >= startUs_) return;
    startUs_ = nowUs;
    startTotal_ = totalBefore;
  }
  uint64_t id() const { return id_; }
  uint64_t startUs() const { return startUs_; }
  uint64_t startTotal() const { return startTotal_; }

 private:
  uint64_t id_ = 0;
  uint64_t startUs_ = 0;
  uint64_t startTotal_ = 0;
};

// r6: what a drain at nowUs (the clock the span was started on) measures. false = nothing is valid now:
// no surface, or inside a startup grace (the span has not begun).
struct ClientDecodeSpan {
  uint64_t id = 0;
  uint64_t frames = 0;  // valid pictures since the span began
  uint64_t us = 0;      // how long it has been measured
};
inline bool ReadDecodeSpan(const ClientDecodeState& s, uint64_t nowUs, ClientDecodeSpan* out) {
  if (!s.valid || nowUs < s.spanStartUs) return false;
  out->id = s.spanId;
  out->frames = s.decodedTotal >= s.spanStartTotal ? s.decodedTotal - s.spanStartTotal : 0;
  out->us = nowUs - s.spanStartUs;
  return true;
}

class ClientDecodeSnapshot {
 public:
  // Single writer.
  void Publish(const ClientDecodeState& v) {
    const uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);  // odd: a write in progress
    std::atomic_thread_fence(std::memory_order_release);
    valid_.store(v.valid, std::memory_order_relaxed);
    total_.store(v.decodedTotal, std::memory_order_relaxed);
    size_.store((static_cast<uint64_t>(v.width) << 32u) | v.height, std::memory_order_relaxed);
    gen_.store(v.generation, std::memory_order_relaxed);
    spanId_.store(v.spanId, std::memory_order_relaxed);
    spanStartUs_.store(v.spanStartUs, std::memory_order_relaxed);
    spanStartTotal_.store(v.spanStartTotal, std::memory_order_relaxed);
    seq_.store(s + 2, std::memory_order_release);
  }

  // false: no consistent copy within `tries` (a writer kept overlapping) -- treat as not valid.
  bool Read(ClientDecodeState* out, int tries = 64) const {
    for (int i = 0; i < tries; ++i) {
      const uint32_t s1 = seq_.load(std::memory_order_acquire);
      if (s1 & 1u) continue;
      ClientDecodeState v;
      v.valid = valid_.load(std::memory_order_relaxed);
      v.decodedTotal = total_.load(std::memory_order_relaxed);
      const uint64_t size = size_.load(std::memory_order_relaxed);
      v.generation = gen_.load(std::memory_order_relaxed);
      v.spanId = spanId_.load(std::memory_order_relaxed);
      v.spanStartUs = spanStartUs_.load(std::memory_order_relaxed);
      v.spanStartTotal = spanStartTotal_.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seq_.load(std::memory_order_relaxed) != s1) continue;
      v.width = static_cast<uint32_t>(size >> 32u);
      v.height = static_cast<uint32_t>(size & 0xFFFFFFFFULL);
      *out = v;
      return true;
    }
    return false;
  }

  uint64_t generation() const { return gen_.load(std::memory_order_acquire); }

 private:
  std::atomic<uint32_t> seq_{0};
  std::atomic<bool> valid_{false};
  std::atomic<uint64_t> total_{0};
  std::atomic<uint64_t> size_{0};
  std::atomic<uint64_t> gen_{0};
  std::atomic<uint64_t> spanId_{0};
  std::atomic<uint64_t> spanStartUs_{0};
  std::atomic<uint64_t> spanStartTotal_{0};
};

}  // namespace remote60::native_poc
