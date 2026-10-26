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
//   localAckless           : rel-0163 r3 (Codex codex_rel_0163_e1_decision.md): a DESKTOP pick at a host
//                            known to be at the logon screen (kCaptureFlagPreloginV1). Such a host takes no
//                            WindowSelect (its allowlist), so no ack will ever come: the transaction is the
//                            same -- pending, its own epoch (decoder reset, wait for a keyframe) -- but its
//                            generation is the one of the FIRST KEYFRAME admitted after it began (bound
//                            once, BindLocalGeneration); until then nothing can reveal, after it frames of
//                            another generation drop exactly as after an ack.
//   deferredPick           : a pick made before the host's role was known (its first pong) -- id + 1, 0 =
//                            none. Run once when the role is known (kMsgRunDeferredPick), never sent blind.
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

// What the host's WindowSelected ack did to the in-flight selection (ApplyAck).
enum class SelectionAck : uint8_t {
  NotPending = 0,  // no PC-side selection tracked (legacy stream-view session): caller's old path
  Acked = 1,       // the transaction now waits for the first frame of its generation
  Failed = 2,      // the caller stops the speculative stream and clears the transaction
};

// Whether a decoded frame of a given stream generation may be presented (AdmitGeneration).
enum class SelectionAdmit : uint8_t {
  Accept = 0,
  DropAwaitingAck = 1,       // no ack yet: old target or an unconfirmed guess
  DropWrongGeneration = 2,   // the previous target's stream still draining after the ack
  DropStraggler = 3,         // after a reveal, not the active target's generation
};

struct SelectionGateState {
  // cross-thread: UI/control write, recv reads.
  std::atomic<bool> pending{false};
  std::atomic<bool> awaitingAck{false};
  std::atomic<uint64_t> expectedGeneration{0};
  std::atomic<uint64_t> epoch{0};
  std::atomic<uint64_t> activeStreamGeneration{0};
  std::atomic<bool> localAckless{false};     // rel-0163 r3: see above
  std::atomic<uint64_t> deferredPick{0};     // rel-0163 r3: UI writes, control reads (posts), UI runs
  // cross-thread: recv records, UI revalidates and commits.
  std::atomic<uint64_t> readyGeneration{0};
  std::atomic<uint64_t> readyEpoch{0};
  std::atomic<bool> revealPosted{false};

  // The transitions (viewer_selection_gate.cpp), each the atomic sequence the former free function
  // or inline block performed, in the same order. Time-free, so viewer_selection_gate_test drives them.
  void Begin();                                            // UI: a pick was accepted for sending
  void BeginLocal();                                       // UI: rel-0163 r3 -- a desktop pick at a logon-screen host
  bool ConvertToLocal(uint64_t opEpoch);                   // UI: that op's WindowSelect(0) was not sendable after all
  bool LocalUnbound() const;                               // recv: a local transaction still waiting for its keyframe
  bool BindLocalGeneration(uint64_t streamGeneration);     // recv: bind it to the first admitted keyframe's generation
  void Clear();                                            // any thread: drop the in-flight transaction
  SelectionAck ApplyAck(bool ok, uint64_t streamGeneration);   // control: the host's WindowSelected
  bool EpochChanged(uint64_t& seenEpoch);                  // recv: a new pick since the last frame (reset the decoder)
  SelectionAdmit AdmitGeneration(uint64_t streamGeneration) const;  // recv: may this frame be presented
  bool AckedSelectionPending() const;                      // recv: a first frame would complete the transaction
  bool RecordReveal(uint64_t readyGeneration, uint64_t readyEpoch);  // recv: record the candidate; true = post once
  bool CommitReveal();                                     // UI: revalidate the posted candidate; true = commit
  void ReleaseRevealLatch();                               // UI: after handling the post, allow a re-post
};

}  // namespace remote60::native_poc::viewer
