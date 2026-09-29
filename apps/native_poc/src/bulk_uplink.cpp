// See bulk_uplink.hpp. Moved from clip_image_client.cpp (OpenBulk / CloseBulk / OnTransmitted /
// ServeLoop) with the chunk key generalised from the image offset to a 64-bit key.

#include "bulk_uplink.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "poc_protocol.hpp"

#include <iostream>

namespace {
// The trace lines go through std::cout: a process that routes its log through std::cout's buffer
// (the host's timestamp prefix, its log file) keeps them in order with its other lines.
void trace_line(const char* fmt, ...) {
  char b[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  std::cout << b;
  std::cout.flush();
}
}  // namespace

namespace remote60::native_poc {

void BulkUplink::Open(uint32_t txStream, uint32_t rxStream, BulkUplinkSource* source) {
  source_ = source;
  txStreamId_ = txStream;
  confirmedBytes_.store(0, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> ev(evMu_);
    seqToKey_.clear();
    lastTxUs_.clear();
    tainted_.clear();
    resentSeqsInWindow_.clear();
    confirmedKeys_.clear();
    chunkTimes_.clear();
    loss_.Reset();
  }
  bulk_.Reset();
  {
    UdpControlChannel::Timings t = bulk_uplink_timings();
    t.retransmitIntervalUs = bulk_retransmit_us((std::min)(rateConfig_.startBps, rateConfig_.capBps), 0, chunkBytes_);
    bulk_.SetTimings(t);
  }
  bulk_.Configure([this](const void* d, size_t n) { return pacer_.Enqueue(d, n); }, txStream, rxStream, mtu_);
  rateNow_.store((std::min)(rateConfig_.startBps, rateConfig_.capBps));
  pacer_.Start(send_, [this](uint64_t) { return rateNow_.load(std::memory_order_relaxed); }, yield_,
               [this](const uint8_t* d, size_t n, uint64_t t, bool resend) { OnTransmitted(d, n, t, resend); });
  serving_.store(true);
  serveThread_ = std::thread([this] { ServeLoop(); });
}

void BulkUplink::Close() {
  serving_.store(false);
  bulk_.Close(ControlCloseReason::SessionRollover);
  if (serveThread_.joinable()) serveThread_.join();
  pacer_.Stop();
}

void BulkUplink::OnTransmitted(const uint8_t* data, size_t len, uint64_t nowUs, bool resend) {
  if (len < sizeof(UdpControlChunkHeader)) return;
  UdpControlChunkHeader h{};
  std::memcpy(&h, data, sizeof(h));
  if (h.magic != kMagic || h.kind != static_cast<uint16_t>(UdpPacketKind::ControlData) || h.streamId != txStreamId_) {
    return;
  }
  std::lock_guard<std::mutex> ev(evMu_);
  if (!resend) {
    loss_.OnOriginal();  // an original fragment's first transmission: the loss ratio's denominator
  } else {
    // Each original fragment counts as lost once, however often it is resent; a resend of a chunk
    // the host already confirmed is a lost ACK, counted apart (BulkLossCounter).
    const auto so = seqToKey_.find(h.messageSeq);
    const bool confirmed = so != seqToKey_.end() && confirmedKeys_.count(so->second) != 0;
    if (loss_.OnResend(h.messageSeq, h.fragIndex, h.fragCount, confirmed) == BulkLossCounter::Resend::AckLost) return;
  }
  uint64_t firstKey = 0;
  if (h.fragIndex == 0 && source_ &&
      source_->ChunkKeyOf(data + sizeof(UdpControlChunkHeader), len - sizeof(UdpControlChunkHeader), &firstKey)) {
    seqToKey_[h.messageSeq] = firstKey;
    while (seqToKey_.size() > 64) seqToKey_.erase(seqToKey_.begin());
  }
  auto it = seqToKey_.find(h.messageSeq);
  if (it == seqToKey_.end()) return;
  const uint64_t key = it->second;
  {
    auto ct = chunkTimes_.find(key);
    if (ct != chunkTimes_.end()) {
      if (!ct->second.firstTx) ct->second.firstTx = nowUs;
      ct->second.lastAnyTx = nowUs;
      if (resend) ct->second.resent = true;
      else if (h.fragIndex + 1 == h.fragCount) ct->second.lastTx = nowUs;
    }
  }
  if (resend) {
    tainted_.insert(key);
    lastTxUs_.erase(key);
    resentSeqsInWindow_.insert(h.messageSeq);
    return;
  }
  if (h.fragIndex + 1 == h.fragCount && !tainted_.count(key)) lastTxUs_[key] = nowUs;
}

void BulkUplink::ServeLoop() {
  BulkRateController rc(rateConfig_);
  BulkRttEstimator rtt;
  std::vector<uint8_t> msg;
  std::vector<uint8_t> out;
  std::vector<uint64_t> rtts;  // this evaluation's valid samples
  uint64_t rounds = 0;
  uint64_t windowStartUs = BulkPacer::NowUs();
  uint64_t sentBytesBase = 0, yieldBase = 0;
  uint64_t completedBytes = 0;  // goodput evidence: chunks the host confirmed in this window
  uint32_t pullsInWindow = 0;   // pending work: the host asked for more in this window
  uint64_t lastTimingRttUs = 0;
  uint64_t lastPullUs = 0;
  uint32_t capInForce = rateConfig_.capBps;
  const char* traceEnv = std::getenv("REMOTE60_CLIP_BULK_TRACE");
  const bool traceRate = traceEnv && (traceEnv[0] == '1' || traceEnv[0] == '2');
  const bool traceChunks = traceEnv && traceEnv[0] == '2';
  const uint64_t traceT0 = windowStartUs;
  rc.MarkEvaluated(windowStartUs, 0);
  while (serving_.load()) {
    const bool got = bulk_.Receive(&msg, 10);
    bulk_.Tick();
    const uint64_t now = BulkPacer::NowUs();
    // The cap is the budget: the configured ceiling and, when one is known, the same-direction
    // budget -- applied at once, 0 included (④). Unknown budget: the ceiling alone.
    const uint32_t budget = budgetBps_.load(std::memory_order_relaxed);
    const uint32_t cap = budget != kBudgetUnknown ? (std::min)(rateConfig_.capBps, budget) : rateConfig_.capBps;
    if (cap != capInForce) {
      rc.SetCapBps(cap);
      capInForce = cap;
    }
    if (got && source_) {
      BulkServed s;
      out.clear();
      const bool ok = source_->OnPull(msg, now, &out, &s);
      {
        std::lock_guard<std::mutex> lock(countersMu_);
        if (ok) ++counters_.pullsServed;
        else ++counters_.pullsDropped;
      }
      if (ok) {
        ++pullsInWindow;
        if (s.completedChunk) {  // a pull naming a chunk counted before is not completedChunk (duplicate)
          ++rounds;
          completedBytes += s.completedBytes;
          confirmedBytes_.fetch_add(s.completedBytes, std::memory_order_relaxed);
          const uint32_t r = rateNow_.load(std::memory_order_relaxed);
          const uint64_t chunkSendUs = r ? static_cast<uint64_t>(s.completedBytes) * 8ull * 1000000ull / r : 0;
          uint64_t sample = 0;
          bool resent = false;
          {
            std::lock_guard<std::mutex> ev(evMu_);
            if (traceChunks) {
              auto ct = chunkTimes_.find(s.triggerKey);
              if (ct != chunkTimes_.end()) {
                const ChunkTimes& c = ct->second;
                trace_line("CHUNKTRACE off=%llu pullAt=%llu enqAt=%llu firstTx=%llu lastTx=%llu lastAnyTx=%llu confirmAt=%llu "
                            "resent=%d rate=%u\n",
                            static_cast<unsigned long long>(s.triggerKey), static_cast<unsigned long long>(c.pullAt),
                            static_cast<unsigned long long>(c.enqAt), static_cast<unsigned long long>(c.firstTx),
                            static_cast<unsigned long long>(c.lastTx), static_cast<unsigned long long>(c.lastAnyTx),
                            static_cast<unsigned long long>(now), c.resent ? 1 : 0, rateNow_.load());
                chunkTimes_.erase(ct);
              }
            }
            confirmedKeys_.insert(s.triggerKey);
            while (confirmedKeys_.size() > 256) confirmedKeys_.erase(confirmedKeys_.begin());
            auto t = lastTxUs_.find(s.triggerKey);
            if (t != lastTxUs_.end() && now > t->second) sample = now - t->second;
            resent = tainted_.count(s.triggerKey) != 0 || t == lastTxUs_.end();
            lastTxUs_.erase(s.triggerKey);
            tainted_.erase(s.triggerKey);
          }
          if (rtt.OnSample(sample, lastPullUs ? now - lastPullUs : 0, chunkSendUs, resent, false)) {
            rtts.push_back(sample);
          }
        }
        lastPullUs = now;
        if (traceChunks) {
          std::lock_guard<std::mutex> ev(evMu_);
          ChunkTimes& c = chunkTimes_[s.chunkKey];
          c.pullAt = now;
          while (chunkTimes_.size() > 64) chunkTimes_.erase(chunkTimes_.begin());
        }
        (void)bulk_.Send(out.data(), out.size());
        if (traceChunks) {
          std::lock_guard<std::mutex> ev(evMu_);
          auto ct = chunkTimes_.find(s.chunkKey);
          if (ct != chunkTimes_.end()) ct->second.enqAt = BulkPacer::NowUs();
        }
      }
    }
    if (rc.Due(now, rounds, rtt.srttUs())) {
      const BulkPacer::Stats ps = pacer_.GetStats();
      BulkRateWindow w;
      uint64_t ackLostTotal = 0, repeatTotal = 0;  // kept apart from the loss ratio (diagnostics)
      {
        std::lock_guard<std::mutex> ev(evMu_);
        w.lossEvents = static_cast<uint32_t>(resentSeqsInWindow_.size());
        resentSeqsInWindow_.clear();
        const BulkLossCounter::Window lw = loss_.Take();
        w.uniqueFragmentsSent = lw.sent;
        w.uniqueFragmentsLost = lw.lost;
        w.rtoEvents = lw.rto;
        ackLostTotal = loss_.ackLostResends();
        repeatTotal = loss_.repeatResends();
      }
      const uint64_t leftBytes = ps.bytesSent - sentBytesBase;
      sentBytesBase = ps.bytesSent;
      if (!rtts.empty()) {
        std::nth_element(rtts.begin(), rtts.begin() + rtts.size() / 2, rtts.end());
        w.pullRttP50Us = rtts[rtts.size() / 2];
      }
      w.rttSamples = static_cast<uint32_t>(rtts.size());
      w.pullSrttUs = rtt.srttUs();
      w.pullCurrentUs = rtt.currentUs();
      // A NACK within about a round trip of the fragment's departure crossed it in flight.
      pacer_.SetResendGuardUs(rtt.srttUs() ? rtt.srttUs() + rtt.srttUs() / 4 + 5000 : 0);
      w.pingRttUs = pingRtt_ ? pingRtt_() : 0;
      const uint64_t span = now > windowStartUs ? now - windowStartUs : 1;
      w.deliveredBps = leftBytes * 8ull * 1000000ull / span;
      w.goodputBps = completedBytes * 8ull * 1000000ull / span;
      w.workPending = pullsInWindow > 0;
      w.yielded = ps.yields != yieldBase;
      yieldBase = ps.yields;
      // A window in which nothing left and nothing was lost says nothing about the path.
      if (leftBytes > 0 || w.lossEvents > 0) {
        const uint32_t lossNow = w.lossEvents;
        const uint32_t rateBefore = rc.rate();
        const BulkRateAction a = rc.Evaluate(w, now, rounds);
        if (traceRate) {  // REMOTE60_CLIP_BULK_TRACE=1: one line per evaluation (diagnostics)
          trace_line("RATETRACE t=%.3f rate=%u->%u act=%d lossEv=%u uLost=%u uSent=%u rto=%u pull=%llu n=%u ping=%llu "
                      "good=%llu work=%d yield=%d lossState=%d plateau=%d cause=%u srtt=%llu cur=%llu ackLost=%llu repeat=%llu\n",
                      (now - traceT0) / 1e6, rateBefore, rc.rate(), static_cast<int>(a), w.lossEvents, w.uniqueFragmentsLost,
                      w.uniqueFragmentsSent, w.rtoEvents, static_cast<unsigned long long>(w.pullRttP50Us), w.rttSamples,
                      static_cast<unsigned long long>(w.pingRttUs), static_cast<unsigned long long>(w.goodputBps),
                      w.workPending ? 1 : 0, w.yielded ? 1 : 0, static_cast<int>(rc.LossState()), rc.onPlateau() ? 1 : 0,
                      rc.lastCause(), static_cast<unsigned long long>(w.pullSrttUs),
                      static_cast<unsigned long long>(w.pullCurrentUs), static_cast<unsigned long long>(ackLostTotal),
                      static_cast<unsigned long long>(repeatTotal));
        }
        std::lock_guard<std::mutex> lock(countersMu_);
        ++counters_.evaluations;
        counters_.lossEvents += lossNow ? 1 : 0;
        if (a == BulkRateAction::Raise) ++counters_.raises;
        if (a == BulkRateAction::Lower) ++counters_.lowers;
        if (a == BulkRateAction::Pause) ++counters_.pauses;
        if (a == BulkRateAction::Hold && lossNow) ++counters_.recoveryHolds;
        counters_.rtoEvents += w.rtoEvents;
      }
      rc.MarkEvaluated(now, rounds);
      rtts.clear();
      completedBytes = 0;
      pullsInWindow = 0;
      windowStartUs = now;
    }
    const uint32_t rate = rc.RateBps(now);
    const uint64_t ping = pingRtt_ ? pingRtt_() : 0;
    if (rate != rateNow_.load(std::memory_order_relaxed) || (ping && (ping > lastTimingRttUs * 2 || ping * 2 < lastTimingRttUs))) {
      UdpControlChannel::Timings t = bulk_uplink_timings();
      t.retransmitIntervalUs = bulk_retransmit_us(rate, ping, chunkBytes_);
      bulk_.SetTimings(t);
      lastTimingRttUs = ping;
    }
    rateNow_.store(rate, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lock(countersMu_);
      counters_.lastRateBps = rc.rate();
      counters_.srttUs = rtt.srttUs();
      counters_.channelFragmentRetransmits = bulk_.GetStats().fragmentRetransmits;
    }
  }
}

}  // namespace remote60::native_poc
