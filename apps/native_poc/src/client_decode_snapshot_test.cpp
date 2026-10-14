// mobile-abr r3 (Codex candidate review R2): the decoder sink's state as one copy (client_decode_snapshot.hpp).
//
// A writer thread publishes states whose four fields all carry the same number k, as fast as it can; the
// reader takes copies while it does. Every copy it gets must be one state (all four fields equal). The
// negative control is c74a033's way -- three separate atomics written one after another and read one
// after another -- under the same writer: it has to produce mixed copies, or this test could not tell.
// Also: Read gives up (false) rather than spin when a writer never lets go.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "client_decode_snapshot.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

// c74a033: valid, count and size as separate atomics.
struct SeparateAtomics {
  std::atomic<uint64_t> total{0};
  std::atomic<bool> valid{false};
  std::atomic<uint64_t> size{0};
  void Publish(uint64_t k) {
    total.store(k, std::memory_order_release);
    valid.store((k & 1) != 0, std::memory_order_release);
    size.store((k << 32) | k, std::memory_order_release);
  }
  bool ReadConsistent() const {  // the reader's order in c74a033: count, valid, size
    const uint64_t t = total.load(std::memory_order_acquire);
    const bool v = valid.load(std::memory_order_acquire);
    const uint64_t s = size.load(std::memory_order_acquire);
    return v == ((t & 1) != 0) && (s >> 32) == (t & 0xFFFFFFFFULL);
  }
};

}  // namespace

int main() {
  constexpr auto kRun = std::chrono::milliseconds(1500);
  {
    ClientDecodeSnapshot snap;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
      for (uint64_t k = 1; !stop.load(std::memory_order_relaxed); ++k) {
        ClientDecodeState s;
        s.valid = (k & 1) != 0;
        s.decodedTotal = k;
        s.width = static_cast<uint32_t>(k);
        s.height = static_cast<uint32_t>(k);
        s.generation = k;
        snap.Publish(s);
      }
    });
    uint64_t reads = 0, gave = 0, mixed = 0;
    const auto end = std::chrono::steady_clock::now() + kRun;
    while (std::chrono::steady_clock::now() < end) {
      ClientDecodeState s;
      if (!snap.Read(&s)) {
        ++gave;
        continue;
      }
      ++reads;
      const uint32_t k32 = static_cast<uint32_t>(s.decodedTotal);
      if (s.generation != s.decodedTotal || s.width != k32 || s.height != k32 || s.valid != ((s.decodedTotal & 1) != 0)) {
        ++mixed;
      }
    }
    stop.store(true);
    writer.join();
    check("every copy the sequence read returns is ONE state (writer publishing flat out)",
          reads > 1000 && mixed == 0,
          std::to_string(reads) + " copies, " + std::to_string(mixed) + " mixed, " + std::to_string(gave) +
              " reads gave up under the writer");
  }
  {
    SeparateAtomics sep;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
      for (uint64_t k = 1; !stop.load(std::memory_order_relaxed); ++k) sep.Publish(k);
    });
    uint64_t reads = 0, mixed = 0;
    const auto end = std::chrono::steady_clock::now() + kRun;
    while (std::chrono::steady_clock::now() < end) {
      ++reads;
      if (!sep.ReadConsistent()) ++mixed;
    }
    stop.store(true);
    writer.join();
    check("NEGATIVE CONTROL: c74a033's separate atomics give mixed copies under the same writer", mixed > 0,
          std::to_string(reads) + " reads, " + std::to_string(mixed) + " mixed");
  }
  {
    // A writer that never finishes (a publish stuck half way, simulated): Read must give up, not spin.
    ClientDecodeSnapshot snap;
    ClientDecodeState s;
    s.decodedTotal = 7;
    snap.Publish(s);
    struct Peek {
      std::atomic<uint32_t> seq;
    };
    // Make the sequence odd as Publish does mid-write.
    reinterpret_cast<Peek*>(&snap)->seq.store(3, std::memory_order_release);
    ClientDecodeState out;
    const auto t0 = std::chrono::steady_clock::now();
    const bool got = snap.Read(&out);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    check("a read that never sees a finished write gives up (false), bounded", !got && us < 100000,
          std::to_string(us) + " us");
  }
  std::printf("\nclient_decode_snapshot_test: %d passed, %d failed\nRESULT: %s\n", gChecks - gFailures, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
