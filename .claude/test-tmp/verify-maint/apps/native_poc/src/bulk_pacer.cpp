// See bulk_pacer.hpp.

#include "bulk_pacer.hpp"

#include <cstring>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

uint64_t BulkPacer::NowUs() {
  static const uint64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return static_cast<uint64_t>(f.QuadPart);
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return static_cast<uint64_t>(c.QuadPart) / freq * 1000000ull +
         static_cast<uint64_t>(c.QuadPart) % freq * 1000000ull / freq;
}

bool BulkPacer::DataKey(const uint8_t* data, size_t len, Key* key) {
  if (len < sizeof(UdpControlChunkHeader)) return false;
  UdpControlChunkHeader h{};
  std::memcpy(&h, data, sizeof(h));
  if (h.magic != kMagic || h.kind != static_cast<uint16_t>(UdpPacketKind::ControlData)) return false;
  *key = Key{h.streamId, h.messageSeq, h.fragIndex};
  return true;
}

bool BulkPacer::Start(RawSendFn send, RateFn rate, YieldFn yield, TransmittedFn transmitted) {
  Stop();
  send_ = std::move(send);
  rate_ = std::move(rate);
  yield_ = std::move(yield);
  transmitted_ = std::move(transmitted);
  wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);  // pre-1803
  if (!wake_ || !timer_) {
    Stop();
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    queue_.clear();
    queuedAcks_ = 0;
    queuedKeys_.clear();
    sentKeys_.clear();
    newestSeq_ = 0;
    stats_ = Stats{};
  }
  running_.store(true, std::memory_order_release);
  thread_ = std::thread([this] { Run(); });
  return true;
}

void BulkPacer::Stop() {
  running_.store(false, std::memory_order_release);
  if (wake_) SetEvent(wake_);
  if (thread_.joinable()) thread_.join();
  if (wake_) CloseHandle(wake_);
  if (timer_) CloseHandle(timer_);
  wake_ = nullptr;
  timer_ = nullptr;
  std::lock_guard<std::mutex> lock(mu_);
  queue_.clear();
  queuedAcks_ = 0;
  queuedKeys_.clear();
  sentKeys_.clear();
}

bool BulkPacer::Enqueue(const void* data, size_t len) {
  if (!running_.load(std::memory_order_acquire) || !data || len == 0) return false;
  const auto* bytes = static_cast<const uint8_t*>(data);
  {
    std::lock_guard<std::mutex> lock(mu_);
    Key key;
    const bool isData = DataKey(bytes, len, &key);
    if (isData && queuedKeys_.count(key)) {
      ++stats_.resendsCoalesced;  // the same datagram is still waiting: sending it twice helps nobody
      return true;
    }
    if (isData) {
      const auto sent = sentKeys_.find(key);
      const uint64_t guard = resendGuardUs_.load(std::memory_order_relaxed);
      if (sent != sentKeys_.end() && guard && NowUs() - sent->second < guard) {
        ++stats_.resendsTooSoon;  // it only just left: a NACK that crossed it, not a loss
        return true;
      }
    }
    if (queue_.size() >= kMaxQueuedDatagrams) {
      ++stats_.droppedQueueFull;
      return false;
    }
    if (isData) {
      queue_.emplace_back(bytes, bytes + len);
      queuedKeys_.insert(key);
    } else {
      // Behind the acknowledgements already waiting, ahead of every data datagram.
      queue_.emplace(queue_.begin() + static_cast<std::ptrdiff_t>(queuedAcks_), bytes, bytes + len);
      ++queuedAcks_;
    }
  }
  SetEvent(wake_);
  return true;
}

void BulkPacer::Run() {
  BulkTokenBucket bucket;
  while (running_.load(std::memory_order_acquire)) {
    size_t frontLen = 0;
    bool frontIsAck = false;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!queue_.empty()) frontLen = queue_.front().size();
      frontIsAck = queuedAcks_ > 0;
    }
    if (frontLen == 0) {
      const uint64_t idleFrom = NowUs();
      WaitForSingleObject(wake_, 50);
      std::lock_guard<std::mutex> lock(mu_);
      stats_.idleUs += NowUs() - idleFrom;
      continue;
    }
    const uint64_t now = NowUs();
    const uint32_t rate = rate_ ? rate_(now) : 0;
    bucket.Refill(now, rate);
    uint64_t waitUs = 0;
    if (frontIsAck) {
      std::vector<uint8_t> dgram;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.empty() || queuedAcks_ == 0) continue;
        dgram = std::move(queue_.front());
        queue_.pop_front();
        --queuedAcks_;
        ++stats_.datagramsSent;
        stats_.bytesSent += dgram.size();
      }
      bucket.TakeOwed(dgram.size());
      (void)send_(dgram.data(), dgram.size());
      if (transmitted_) transmitted_(dgram.data(), dgram.size(), NowUs(), false);
      continue;
    }
    if (yield_ && yield_()) {
      // Control first: one beat (1 ms), then go on whether or not it has cleared -- control can
      // hold a message for its whole retransmit budget, and bulk must not stall behind that.
      {
        std::lock_guard<std::mutex> lock(mu_);
        ++stats_.yields;
      }
      waitUs = 1000;
    } else if (bucket.TryTake(frontLen)) {
      std::vector<uint8_t> dgram;
      bool resend = false;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.empty()) continue;
        dgram = std::move(queue_.front());
        queue_.pop_front();
        Key key;
        if (DataKey(dgram.data(), dgram.size(), &key)) {
          queuedKeys_.erase(key);
          const auto ins = sentKeys_.insert({key, 0});
          resend = !ins.second;
          ins.first->second = NowUs();
          if (resend) ++stats_.resendsAfterTransmit;
          const uint32_t seq = std::get<1>(key);
          if (seq > newestSeq_) newestSeq_ = seq;
          // Only the recent messages can still be retransmitted (the channel is head-only).
          while (!sentKeys_.empty() && std::get<1>(sentKeys_.begin()->first) + 64 < newestSeq_) {
            sentKeys_.erase(sentKeys_.begin());
          }
        }
        ++stats_.datagramsSent;
        stats_.bytesSent += dgram.size();
      }
      (void)send_(dgram.data(), dgram.size());
      if (transmitted_) transmitted_(dgram.data(), dgram.size(), NowUs(), resend);
      continue;
    } else {
      waitUs = bucket.WaitUs(frontLen, rate);
      if (waitUs == UINT64_MAX || waitUs > 50000) waitUs = 50000;  // paused: look again in 50 ms
    }
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(waitUs * 10);  // relative, 100 ns units
    SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
    HANDLE handles[2] = {timer_, wake_};
    WaitForMultipleObjects(2, handles, FALSE, 100);
  }
}

}  // namespace remote60::native_poc
