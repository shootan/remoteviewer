#pragma once

#include <cstddef>
#include <cstdint>

namespace remote60::native_poc {

// The selection gate of a viewer that switches what it shows (the Android decoder sink): after
// the user picks a window or a screen, no frame is shown until the host has ANSWERED that pick
// with the stream generation it now sends, and then only frames of that generation.
//
// t-970r4zgo. Pulled out of android_video_decoder.cpp so the rule can be run where there is no
// MediaCodec -- the same object the APK uses, driven by Windows tests against a real host. What
// it adds to the rule the decoder had:
//   * the answer must be to THIS selection (requestTag == the selection armed by Prepare), so a
//     late answer to an earlier pick cannot open a later pick's gate;
//   * control and video travel separately, so the new generation's first IDR can arrive BEFORE
//     the answer and be dropped. If any frame of the answered generation was dropped while
//     waiting, the selection is OWED a key frame (KeyOwed) until an IDR of that generation is
//     actually HANDED TO THE DECODER (KeyDelivered) -- not when one was asked for (a request can
//     be refused on the way, either side's limiter, r4 M1-A) and not when one merely passed this
//     gate (the decoder can still drop it: no surface yet, codec not up, r5 N1). An IDR of that
//     generation dropped for want of a decoder (KeyLost) owes one again. The session asks,
//     bounded, while it is owed; OwedToken changes when the duty is re-armed (a new surface),
//     so the session's bounded retries start over for a decoder that has just become able.
// It never opens on a frame before the answer.
//
// apk-0232 r1 (Codex codex_rel_0163_e1_decision.md, the shared-session half): a LOCAL selection. A desktop pick at
// a host known to be at the logon screen (kCaptureFlagPreloginV1) gets no answer -- that host takes no WindowSelect
// and serves its one desktop. The same armed selection (same tag) then needs no ack, but keeps everything else:
// pending, the first-picture wait, the straggler filter after it. Its generation is BOUND by its first IDR that
// actually reaches the decoder; until then it owes an IDR (the session asks, bounded), deltas are dropped, and an
// IDR the decoder could not take binds nothing and is owed again. A host answer never completes a local selection;
// a local selection never completes another's.
//
// apk-decoder-liveness r4 (Codex codex_apk_decoder_liveness_src_r1.md N1): a queue is a RESERVATION, not a check.
// The decoder's worker calls the codec with its state lock released (an input buffer dequeued, then the frame
// queued), so a check made before those calls says nothing about the moment the bytes go in. r2-r3 let the
// RECEIVE side choose the local selection's candidate (Admit): an IDR of generation 202 arriving while 101 was
// being queued moved the candidate to 202, and both went into the codec. Now:
//   * the candidate of an unbound local selection is chosen by the decoder's worker, at ReserveQueue, for the one
//     frame it is about to queue. Admit lets an IDR through and changes nothing; KeyLost re-owes and leaves the
//     candidate alone. Nothing the receive side does can move a reservation. The first IDR that really reaches the
//     codec binds; an IDR of another generation is then refused at its own queue (PermitsQueue);
//   * what can still change during an exchange -- a new pick (Prepare), an abandon / a stream reset / a new binding
//     (Reset), BeginLocal, Refuse, an answer (OnAck) -- bumps the gate's epoch. CommitQueue pays / binds only under
//     the epoch the frame was reserved in; a stale reservation pays nothing and binds nothing. The decoder asks for
//     a codec reset with every such change, and voids the codec itself if a commit is ever stale without one. So an
//     exchange begun before such a change completes, if at all, into a codec that change has voided; an exchange
//     begun after it is judged against the new state.
class SelectionAckGate {
 public:
  enum class AckResult { Ignored, Applied, Failed };
  struct Ack {
    AckResult result = AckResult::Ignored;
    bool requestKeyframe = false;
  };
  // r4 N1: one frame's queue into the codec, from its start (ReserveQueue) to its end (CommitQueue / AbortQueue).
  struct Reservation {
    bool valid = false;
    uint64_t epoch = 0;
    uint64_t generation = 0;
    bool key = false;
  };

  void Prepare(uint64_t selection) {
    ++epoch_;
    pending_ = selection;
    awaiting_ = true;
    expected_ = 0;
    droppedCount_ = 0;
    keyOwed_ = false;
    local_ = false;
    candidate_ = 0;
  }

  // apk-0232 r1: the armed selection `selection`, still waiting for its answer, continues as a LOCAL one (see the
  // class comment). Only that selection, only while it waits; false otherwise (a later pick replaced it, it was
  // answered, refused or abandoned) -- then nothing changes.
  bool BeginLocal(uint64_t selection) {
    if (selection == 0 || pending_ != selection || !awaiting_ || local_) return false;
    ++epoch_;
    awaiting_ = false;
    local_ = true;
    expected_ = 0;
    candidate_ = 0;
    keyOwed_ = true;  // the first picture needs an IDR: asked for until one reaches the decoder
    ++owedToken_;
    return true;
  }

  // apk-0232 r1: the armed selection `selection` cannot be made (a window pick at the logon screen): it is over
  // as a failure -- nothing passes for it (like a failed answer). Only that selection, only while it waits.
  bool Refuse(uint64_t selection) {
    if (selection == 0 || pending_ != selection || !awaiting_) return false;
    ++epoch_;
    awaiting_ = false;
    local_ = false;
    expected_ = 0;
    keyOwed_ = false;
    return true;
  }

  // The generation local selection `selection` was bound to, 0 while unbound (or not that local selection).
  uint64_t LocalBoundFor(uint64_t selection) const {
    return (selection != 0 && local_ && pending_ == selection) ? expected_ : 0;
  }

  // r4 N1: the worker is about to queue a frame of this generation: valid only if the gate admits it NOW
  // (PermitsQueue). For an unbound local selection the IDR becomes the candidate -- the worker's own choice, which
  // nothing but the end of this reservation or an epoch change undoes.
  Reservation ReserveQueue(uint64_t frameStreamGeneration, bool isKey) {
    Reservation r;
    r.epoch = epoch_;
    r.generation = frameStreamGeneration;
    r.key = isKey;
    r.valid = PermitsQueue(frameStreamGeneration, isKey);
    if (r.valid && local_ && pending_ != 0 && expected_ == 0) candidate_ = frameStreamGeneration;
    return r;
  }

  // Still the state the reservation was made in (no Prepare / Reset / BeginLocal / Refuse / OnAck since).
  bool ReservationCurrent(const Reservation& r) const { return r.valid && r.epoch == epoch_; }

  // r4 N1: the reserved frame is in the codec. An IDR pays the owed key / binds the unbound local selection -- only
  // if the reservation is still current; false = stale (it paid and bound nothing; that codec must not be used).
  bool CommitQueue(const Reservation& r) {
    if (!ReservationCurrent(r)) return false;
    if (r.key) KeyDelivered(r.generation);
    return true;
  }

  // r4 N1: the reserved frame did not go in (no input buffer, a codec error, the exchange voided): its candidacy
  // ends -- only within the reservation's epoch (a later state is not touched).
  void AbortQueue(const Reservation& r) {
    if (!ReservationCurrent(r)) return;
    if (local_ && pending_ != 0 && expected_ == 0 && candidate_ == r.generation) candidate_ = 0;
  }

  // An IDR of the answered generation reached the decoder (queued): the duty is paid.
  // apk-0232 r1: for a local selection still unbound, the first IDR that reaches it binds it (r4: the candidate the
  // worker reserved, or -- for a caller that does not reserve -- any IDR while none is reserved).
  void KeyDelivered(uint64_t frameStreamGeneration) {
    if (local_ && pending_ != 0 && expected_ == 0) {
      if (frameStreamGeneration != 0 && (candidate_ == 0 || frameStreamGeneration == candidate_)) {
        expected_ = frameStreamGeneration;
        candidate_ = 0;
        keyOwed_ = false;
      }
      return;
    }
    if (expected_ != 0 && frameStreamGeneration == expected_) keyOwed_ = false;
  }

  // An IDR of the answered generation passed the gate but the decoder could not take it (no
  // surface, codec not configured, ...): the selection is owed one (again).
  void KeyLost(uint64_t frameStreamGeneration) {
    if (local_ && pending_ != 0 && expected_ == 0) {
      // apk-0232 r1: an unbound local selection's IDR the decoder did not take binds nothing and is owed again.
      // r4 N1: the candidate is the worker's reservation -- not undone from here.
      if (!keyOwed_) {
        keyOwed_ = true;
        ++owedToken_;
      }
      return;
    }
    if (pending_ == 0 || expected_ == 0 || frameStreamGeneration != expected_ || keyOwed_) return;
    keyOwed_ = true;
    ++owedToken_;
  }

  // The decoder became able to take an IDR (a surface arrived, r4 N3: a codec came back after a failure): if one is
  // owed, ask afresh.
  void Rearm() {
    if (keyOwed_) ++owedToken_;
  }

  // Non-zero while an IDR is owed; changes each time the duty is (re)armed.
  uint64_t OwedToken() const { return keyOwed_ ? owedToken_ : 0; }

  void Reset() {
    ++epoch_;
    pending_ = 0;
    awaiting_ = false;
    expected_ = 0;
    droppedCount_ = 0;
    keyOwed_ = false;
    local_ = false;
    candidate_ = 0;
  }

  Ack OnAck(bool ok, uint64_t streamGeneration, uint64_t requestTag) {
    // A local selection waits for no answer (awaiting_ is false): a host's answer never completes it.
    if (pending_ == 0 || !awaiting_ || requestTag != pending_) return {AckResult::Ignored, false};
    ++epoch_;
    awaiting_ = false;
    if (!ok || streamGeneration == 0) {
      expected_ = 0;
      return {AckResult::Failed, false};
    }
    expected_ = streamGeneration;
    keyOwed_ = DroppedWhileAwaiting(streamGeneration);
    if (keyOwed_) ++owedToken_;
    return {AckResult::Applied, keyOwed_};
  }

  // True = the frame may be decoded. Frames before the answer are dropped (and their generation
  // remembered); after it, only the answered generation passes.
  // Whether the frame may go to the decoder. Passing here pays nothing (KeyDelivered does).
  bool Admit(uint64_t frameStreamGeneration, bool isKey = false) {
    if (pending_ != 0 && local_ && expected_ == 0) {
      // apk-0232 r1: an unbound local selection starts at an IDR; a delta does not pass. r4 N1: passing changes
      // nothing here -- the candidate is the worker's (ReserveQueue).
      return isKey && frameStreamGeneration != 0;
    }
    if (pending_ != 0 && awaiting_) {
      RememberDropped(frameStreamGeneration);
      return false;
    }
    if (pending_ != 0 && expected_ == 0) return false;
    if (expected_ != 0 && frameStreamGeneration != expected_) return false;
    return true;
  }

  // apk-0232 r2 (N2): may a frame of this generation go into the codec NOW -- asked again at the moment of queueing
  // (a frame held for an input buffer included), with no side effect. A selection waiting for its answer admits
  // nothing; an unbound local selection only an IDR (r4: of the reserved candidate's generation while one is
  // reserved); an answered or bound one only its generation.
  bool PermitsQueue(uint64_t frameStreamGeneration, bool isKey) const {
    if (pending_ == 0) return true;
    if (awaiting_) return false;
    if (local_ && expected_ == 0) {
      return isKey && frameStreamGeneration != 0 && (candidate_ == 0 || frameStreamGeneration == candidate_);
    }
    return expected_ != 0 && frameStreamGeneration == expected_;
  }

  // The answered selection still has no IDR of its generation (see the class comment).
  bool KeyOwed() const { return keyOwed_; }
  uint64_t pending() const { return pending_; }
  bool awaiting() const { return awaiting_; }
  uint64_t expected() const { return expected_; }
  bool local() const { return local_; }
  uint64_t epoch() const { return epoch_; }

 private:
  static constexpr size_t kDroppedMemory = 4;

  void RememberDropped(uint64_t gen) {
    for (size_t i = 0; i < droppedCount_; ++i) {
      if (dropped_[i] == gen) return;
    }
    if (droppedCount_ < kDroppedMemory) {
      dropped_[droppedCount_++] = gen;
      return;
    }
    for (size_t i = 1; i < kDroppedMemory; ++i) dropped_[i - 1] = dropped_[i];
    dropped_[kDroppedMemory - 1] = gen;
  }

  bool DroppedWhileAwaiting(uint64_t gen) const {
    for (size_t i = 0; i < droppedCount_; ++i) {
      if (dropped_[i] == gen) return true;
    }
    return false;
  }

  uint64_t pending_ = 0;
  bool awaiting_ = false;
  uint64_t expected_ = 0;
  uint64_t dropped_[kDroppedMemory] = {};
  size_t droppedCount_ = 0;
  bool keyOwed_ = false;
  uint64_t owedToken_ = 0;
  bool local_ = false;      // apk-0232 r1: the armed selection needs no answer (see BeginLocal)
  uint64_t candidate_ = 0;  // ...and r4: the IDR generation the worker has reserved for it (one queue in flight)
  uint64_t epoch_ = 0;      // r4 N1: bumped by every change a reservation must not outlive
};

}  // namespace remote60::native_poc
