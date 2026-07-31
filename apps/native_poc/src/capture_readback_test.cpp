// Ring policy and buffer pool of the capture readback pipeline. Everything here runs
// without a GPU: the drop policy and the recycle-only-when-unreferenced guarantee are the
// two behaviours that silently corrupt frames when they regress.

#include <cstdio>
#include <cstring>

#include "d3d_capture_readback.hpp"

using remote60::native_poc::CaptureBufferPool;
using remote60::native_poc::pick_latest_ready_slot;

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

  std::printf(gFailures == 0 ? "\nRESULT: ALL PASS\n" : "\nRESULT: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
