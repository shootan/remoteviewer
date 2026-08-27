// Ring policy and buffer pool of the capture readback pipeline. Everything here runs
// without a GPU: the drop policy and the recycle-only-when-unreferenced guarantee are the
// two behaviours that silently corrupt frames when they regress.

#include <cstdio>
#include <cstring>

#include "d3d_capture_readback.hpp"

using remote60::native_poc::CaptureBufferPool;
using remote60::native_poc::pick_latest_ready_slot;
using remote60::native_poc::readback_slot_is_current;
using remote60::native_poc::ReadbackSlotState;

namespace {

int gFailures = 0;

void check(const char* name, bool cond) {
  std::printf("%s  %s\n", cond ? "PASS" : "FAIL", name);
  if (!cond) ++gFailures;
}

}  // namespace

int main() {
  {
    size_t superseded = 99;
    const size_t pick = pick_latest_ready_slot({}, {}, &superseded);
    check("empty ring picks nothing", pick == SIZE_MAX && superseded == 0);
  }
  {
    size_t superseded = 99;
    const size_t pick = pick_latest_ready_slot({5, 7, 6}, {false, false, false}, &superseded);
    check("no ready slot picks nothing", pick == SIZE_MAX && superseded == 0);
  }
  {
    size_t superseded = 99;
    const size_t pick = pick_latest_ready_slot({5, 7, 6}, {true, false, true}, &superseded);
    check("latest ready slot wins", pick == 2);
    check("older ready slots count as superseded", superseded == 1);
  }
  {
    size_t superseded = 99;
    const size_t pick = pick_latest_ready_slot({5, 9, 6}, {true, true, true}, &superseded);
    check("all ready picks the newest submit", pick == 1);
    check("two older frames dropped", superseded == 2);
  }
  {
    size_t superseded = 99;
    const size_t pick = pick_latest_ready_slot({4}, {true}, &superseded);
    check("single ready slot is not superseded", pick == 0 && superseded == 0);
  }

  {
    CaptureBufferPool pool;
    auto a = pool.Acquire(64);
    std::memset(a->data(), 0xAB, a->size());
    auto* firstStorage = a.get();
    check("fresh buffer is not a reuse", pool.ReuseCount() == 0);

    // A second holder (the gating reference pattern) keeps the storage out of the pool.
    auto gatingRef = a;
    a.reset();
    auto b = pool.Acquire(64);
    check("referenced buffer is never recycled", b.get() != firstStorage);
    check("still no reuse while referenced", pool.ReuseCount() == 0);

    // Once the last holder lets go the storage comes back.
    gatingRef.reset();
    b.reset();
    auto c = pool.Acquire(128);
    check("released buffer is recycled", pool.ReuseCount() >= 1);
    check("recycled buffer takes the requested size", c->size() == 128);
  }
  {
    CaptureBufferPool pool;
    auto a = pool.Acquire(16);
    (*a)[0] = 1;
    a.reset();
    auto b = pool.Acquire(16);
    auto c = pool.Acquire(16);
    check("pool serves distinct buffers concurrently", b.get() != c.get());
  }

  // Slot identity (ledger H-23). The bug this pins: Submit released the context lock, then
  // finalised the slot's meta -- but the slot was already GpuPending, so the worker could pick it
  // up with a meta that still claimed a failed preprocess had succeeded, or still held an NV12
  // lease Submit was about to return. Submitting is the barrier; submitSeq is the identity.
  {
    constexpr uint64_t gen = 7;
    constexpr uint64_t seq = 42;
    check("submit finalises its own still-Submitting slot",
          readback_slot_is_current(gen, gen, ReadbackSlotState::Submitting,
                                   ReadbackSlotState::Submitting, seq, seq));
    check("a reconfigure (new generation) stops submit finalising",
          !readback_slot_is_current(gen + 1, gen, ReadbackSlotState::Submitting,
                                    ReadbackSlotState::Submitting, seq, seq));
    // Same generation, slot freed by the worker and re-reserved by a newer Submit: the staging
    // texture is unchanged, so only submitSeq can tell the two apart.
    check("same-generation slot reuse stops the older submit finalising",
          !readback_slot_is_current(gen, gen, ReadbackSlotState::Submitting,
                                    ReadbackSlotState::Submitting, seq + 1, seq));
    check("a slot already promoted is not finalised again",
          !readback_slot_is_current(gen, gen, ReadbackSlotState::GpuPending,
                                    ReadbackSlotState::Submitting, seq, seq));
    check("the worker frees its own still-GpuPending slot",
          readback_slot_is_current(gen, gen, ReadbackSlotState::GpuPending,
                                   ReadbackSlotState::GpuPending, seq, seq));
    check("the worker does not free a slot a newer submit reserved",
          !readback_slot_is_current(gen, gen, ReadbackSlotState::Submitting,
                                    ReadbackSlotState::GpuPending, seq + 1, seq));
    check("a freed slot is neither finalised nor freed again",
          !readback_slot_is_current(gen, gen, ReadbackSlotState::Free,
                                    ReadbackSlotState::GpuPending, seq, seq) &&
              !readback_slot_is_current(gen, gen, ReadbackSlotState::Free,
                                        ReadbackSlotState::Submitting, seq, seq));
  }

  std::printf(gFailures == 0 ? "\nRESULT: ALL PASS\n" : "\nRESULT: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
