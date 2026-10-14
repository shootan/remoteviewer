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
  // r5 (Codex F1 contract): the count and the time (sink's steady clock, us) at which this generation
  // began -- a drain over a generation change counts only what came after it, over the time it covers.
  uint64_t generationStartTotal = 0;
  uint64_t generationStartUs = 0;
  // r5: an active transition's startup grace -- until the first counted picture, at most until the
  // deadline (steady us). graceBaseTotal = the count when it began.
  bool graceActive = false;
  uint64_t graceDeadlineUs = 0;
  uint64_t graceBaseTotal = 0;
};

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
    genStartTotal_.store(v.generationStartTotal, std::memory_order_relaxed);
    genStartUs_.store(v.generationStartUs, std::memory_order_relaxed);
    grace_.store(v.graceActive, std::memory_order_relaxed);
    graceDeadlineUs_.store(v.graceDeadlineUs, std::memory_order_relaxed);
    graceBase_.store(v.graceBaseTotal, std::memory_order_relaxed);
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
      v.generationStartTotal = genStartTotal_.load(std::memory_order_relaxed);
      v.generationStartUs = genStartUs_.load(std::memory_order_relaxed);
      v.graceActive = grace_.load(std::memory_order_relaxed);
      v.graceDeadlineUs = graceDeadlineUs_.load(std::memory_order_relaxed);
      v.graceBaseTotal = graceBase_.load(std::memory_order_relaxed);
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
  std::atomic<uint64_t> genStartTotal_{0};
  std::atomic<uint64_t> genStartUs_{0};
  std::atomic<bool> grace_{false};
  std::atomic<uint64_t> graceDeadlineUs_{0};
  std::atomic<uint64_t> graceBase_{0};
};

}  // namespace remote60::native_poc
