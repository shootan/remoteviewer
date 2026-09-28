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
  if (const char* e = std::getenv("REMOTE60_CLIP_BULK_LOSS_PERMILLE")) {
    c.lossTolerancePerMille = static_cast<uint32_t>(std::strtoul(e, nullptr, 10));
  }
  if (const char* e = std::getenv("REMOTE60_CLIP_BULK_LOSS_HORIZON")) {
    c.lossHorizonDatagrams = static_cast<uint32_t>(std::strtoul(e, nullptr, 10));
  }
  if (const char* e = std::getenv("REMOTE60_CLIP_BULK_LOSS_RULE")) {
    if (std::strcmp(e, "rtt_or_rate") == 0) c.lossRule = BulkLossRule::RttOrRoundRate;
  }
  if (const char* e = std::getenv("REMOTE60_CLIP_BULK_RAISE_RULE")) {
    if (std::strcmp(e, "timely") == 0) c.raiseRule = BulkRaiseRule::TimelyRound;
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
      return info.reason == DibReason::UnsupportedColorSpace ? ClipPackageResult::ColorProfile
                                                            : ClipPackageResult::NotAnImage;
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
  bulk_.SetTimings(clip_bulk_timings());
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
  // Anything packaged from an older copy is now stale: it must not be offered after this one.
  havePending_ = false;
  pending_ = ClipPackage{};
  cv_.notify_all();
}

void ClipImageClient::CancelForNewerCopy() {
  std::lock_guard<std::mutex> lock(mu_);
  ++snapshotGen_;  // a snapshot being packaged is stale too
  haveSnapshot_ = false;
  havePending_ = false;
  pending_ = ClipPackage{};
  if (sender_.Active()) cancelActive_ = true;
}

void ClipImageClient::PackageWorker() {
  (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // WIC
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
      if (sender_.Active()) cancelActive_ = true;  // the newest copy is not this transfer's
      std::ostringstream os;
      os << "package refused result=" << static_cast<int>(r) << " (not sent; text, if any, goes by text sync)";
      Log(os.str());
      if (!snap.text.empty()) {
        fallbackText_ = snap.text;
        haveFallback_ = true;
      }
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
  *out = std::move(fallbackText_);
  fallbackText_.clear();
  haveFallback_ = false;
  return true;
}

void ClipImageClient::EndActive(ClipImageState finalState, ClipImageReason why) {
  if (!sender_.Active()) return;
  const uint64_t ms = (BulkPacer::NowUs() - sender_.startedUs()) / 1000;
  counters_.lastTransferMs = ms;
  counters_.lastState = static_cast<uint8_t>(finalState);
  counters_.lastReason = static_cast<uint8_t>(why);
  switch (finalState) {
    case ClipImageState::Published: ++counters_.published; break;
    case ClipImageState::Superseded: ++counters_.superseded; break;
    case ClipImageState::Cancelled: ++counters_.cancelled; break;
    default: ++counters_.failed; break;
  }
  std::ostringstream os;
  os << "end state=" << static_cast<int>(finalState) << " reason=" << static_cast<int>(why)
     << " bytes=" << sender_.offer().packageBytes() << " served=" << sender_.served() << " ms=" << ms
     << " sha=" << hex8(sender_.offer().sha256) << " rateBps=" << rateNow_.load();
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
    // 1. A newer copy is ready while one is running: cancel it first, and wait for the answer
    //    (r2 8-2), so the host is free when the new offer arrives.
    if (sender_.Active() && (havePending_ || cancelActive_)) {
      cancelActive_ = false;
      ControlClipImageCancelMessage c{};
      c.header.type = static_cast<uint16_t>(MessageType::ControlClipImageCancel);
      c.header.size = sizeof(c);
      c.seq = ++nextSeq_;
      c.reason = static_cast<uint8_t>(ClipImageReason::Superseded);
      c.transferId = sender_.transferId();
      c.epochTag = sender_.epochTag();
      EndActive(ClipImageState::Cancelled, ClipImageReason::Superseded);  // local cleanup is immediate
      bulkClosePending_ = false;  // closed right here -- a flag left set would close the NEXT transfer's
      lock.unlock();
      CloseBulk();
      if (!link.Write(&c, sizeof(c)) || !link.EndMessage()) return -1;
      ControlClipImageStatusReplyMessage r{};
      if (!read_reply(link, MessageType::ControlClipImageCancelReply, &r)) return -1;
      return 1;
    }
    // 2. Offer the newest package.
    if (!sender_.Active() && havePending_ && Usable()) {
      ClipPackage pkg = std::move(pending_);
      pending_ = ClipPackage{};
      havePending_ = false;
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
        std::ostringstream os;
        os << "refused verdict=" << static_cast<int>(r.verdict) << " (text, if any, goes by text sync)";
        Log(os.str());
        if (!pkg.text.empty()) {
          fallbackText_ = pkg.text;
          haveFallback_ = true;
        }
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
    havePending_ = false;
    pending_ = ClipPackage{};
    haveSnapshot_ = false;
    snapshot_ = ClipSnapshot{};
    ++snapshotGen_;
    haveFallback_ = false;
    bulkClosePending_ = false;
    cancelActive_ = false;
  }
  CloseBulk();
  hostSupports_.store(false);
}

bool ClipImageClient::OnDatagram(const void* data, size_t len) {
  if (!bulk_stream_claims(data, len)) return false;
  (void)bulk_.OnPacket(data, len);  // dropped by the channel unless it is the open stream
  return true;
}

void ClipImageClient::OpenBulk() {
  bulkClosePending_ = false;
  counters_.raises = counters_.lowers = counters_.pauses = counters_.recoveryHolds = counters_.evaluations = 0;
  counters_.lossEvents = counters_.channelFragmentRetransmits = 0;  // this transfer's channel: nothing queued for closing applies to it
  const uint32_t gen = sender_.bulkGen();
  txStreamId_ = bulk_stream_id(gen, kBulkStreamClientToHost);
  {
    std::lock_guard<std::mutex> ev(evMu_);
    seqToOffset_.clear();
    lastTxUs_.clear();
    tainted_.clear();
    resentSeqsInWindow_.clear();
  }
  bulk_.Reset();
  {
    UdpControlChannel::Timings t = clip_bulk_timings();
    t.retransmitIntervalUs = clip_bulk_retransmit_us((std::min)(rateConfig_.startBps, rateConfig_.capBps));
    bulk_.SetTimings(t);
  }
  bulk_.Configure([this](const void* d, size_t n) { return pacer_.Enqueue(d, n); }, txStreamId_,
                  bulk_stream_id(gen, kBulkStreamHostToClient), mtu_);
  rateNow_.store((std::min)(rateConfig_.startBps, rateConfig_.capBps));
  pacer_.Start(send_, [this](uint64_t) { return rateNow_.load(std::memory_order_relaxed); }, yield_,
               [this](const uint8_t* d, size_t n, uint64_t t, bool resend) { OnTransmitted(d, n, t, resend); });
  serving_.store(true);
  serveThread_ = std::thread([this] { ServeLoop(); });
}

void ClipImageClient::CloseBulk() {
  serving_.store(false);
  bulk_.Close(ControlCloseReason::SessionRollover);
  if (serveThread_.joinable()) serveThread_.join();
  pacer_.Stop();
}

void ClipImageClient::OnTransmitted(const uint8_t* data, size_t len, uint64_t nowUs, bool resend) {
  if (len < sizeof(UdpControlChunkHeader)) return;
  UdpControlChunkHeader h{};
  std::memcpy(&h, data, sizeof(h));
  if (h.magic != kMagic || h.kind != static_cast<uint16_t>(UdpPacketKind::ControlData) || h.streamId != txStreamId_) {
    return;
  }
  std::lock_guard<std::mutex> ev(evMu_);
  if (h.fragIndex == 0 && len >= sizeof(UdpControlChunkHeader) + sizeof(ClipBulkChunkHeader)) {
    ClipBulkChunkHeader c{};
    std::memcpy(&c, data + sizeof(UdpControlChunkHeader), sizeof(c));
    seqToOffset_[h.messageSeq] = c.offset;
    while (seqToOffset_.size() > 64) seqToOffset_.erase(seqToOffset_.begin());
  }
  auto it = seqToOffset_.find(h.messageSeq);
  if (it == seqToOffset_.end()) return;
  const uint32_t offset = it->second;
  if (resend) {
    tainted_.insert(offset);
    lastTxUs_.erase(offset);
    resentSeqsInWindow_.insert(h.messageSeq);
    return;
  }
  if (h.fragIndex + 1 == h.fragCount && !tainted_.count(offset)) lastTxUs_[offset] = nowUs;
}

void ClipImageClient::ServeLoop() {
  BulkRateController rc(rateConfig_);
  BulkRttEstimator rtt;
  std::vector<uint8_t> msg;
  std::vector<uint8_t> out;
  std::vector<uint64_t> rtts;  // this evaluation's valid samples
  uint64_t rounds = 0;
  uint64_t windowStartUs = BulkPacer::NowUs();
  uint64_t sentBytesBase = 0, sentDgramBase = 0, yieldBase = 0, idleBase = 0;
  uint64_t lastTimingRttUs = 0;
  uint64_t lossDgramBase = 0;
  uint64_t lastPullUs = 0;
  uint32_t capInForce = rateConfig_.capBps;
  std::deque<std::pair<uint64_t, uint64_t>> horizon;  // (datagrams, loss events) per evaluation
  rc.MarkEvaluated(windowStartUs, 0);
  while (serving_.load()) {
    const bool got = bulk_.Receive(&msg, 10);
    bulk_.Tick();
    const uint64_t now = BulkPacer::NowUs();
    // The cap: the configured ceiling and, when one is known, the same-direction budget -- a shrink
    // applies at once (SetCapBps). Unknown budget: the ceiling alone, and congestion decides.
    const uint32_t budget = budgetBps_.load(std::memory_order_relaxed);
    const uint32_t cap = budget ? (std::min)(rateConfig_.capBps, budget) : rateConfig_.capBps;
    if (cap != capInForce) {
      rc.SetCapBps(cap);
      capInForce = cap;
    }
    if (got && msg.size() == sizeof(ClipBulkPullMessage)) {
      ClipBulkPullMessage p{};
      std::memcpy(&p, msg.data(), sizeof(p));
      ClipImageSender::Served s;
      bool ok = false;
      if (p.header.magic == kMagic && p.header.type == static_cast<uint16_t>(MessageType::ClipBulkPull) &&
          p.header.size == sizeof(p)) {
        std::lock_guard<std::mutex> lock(mu_);
        ok = sender_.OnPull(p, now, &s);
        if (ok) {
          ++counters_.pullsServed;
          out.resize(sizeof(ClipBulkChunkHeader) + s.header.len);
          std::memcpy(out.data(), &s.header, sizeof(s.header));
          std::memcpy(out.data() + sizeof(s.header), s.data, s.header.len);  // the package outlives this lock
        } else {
          ++counters_.pullsDropped;
        }
      }
      if (ok) {
        if (s.completedChunk) {  // a pull naming a chunk counted before is not completedChunk (duplicate)
          ++rounds;
          const uint32_t r = rateNow_.load(std::memory_order_relaxed);
          const uint64_t chunkSendUs = r ? static_cast<uint64_t>(s.completedBytes) * 8ull * 1000000ull / r : 0;
          uint64_t sample = 0;
          bool resent = false;
          {
            std::lock_guard<std::mutex> ev(evMu_);
            auto t = lastTxUs_.find(p.triggerOffset);
            if (t != lastTxUs_.end() && now > t->second) sample = now - t->second;
            resent = tainted_.count(p.triggerOffset) != 0 || t == lastTxUs_.end();
            lastTxUs_.erase(p.triggerOffset);
            tainted_.erase(p.triggerOffset);
          }
          if (rtt.OnSample(sample, lastPullUs ? now - lastPullUs : 0, chunkSendUs, resent, false)) {
            rtts.push_back(sample);
          }
        }
        lastPullUs = now;
        (void)bulk_.Send(out.data(), out.size());
      }
    }
    if (rc.Due(now, rounds, rtt.srttUs())) {
      const BulkPacer::Stats ps = pacer_.GetStats();
      BulkRateWindow w;
      {
        std::lock_guard<std::mutex> ev(evMu_);
        w.lossOrNacks = static_cast<uint32_t>(resentSeqsInWindow_.size());
        resentSeqsInWindow_.clear();
      }
      w.datagramsSent = static_cast<uint32_t>(ps.datagramsSent - sentDgramBase);
      sentDgramBase = ps.datagramsSent;
      if (rateConfig_.lossHorizonDatagrams) {
        // Judge loss over the recent horizon rather than this one round: a lone loss in a
        // 14-datagram round is 70 per mille, and says little about a path losing 1 % at random.
        horizon.push_back({w.datagramsSent, w.lossOrNacks});
        uint64_t d = 0, l = 0;
        for (auto it = horizon.rbegin(); it != horizon.rend(); ++it) {
          d += it->first;
          l += it->second;
          if (d >= rateConfig_.lossHorizonDatagrams) break;
        }
        while (horizon.size() > 1 && d >= rateConfig_.lossHorizonDatagrams) {
          uint64_t dd = 0;
          for (size_t k = 1; k < horizon.size(); ++k) dd += horizon[k].first;
          if (dd < rateConfig_.lossHorizonDatagrams) break;
          horizon.pop_front();
        }
        // A new loss still has to exist in THIS window to count as a new event.
        if (w.lossOrNacks == 0) l = 0;
        w.lossOrNacks = static_cast<uint32_t>(l);
        w.datagramsSent = static_cast<uint32_t>(d);
      }
      const uint64_t leftBytes = ps.bytesSent - sentBytesBase;
      sentBytesBase = ps.bytesSent;
      if (!rtts.empty()) {
        std::nth_element(rtts.begin(), rtts.begin() + rtts.size() / 2, rtts.end());
        w.pullRttP50Us = rtts[rtts.size() / 2];
      }
      w.pingRttUs = pingRtt_ ? pingRtt_() : 0;
      const uint64_t span = now > windowStartUs ? now - windowStartUs : 1;
      // "Used its rate": what the pacer actually let out in the window.
      w.deliveredBps = leftBytes * 8ull * 1000000ull / span;
      // Evidence a raise may not stand on: the pacer sat empty for a quarter of the window
      // (app-limited -- waiting on pulls, not on the path), or it stood aside for control.
      w.appLimited = (ps.idleUs - idleBase) * 4 > span;
      w.lostDatagrams = static_cast<uint32_t>(ps.resendsAfterTransmit - lossDgramBase);
      lossDgramBase = ps.resendsAfterTransmit;
      w.rttSamples = static_cast<uint32_t>(rtts.size());
      // With a horizon, the loss ratio is loss EVENTS over the horizon's datagrams: a whole-message
      // resend after one lost acknowledgement is one loss, not fifteen.
      if (rateConfig_.lossHorizonDatagrams) w.lostDatagrams = w.lossOrNacks;
      w.yielded = ps.yields != yieldBase;
      idleBase = ps.idleUs;
      yieldBase = ps.yields;
      // A window in which nothing left and nothing was lost says nothing about the path.
      if (leftBytes > 0 || w.lossOrNacks > 0) {
        const uint32_t lossNow = w.lossOrNacks;
        const BulkRateAction a = rc.Evaluate(w, now, rounds);
        std::lock_guard<std::mutex> lock(mu_);
        ++counters_.evaluations;
        counters_.lossEvents += lossNow ? 1 : 0;
        if (a == BulkRateAction::Raise) ++counters_.raises;
        if (a == BulkRateAction::Lower) ++counters_.lowers;
        if (a == BulkRateAction::Pause) ++counters_.pauses;
        if (a == BulkRateAction::Hold && lossNow) ++counters_.recoveryHolds;
      }
      rc.MarkEvaluated(now, rounds);
      rtts.clear();
      windowStartUs = now;
    }
    const uint32_t rate = rc.RateBps(now);
    const uint64_t ping = pingRtt_ ? pingRtt_() : 0;
    if (rate != rateNow_.load(std::memory_order_relaxed) || (ping && (ping > lastTimingRttUs * 2 || ping * 2 < lastTimingRttUs))) {
      UdpControlChannel::Timings t = clip_bulk_timings();
      t.retransmitIntervalUs = clip_bulk_retransmit_us(rate, ping);
      bulk_.SetTimings(t);
      lastTimingRttUs = ping;
    }
    rateNow_.store(rate, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lock(mu_);
      counters_.lastRateBps = rc.rate();
      counters_.srttUs = rtt.srttUs();
      counters_.channelFragmentRetransmits = bulk_.GetStats().fragmentRetransmits;
    }
  }
}

}  // namespace remote60::native_poc
