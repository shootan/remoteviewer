// See clip_image_client.hpp.

#include "clip_image_client.hpp"

#include <bcrypt.h>
#include <objbase.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

#include "clip_image_core.hpp"
#include "clip_image_wic.hpp"
#include "clipboard_sync.hpp"

namespace remote60::native_poc {

namespace {

uint64_t random64() {
  uint64_t v = 0;
  while (v == 0) {
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
      v = BulkPacer::NowUs() * 6364136223846793005ull + GetCurrentProcessId();
    }
  }
  return v;
}

std::string hex8(const uint8_t* sha) {
  char buf[9];
  std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x", sha[0], sha[1], sha[2], sha[3]);
  return buf;
}

bool parse_ratio(const char* s, uint32_t* num, uint32_t* den) {
  if (!s || !*s) return false;
  unsigned a = 0, b = 0;
  if (std::sscanf(s, "%u/%u", &a, &b) == 2 && a > 0 && b > 0) {
    *num = a;
    *den = b;
    return true;
  }
  return false;
}

template <typename Msg>
bool read_reply(ControlLink& link, MessageType type, Msg* out) {
  if (!link.Read(out, sizeof(*out))) return false;
  return out->header.magic == kMagic && out->header.type == static_cast<uint16_t>(type) &&
         out->header.size == sizeof(*out);
}

}  // namespace

BulkRateConfig clip_bulk_rate_config_from_env() {
  BulkRateConfig c;
  if (const char* e = std::getenv("REMOTE60_CLIP_BULK_EVAL")) {
    if (std::strcmp(e, "wall") == 0) c.evalUnit = BulkRateEvalUnit::WallClock;
  }
  parse_ratio(std::getenv("REMOTE60_CLIP_BULK_SS"), &c.slowStartNumerator, &c.slowStartDenominator);
  if (const char* e = std::getenv("REMOTE60_CLIP_BULK_CA_PCT")) {
    c.caPercentPerSec = static_cast<uint32_t>(std::strtoul(e, nullptr, 10));
  }
  if (const char* e = std::getenv("REMOTE60_CLIP_BULK_CAP_BPS")) {
    const unsigned long v = std::strtoul(e, nullptr, 10);
    if (v >= c.floorBps) c.capBps = static_cast<uint32_t>(v);
  }
  if (const char* e = std::getenv("REMOTE60_CLIP_BULK_LOSS_HIGH_PM")) {
    c.lossHighPerMille = static_cast<uint32_t>(std::strtoul(e, nullptr, 10));
  }
  if (const char* e = std::getenv("REMOTE60_CLIP_BULK_START_BPS")) {
    const unsigned long v = std::strtoul(e, nullptr, 10);
    if (v >= c.floorBps) c.startBps = static_cast<uint32_t>(v);
  }
  return c;
}

ClipPackageResult clip_build_package(const ClipSnapshot& snap, ClipPackage* out) {
  *out = ClipPackage{};
  std::vector<uint8_t> png;
  uint32_t w = 0, h = 0;
  if (snap.kind == ClipSnapshotKind::Png) {
    if (clip_png_dimensions(snap.bytes.data(), snap.bytes.size(), &w, &h) != ClipWicResult::Ok) {
      return ClipPackageResult::NotAnImage;
    }
    png = snap.bytes;
  } else if (snap.kind == ClipSnapshotKind::Dib) {
    const DibInfo info = validate_dib(snap.bytes.data(), snap.bytes.size());
    if (!info.ok()) {
      if (info.reason == DibReason::UnsupportedColorSpace) return ClipPackageResult::ColorProfile;
      // A well-formed image with a side over the limit is too large, not unreadable: the user is
      // told which (validate_dib reports both as BadDimensions, with the sides already read).
      if (info.reason == DibReason::BadDimensions &&
          (info.width > kClipImageMaxSide || info.height > kClipImageMaxSide)) {
        return ClipPackageResult::TooLarge;
      }
      return ClipPackageResult::NotAnImage;
    }
    // Gate on the decoded size before encoding: the encoder's output is the second buffer of the
    // sender's peak (plan r2 §9 S2).
    if (!clip_image_gate(info.width, info.height, 0, 0).ok()) return ClipPackageResult::TooLarge;
    const ClipWicResult enc = clip_dib_to_png(snap.bytes.data(), snap.bytes.size(), &png, &w, &h);
    if (enc == ClipWicResult::BadDib) return ClipPackageResult::ColorProfile;
    if (enc != ClipWicResult::Ok) return ClipPackageResult::EncodeFailed;
  } else {
    return ClipPackageResult::NotAnImage;
  }
  // Same-copy text rides along, unless text v1 would have refused it too.
  std::u16string text = snap.text;
  if (text.size() > kClipboardTextMaxUtf16) text.clear();
  const ClipImageGate g = clip_image_gate(w, h, png.size(), 2ull * text.size());
  if (!g.ok()) return ClipPackageResult::TooLarge;
  auto bytes = std::make_shared<std::vector<uint8_t>>(std::move(png));
  const size_t pngBytes = bytes->size();
  if (!text.empty()) {
    bytes->resize(pngBytes + 2 * text.size());
    std::memcpy(bytes->data() + pngBytes, text.data(), 2 * text.size());  // UTF-16LE on Windows
  }
  ClipImageOffer o;
  o.revision = snap.sequence;
  o.formats = kClipImageFormatPng | (text.empty() ? 0 : kClipImageFormatText);
  o.pngBytes = static_cast<uint32_t>(pngBytes);
  o.textUtf16 = static_cast<uint32_t>(text.size());
  o.width = w;
  o.height = h;
  if (!clip_sha256(bytes->data(), bytes->size(), o.sha256)) return ClipPackageResult::HashFailed;
  out->bytes = std::move(bytes);
  out->offer = o;
  out->text = std::move(text);
  return ClipPackageResult::Ok;
}

void ClipImageClient::Log(const std::string& line) {
  if (log_) log_(line);
  else std::cout << "[native-video-client][clip-image] " << line << "\n";
}

void ClipImageClient::Start(SendFn send, PingRttFn pingRtt, YieldFn yield, uint32_t mtuBytes, BulkRateConfig rate,
                            LogFn log) {
  if (running_.load()) return;
  send_ = std::move(send);
  pingRtt_ = std::move(pingRtt);
  yield_ = std::move(yield);
  log_ = std::move(log);
  mtu_ = mtuBytes;
  rateConfig_ = rate;
  uplink_.Configure(send_, pingRtt_, yield_, mtuBytes, rate, kClipImageChunkBytes);
  running_.store(true);
  packageWorker_ = std::thread([this] { PackageWorker(); });
}

void ClipImageClient::Stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (packageWorker_.joinable()) packageWorker_.join();
  EndSession();
}

void ClipImageClient::SubmitSnapshot(ClipSnapshot snap) {
  std::lock_guard<std::mutex> lock(mu_);
  ++counters_.submitted;
  snapshot_ = std::move(snap);
  haveSnapshot_ = true;
  ++snapshotGen_;
  // Anything from an older copy is now stale: its package must not be offered after this one, its
  // fallback text must not arrive after this one, and a transfer of it is cancelled NOW -- not once
  // this copy has been encoded, which gave the old image seconds in which to be published.
  havePending_ = false;
  pending_ = ClipPackage{};
  haveFallback_ = false;
  fallbackText_.clear();
  RequestCancel(ClipImageReason::Superseded);
  cv_.notify_all();
}

void ClipImageClient::CancelForNewerCopy() {
  std::lock_guard<std::mutex> lock(mu_);
  ++snapshotGen_;  // a snapshot being packaged is stale too
  haveSnapshot_ = false;
  havePending_ = false;
  pending_ = ClipPackage{};
  haveFallback_ = false;  // an older copy's text must not follow this one
  fallbackText_.clear();
  RequestCancel(ClipImageReason::Superseded);
}

void ClipImageClient::RequestCancel(ClipImageReason why) {
  if (!sender_.Active()) return;
  // A user's cancel of this same transfer keeps its reason; a newer copy does not turn it into
  // "superseded".
  if (cancelTargetId_ == sender_.transferId() && why == ClipImageReason::Superseded) return;
  cancelTargetId_ = sender_.transferId();
  cancelReason_ = why;
}

void ClipImageClient::ClearCancelFor(uint64_t transferId) {
  if (cancelTargetId_ != 0 && cancelTargetId_ == transferId) {
    cancelTargetId_ = 0;
    cancelReason_ = ClipImageReason::Superseded;
  }
}

void ClipImageClient::NoteLocalCopyNotSent(ClipPackageResult why) {
  std::lock_guard<std::mutex> lock(mu_);
  std::ostringstream os;
  os << "copy not sent result=" << static_cast<int>(why) << " (text, if any, goes by text sync)";
  Log(os.str());
  NoteNotSent(why, snapshotGen_);  // the copy CancelForNewerCopy just made current
}

void ClipImageClient::NoteNotSent(ClipPackageResult why, uint64_t gen) {
  if (gen != snapshotGen_) return;  // a newer copy exists: this one's news is stale
  if (CancelPending()) {            // the older transfer's cancel is still to be settled: after it
    deferredNotSent_ = true;
    deferredNotSentWhy_ = static_cast<uint8_t>(why);
    deferredNotSentGen_ = gen;
    return;
  }
  RecordOutcome(ClipOutcome::NotSent, static_cast<uint8_t>(why));
}

void ClipImageClient::FlushDeferredNotSent() {
  if (!deferredNotSent_ || CancelPending()) return;
  deferredNotSent_ = false;
  if (deferredNotSentGen_ == snapshotGen_) RecordOutcome(ClipOutcome::NotSent, deferredNotSentWhy_);
}

void ClipImageClient::SettleAwaiting(ClipOutcome o, uint8_t detail) {
  awaiting_.on = false;
  if (arbiter_) arbiter_->Release(awaiting_.transferId);  // terminal on both ends: the bulk is free
  RecordOutcome(o, detail);
  FlushDeferredNotSent();  // the newest copy's line comes last
}

void ClipImageClient::RecordOutcome(ClipOutcome o, uint8_t detail) {
  ++outcomes_;
  lastOutcome_ = o;
  lastDetail_ = detail;
  std::ostringstream os;
  os << "outcome=" << static_cast<int>(o) << " detail=" << static_cast<int>(detail);
  Log(os.str());
}

void ClipImageClient::ApplyCancelAnswer(const ControlClipImageStatusReplyMessage& r) {
  if (!awaiting_.on || r.transferId != awaiting_.transferId) return;  // not the transfer being settled
  const auto st = static_cast<ClipImageState>(r.state);
  const auto why = static_cast<ClipImageReason>(r.reason);
  switch (st) {
    case ClipImageState::Cancelled:
      ++counters_.cancelled;
      counters_.lastState = r.state;
      counters_.lastReason = static_cast<uint8_t>(why == ClipImageReason::None ? awaiting_.why : why);
      SettleAwaiting(ClipOutcome::Cancelled, counters_.lastReason);
      return;
    case ClipImageState::Published:  // it was already on the host's clipboard: say so, do not undo it
      ++counters_.published;
      counters_.lastState = r.state;
      counters_.lastReason = 0;
      SettleAwaiting(ClipOutcome::CancelTooLate, static_cast<uint8_t>(awaiting_.why));
      return;
    case ClipImageState::Superseded:
      ++counters_.superseded;
      counters_.lastState = r.state;
      counters_.lastReason = r.reason;
      SettleAwaiting(ClipOutcome::HostSuperseded, r.reason);
      return;
    case ClipImageState::Failed:
      ++counters_.failed;
      counters_.lastState = r.state;
      counters_.lastReason = r.reason;
      SettleAwaiting(ClipOutcome::Failed, r.reason);
      return;
    case ClipImageState::Unknown:  // the host holds no record of it: what happened is not known
      counters_.lastState = r.state;
      counters_.lastReason = static_cast<uint8_t>(awaiting_.why);
      SettleAwaiting(ClipOutcome::CancelUnconfirmed, static_cast<uint8_t>(awaiting_.why));
      return;
    default:
      return;  // still verifying / publishing on the host: the next Status settles it
  }
}

void ClipImageClient::CancelByUser() {
  std::lock_guard<std::mutex> lock(mu_);
  ++snapshotGen_;  // a snapshot being packaged is this same copy
  haveSnapshot_ = false;
  havePending_ = false;
  pending_ = ClipPackage{};
  haveFallback_ = false;
  fallbackText_.clear();
  RequestCancel(ClipImageReason::User);
  Log("user cancel requested active=" + std::string(sender_.Active() ? "1" : "0"));
}

ClipImageClient::Progress ClipImageClient::GetProgress() const {
  std::lock_guard<std::mutex> lock(mu_);
  Progress p;
  p.active = sender_.Active();
  p.cancelling = awaiting_.on;
  p.cancellingWhy = static_cast<uint8_t>(awaiting_.why);
  p.finished = outcomes_;
  p.outcome = lastOutcome_;
  p.detail = lastDetail_;
  if (p.active) {
    p.bytesTotal = sender_.offer().packageBytes();
    const uint64_t confirmed = sender_.phase() == ClipImageSender::Phase::Serving ? uplink_.confirmed_bytes() : 0;
    p.bytesConfirmed = (std::min<uint64_t>)(confirmed, p.bytesTotal);
    p.elapsedMs = (BulkPacer::NowUs() - sender_.startedUs()) / 1000;
  } else {
    p.bytesTotal = lastBytesTotal_;
    p.bytesConfirmed = lastBytesTotal_;
    p.elapsedMs = counters_.lastTransferMs;
  }
  return p;
}

void ClipImageClient::PackageWorker() {
  (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // WIC
  // Encoding and hashing are background work: the viewer's decode and present come first.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  while (running_.load()) {
    ClipSnapshot snap;
    uint64_t gen = 0;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [&] { return haveSnapshot_ || !running_.load(); });
      if (!running_.load()) break;
      snap = std::move(snapshot_);
      snapshot_ = ClipSnapshot{};
      haveSnapshot_ = false;
      gen = snapshotGen_;
    }
    ClipPackage pkg;
    const uint64_t t0 = BulkPacer::NowUs();
    const ClipPackageResult r = clip_build_package(snap, &pkg);
    const uint64_t buildMs = (BulkPacer::NowUs() - t0) / 1000;
    std::vector<uint8_t>().swap(snap.bytes);  // the snapshot is released before anything is sent (S3)
    std::lock_guard<std::mutex> lock(mu_);
    if (gen != snapshotGen_) continue;  // a newer copy arrived meanwhile: this one is dead
    if (r != ClipPackageResult::Ok) {
      // (A running transfer of an older copy was already cancelled when this copy was submitted.)
      std::ostringstream os;
      os << "package refused result=" << static_cast<int>(r) << " (not sent; text, if any, goes by text sync)";
      Log(os.str());
      if (!snap.text.empty()) {
        fallbackText_ = snap.text;
        haveFallback_ = true;
        fallbackGen_ = gen;
      }
      NoteNotSent(r, gen);
      continue;
    }
    ++counters_.packaged;
    pending_ = std::move(pkg);
    pending_.offer.transferId = random64();
    havePending_ = true;
    pendingGen_ = gen;
    std::ostringstream os;
    os << "package ready " << pending_.offer.width << "x" << pending_.offer.height << " png=" << pending_.offer.pngBytes
       << " textUtf16=" << pending_.offer.textUtf16 << " sha=" << hex8(pending_.offer.sha256) << " buildMs=" << buildMs;
    Log(os.str());
  }
  CoUninitialize();
}

bool ClipImageClient::TakeFallbackText(std::u16string* out) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!haveFallback_) return false;
  if (fallbackGen_ != snapshotGen_) {  // a newer copy exists: this text would arrive after it
    haveFallback_ = false;
    fallbackText_.clear();
    return false;
  }
  *out = std::move(fallbackText_);
  fallbackText_.clear();
  haveFallback_ = false;
  return true;
}

void ClipImageClient::EndActive(ClipImageState finalState, ClipImageReason why) {
  if (!sender_.Active()) return;
  ClearCancelFor(sender_.transferId());  // a cancel asked for this transfer ends with it
  if (arbiter_) arbiter_->Release(sender_.transferId());  // over, and its channel closes after this
  const uint64_t ms = (BulkPacer::NowUs() - sender_.startedUs()) / 1000;
  counters_.lastTransferMs = ms;
  lastBytesTotal_ = sender_.offer().packageBytes();
  counters_.lastState = static_cast<uint8_t>(finalState);
  counters_.lastReason = static_cast<uint8_t>(why);
  switch (finalState) {
    case ClipImageState::Published:
      ++counters_.published;
      RecordOutcome(ClipOutcome::Published, 0);
      break;
    case ClipImageState::Superseded:
      ++counters_.superseded;
      RecordOutcome(ClipOutcome::HostSuperseded, static_cast<uint8_t>(why));
      break;
    case ClipImageState::Cancelled:
      ++counters_.cancelled;
      RecordOutcome(ClipOutcome::Cancelled, static_cast<uint8_t>(why));
      break;
    default:
      ++counters_.failed;
      RecordOutcome(ClipOutcome::Failed, static_cast<uint8_t>(why));
      break;
  }
  std::ostringstream os;
  os << "end state=" << static_cast<int>(finalState) << " reason=" << static_cast<int>(why)
     << " bytes=" << sender_.offer().packageBytes() << " served=" << sender_.served() << " ms=" << ms
     << " sha=" << hex8(sender_.offer().sha256) << " rateBps=" << uplink_.rate_now();
  Log(os.str());
  sender_.End();
  bulkClosePending_ = true;
}

int ClipImageClient::Pump(ControlLink& link) {
  const uint64_t now = BulkPacer::NowUs();
  bool closeBulk = false;
  int result = 0;
  {
    std::unique_lock<std::mutex> lock(mu_);
    // 1. A newer copy, or the user, cancels the running transfer. Sending stops here at once; what
    //    happened on the host is a separate question the answer settles (it may already have
    //    published, or be verifying and still publish). Until it is settled nothing new is offered.
    const bool cancelThis = cancelTargetId_ != 0 && sender_.Active() && sender_.transferId() == cancelTargetId_;
    if (sender_.Active() && (havePending_ || cancelThis)) {
      const ClipImageReason why = cancelThis ? cancelReason_ : ClipImageReason::Superseded;
      cancelTargetId_ = 0;
      cancelReason_ = ClipImageReason::Superseded;
      ControlClipImageCancelMessage c{};
      c.header.type = static_cast<uint16_t>(MessageType::ControlClipImageCancel);
      c.header.size = sizeof(c);
      c.seq = ++nextSeq_;
      c.reason = static_cast<uint8_t>(why);
      c.transferId = sender_.transferId();
      c.epochTag = sender_.epochTag();
      awaiting_ = Awaiting{true, c.transferId, c.epochTag, why, now + kClipImageStatusIntervalUs,
                           now + kClipCancelConfirmBudgetUs};
      {
        const uint64_t ms = (now - sender_.startedUs()) / 1000;
        counters_.lastTransferMs = ms;
        lastBytesTotal_ = sender_.offer().packageBytes();
        std::ostringstream os;
        os << "stopped here, cancel sent reason=" << static_cast<int>(why) << " bytes=" << lastBytesTotal_
           << " served=" << sender_.served() << " ms=" << ms << " sha=" << hex8(sender_.offer().sha256);
        Log(os.str());
      }
      sender_.End();
      bulkClosePending_ = false;  // closed right here -- a flag left set would close the NEXT transfer's
      lock.unlock();
      CloseBulk();
      ControlClipImageStatusReplyMessage r{};
      const bool answered = link.Write(&c, sizeof(c)) && link.EndMessage() &&
                            read_reply(link, MessageType::ControlClipImageCancelReply, &r);
      lock.lock();
      if (!answered) {  // the link failed: nobody knows what the host did
        if (awaiting_.on && awaiting_.transferId == c.transferId) {
          SettleAwaiting(ClipOutcome::CancelUnconfirmed, static_cast<uint8_t>(why));
        }
        return -1;
      }
      std::ostringstream os;
      os << "cancel answered reason=" << static_cast<int>(why) << " hostState=" << static_cast<int>(r.state)
         << " hostReason=" << static_cast<int>(r.reason) << " seqOk=" << (r.seq == c.seq ? 1 : 0)
         << " idOk=" << (r.transferId == c.transferId ? 1 : 0);
      Log(os.str());
      if (r.seq == c.seq) ApplyCancelAnswer(r);  // a mismatch settles nothing: Status will
      return 1;
    }
    // 1b. A cancel not settled yet: ask about THAT transfer, bounded.
    if (awaiting_.on && now >= awaiting_.nextStatusUs) {
      if (now >= awaiting_.deadlineUs) {
        SettleAwaiting(ClipOutcome::CancelUnconfirmed, static_cast<uint8_t>(awaiting_.why));
        return 0;
      }
      ControlClipImageStatusMessage s{};
      s.header.type = static_cast<uint16_t>(MessageType::ControlClipImageStatus);
      s.header.size = sizeof(s);
      s.seq = ++nextSeq_;
      s.transferId = awaiting_.transferId;
      s.epochTag = awaiting_.epochTag;
      awaiting_.nextStatusUs = now + kClipImageStatusIntervalUs;
      lock.unlock();
      ControlClipImageStatusReplyMessage r{};
      const bool answered = link.Write(&s, sizeof(s)) && link.EndMessage() &&
                            read_reply(link, MessageType::ControlClipImageStatusReply, &r);
      lock.lock();
      if (!answered) {
        if (awaiting_.on && awaiting_.transferId == s.transferId) {
          SettleAwaiting(ClipOutcome::CancelUnconfirmed, static_cast<uint8_t>(awaiting_.why));
        }
        return -1;
      }
      if (r.seq == s.seq) ApplyCancelAnswer(r);
      return 1;
    }
    // 2. Offer the newest package -- only once the host has settled the previous one.
    if (!sender_.Active() && !awaiting_.on && havePending_ && Usable() &&
        (!arbiter_ || arbiter_->TryAcquire(BulkUse::Image, pending_.offer.transferId))) {
      ClipPackage pkg = std::move(pending_);
      pending_ = ClipPackage{};
      havePending_ = false;
      offerGen_ = pendingGen_;
      ControlClipImageOfferMessage m{};
      m.header.type = static_cast<uint16_t>(MessageType::ControlClipImageOffer);
      m.header.size = sizeof(m);
      m.seq = ++nextSeq_;
      m.formats = pkg.offer.formats;
      m.transferId = pkg.offer.transferId;
      m.revision = pkg.offer.revision;
      m.pngBytes = pkg.offer.pngBytes;
      m.textUtf16 = pkg.offer.textUtf16;
      m.width = pkg.offer.width;
      m.height = pkg.offer.height;
      std::memcpy(m.sha256, pkg.offer.sha256, 32);
      m.clientSendQpcUs = now;
      sender_.Begin(pkg.bytes, pkg.offer, now);
      ++counters_.offered;
      lock.unlock();
      if (!link.Write(&m, sizeof(m)) || !link.EndMessage()) return -1;
      ControlClipImageOfferReplyMessage r{};
      if (!read_reply(link, MessageType::ControlClipImageOfferReply, &r)) return -1;
      lock.lock();
      if (sender_.OnOfferReply(r)) {
        ++counters_.accepted;
        nextStatusUs_ = BulkPacer::NowUs() + kClipImageStatusIntervalUs;
        OpenBulk();
        std::ostringstream os;
        os << "accepted gen=" << r.bulkGen << " window=" << r.pullWindow;
        Log(os.str());
      } else {
        ++counters_.refused;
        ClearCancelFor(m.transferId);  // the refused offer's cancel ends with it (P1)
        if (arbiter_) arbiter_->Release(m.transferId);
        std::ostringstream os;
        os << "refused verdict=" << static_cast<int>(r.verdict) << " (text, if any, goes by text sync)";
        Log(os.str());
        if (offerGen_ == snapshotGen_) {  // still the newest copy: say so, and send its text instead
          RecordOutcome(ClipOutcome::Refused, r.verdict);
          if (!pkg.text.empty()) {
            fallbackText_ = pkg.text;
            haveFallback_ = true;
            fallbackGen_ = offerGen_;
          }
        }
        FlushDeferredNotSent();
      }
      return 1;
    }
    // 3. Ask how it stands, every 500 ms: the only way the host's outcome reaches this side.
    if (sender_.Active() && sender_.phase() == ClipImageSender::Phase::Serving && now >= nextStatusUs_) {
      ControlClipImageStatusMessage s{};
      s.header.type = static_cast<uint16_t>(MessageType::ControlClipImageStatus);
      s.header.size = sizeof(s);
      s.seq = ++nextSeq_;
      s.transferId = sender_.transferId();
      s.epochTag = sender_.epochTag();
      nextStatusUs_ = now + kClipImageStatusIntervalUs;
      lock.unlock();
      if (!link.Write(&s, sizeof(s)) || !link.EndMessage()) return -1;
      ControlClipImageStatusReplyMessage r{};
      if (!read_reply(link, MessageType::ControlClipImageStatusReply, &r)) return -1;
      lock.lock();
      if (sender_.Active() && r.transferId == sender_.transferId()) {
        const auto st = static_cast<ClipImageState>(r.state);
        if (clip_image_state_terminal(st) || st == ClipImageState::Unknown) {
          EndActive(st, static_cast<ClipImageReason>(r.reason));
          FlushDeferredNotSent();
        }
      }
      result = 1;
    }
    closeBulk = bulkClosePending_;
    bulkClosePending_ = false;
  }
  if (closeBulk) CloseBulk();
  return result;
}

void ClipImageClient::EndSession() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    EndActive(ClipImageState::Cancelled, ClipImageReason::Session);
    if (awaiting_.on) SettleAwaiting(ClipOutcome::CancelUnconfirmed, static_cast<uint8_t>(awaiting_.why));
    deferredNotSent_ = false;
    havePending_ = false;
    pending_ = ClipPackage{};
    haveSnapshot_ = false;
    snapshot_ = ClipSnapshot{};
    ++snapshotGen_;
    haveFallback_ = false;
    bulkClosePending_ = false;
    cancelTargetId_ = 0;
  }
  CloseBulk();
  hostSupports_.store(false);
}

bool ClipImageClient::OnDatagram(const void* data, size_t len) {
  if (!bulk_stream_claims(data, len)) return false;
  (void)uplink_.OnDatagram(data, len);  // dropped by the channel unless it is the open stream
  return true;
}

void ClipImageClient::OpenBulk() {
  bulkClosePending_ = false;
  uplink_.ResetRateCounters();  // this transfer's channel: nothing queued for closing applies to it
  const uint32_t gen = sender_.bulkGen();
  uplink_.Open(bulk_stream_id(gen, kBulkStreamClientToHost), bulk_stream_id(gen, kBulkStreamHostToClient), this);
}

void ClipImageClient::CloseBulk() { uplink_.Close(); }

bool ClipImageClient::OnPull(const std::vector<uint8_t>& msg, uint64_t nowUs, std::vector<uint8_t>* out,
                             BulkServed* served) {
  if (msg.size() != sizeof(ClipBulkPullMessage)) return false;
  ClipBulkPullMessage p{};
  std::memcpy(&p, msg.data(), sizeof(p));
  if (p.header.magic != kMagic || p.header.type != static_cast<uint16_t>(MessageType::ClipBulkPull) ||
      p.header.size != sizeof(p)) {
    return false;
  }
  ClipImageSender::Served s;
  std::lock_guard<std::mutex> lock(mu_);
  if (!sender_.OnPull(p, nowUs, &s)) return false;
  out->resize(sizeof(ClipBulkChunkHeader) + s.header.len);
  std::memcpy(out->data(), &s.header, sizeof(s.header));
  std::memcpy(out->data() + sizeof(s.header), s.data, s.header.len);  // the package outlives this lock
  served->completedChunk = s.completedChunk;
  served->completedBytes = s.completedBytes;
  served->triggerKey = p.triggerOffset == 0xFFFFFFFFu ? kBulkNoKey : p.triggerOffset;
  served->chunkKey = p.offset;
  return true;
}

bool ClipImageClient::ChunkKeyOf(const uint8_t* message, size_t len, uint64_t* key) {
  if (len < sizeof(ClipBulkChunkHeader)) return false;
  ClipBulkChunkHeader c{};
  std::memcpy(&c, message, sizeof(c));
  *key = c.offset;
  return true;
}

}  // namespace remote60::native_poc
