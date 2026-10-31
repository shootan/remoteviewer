#pragma once

// apk-inbox C (design caplife/design_apk_decoder_inbox_r2.md r3): the decoder's bounded frame inbox that knows the
// reference chain, and the recovery episode that asks the host for an IDR when the chain is lost. Pure logic (no
// codec, no lock of its own -- the owner's lock covers it), shared with the Windows tests.
//
// ORDER. The session's UDP assembler delivers AUs in sequence order (native_video_client_shared_core.cpp
// PopDelivery:906-915 takes the OLDEST assembly; :1058 ignores any packet not newer than the last delivered AU), so
// the sink sees strictly increasing `seq`. The duplicate / older-than-last checks here are DEFENSIVE: what a second
// path or a later change could feed in. Comparison: (int32_t)(b - a) > 0 means b is after a (wrap-safe within 2^31).
// A safety valve: kOrderResyncAfter such drops in a row are taken as a restarted sequence, not reordering -- the order
// starts over from the next AU (counted), so no unforeseen restart can blank the picture for good.
//
// THE INBOX (§1, §2). At most kMaxFrames frames and kMaxBytes bytes. Arriving frames:
//   * older than or equal to the last one admitted (same generation): dropped (stale / duplicate), counted;
//   * larger than kMaxBytes on its own: refused (oversize) -- a LOSS;
//   * a delta while the tail gap is open (a loss happened and no usable IDR came since): refused -- it references
//     what was lost, so it could only corrupt the picture; it is part of the same loss;
//   * an IDR after the loss point: admitted and closes the tail gap.
// When the inbox is over its bounds after an admit, in this order (the decision table, §1 "usable IDR"):
//   1. an IDR in the inbox whose IDR + everything after it fits: the EARLIEST such IDR is kept with its whole suffix,
//      everything before it goes (the older chain is skipped, not lost: decoding restarts at that IDR) -- no loss;
//   2. an IDR in the inbox but no IDR's suffix fits: from the LAST IDR on is kept and the NEWEST frames go until it
//      fits -- the cut is a loss at the tail (the kept IDR + its shortened suffix still decode) and the tail gap opens;
//   3. no IDR in the inbox: everything goes (all of it continues a chain whose next reference is gone) -- a loss.
// A loss opens the tail gap; `gapAfterSeq` is the newest AU lost, and only an IDR after it is usable.
//
// THE EPISODE (§2, §3, r3 보충). A chain loss opens a recovery episode (if none is open). It asks for an IDR at most
// kMaxRequests times: the first at once, then 1 / 2 / 4 s after the previous request was actually SENT (OnSent --
// the session calls it when the control exchange starts; a limiter refusal or a request merged into one already
// pending is not a send). While a usable IDR is preserved (in the inbox, the decoder's pending slot, being processed)
// nothing is asked. After the 4th send, a final window of kFinalWindowUs for the answer; then, with no usable IDR
// preserved, the episode is EXHAUSTED: recorded once, nothing more is asked by it -- but a later usable IDR (the host's
// periodic key) is still admitted and decoded, and its commit ends the state. Only an IDR committed to the codec
// (CommitQueue) that comes after the loss point ends an episode; one lost again resumes the same episode with the
// budget and deadlines it had. Invalidate() (a new pick, bind, unbind, stream reset, abort, new surface) ends it at once.
// A want handed out and never sent (the session lost it -- not expected: held wants are retried, accepted ones sent or
// judged stale) is handed out again after kReissueUs; that is not an attempt.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

namespace remote60::native_poc {

inline bool chain_seq_after(uint32_t b, uint32_t a) { return static_cast<int32_t>(b - a) > 0; }

struct ChainAuInfo {
  uint64_t gen = 0;
  uint32_t seq = 0;
  bool key = false;
  size_t bytes = 0;
};

enum class ChainAdmit : uint8_t {
  Admitted = 0,
  Duplicate,  // same generation and seq as the last admitted
  Stale,      // same generation, older than the last admitted
  Oversize,   // larger than the whole byte bound -- a loss
  GapDelta,   // a delta while the tail gap is open -- part of that loss
};

struct ChainOfferResult {
  ChainAdmit admit = ChainAdmit::Admitted;
  bool loss = false;           // this offer lost something of the chain (a loss point moved / opened)
  uint32_t lossSeq = 0;        // the newest AU lost by this offer (valid if loss)
  size_t skippedFrames = 0;    // case 1: older-chain frames dropped in favour of a later IDR (no loss)
  size_t lostFrames = 0;       // frames dropped as a loss (cases 2 / 3, oversize, gap delta)
  // IDRs dropped by this offer whose generation has NO other IDR left in the inbox -- the caller tells the selection
  // gate (KeyLost) for these; an IDR skipped for a later IDR of the same generation is not lost.
  size_t keysLost = 0;
  uint64_t keyLostGen = 0;     // the generation of the last such IDR
};

// T: the stored item; InfoFn: ChainAuInfo(const T&).
template <class T, class InfoFn>
class ChainInbox {
 public:
  ChainInbox(size_t maxFrames, size_t maxBytes, InfoFn info) : maxFrames_(maxFrames), maxBytes_(maxBytes), info_(info) {}

  ChainOfferResult Offer(T&& item) {
    ChainOfferResult r;
    const ChainAuInfo a = info_(item);
    if (false && haveLast_ && a.gen == lastGen_ && orderDropsInRow_ < kOrderResyncAfter) {
      if (a.seq == lastSeq_) {
        r.admit = ChainAdmit::Duplicate;
        ++duplicates_;
        ++orderDropsInRow_;
        return r;
      }
      if (!chain_seq_after(a.seq, lastSeq_)) {
        r.admit = ChainAdmit::Stale;
        ++stale_;
        ++orderDropsInRow_;
        return r;
      }
    }
    if (haveLast_ && a.gen == lastGen_ && orderDropsInRow_ >= kOrderResyncAfter) {
      // the safety valve: so many in a row is no reordering -- the sequence restarted (not expected: the assembler
      // ignores anything not newer, and a stream reset clears this inbox). The order starts over from this AU.
      ++orderResyncs_;
    }
    orderDropsInRow_ = 0;
    haveLast_ = true;
    lastGen_ = a.gen;
    lastSeq_ = a.seq;
    if (a.bytes > maxBytes_) {
      r.admit = ChainAdmit::Oversize;
      ++oversize_;
      LoseOne(a, &r);
      return r;
    }
    if (gapOpen_) {
      if (!a.key) {
        r.admit = ChainAdmit::GapDelta;
        ++gapDeltas_;
        LoseOne(a, &r);
        return r;
      }
      gapOpen_ = false;  // an IDR after the loss point (it is newer than everything seen): usable
    }
    bytes_ += a.bytes;
    items_.push_back(std::move(item));
    if (items_.size() > maxFrames_ || bytes_ > maxBytes_) Trim(&r);
    return r;
  }

  bool Pop(T* out) {
    if (items_.empty()) return false;
    const ChainAuInfo a = info_(items_.front());
    bytes_ -= (a.bytes <= bytes_ ? a.bytes : bytes_);
    *out = std::move(items_.front());
    items_.pop_front();
    return true;
  }

  // Everything goes and the order and the gap start over (a new pick / binding / stream).
  void Clear() {
    items_.clear();
    bytes_ = 0;
    gapOpen_ = false;
    haveLast_ = false;
    orderDropsInRow_ = 0;
  }

  // Duplicate / older AUs in a row after which the order is taken to have restarted (about a second at 30 fps).
  static constexpr uint32_t kOrderResyncAfter = 30;

  // A usable IDR newer than `afterSeq` is in the inbox (a candidate for the open episode).
  bool HoldsKeyAfter(uint32_t afterSeq) const {
    for (const T& t : items_) {
      const ChainAuInfo a = info_(t);
      if (a.key && chain_seq_after(a.seq, afterSeq)) return true;
    }
    return false;
  }

  // A loss seen outside the inbox (the decoder's own: a codec that lost its references, a delta it could not take, a
  // frame its pending slot gave up): the tail gap opens after `seq` -- deltas are refused from here until a usable IDR.
  // Not if the inbox already holds an IDR after `seq`: the chain restarts there, and what follows it is that chain's.
  void MarkLoss(uint32_t seq) {
    if (HoldsKeyAfter(seq)) return;
    if (!gapOpen_ || chain_seq_after(seq, gapAfterSeq_)) gapAfterSeq_ = seq;
    gapOpen_ = true;
  }

  size_t frames() const { return items_.size(); }
  size_t bytes() const { return bytes_; }
  bool empty() const { return items_.empty(); }
  bool gapOpen() const { return gapOpen_; }
  uint32_t gapAfterSeq() const { return gapAfterSeq_; }
  uint64_t duplicates() const { return duplicates_; }
  uint64_t stale() const { return stale_; }
  uint64_t oversize() const { return oversize_; }
  uint64_t gapDeltas() const { return gapDeltas_; }
  uint64_t skipped() const { return skipped_; }
  uint64_t lost() const { return lost_; }
  uint64_t orderResyncs() const { return orderResyncs_; }

 private:
  void NoteLoss(uint32_t seq, ChainOfferResult* r) {
    if (!r->loss || chain_seq_after(seq, r->lossSeq)) r->lossSeq = seq;
    r->loss = true;
    if (!gapOpen_ || chain_seq_after(seq, gapAfterSeq_)) gapAfterSeq_ = seq;
    gapOpen_ = true;
  }
  void LoseOne(const ChainAuInfo& a, ChainOfferResult* r) {
    ++r->lostFrames;
    ++lost_;
    if (a.key && !HoldsKeyOfGen(a.gen)) {
      ++r->keysLost;
      r->keyLostGen = a.gen;
    }
    NoteLoss(a.seq, r);
  }
  bool HoldsKeyOfGen(uint64_t gen) const {
    for (const T& t : items_) {
      const ChainAuInfo a = info_(t);
      if (a.key && a.gen == gen) return true;
    }
    return false;
  }
  // Drop the front item: `asLoss` (a chain loss) or skipped (an older chain left for a later IDR).
  void DropFront(bool asLoss, ChainOfferResult* r) {
    const ChainAuInfo a = info_(items_.front());
    bytes_ -= (a.bytes <= bytes_ ? a.bytes : bytes_);
    items_.pop_front();
    CountDropped(a, asLoss, r);
  }
  void DropBack(ChainOfferResult* r) {
    const ChainAuInfo a = info_(items_.back());
    bytes_ -= (a.bytes <= bytes_ ? a.bytes : bytes_);
    items_.pop_back();
    CountDropped(a, true, r);
  }
  void CountDropped(const ChainAuInfo& a, bool asLoss, ChainOfferResult* r) {
    if (asLoss) {
      ++r->lostFrames;
      ++lost_;
      NoteLoss(a.seq, r);
    } else {
      ++r->skippedFrames;
      ++skipped_;
    }
    if (a.key && !HoldsKeyOfGen(a.gen)) {
      ++r->keysLost;
      r->keyLostGen = a.gen;
    }
  }
  void Trim(ChainOfferResult* r) {
    // case 1: the earliest IDR whose IDR + suffix fits
    size_t suffixFrames = 0, suffixBytes = 0;
    long fit = -1, lastKey = -1;
    for (long i = static_cast<long>(items_.size()) - 1; i >= 0; --i) {
      const ChainAuInfo a = info_(items_[static_cast<size_t>(i)]);
      ++suffixFrames;
      suffixBytes += a.bytes;
      if (!a.key) continue;
      if (lastKey < 0) lastKey = i;
      if (suffixFrames <= maxFrames_ && suffixBytes <= maxBytes_) fit = i;  // keeps moving to the earliest that fits
    }
    if (fit >= 0) {
      for (long i = 0; i < fit; ++i) DropFront(false, r);
      return;
    }
    if (lastKey >= 0) {
      // case 2: from the last IDR on, the newest frames cut until it fits (a loss at the tail)
      for (long i = 0; i < lastKey; ++i) DropFront(false, r);
      while (items_.size() > maxFrames_ || bytes_ > maxBytes_) DropBack(r);
      return;
    }
    // case 3: no IDR -- the whole chain in here is undecodable once its head goes
    while (!items_.empty()) DropFront(true, r);
  }

  size_t maxFrames_;
  size_t maxBytes_;
  InfoFn info_;
  std::deque<T> items_;
  size_t bytes_ = 0;
  bool gapOpen_ = false;
  uint32_t gapAfterSeq_ = 0;
  bool haveLast_ = false;
  uint64_t lastGen_ = 0;
  uint32_t lastSeq_ = 0;
  uint64_t duplicates_ = 0, stale_ = 0, oversize_ = 0, gapDeltas_ = 0, skipped_ = 0, lost_ = 0;
  uint32_t orderDropsInRow_ = 0;
  uint64_t orderResyncs_ = 0;
};

class KeyRecoveryEpisode {
 public:
  static constexpr uint32_t kMaxRequests = 4;                // the first + 3 retries
  static constexpr uint64_t kBackoffUs[kMaxRequests - 1] = {1000000ULL, 2000000ULL, 4000000ULL};
  static constexpr uint64_t kFinalWindowUs = 4000000ULL;     // after the 4th send: its answer may still come
  static constexpr uint64_t kReissueUs = 4000000ULL;         // a want handed out but never sent
  enum class State : uint8_t { Idle = 0, Open, Exhausted };

  // A loss of the chain after which only an IDR newer than `lossSeq` is usable. Opens an episode if none is open;
  // an open one keeps its id, budget and deadlines (the loss point moves forward); an exhausted one stays exhausted.
  void OnLoss(uint32_t cause, uint32_t lossSeq, uint64_t nowUs) {
    causes_ |= cause;
    if (state_ == State::Idle) {
      state_ = State::Open;
      ++id_;
      ++opened_;
      attempts_ = 0;
      wantOut_ = false;
      nextDueUs_ = 0;
      finalDeadlineUs_ = 0;
      openedUs_ = nowUs;
      lossSeq_ = lossSeq;
      causes_ = cause;
      return;
    }
    if (chain_seq_after(lossSeq, lossSeq_)) lossSeq_ = lossSeq;
  }

  // A want for the session now? `candidate`: a usable IDR (after the loss point) is preserved somewhere.
  bool TakeWant(uint64_t nowUs, bool candidate, uint64_t* episodeId) {
    Tick(nowUs, candidate);
    if (state_ != State::Open || candidate || attempts_ >= kMaxRequests) return false;
    if (wantOut_) {
      if (nowUs - wantOutUs_ < kReissueUs) return false;
      ++reissued_;
    } else if (attempts_ > 0 && nowUs < nextDueUs_) {
      return false;
    }
    wantOut_ = true;
    wantOutUs_ = nowUs;
    ++wants_;
    if (episodeId) *episodeId = id_;
    return true;
  }

  // The session started the control exchange for a request carrying this episode.
  void OnSent(uint64_t episodeId, uint64_t nowUs) {
    if (state_ != State::Open || episodeId != id_ || attempts_ >= kMaxRequests) {
      ++staleSends_;
      return;
    }
    wantOut_ = false;
    ++attempts_;
    ++sent_;
    if (attempts_ < kMaxRequests) {
      nextDueUs_ = nowUs + kBackoffUs[attempts_ - 1];
    } else {
      finalDeadlineUs_ = nowUs + kFinalWindowUs;
    }
  }

  // Exhausted once the 4th request's window has passed with no usable IDR preserved. true: it just became exhausted.
  bool Tick(uint64_t nowUs, bool candidate) {
    if (state_ != State::Open || attempts_ < kMaxRequests || candidate || nowUs < finalDeadlineUs_) return false;
    state_ = State::Exhausted;
    wantOut_ = false;
    ++exhausted_;
    return true;
  }

  // An IDR went into the codec (CommitQueue). Ends the episode (or the exhausted state) if it comes after the loss.
  bool OnKeyCommitted(uint32_t seq) {
    if (state_ == State::Idle || !chain_seq_after(seq, lossSeq_)) return false;
    state_ = State::Idle;
    wantOut_ = false;
    ++closed_;
    return true;
  }

  // A new pick / binding / stream / surface, an abort: whatever was open or exhausted is moot.
  void Invalidate() {
    if (state_ != State::Idle) ++invalidated_;
    state_ = State::Idle;
    ++id_;  // a stale send / want of the old one never matches again
    wantOut_ = false;
    attempts_ = 0;
    causes_ = 0;
  }

  // A want of this episode was judged moot at the send point (a usable IDR is preserved after all): no send came of
  // it, so the next want is not held back by it (not an attempt either).
  void OnWantDropped(uint64_t episodeId) {
    if (episodeId == id_) wantOut_ = false;
  }

  bool StillValid(uint64_t episodeId) const { return state_ == State::Open && episodeId == id_; }
  State state() const { return state_; }
  uint64_t id() const { return id_; }
  uint32_t attempts() const { return attempts_; }
  uint32_t causes() const { return causes_; }
  uint32_t lossSeq() const { return lossSeq_; }
  uint64_t opened() const { return opened_; }
  uint64_t closed() const { return closed_; }
  uint64_t exhausted() const { return exhausted_; }
  uint64_t invalidated() const { return invalidated_; }
  uint64_t wants() const { return wants_; }
  uint64_t sent() const { return sent_; }
  uint64_t staleSends() const { return staleSends_; }
  uint64_t reissued() const { return reissued_; }
  const char* StateName() const {
    return state_ == State::Idle ? "idle" : state_ == State::Open ? "open" : "exhausted";
  }

 private:
  State state_ = State::Idle;
  uint64_t id_ = 0;
  uint32_t attempts_ = 0;
  uint32_t causes_ = 0;
  uint32_t lossSeq_ = 0;
  bool wantOut_ = false;
  uint64_t wantOutUs_ = 0;
  uint64_t nextDueUs_ = 0;
  uint64_t finalDeadlineUs_ = 0;
  uint64_t openedUs_ = 0;
  uint64_t opened_ = 0, closed_ = 0, exhausted_ = 0, invalidated_ = 0, wants_ = 0, sent_ = 0, staleSends_ = 0,
           reissued_ = 0;
};

}  // namespace remote60::native_poc
