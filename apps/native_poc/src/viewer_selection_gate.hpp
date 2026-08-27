#pragma once

// The PC-side target-selection gate (Phase 1-7 state struct).
//
// Role:    the in-flight selection transaction and the persistent active-generation filter.
// Thread:  UI begins/clears a selection and commits the reveal; the control thread applies the
//          host's ack; the recv thread gates frames and records/posts the reveal candidate. All
//          fields are atomics; the protocol between them is described below and is unchanged.
// Input:   picker clicks, WindowSelected acks, decoded frame generations.
// Output:  whether a frame may be presented, when the picker closes.
// Callers: viewer_picker, viewer_window_proc (kMsgRevealStreamView), recv thread, viewer_cursor_overlay.
//
// Target-selection gate, mirroring the Android policy (commit 4892dea). After connecting the
// session opens on the picker; picking a target starts the stream but the picker stays up, and
// video is not presented, until the first frame of the *acknowledged* generation has decoded.
// That keeps an initial default-desktop frame -- or a frame from the previously selected target
// -- from flashing under the picker, and keeps a slow first frame from being mistaken for a
// failed selection.
//   pending                : a selection is in flight (from click until first frame or failure).
//   awaitingAck            : request sent, host's WindowSelected ack not yet seen.
//   expectedGeneration     : the ack's streamGeneration for the *in-flight* transaction;
//                            frames of other generations drop while pending.
//   epoch                  : bumped per selection so the receive loop resets the decoder once.
//   activeStreamGeneration : generation of the last successfully revealed selection; after
//                            reveal this is the persistent filter (0 = accept anything, which
//                            covers the legacy stream-view start and the window before any pick).
// The reveal is decided on the video thread but *committed* on the UI thread, so a cancel / new
// selection / disconnect that races the post cannot wrongly close the picker. The video thread
// records the candidate (generation + epoch) and posts once; the UI handler revalidates against
// the live selection state before committing, and always releases the latch so a later legitimate
// first frame can re-post.
//
// Fields are the former globals gSelectionPending / gSelectionAwaitingAck /
// gSelectionExpectedGeneration / gSelectionEpoch / gActiveStreamGeneration /
// gSelectionReadyGeneration / gSelectionReadyEpoch / gSelectionRevealPosted, initialisers unchanged
// (viewer split refactor Phase 1-7).

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

struct SelectionGateState {
  // cross-thread: UI/control write, recv reads.
  std::atomic<bool> pending{false};
  std::atomic<bool> awaitingAck{false};
  std::atomic<uint64_t> expectedGeneration{0};
  std::atomic<uint64_t> epoch{0};
  std::atomic<uint64_t> activeStreamGeneration{0};
  // cross-thread: recv records, UI revalidates and commits.
  std::atomic<uint64_t> readyGeneration{0};
  std::atomic<uint64_t> readyEpoch{0};
  std::atomic<bool> revealPosted{false};
};

}  // namespace remote60::native_poc::viewer
