// See host_clip_image.hpp.

#include "host_clip_image.hpp"

#include <bcrypt.h>
#include <objbase.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "clip_image_clipboard.hpp"
#include "clip_image_core.hpp"
#include "clip_image_wic.hpp"
#include "clipboard_monitor.hpp"

namespace remote60::native_poc {

namespace {

uint64_t steady_us() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

uint32_t random32() {
  uint32_t v = 0;
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    v = static_cast<uint32_t>(steady_us() ^ (GetCurrentProcessId() * 2654435761u));
  }
  return v;
}

std::string hex8(const uint8_t* sha) {
  char buf[9];
  std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x", sha[0], sha[1], sha[2], sha[3]);
  return buf;
}

const char* state_name(ClipImageState s) {
  switch (s) {
    case ClipImageState::Pulling: return "pulling";
    case ClipImageState::Verifying: return "verifying";
    case ClipImageState::Publishing: return "publishing";
    case ClipImageState::Published: return "published";
    case ClipImageState::Failed: return "failed";
    case ClipImageState::Cancelled: return "cancelled";
    case ClipImageState::Superseded: return "superseded";
    default: return "unknown";
  }
}

// The host's side of the bulk stream sends only pulls (48 bytes) and acknowledgements, so a quick
// resend costs nothing -- and it is what covers the one race the protocol has: the first pulls
// leave with the OfferReply, and can reach the viewer before it has opened the stream they are on
// (they are then dropped unacknowledged). The NACK wait is the control channel's own (90 ms, left
// unchanged by agreement): a NACK for datagrams still waiting in the viewer's pacer is merged there
// into the queued copy and costs nothing (bulk_pacer.hpp).
UdpControlChannel::Timings bulk_timings() {
  UdpControlChannel::Timings t;
  t.retransmitIntervalUs = 150000;
  t.maxAttempts = 200;  // ~30 s: the transfer's own stall rule decides first
  return t;
}

}  // namespace

bool HubClipImagePublisher::Enabled() const { return hub_ && hub_->enabled(); }

ClipPublishResult HubClipImagePublisher::Publish(uint64_t expectSequence, HGLOBAL pngGlobal, HGLOBAL dibv5,
                                                 const std::u16string& text) {
  if (!hub_) {
    if (pngGlobal) GlobalFree(pngGlobal);
    if (dibv5) GlobalFree(dibv5);
    return ClipPublishResult::OpenFailed;
  }
  return hub_->PublishImage(expectSequence, pngGlobal, dibv5, text);
}

HostClipImageService::HostClipImageService(HostClipImagePublisher* publisher)
    : publisher_(publisher), receiver_(random32()) {
  bulk_.SetTimings(bulk_timings());
}

bool HostClipImageService::Enabled() const { return Available() && running_.load(); }

bool HostClipImageService::Available() const { return publisher_ && publisher_->Enabled(); }

bool HostClipImageService::Start(SendFn send, uint32_t mtuBytes) {
  if (running_.load()) return true;
  send_ = std::move(send);
  mtu_ = mtuBytes;
  running_.store(true);
  worker_ = std::thread([this] { Run(); });
  return true;
}

void HostClipImageService::Stop() {
  if (!running_.exchange(false)) return;
  {
    std::lock_guard<std::mutex> lock(mu_);
    receiver_.OnSessionEnd();
    CloseBulk();
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void HostClipImageService::CloseBulk() {
  if (!bulkOpen_) return;
  bulkOpen_ = false;
  bulkRx_ = 0;
  bulk_.Close(ControlCloseReason::SessionRollover);
}

void HostClipImageService::LogEnd(const char* where) {
  const ClipImageState s = receiver_.state();
  if (s == ClipImageState::Published) ++counters_.published;
  else if (s == ClipImageState::Superseded) ++counters_.superseded;
  else if (s == ClipImageState::Cancelled) ++counters_.cancelled;
  else if (s == ClipImageState::Failed) ++counters_.failed;
  std::cout << "[native-video-host][clip-image] end state=" << state_name(s)
            << " reason=" << static_cast<int>(receiver_.reason()) << " at=" << where
            << " sha=" << hex8(receiver_.offer().sha256) << " bytes=" << receiver_.offer().packageBytes()
            << " ms=" << (steady_us() - receiver_.acceptedUs()) / 1000 << "\n";
}

void HostClipImageService::OnSessionEnd(uint64_t newEpoch) {
  std::lock_guard<std::mutex> lock(mu_);
  const bool wasPulling = receiver_.state() == ClipImageState::Pulling;
  receiver_.OnSessionEnd();
  CloseBulk();
  if (wasPulling) LogEnd("session");
  (void)newEpoch;
  cv_.notify_all();
}

ControlClipImageOfferReplyMessage HostClipImageService::HandleOffer(const ControlClipImageOfferMessage& m,
                                                                     uint64_t sessionEpoch) {
  ControlClipImageOfferReplyMessage r{};
  r.header.type = static_cast<uint16_t>(MessageType::ControlClipImageOfferReply);
  r.header.size = sizeof(r);
  r.seq = m.seq;
  r.transferId = m.transferId;
  const ClipImageOffer offer = clip_image_offer_from_wire(m);
  std::lock_guard<std::mutex> lock(mu_);
  ++counters_.offers;
  const bool enabled = Enabled() && bulkNegotiated_.load(std::memory_order_acquire);
  // Read now: publishing later is allowed only if the host clipboard has not moved since (r2 8-4).
  const uint64_t seqNow = publisher_ ? publisher_->Sequence() : 0;
  const auto res = receiver_.OnOffer(offer, sessionEpoch, enabled, seqNow, steady_us());
  r.verdict = static_cast<uint8_t>(res.verdict);
  std::cout << "[native-video-host][clip-image] offer id=" << (m.transferId & 0xFFFF) << " "
            << m.width << "x" << m.height << " png=" << m.pngBytes << " textUtf16=" << m.textUtf16
            << " sha=" << hex8(m.sha256) << " verdict=" << static_cast<int>(res.verdict) << "\n";
  if (res.verdict != ClipImageVerdict::Accept) return r;
  ++counters_.accepted;
  r.epochTag = res.epochTag;
  r.bulkGen = res.bulkGen;
  r.pullWindow = kClipImagePullWindowDefault;
  // The bulk stream for this transfer: a fresh channel on the generation's ids, so a late datagram
  // of any earlier transfer names a stream nobody listens on.
  bulk_.Reset();
  bulk_.SetTimings(bulk_timings());
  lastChunkUs_ = 0;
  rxRateBps_ = 0;
  if (const char* e = std::getenv("REMOTE60_CLIP_ADAPTIVE_CHUNKS")) adaptiveChunks_ = e[0] != '0';
  bulkRx_ = bulk_stream_id(res.bulkGen, kBulkStreamClientToHost);
  bulk_.Configure(send_, bulk_stream_id(res.bulkGen, kBulkStreamHostToClient), bulkRx_, mtu_);
  bulkOpen_ = true;
  PumpPulls();
  cv_.notify_all();
  return r;
}

ControlClipImageStatusReplyMessage HostClipImageService::HandleCancel(const ControlClipImageCancelMessage& m) {
  std::lock_guard<std::mutex> lock(mu_);
  const bool wasActive = receiver_.Active() && receiver_.offer().transferId == m.transferId;
  ControlClipImageStatusReplyMessage r =
      receiver_.Cancel(m.transferId, m.epochTag, static_cast<ClipImageReason>(m.reason));
  if (wasActive && !receiver_.BulkOpen()) CloseBulk();
  if (wasActive && receiver_.state() == ClipImageState::Cancelled) LogEnd("cancel");
  r.seq = m.seq;
  cv_.notify_all();
  return r;
}

ControlClipImageStatusReplyMessage HostClipImageService::HandleStatus(const ControlClipImageStatusMessage& m) {
  std::lock_guard<std::mutex> lock(mu_);
  ControlClipImageStatusReplyMessage r = receiver_.Status(m.transferId);
  r.seq = m.seq;
  return r;
}

bool HostClipImageService::OnDatagram(const void* data, size_t len) {
  if (!bulk_stream_claims(data, len)) return false;
  // Every bulk datagram stops here, open transfer or not: the channel drops what is not on its
  // current stream ids (or everything, once closed).
  (void)bulk_.OnPacket(data, len);
  return true;
}

void HostClipImageService::PumpPulls() {
  ClipBulkPullMessage p{};
  while (receiver_.NextPull(&p)) (void)bulk_.Send(&p, sizeof(p));
}

void HostClipImageService::Run() {
  (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // WIC, for the decode
  std::vector<uint8_t> msg;
  while (running_.load()) {
    bool open = false;
    bool verify = false;
    {
      std::unique_lock<std::mutex> lock(mu_);
      open = bulkOpen_;
      verify = receiver_.state() == ClipImageState::Verifying;
      if (!open && !verify) {
        cv_.wait_for(lock, std::chrono::milliseconds(200));
        continue;
      }
    }
    if (verify) {
      VerifyAndPublish();
      continue;
    }
    const bool got = bulk_.Receive(&msg, 20);
    bulk_.Tick();
    std::lock_guard<std::mutex> lock(mu_);
    if (!bulkOpen_) continue;
    const uint64_t now = steady_us();
    if (got) {
      ClipBulkChunkHeader h{};
      if (msg.size() >= sizeof(h)) std::memcpy(&h, msg.data(), sizeof(h));
      const bool shaped = msg.size() >= sizeof(h) && h.header.magic == kMagic &&
                          h.header.type == static_cast<uint16_t>(MessageType::ClipBulkChunk) &&
                          h.header.size == sizeof(h);
      const auto res = shaped ? receiver_.OnChunk(h, msg.data() + sizeof(h), msg.size() - sizeof(h), now)
                              : ClipImageReceiver::ChunkResult::Dropped;
      if (res == ClipImageReceiver::ChunkResult::Dropped) {
        ++counters_.chunksDropped;
      } else {
        ++counters_.chunksAccepted;
        if (res == ClipImageReceiver::ChunkResult::Complete) {
          // The channel acknowledged the last chunk as it assembled it; nothing more rides it.
          CloseBulk();
          cv_.notify_all();
          continue;
        }
        if (adaptiveChunks_) {
          // Size the next pulls to ~100 ms of what is arriving (16..64 KiB).
          if (lastChunkUs_ && now > lastChunkUs_) {
            const uint64_t inst = static_cast<uint64_t>(h.len) * 8ull * 1000000ull / (now - lastChunkUs_);
            rxRateBps_ = rxRateBps_ ? (rxRateBps_ * 3 + inst) / 4 : inst;
            receiver_.SetChunkBytes(static_cast<uint32_t>(rxRateBps_ / 8 / 10 / 1024 * 1024));
          }
          lastChunkUs_ = now;
        }
        ClipBulkPullMessage p{};
        if (receiver_.NextPull(&p, h.offset)) (void)bulk_.Send(&p, sizeof(p));
        PumpPulls();
      }
    }
    if (bulk_.IsClosed() && receiver_.state() == ClipImageState::Pulling) {
      // The viewer stopped answering for the channel's whole retry budget.
      receiver_.Finish(ClipImageState::Failed, ClipImageReason::Stalled);
      CloseBulk();
      LogEnd("bulk-lost");
      continue;
    }
    if (receiver_.Tick(now)) {
      CloseBulk();
      LogEnd("timer");
    }
  }
  CoUninitialize();
}

void HostClipImageService::VerifyAndPublish() {
  // The package is not touched by anyone else while Verifying/Publishing: chunks are refused, a
  // cancel only marks it. So it is read here without the lock -- the decode takes real time and
  // the control dispatchers must keep answering Status meanwhile.
  const std::vector<uint8_t>* pkg = nullptr;
  ClipImageOffer offer;
  uint64_t expectSeq = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (receiver_.state() != ClipImageState::Verifying) return;
    pkg = &receiver_.package();
    offer = receiver_.offer();
    expectSeq = receiver_.localSeqAtAccept();
  }
  auto finish = [&](ClipImageState s, ClipImageReason why) {
    std::lock_guard<std::mutex> lock(mu_);
    receiver_.Finish(s, why);
    LogEnd("publish");
    cv_.notify_all();
  };
  uint8_t digest[32];
  if (!clip_sha256(pkg->data(), pkg->size(), digest) || std::memcmp(digest, offer.sha256, 32) != 0) {
    finish(ClipImageState::Failed, ClipImageReason::VerifyFailed);
    return;
  }
  const uint8_t* png = pkg->data();
  const size_t pngBytes = offer.pngBytes;
  uint32_t w = 0, h = 0;
  // The PNG's own header, read before any pixel is decoded: it must be the size that was offered
  // and pass the gate by itself (plan r2 §9).
  if (clip_png_dimensions(png, pngBytes, &w, &h) != ClipWicResult::Ok) {
    finish(ClipImageState::Failed, ClipImageReason::DecodeFailed);
    return;
  }
  if (w != offer.width || h != offer.height) {
    finish(ClipImageState::Failed, ClipImageReason::SizeMismatch);
    return;
  }
  if (!clip_image_gate(w, h, pngBytes, 2ull * offer.textUtf16).ok()) {
    finish(ClipImageState::Failed, ClipImageReason::Limit);
    return;
  }
  HGLOBAL dib = nullptr;
  const ClipWicResult dec = clip_png_to_dibv5(png, pngBytes, w, h, &dib);
  if (dec != ClipWicResult::Ok) {
    finish(ClipImageState::Failed,
           dec == ClipWicResult::SizeMismatch ? ClipImageReason::SizeMismatch : ClipImageReason::DecodeFailed);
    return;
  }
  std::u16string text;
  if (offer.textUtf16 > 0) {
    text.resize(offer.textUtf16);
    std::memcpy(text.data(), pkg->data() + pngBytes, 2ull * offer.textUtf16);
    const size_t nul = text.find(u'\0');
    if (nul != std::u16string::npos) text.resize(nul);
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (receiver_.CancelRequested()) {
      GlobalFree(dib);
      receiver_.Finish(ClipImageState::Cancelled, ClipImageReason::None);
      LogEnd("cancel-before-publish");
      return;
    }
    receiver_.BeginPublishing();
  }
  // The PNG the clipboard will own is copied before the package can be released (plan r2 §9 R4:
  // this copy is the second P of the receiver's peak).
  HGLOBAL pngGlobal = clip_global_copy(png, pngBytes);
  ClipPublishResult pub = ClipPublishResult::OpenFailed;
  if (publisher_) {
    pub = publisher_->Publish(expectSeq, pngGlobal, dib, text);
  } else {
    if (pngGlobal) GlobalFree(pngGlobal);
    GlobalFree(dib);
  }
  switch (pub) {
    case ClipPublishResult::Published: finish(ClipImageState::Published, ClipImageReason::None); break;
    case ClipPublishResult::Superseded: finish(ClipImageState::Superseded, ClipImageReason::Superseded); break;
    default: finish(ClipImageState::Failed, ClipImageReason::PublishFailed); break;
  }
}

}  // namespace remote60::native_poc
