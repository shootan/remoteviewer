#pragma once

// Clipboard image v1: the transfer state machines (plan r1 ⑵ + r2 8-1..8-4), direction A.
//
// Role:    ClipImageReceiver -- the host's side: judges an Offer (gate, busy, disabled), hands out
//          the epochTag and bulk generation, keeps a small window of pulls outstanding, accepts
//          only the chunk each pull asked for, and holds the transfer's state for Status/Cancel
//          until a newer offer replaces it. ClipImageSender -- the viewer's side: owns the package,
//          answers only pulls that name the current transfer and stay inside it, and records
//          when each chunk was asked for and answered (the rate controller's evidence).
// Thread:  not synchronised. The host wraps the receiver in its service mutex (control
//          dispatchers and the bulk worker both reach it); the viewer's sender is driven by its
//          bulk worker, with the control thread reading state under the owner's mutex.
// Callers: host_clip_image.cpp, viewer_clip_image.cpp, clip_image_transfer_test.
//
// Pure: no sockets, no clipboard, no clock of its own (every entry point takes nowUs). What
// travels is the package [PNG][UTF-16 text]; SHA-256, the PNG header check and the decode are the
// caller's, after Complete.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include "clip_image_core.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

constexpr uint32_t kClipImageChunkBytes = 16u * 1024u;       // plan r1 ⑶
constexpr uint32_t kClipImageMaxChunkBytes = 64u * 1024u;    // what a sender will answer at most
constexpr uint32_t kClipImagePullWindowDefault = 2;           // plan r2 §2 (logical: head-only channel)
constexpr uint32_t kClipImagePullWindowMax = 4;
constexpr uint64_t kClipImageStallUs = 30000000;             // 30 s without a chunk
constexpr uint64_t kClipImageOfferReplyTimeoutUs = 5000000;
constexpr uint64_t kClipImageStatusIntervalUs = 500000;
constexpr uint64_t kClipImageTimeoutFloorBps = 128000;       // the wall-clock budget's rate
constexpr uint64_t kClipImageTimeoutSlackUs = 30000000;
constexpr uint64_t kClipImageTimeoutCapUs = 20ull * 60ull * 1000000ull;

/** Wall-clock budget for a package: its bits at 128 kbps + 30 s, at most 20 min (plan r2 8-1). */
inline uint64_t clip_image_total_timeout_us(uint64_t packageBytes) {
  const uint64_t us = packageBytes * 8ull * 1000000ull / kClipImageTimeoutFloorBps + kClipImageTimeoutSlackUs;
  return (std::min)(us, kClipImageTimeoutCapUs);
}

inline bool clip_image_state_terminal(ClipImageState s) {
  return s == ClipImageState::Published || s == ClipImageState::Failed || s == ClipImageState::Cancelled ||
         s == ClipImageState::Superseded;
}

/** What an Offer says, decoded from the wire message. */
struct ClipImageOffer {
  uint64_t transferId = 0;
  uint64_t revision = 0;
  uint8_t formats = 0;
  uint32_t pngBytes = 0;
  uint32_t textUtf16 = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t sha256[32] = {};

  uint64_t packageBytes() const { return static_cast<uint64_t>(pngBytes) + 2ull * textUtf16; }
};

inline ClipImageOffer clip_image_offer_from_wire(const ControlClipImageOfferMessage& m) {
  ClipImageOffer o;
  o.transferId = m.transferId;
  o.revision = m.revision;
  o.formats = m.formats;
  o.pngBytes = m.pngBytes;
  o.textUtf16 = m.textUtf16;
  o.width = m.width;
  o.height = m.height;
  std::memcpy(o.sha256, m.sha256, sizeof(o.sha256));
  return o;
}

/** The offer's own consistency, before any limit: can these numbers describe a package at all? */
inline bool clip_image_offer_well_formed(const ClipImageOffer& o) {
  if (o.transferId == 0) return false;
  if ((o.formats & kClipImageFormatPng) == 0) return false;
  if ((o.formats & ~(kClipImageFormatPng | kClipImageFormatText)) != 0) return false;
  if (o.pngBytes < 8) return false;  // not even a PNG signature
  const bool hasText = (o.formats & kClipImageFormatText) != 0;
  if (hasText != (o.textUtf16 > 0)) return false;
  return true;
}

/** Offer verdict from the numbers alone (the receiver adds busy / disabled). */
inline ClipImageVerdict clip_image_offer_verdict(const ClipImageOffer& o) {
  if (!clip_image_offer_well_formed(o)) return ClipImageVerdict::BadRequest;
  if (o.width == 0 || o.height == 0) return ClipImageVerdict::BadDims;
  const uint64_t textBytes = 2ull * o.textUtf16;
  const ClipImageGate g = clip_image_gate(o.width, o.height, o.pngBytes, textBytes);
  if (!g.ok()) {
    return (g.reason == ClipImageGateReason::ZeroSize) ? ClipImageVerdict::BadDims : ClipImageVerdict::TooLarge;
  }
  return ClipImageVerdict::Accept;
}

/** Host side, direction A. */
class ClipImageReceiver {
 public:
  explicit ClipImageReceiver(uint32_t processRandom32, uint32_t pullWindow = kClipImagePullWindowDefault)
      : processRandom_(processRandom32),
        pullWindow_((std::max<uint32_t>)(1, (std::min)(pullWindow, kClipImagePullWindowMax))) {}

  struct OfferResult {
    ClipImageVerdict verdict = ClipImageVerdict::BadRequest;
    uint64_t epochTag = 0;
    uint32_t bulkGen = 0;
  };

  /**
   * Judges an offer. `localSeqAtAccept` is the host clipboard's sequence number now: publishing is
   * allowed later only if it has not moved (r2 8-4). One transfer per session (r2 8-2): while one
   * is running, every new offer is Busy -- including the same viewer's, which cancels first.
   */
  OfferResult OnOffer(const ClipImageOffer& o, uint64_t sessionEpoch, bool enabled, uint64_t localSeqAtAccept,
                      uint64_t nowUs) {
    OfferResult r;
    if (!enabled) { r.verdict = ClipImageVerdict::Disabled; return r; }
    if (Active()) { r.verdict = ClipImageVerdict::Busy; return r; }
    r.verdict = clip_image_offer_verdict(o);
    if (r.verdict != ClipImageVerdict::Accept) return r;
    std::vector<uint8_t> buf;
    try {
      buf.assign(static_cast<size_t>(o.packageBytes()), 0);  // the declared total only (plan r2 §9 R1)
    } catch (...) {
      r.verdict = ClipImageVerdict::TooLarge;
      return r;
    }
    offer_ = o;
    package_ = std::move(buf);
    epochTag_ = (static_cast<uint64_t>(processRandom_) << 32) | (sessionEpoch & 0xFFFFFFFFull);
    bulkGen_ = gens_.Next();
    state_ = ClipImageState::Pulling;
    reason_ = ClipImageReason::None;
    received_ = 0;
    nextOffset_ = 0;
    outstanding_.clear();
    acceptedUs_ = nowUs;
    lastProgressUs_ = nowUs;
    deadlineUs_ = nowUs + clip_image_total_timeout_us(o.packageBytes());
    localSeqAtAccept_ = localSeqAtAccept;
    cancelRequested_ = false;
    r.epochTag = epochTag_;
    r.bulkGen = bulkGen_;
    return r;
  }

  /** The next pull to send, while fewer than the window are outstanding. */
  bool NextPull(ClipBulkPullMessage* out, uint32_t triggerOffset = 0xFFFFFFFFu) {
    if (state_ != ClipImageState::Pulling) return false;
    if (outstanding_.size() >= pullWindow_) return false;
    const uint64_t total = package_.size();
    if (nextOffset_ >= total) return false;
    const uint32_t len = static_cast<uint32_t>((std::min<uint64_t>)(chunkBytes_, total - nextOffset_));
    ClipBulkPullMessage p{};
    p.header.type = static_cast<uint16_t>(MessageType::ClipBulkPull);
    p.header.size = sizeof(p);
    p.transferId = offer_.transferId;
    p.epochTag = epochTag_;
    p.bulkGen = bulkGen_;
    p.offset = static_cast<uint32_t>(nextOffset_);
    p.len = len;
    p.triggerOffset = triggerOffset;
    p.bytesReceived = static_cast<uint32_t>(received_);
    outstanding_.push_back({p.offset, len});
    nextOffset_ += len;
    *out = p;
    return true;
  }

  enum class ChunkResult : uint8_t { Dropped = 0, Accepted, Complete };

  /**
   * A chunk off the bulk stream. Anything not naming the current transfer, epoch and generation,
   * or not exactly a range a pull asked for, is dropped without effect: an old or forged datagram
   * never cancels, advances or corrupts the current transfer (r2 8-3).
   */
  ChunkResult OnChunk(const ClipBulkChunkHeader& h, const uint8_t* data, size_t dataLen, uint64_t nowUs) {
    if (state_ != ClipImageState::Pulling) return ChunkResult::Dropped;
    if (h.transferId != offer_.transferId || h.epochTag != epochTag_ || h.bulkGen != bulkGen_) {
      return ChunkResult::Dropped;
    }
    if (h.len == 0 || h.len != dataLen) return ChunkResult::Dropped;
    auto it = std::find_if(outstanding_.begin(), outstanding_.end(),
                           [&](const Range& r) { return r.offset == h.offset && r.len == h.len; });
    if (it == outstanding_.end()) return ChunkResult::Dropped;  // not asked for (or already answered)
    if (static_cast<uint64_t>(h.offset) + h.len > package_.size()) return ChunkResult::Dropped;
    std::memcpy(package_.data() + h.offset, data, h.len);
    outstanding_.erase(it);
    received_ += h.len;
    lastProgressUs_ = nowUs;
    if (received_ == package_.size()) {
      state_ = ClipImageState::Verifying;
      return ChunkResult::Complete;
    }
    return ChunkResult::Accepted;
  }

  /** Stall and wall-clock budget. True when this call ended the transfer. */
  bool Tick(uint64_t nowUs) {
    if (state_ != ClipImageState::Pulling) return false;
    if (nowUs - lastProgressUs_ >= kClipImageStallUs) return End(ClipImageState::Failed, ClipImageReason::Stalled);
    if (nowUs >= deadlineUs_) return End(ClipImageState::Failed, ClipImageReason::Timeout);
    return false;
  }

  /** Where the named transfer stands. Another id is Unknown and changes nothing. */
  ControlClipImageStatusReplyMessage Status(uint64_t transferId) const {
    ControlClipImageStatusReplyMessage m{};
    m.header.type = static_cast<uint16_t>(MessageType::ControlClipImageStatusReply);
    m.header.size = sizeof(m);
    m.transferId = transferId;
    if (transferId == 0 || transferId != offer_.transferId || state_ == ClipImageState::Unknown) {
      m.state = static_cast<uint8_t>(ClipImageState::Unknown);
      return m;
    }
    m.state = static_cast<uint8_t>(state_);
    m.reason = static_cast<uint8_t>(reason_);
    m.bytesReceived = static_cast<uint32_t>(received_);
    m.totalBytes = static_cast<uint32_t>(offer_.packageBytes());
    return m;
  }

  /**
   * The viewer's cancel. Only the current transfer, and only with its epochTag; a verifying or
   * publishing transfer is marked so the worker drops it instead of publishing (it cannot be
   * interrupted mid-decode, plan r2 §9).
   */
  ControlClipImageStatusReplyMessage Cancel(uint64_t transferId, uint64_t epochTag, ClipImageReason reason) {
    if (transferId != 0 && transferId == offer_.transferId && epochTag == epochTag_ && Active()) {
      if (state_ == ClipImageState::Pulling) {
        End(ClipImageState::Cancelled, reason);
      } else {
        cancelRequested_ = true;
        cancelReason_ = reason;
      }
    }
    return Status(transferId);
  }

  /** The session ended or rolled over: nothing in flight survives it. */
  void OnSessionEnd() {
    if (state_ == ClipImageState::Pulling) {
      End(ClipImageState::Cancelled, ClipImageReason::Session);
    } else if (Active()) {
      cancelRequested_ = true;
      cancelReason_ = ClipImageReason::Session;
    }
  }

  // The worker's steps after Complete: Verifying -> Publishing -> terminal.
  bool CancelRequested() const { return cancelRequested_; }
  void BeginPublishing() {
    if (state_ == ClipImageState::Verifying) state_ = ClipImageState::Publishing;
  }
  /** Ends the transfer after verification/publishing. A cancel that arrived meanwhile wins. */
  void Finish(ClipImageState s, ClipImageReason why) {
    if (!Active()) return;
    if (cancelRequested_ && s != ClipImageState::Published) {
      End(ClipImageState::Cancelled, cancelReason_);
      return;
    }
    End(s, why);
  }

  bool Active() const {
    return state_ == ClipImageState::Pulling || state_ == ClipImageState::Verifying ||
           state_ == ClipImageState::Publishing;
  }
  bool BulkOpen() const { return state_ == ClipImageState::Pulling; }
  /**
   * The size of the next pulls, within [16 KiB, 64 KiB]. The bulk channel carries one message at a
   * time and waits for its acknowledgement, so one chunk per round trip is the ceiling: at 40 ms a
   * 16 KiB chunk cannot exceed ~3 Mbps whatever the pacing rate. Larger chunks at a higher rate lift
   * it without touching the channel.
   */
  void SetChunkBytes(uint32_t n) {
    chunkBytes_ = (std::max)(kClipImageChunkBytes, (std::min)(n, kClipImageMaxChunkBytes));
  }
  uint32_t chunkBytes() const { return chunkBytes_; }
  ClipImageState state() const { return state_; }
  ClipImageReason reason() const { return reason_; }
  const ClipImageOffer& offer() const { return offer_; }
  const std::vector<uint8_t>& package() const { return package_; }
  uint64_t epochTag() const { return epochTag_; }
  uint32_t bulkGen() const { return bulkGen_; }
  uint64_t localSeqAtAccept() const { return localSeqAtAccept_; }
  uint64_t received() const { return received_; }
  size_t outstandingPulls() const { return outstanding_.size(); }
  uint64_t acceptedUs() const { return acceptedUs_; }
  size_t heldBytes() const { return package_.capacity(); }

  // Tests only.
  void SeedGenForTest(uint32_t v) { gens_.SeedForTest(v); }

 private:
  struct Range {
    uint32_t offset = 0;
    uint32_t len = 0;
  };

  bool End(ClipImageState s, ClipImageReason why) {
    state_ = s;
    reason_ = why;
    outstanding_.clear();
    std::vector<uint8_t>().swap(package_);  // released now, not when the next offer comes
    cancelRequested_ = false;
    return true;
  }

  uint32_t processRandom_;
  uint32_t pullWindow_;
  uint32_t chunkBytes_ = kClipImageChunkBytes;
  BulkGenAllocator gens_;
  ClipImageOffer offer_;
  std::vector<uint8_t> package_;
  uint64_t epochTag_ = 0;
  uint32_t bulkGen_ = 0;
  ClipImageState state_ = ClipImageState::Unknown;
  ClipImageReason reason_ = ClipImageReason::None;
  uint64_t received_ = 0;
  uint64_t nextOffset_ = 0;
  std::deque<Range> outstanding_;
  uint64_t acceptedUs_ = 0;
  uint64_t lastProgressUs_ = 0;
  uint64_t deadlineUs_ = 0;
  uint64_t localSeqAtAccept_ = 0;
  bool cancelRequested_ = false;
  ClipImageReason cancelReason_ = ClipImageReason::None;
};

/** Viewer side, direction A: the package and the pulls it answers. */
class ClipImageSender {
 public:
  enum class Phase : uint8_t { Idle = 0, Offering, Serving };

  /** A new package to offer. Replaces whatever was held (the caller cancels the old one first). */
  void Begin(std::shared_ptr<const std::vector<uint8_t>> package, const ClipImageOffer& offer, uint64_t nowUs) {
    package_ = std::move(package);
    offer_ = offer;
    phase_ = Phase::Offering;
    epochTag_ = 0;
    bulkGen_ = 0;
    served_ = 0;
    startedUs_ = nowUs;
    lastPullUs_ = nowUs;
    sent_.clear();
  }

  /** The host's verdict. Serving on Accept; anything else drops the package. */
  bool OnOfferReply(const ControlClipImageOfferReplyMessage& r) {
    if (phase_ != Phase::Offering || r.transferId != offer_.transferId) return false;
    if (r.verdict != static_cast<uint8_t>(ClipImageVerdict::Accept)) {
      End();
      return false;
    }
    epochTag_ = r.epochTag;
    bulkGen_ = r.bulkGen;
    phase_ = Phase::Serving;
    return true;
  }

  struct Served {
    ClipBulkChunkHeader header{};
    const uint8_t* data = nullptr;
    // The pull named a chunk this side had sent: its round trip, first transmissions only.
    bool completedChunk = false;
    uint32_t completedBytes = 0;
  };

  /**
   * A pull off the bulk stream. Answered only when it names the current transfer, epoch and
   * generation and stays inside the package; anything else is dropped without an answer.
   */
  bool OnPull(const ClipBulkPullMessage& p, uint64_t nowUs, Served* out) {
    if (phase_ != Phase::Serving || !package_) return false;
    if (p.transferId != offer_.transferId || p.epochTag != epochTag_ || p.bulkGen != bulkGen_) return false;
    if (p.len == 0 || p.len > kClipImageMaxChunkBytes) return false;
    if (static_cast<uint64_t>(p.offset) + p.len > package_->size()) return false;
    Served s;
    if (p.triggerOffset != 0xFFFFFFFFu) {
      auto it = std::find_if(sent_.begin(), sent_.end(), [&](const Sent& x) { return x.offset == p.triggerOffset; });
      if (it != sent_.end()) {
        s.completedChunk = true;
        s.completedBytes = it->len;
        sent_.erase(it);
      }
    }
    s.header.header.type = static_cast<uint16_t>(MessageType::ClipBulkChunk);
    s.header.header.size = sizeof(ClipBulkChunkHeader);
    s.header.transferId = offer_.transferId;
    s.header.epochTag = epochTag_;
    s.header.bulkGen = bulkGen_;
    s.header.offset = p.offset;
    s.header.len = p.len;
    s.data = package_->data() + p.offset;
    sent_.push_back({p.offset, p.len});
    if (sent_.size() > 16) sent_.pop_front();
    served_ += p.len;
    lastPullUs_ = nowUs;
    *out = s;
    return true;
  }

  /** The host said the transfer is over (or unknown to it): drop the package. */
  void End() {
    phase_ = Phase::Idle;
    package_.reset();
    sent_.clear();
  }

  Phase phase() const { return phase_; }
  bool Active() const { return phase_ != Phase::Idle; }
  const ClipImageOffer& offer() const { return offer_; }
  uint64_t transferId() const { return offer_.transferId; }
  uint64_t epochTag() const { return epochTag_; }
  uint32_t bulkGen() const { return bulkGen_; }
  uint64_t served() const { return served_; }
  uint64_t startedUs() const { return startedUs_; }
  uint64_t lastPullUs() const { return lastPullUs_; }

 private:
  struct Sent {
    uint32_t offset = 0;
    uint32_t len = 0;
  };
  std::shared_ptr<const std::vector<uint8_t>> package_;
  ClipImageOffer offer_;
  Phase phase_ = Phase::Idle;
  uint64_t epochTag_ = 0;
  uint32_t bulkGen_ = 0;
  uint64_t served_ = 0;
  uint64_t startedUs_ = 0;
  uint64_t lastPullUs_ = 0;
  std::deque<Sent> sent_;
};

}  // namespace remote60::native_poc
