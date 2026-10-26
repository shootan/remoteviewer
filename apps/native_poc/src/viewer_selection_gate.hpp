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
//                            none, with deferredRun (the control run it belongs to). Run once when the role is
//                            known (kMsgRunDeferredPick, or the UI's own re-check right after holding it).
// rel-0163 r4 (Codex codex_rel_0163_src_r3.md R3/R4): every transition and every read that decides a frame or a
// reveal takes `mu`, so Begin/BeginLocal/Clear/Convert/bind/commit are seen whole. A local op's generation is
// bound only for THAT op (its epoch, the token the receive path took when it began the frame), so an old frame
// can never bind a newer op; a host ack never touches a local op (IgnoredLocal).
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

#include <functional>
#include <mutex>
#include <utility>

namespace remote60::native_poc::viewer {

// What the host's WindowSelected ack did to the in-flight selection (ApplyAck).
enum class SelectionAck : uint8_t {
  NotPending = 0,  // no PC-side selection tracked (legacy stream-view session): caller's old path
  Acked = 1,       // the transaction now waits for the first frame of its generation
  Failed = 2,      // the caller stops the speculative stream and clears the transaction
  IgnoredLocal = 3,  // rel-0163 r4: the in-flight op is local (no ack asked) -- an ack changes nothing
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
  // rel-0163 r6 (R3 2): the op whose reveal was committed (with activeStreamGeneration, its generation) -- only
  // THAT op's later pictures go on after its pending is over; a cancelled op of the same epoch is not it.
  std::atomic<uint64_t> revealedOp{0};
  std::atomic<bool> localAckless{false};     // rel-0163 r3: see above
  std::atomic<uint64_t> deferredPick{0};     // rel-0163 r3: UI writes, control reads (posts), UI runs
  std::atomic<uint64_t> deferredRun{0};      // rel-0163 r4: the control run the held pick belongs to
  mutable std::mutex mu;                     // rel-0163 r4: the transitions and the deciding reads
  // Tests only (empty in the product): runs between the UI's role read and the held pick's store -- a
  // pong landing exactly there (Codex R1's order).
  std::function<void()> testAfterRoleRead;
  // Tests only: right before a decoded picture's final adoption (CommitFrame) -- a cancel / new op lands here.
  std::function<void()> testBeforeFrameCommit;
  // Tests only: between a picture's permission and its write (the gate's lock held) -- a Begin/Clear tried here
  // must wait until the write is done.
  std::function<void()> testAfterFramePermit;
  // Tests only: right before a host ack's boundary (apply_window_selected_result) -- a local op begins here.
  std::function<void()> testBeforeAckBoundary;
  std::atomic<uint64_t> localFramesRefused{0};  // pictures of a no-longer-live local op not adopted (telemetry)
  // cross-thread: recv records, UI revalidates and commits.
  std::atomic<uint64_t> readyGeneration{0};
  std::atomic<uint64_t> readyEpoch{0};
  std::atomic<bool> revealPosted{false};

  // The transitions (viewer_selection_gate.cpp), each the atomic sequence the former free function
  // or inline block performed, in the same order. Time-free, so viewer_selection_gate_test drives them.
  void Begin();                                            // UI: a pick was accepted for sending
  void BeginLocal();                                       // UI: rel-0163 r3 -- a desktop pick at a logon-screen host
  bool ConvertToLocal(uint64_t opEpoch);                   // UI: that op's WindowSelect(0) was not sendable after all
  bool LocalUnbound(uint64_t opEpoch) const;               // recv: THAT local op still waits for its keyframe
  // recv: bind THAT local op to the generation of the keyframe whose decode produced its first output.
  bool BindLocalGeneration(uint64_t opEpoch, uint64_t streamGeneration);
  bool IsLiveLocal(uint64_t opEpoch) const;                // recv: the frame's op is the live local op
  // rel-0163 r5 (Codex codex_rel_0163_src_r4.md R3): the FINAL adoption of a decoded picture, called inside the
  // FrameBuffer's own lock right before it is written. `frameLocal` is what the frame was processed as (taken when
  // its op token was taken -- a frame of a local op is never re-read as an ordinary one). For a local frame: only
  // while its op is still the live local op; an unbound op is bound HERE to `candidateGen` (a valid output exists
  // by now). *revealGen = the generation this adoption stands for. Ordinary frames: permitted as before.
  // rel-0163 r6 (Codex codex_rel_0163_src_r5.md R3 1): when it permits, the gate's lock is handed to the caller
  // in *held and must be kept until the FrameBuffer write is done -- a Begin/Clear (which take only this lock)
  // can then never fall between the permission and the write. Lock order: FrameBuffer, then this.
  bool CommitFrame(uint64_t opEpoch, bool frameLocal, uint64_t candidateGen, uint64_t inputGeneration,
                   uint64_t* revealGen, std::unique_lock<std::mutex>* held);
  // rel-0163 r5 (R4): a host WindowSelected, applied to the panel AND the gate under one boundary. `applyPanel`
  // runs only when the in-flight op asked for an ack (never for a local op) and returns {ok, streamGeneration}.
  SelectionAck ApplyHostAck(const std::function<std::pair<bool, uint64_t>()>& applyPanel);
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
