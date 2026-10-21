#pragma once

// dxgi-wedge-fix r1: the recv-thread half of viewer_restore (see viewer_restore.hpp) -- inline, so the
// targets that compile the frame path without the picker (viewer_udp_recovery_test) link as before.
// Atomics and one PostMessage only.

#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {

// Recv thread: a frame of `streamGeneration` was decoded. A screen switch armed by the control thread is
// reported once a NEWER generation than the last decoded one at that moment arrives.
inline void restore_frame_decoded(ViewerState& ctx, uint64_t streamGeneration) {
  auto& r = ctx.picker.restore;
  r.lastDecodedGeneration.store(streamGeneration, std::memory_order_release);
  const uint64_t armed = r.reportAfterGenerationPlusOne.load(std::memory_order_acquire);
  if (armed == 0 || streamGeneration + 1 <= armed) return;
  uint64_t expected = armed;
  if (r.reportAfterGenerationPlusOne.compare_exchange_strong(expected, 0, std::memory_order_acq_rel) &&
      ctx.session.hwnd) {
    PostMessageW(ctx.session.hwnd, kMsgRestoreReport, 0, 0);
  }
}

}  // namespace remote60::native_poc::viewer
